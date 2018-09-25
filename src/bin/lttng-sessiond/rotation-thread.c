/*
 * Copyright (C) 2017 - Julien Desfossez <jdesfossez@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2 only, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _LGPL_SOURCE
#include <lttng/trigger/trigger.h>
#include <common/error.h>
#include <common/config/session-config.h>
#include <common/defaults.h>
#include <common/utils.h>
#include <common/futex.h>
#include <common/align.h>
#include <common/time.h>
#include <common/hashtable/utils.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <inttypes.h>

#include <common/kernel-ctl/kernel-ctl.h>
#include <lttng/notification/channel-internal.h>
#include <lttng/rotate-internal.h>

#include "rotation-thread.h"
#include "lttng-sessiond.h"
#include "health-sessiond.h"
#include "rotate.h"
#include "cmd.h"
#include "session.h"
#include "sessiond-timer.h"
#include "notification-thread-commands.h"

#include <urcu.h>
#include <urcu/list.h>

struct lttng_notification_channel *rotate_notification_channel = NULL;

struct rotation_thread {
	struct lttng_poll_event events;
};

struct rotation_thread_job {
	enum rotation_thread_job_type type;
	uint64_t session_id;
	/* List member in struct rotation_thread_timer_queue. */
	struct cds_list_head head;
};

/*
 * Destroy the thread data previously created by the init function.
 */
void rotation_thread_handle_destroy(
		struct rotation_thread_handle *handle)
{
	free(handle);
}

struct rotation_thread_handle *rotation_thread_handle_create(
		int quit_pipe,
		struct rotation_thread_timer_queue *rotation_timer_queue,
		struct notification_thread_handle *notification_thread_handle,
		sem_t *notification_thread_ready)
{
	struct rotation_thread_handle *handle;

	handle = zmalloc(sizeof(*handle));
	if (!handle) {
		goto end;
	}

	handle->quit_pipe = quit_pipe;
	handle->rotation_timer_queue = rotation_timer_queue;
	handle->notification_thread_handle = notification_thread_handle;
	handle->notification_thread_ready = notification_thread_ready;

end:
	return handle;
}

/*
 * Called with the rotation_thread_timer_queue lock held.
 * Return true if the same timer job already exists in the queue, false if not.
 */
static
bool timer_job_exists(const struct rotation_thread_timer_queue *queue,
		enum rotation_thread_job_type job_type, uint64_t session_id)
{
	bool found = false;
	struct rotation_thread_job *job;

	cds_list_for_each_entry(job, &queue->list, head) {
		if (job->session_id == session_id && job->type == job_type) {
			found = true;
			goto end;
		}
	}
end:
	return found;
}

void rotation_thread_enqueue_job(struct rotation_thread_timer_queue *queue,
		enum rotation_thread_job_type job_type, uint64_t session_id)
{
	int ret;
	const char * const dummy = "!";
	const char *job_type_str;
	struct rotation_thread_job *job = NULL;

	switch (job_type) {
	case ROTATION_THREAD_JOB_TYPE_CHECK_PENDING_ROTATION:
		job_type_str = "CHECK_PENDING_ROTATION";
		break;
	case ROTATION_THREAD_JOB_TYPE_SCHEDULED_ROTATION:
		job_type_str = "SCHEDULED_ROTATION";
		break;
	default:
		abort();
	}

	pthread_mutex_lock(&queue->lock);
	if (timer_job_exists(queue, session_id, job_type)) {
		/*
		 * This timer job is already pending, we don't need to add
		 * it.
		 */
		goto end;
	}

	job = zmalloc(sizeof(struct rotation_thread_job));
	if (!job) {
		PERROR("Failed to allocate rotation thread job of type \"%s\" for session id %" PRIu64,
				job_type_str, session_id);
		goto end;
	}
	job->type = job_type;
	job->session_id = session_id;
	cds_list_add_tail(&job->head, &queue->list);

	ret = lttng_write(lttng_pipe_get_writefd(queue->event_pipe), dummy,
			1);
	if (ret < 0) {
		/*
		 * We do not want to block in the timer handler, the job has
		 * been enqueued in the list, the wakeup pipe is probably full,
		 * the job will be processed when the rotation_thread catches
		 * up.
		 */
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			/*
			 * Not an error, but would be surprising and indicate
			 * that the rotation thread can't keep up with the
			 * current load.
			 */
			DBG("Wake-up pipe of rotation thread job queue is full");
			goto end;
		}
		PERROR("Failed to wake-up the rotation thread after pushing a job of type \"%s\" for session id %" PRIu64,
				job_type_str, session_id);
		goto end;
	}

end:
	pthread_mutex_unlock(&queue->lock);
}

static
int init_poll_set(struct lttng_poll_event *poll_set,
		struct rotation_thread_handle *handle)
{
	int ret;

	/*
	 * Create pollset with size 2:
	 *	- quit pipe,
	 *	- rotation thread timer queue pipe,
	 */
	ret = lttng_poll_create(poll_set, 2, LTTNG_CLOEXEC);
	if (ret < 0) {
		goto end;
	}

	ret = lttng_poll_add(poll_set, handle->quit_pipe,
			LPOLLIN | LPOLLERR);
	if (ret < 0) {
		ERR("[rotation-thread] Failed to add quit_pipe fd to pollset");
		goto error;
	}
	ret = lttng_poll_add(poll_set,
			lttng_pipe_get_readfd(handle->rotation_timer_queue->event_pipe),
			LPOLLIN | LPOLLERR);
	if (ret < 0) {
		ERR("[rotation-thread] Failed to add rotate_pending fd to pollset");
		goto error;
	}

end:
	return ret;
error:
	lttng_poll_clean(poll_set);
	return ret;
}

static
void fini_thread_state(struct rotation_thread *state)
{
	lttng_poll_clean(&state->events);
	if (rotate_notification_channel) {
		lttng_notification_channel_destroy(rotate_notification_channel);
	}
}

static
int init_thread_state(struct rotation_thread_handle *handle,
		struct rotation_thread *state)
{
	int ret;

	memset(state, 0, sizeof(*state));
	lttng_poll_init(&state->events);

	ret = init_poll_set(&state->events, handle);
	if (ret) {
		ERR("[rotation-thread] Failed to initialize rotation thread poll set");
		goto end;
	}

	/*
	 * We wait until the notification thread is ready to create the
	 * notification channel and add it to the poll_set.
	 */
	sem_wait(handle->notification_thread_ready);
	rotate_notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	if (!rotate_notification_channel) {
		ERR("[rotation-thread] Could not create notification channel");
		ret = -1;
		goto end;
	}
	ret = lttng_poll_add(&state->events, rotate_notification_channel->socket,
			LPOLLIN | LPOLLERR);
	if (ret < 0) {
		ERR("[rotation-thread] Failed to add notification fd to pollset");
		goto end;
	}

end:
	return ret;
}
/*
static
int handle_channel_rotation_pipe(int fd, uint32_t revents,
		struct rotation_thread_handle *handle,
		struct rotation_thread *state)
{
	int ret = 0;
	enum lttng_domain_type domain;
	struct rotation_channel_info *channel_info;
	struct ltt_session *session = NULL;
	uint64_t key;

	if (fd == handle->ust32_consumer ||
			fd == handle->ust64_consumer) {
		domain = LTTNG_DOMAIN_UST;
	} else if (fd == handle->kernel_consumer) {
		domain = LTTNG_DOMAIN_KERNEL;
	} else {
		ERR("[rotation-thread] Unknown channel rotation pipe fd %d",
				fd);
		abort();
	}

	if (revents & (LPOLLERR | LPOLLHUP | LPOLLRDHUP)) {
		ret = lttng_poll_del(&state->events, fd);
		if (ret) {
			ERR("[rotation-thread] Failed to remove consumer "
					"rotation pipe from poll set");
		}
		goto end;
	}

	do {
		ret = read(fd, &key, sizeof(key));
	} while (ret == -1 && errno == EINTR);
	if (ret != sizeof(key)) {
		ERR("[rotation-thread] Failed to read from pipe (fd = %i)",
				fd);
		ret = -1;
		goto end;
	}

	DBG("[rotation-thread] Received notification for chan %" PRIu64
			", domain %d", key, domain);

	channel_info = lookup_channel_pending(key, domain);
	if (!channel_info) {
		ERR("[rotation-thread] Failed to find channel_info (key = %"
				PRIu64 ")", key);
		ret = -1;
		goto end;
	}
	rcu_read_lock();
	session_lock_list();
	session = session_find_by_id(channel_info->session_id);
	if (!session) {
		DBG("[rotation-thread] Session %" PRIu64 " not found",
				channel_info->session_id);
		ret = 0;
		goto end_unlock_session_list;
	}

	session_lock(session);
	if (--session->nr_chan_rotate_pending == 0) {
		time_t now = time(NULL);

		if (now == (time_t) -1) {
			session->rotation_state = LTTNG_ROTATION_STATE_ERROR;
			ret = LTTNG_ERR_UNK;
			goto end_unlock_session;
		}

		ret = rename_complete_chunk(session, now);
		if (ret < 0) {
			ERR("Failed to rename completed rotation chunk");
			goto end_unlock_session;
		}
		session->rotate_pending = false;
		session->last_chunk_start_ts = session->current_chunk_start_ts;
		if (session->rotate_pending_relay) {
			ret = sessiond_timer_rotate_pending_start(
					session,
					DEFAULT_ROTATE_PENDING_RELAY_TIMER);
			if (ret) {
				ERR("Failed to enable rotate pending timer");
				ret = -1;
				goto end_unlock_session;
			}
		} else {
			struct lttng_trace_archive_location *location;

			session->rotation_state = LTTNG_ROTATION_STATE_COMPLETED;
			location = session_get_trace_archive_location(session);
			ret = notification_thread_command_session_rotation_completed(
					notification_thread_handle,
					session->name,
					session->uid,
					session->gid,
					session->current_archive_id,
					location);
			if (ret != LTTNG_OK) {
				ERR("Failed to notify notification thread that rotation is complete for session %s",
						session->name);
			}

		}
		DBG("Rotation completed for session %s", session->name);
	}

	ret = 0;

end_unlock_session:
	channel_rotation_info_destroy(channel_info);
	session_unlock(session);
end_unlock_session_list:
	session_unlock_list();
	rcu_read_unlock();
end:
	return ret;
}
*/

/*
 * Process the rotate_pending check, called with session lock held.
 */
static
int rotate_pending_relay_timer(struct ltt_session *session)
{
	int ret;

	DBG("[rotation-thread] Check rotate pending on session %" PRIu64,
			session->id);
	ret = relay_rotate_pending(session, session->current_archive_id - 1);
	if (ret < 0) {
		ERR("[rotation-thread] Check relay rotate pending");
		goto end;
	}
	if (ret == 0) {
		struct lttng_trace_archive_location *location;

		DBG("[rotation-thread] Rotation completed on the relay for "
				"session %" PRIu64, session->id);
		/*
		 * Now we can clear the pending flag in the session. New
		 * rotations can start now.
		 */
		session->rotate_pending_relay = false;
		session->rotation_state = LTTNG_ROTATION_STATE_COMPLETED;

		session->rotation_state = LTTNG_ROTATION_STATE_COMPLETED;
		/* Ownership of location is transferred. */
		location = session_get_trace_archive_location(session);
		ret = notification_thread_command_session_rotation_completed(
				notification_thread_handle,
				session->name,
				session->uid,
				session->gid,
				session->current_archive_id,
				location);
		if (ret != LTTNG_OK) {
			ERR("Failed to notify notification thread that rotation is complete for session %s",
					session->name);
		}
	} else if (ret == 1) {
		DBG("[rotation-thread] Rotation still pending on the relay for "
				"session %" PRIu64, session->id);
		ret = sessiond_timer_rotate_pending_start(session,
				DEFAULT_ROTATE_PENDING_RELAY_TIMER);
		if (ret) {
			ERR("Re-enabling rotate pending timer");
			ret = -1;
			goto end;
		}
	}

	ret = 0;

end:
	return ret;
}

/*
 * Process the rotate_timer, called with session lock held.
 */
static
int rotate_timer(struct ltt_session *session)
{
	int ret;

	/*
	 * Complete _at most_ one scheduled rotation on a stopped session.
	 */
	if (!session->active && session->rotate_timer_enabled &&
			session->rotated_after_last_stop) {
		ret = 0;
		goto end;
	}

	/* Ignore this timer if a rotation is already in progress. */
	if (session->rotate_pending || session->rotate_pending_relay) {
		ret = 0;
		goto end;
	}

	DBG("[rotation-thread] Rotate timer on session %s", session->name);

	ret = cmd_rotate_session(session, NULL);
	if (ret == -LTTNG_ERR_ROTATION_PENDING) {
		DBG("Scheduled rotation aborted since a rotation is already in progress");
		ret = 0;
		goto end;
	} else if (ret != LTTNG_OK) {
		ERR("[rotation-thread] Automatic time-triggered rotation failed with error code %i", ret);
		ret = -1;
		goto end;
	}

	ret = 0;

end:
	return ret;
}

static
int handle_job_queue(uint32_t revents,
		struct rotation_thread_handle *handle,
		struct rotation_thread *state,
		struct rotation_thread_timer_queue *queue)
{
	int ret = 0;
	int fd = lttng_pipe_get_readfd(queue->event_pipe);
	struct ltt_session *session;
	char buf[1];

	if (revents & (LPOLLERR | LPOLLHUP | LPOLLRDHUP)) {
		ERR("[rotation-thread] Error reported on timer queue pipe");
		goto end;
	}

	ret = lttng_read(fd, buf, 1);
	if (ret != 1) {
		ERR("[rotation-thread] Failed to read from wakeup pipe (fd = %i)", fd);
		ret = -1;
		goto end;
	}

	for (;;) {
		struct sessiond_rotation_timer *timer_data;

		/*
		 * Take the queue lock only to pop elements from the list.
		 */
		pthread_mutex_lock(&queue->lock);
		if (cds_list_empty(&queue->list)) {
			pthread_mutex_unlock(&queue->lock);
			break;
		}
		timer_data = cds_list_first_entry(&queue->list,
				struct sessiond_rotation_timer, head);
		cds_list_del(&timer_data->head);
		pthread_mutex_unlock(&queue->lock);

		/*
		 * session lock to lookup the session ID.
		 */
		session_lock_list();
		session = session_find_by_id(timer_data->session_id);
		if (!session) {
			DBG("[rotation-thread] Session %" PRIu64 " not found",
					timer_data->session_id);
			/*
			 * This is a non-fatal error, and we cannot report it to the
			 * user (timer), so just print the error and continue the
			 * processing.
			 */
			session_unlock_list();
			free(timer_data);
			continue;
		}

		/*
		 * Take the session lock and release the session_list lock.
		 */
		session_lock(session);
		session_unlock_list();

		if (timer_data->signal == LTTNG_SESSIOND_SIG_ROTATE_PENDING) {
			ret = rotate_pending_relay_timer(session);
		} else if (timer_data->signal == LTTNG_SESSIOND_SIG_ROTATE_TIMER) {
			ret = rotate_timer(session);
		} else {
			ERR("Unknown signal in rotate timer %d", timer_data->signal);
			ret = -1;
		}
		session_unlock(session);
		free(timer_data);
		if (ret) {
			ERR("Error processing timer");
			goto end;
		}
	}

	ret = 0;

end:
	return ret;
}

static
int handle_condition(const struct lttng_condition *condition,
		const struct lttng_evaluation *evaluation,
		struct notification_thread_handle *notification_thread_handle)
{
	int ret = 0;
	const char *condition_session_name = NULL;
	enum lttng_condition_type condition_type;
	enum lttng_condition_status condition_status;
	enum lttng_evaluation_status evaluation_status;
	uint64_t consumed;
	struct ltt_session *session;

	condition_type = lttng_condition_get_type(condition);

	if (condition_type != LTTNG_CONDITION_TYPE_SESSION_CONSUMED_SIZE) {
		ret = -1;
		ERR("[rotation-thread] Condition type and session usage type are not the same");
		goto end;
	}

	/* Fetch info to test */
	condition_status = lttng_condition_session_consumed_size_get_session_name(
			condition, &condition_session_name);
	if (condition_status != LTTNG_CONDITION_STATUS_OK) {
		ERR("[rotation-thread] Session name could not be fetched");
		ret = -1;
		goto end;
	}
	evaluation_status = lttng_evaluation_session_consumed_size_get_consumed_size(evaluation,
			&consumed);
	if (evaluation_status != LTTNG_EVALUATION_STATUS_OK) {
		ERR("[rotation-thread] Failed to get evaluation");
		ret = -1;
		goto end;
	}

	session_lock_list();
	session = session_find_by_name(condition_session_name);
	if (!session) {
		ret = -1;
		session_unlock_list();
		ERR("[rotation-thread] Session \"%s\" not found",
				condition_session_name);
		goto end;
	}
	session_lock(session);
	session_unlock_list();

	ret = unsubscribe_session_consumed_size_rotation(session,
			notification_thread_handle);
	if (ret) {
		goto end;
	}

	ret = cmd_rotate_session(session, NULL);
	if (ret == -LTTNG_ERR_ROTATION_PENDING) {
		DBG("Rotate already pending, subscribe to the next threshold value");
	} else if (ret != LTTNG_OK) {
		ERR("[rotation-thread] Failed to rotate on size notification with error: %s",
				lttng_strerror(ret));
		ret = -1;
		goto end_unlock;
	}
	ret = subscribe_session_consumed_size_rotation(session,
			consumed + session->rotate_size,
			notification_thread_handle);
	if (ret) {
		ERR("[rotation-thread] Failed to subscribe to session consumed size condition");
		goto end_unlock;
	}
	ret = 0;

end_unlock:
	session_unlock(session);
end:
	return ret;
}

static
int handle_notification_channel(int fd, uint32_t revents,
		struct rotation_thread_handle *handle,
		struct rotation_thread *state)
{
	int ret;
	bool notification_pending;
	struct lttng_notification *notification = NULL;
	enum lttng_notification_channel_status status;
	const struct lttng_evaluation *notification_evaluation;
	const struct lttng_condition *notification_condition;

	status = lttng_notification_channel_has_pending_notification(
			rotate_notification_channel, &notification_pending);
	if (status != LTTNG_NOTIFICATION_CHANNEL_STATUS_OK) {
		ERR("[rotation-thread ]Error occured while checking for pending notification");
		ret = -1;
		goto end;
	}

	if (!notification_pending) {
		ret = 0;
		goto end;
	}

	/* Receive the next notification. */
	status = lttng_notification_channel_get_next_notification(
			rotate_notification_channel,
			&notification);

	switch (status) {
	case LTTNG_NOTIFICATION_CHANNEL_STATUS_OK:
		break;
	case LTTNG_NOTIFICATION_CHANNEL_STATUS_NOTIFICATIONS_DROPPED:
		/* Not an error, we will wait for the next one */
		ret = 0;
		goto end;;
	case LTTNG_NOTIFICATION_CHANNEL_STATUS_CLOSED:
		ERR("Notification channel was closed");
		ret = -1;
		goto end;
	default:
		/* Unhandled conditions / errors. */
		ERR("Unknown notification channel status");
		ret = -1;
		goto end;
	}

	notification_condition = lttng_notification_get_condition(notification);
	notification_evaluation = lttng_notification_get_evaluation(notification);

	ret = handle_condition(notification_condition, notification_evaluation,
			handle->notification_thread_handle);

end:
	lttng_notification_destroy(notification);
	return ret;
}

void *thread_rotation(void *data)
{
	int ret;
	struct rotation_thread_handle *handle = data;
	struct rotation_thread thread;

	DBG("[rotation-thread] Started rotation thread");

	if (!handle) {
		ERR("[rotation-thread] Invalid thread context provided");
		goto end;
	}

	health_register(health_sessiond, HEALTH_SESSIOND_TYPE_ROTATION);
	health_code_update();

	ret = init_thread_state(handle, &state);
	if (ret) {
		goto end;
	}

	/* Ready to handle client connections. */
	sessiond_notify_ready();

	while (true) {
		int fd_count, i;

		health_poll_entry();
		DBG("[rotation-thread] Entering poll wait");
		ret = lttng_poll_wait(&state.events, -1);
		DBG("[rotation-thread] Poll wait returned (%i)", ret);
		health_poll_exit();
		if (ret < 0) {
			/*
			 * Restart interrupted system call.
			 */
			if (errno == EINTR) {
				continue;
			}
			ERR("[rotation-thread] Error encountered during lttng_poll_wait (%i)", ret);
			goto error;
		}

		fd_count = ret;
		for (i = 0; i < fd_count; i++) {
			int fd = LTTNG_POLL_GETFD(&state.events, i);
			uint32_t revents = LTTNG_POLL_GETEV(&state.events, i);

			DBG("[rotation-thread] Handling fd (%i) activity (%u)",
					fd, revents);

			if (revents & (LPOLLERR | LPOLLHUP | LPOLLRDHUP)) {
				ERR("[rotation-thread] Polling returned an error on fd %i", fd);
				goto error;
			}

			if (fd == handle->quit_pipe) {
				DBG("[rotation-thread] Quit pipe activity");
				goto exit;
			} else if (fd == lttng_pipe_get_readfd(handle->rotation_timer_queue->event_pipe)) {
				ret = handle_job_queue(handle, &state,
						handle->rotation_timer_queue);
				if (ret) {
					ERR("[rotation-thread] Failed to handle rotation timer pipe event");
					goto error;
				}
			} else if (fd == rotate_notification_channel->socket) {
				ret = handle_notification_channel(fd, handle,
						&state);
				if (ret) {
					ERR("[rotation-thread] Error occured while handling activity on notification channel socket");
					goto error;
				}
			}
		}
	}
exit:
error:
	DBG("[rotation-thread] Exit");
	fini_thread_state(&state);
	health_unregister(health_sessiond);
end:
	return NULL;
}
