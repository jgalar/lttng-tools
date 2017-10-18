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

#include "rotation-thread.h"
#include "lttng-sessiond.h"
#include "health-sessiond.h"
#include "rotate.h"
#include "cmd.h"
#include "session.h"
#include "sessiond-timer.h"

#include <urcu.h>
#include <urcu/list.h>
#include <urcu/rculfhash.h>

struct cds_lfht *channel_pending_rotate_ht;
struct lttng_notification_channel *notification_channel = NULL;

static
void channel_rotation_info_destroy(struct rotation_channel_info *channel_info)
{
	assert(channel_info);
	free(channel_info);
}

static
int match_channel_info(struct cds_lfht_node *node, const void *key)
{
	struct rotation_channel_key *channel_key = (struct rotation_channel_key *) key;
	struct rotation_channel_info *channel_info;

	channel_info = caa_container_of(node, struct rotation_channel_info,
			rotate_channels_ht_node);

	return !!((channel_key->key == channel_info->channel_key.key) &&
			(channel_key->domain == channel_info->channel_key.domain));
}

static
struct rotation_channel_info *lookup_channel_pending(uint64_t key,
		enum lttng_domain_type domain)
{
	struct cds_lfht_iter iter;
	struct cds_lfht_node *node;
	struct rotation_channel_info *channel_info = NULL;
	struct rotation_channel_key channel_key = { .key = key,
		.domain = domain };

	cds_lfht_lookup(channel_pending_rotate_ht,
			hash_channel_key(&channel_key),
			match_channel_info,
			&channel_key, &iter);
	node = cds_lfht_iter_get_node(&iter);
	if (!node) {
		goto end;
	}

	channel_info = caa_container_of(node, struct rotation_channel_info,
			rotate_channels_ht_node);
	cds_lfht_del(channel_pending_rotate_ht, node);
end:
	return channel_info;
}

/*
 * Destroy the thread data previously created by the init function.
 */
void rotation_thread_handle_destroy(
		struct rotation_thread_handle *handle)
{
	int ret;

	if (!handle) {
		goto end;
	}

	if (handle->ust32_consumer >= 0) {
		ret = close(handle->ust32_consumer);
		if (ret) {
			PERROR("close 32-bit consumer channel rotation pipe");
		}
	}
	if (handle->ust64_consumer >= 0) {
		ret = close(handle->ust64_consumer);
		if (ret) {
			PERROR("close 64-bit consumer channel rotation pipe");
		}
	}
	if (handle->kernel_consumer >= 0) {
		ret = close(handle->kernel_consumer);
		if (ret) {
			PERROR("close kernel consumer channel rotation pipe");
		}
	}
	if (handle->client_rotate_pipe >= 0) {
		ret = close(handle->client_rotate_pipe);
		if (ret) {
			PERROR("close client command rotation pipe");
		}
	}

end:
	free(handle);
}

struct rotation_thread_handle *rotation_thread_handle_create(
		struct lttng_pipe *ust32_channel_rotate_pipe,
		struct lttng_pipe *ust64_channel_rotate_pipe,
		struct lttng_pipe *kernel_channel_rotate_pipe,
		struct lttng_pipe *client_rotate_pipe,
		int thread_quit_pipe, int rotate_timer_pipe)
{
	struct rotation_thread_handle *handle;

	handle = zmalloc(sizeof(*handle));
	if (!handle) {
		goto end;
	}

	if (ust32_channel_rotate_pipe) {
		handle->ust32_consumer =
				lttng_pipe_release_readfd(
					ust32_channel_rotate_pipe);
		if (handle->ust32_consumer < 0) {
			goto error;
		}
	} else {
		handle->ust32_consumer = -1;
	}
	if (ust64_channel_rotate_pipe) {
		handle->ust64_consumer =
				lttng_pipe_release_readfd(
					ust64_channel_rotate_pipe);
		if (handle->ust64_consumer < 0) {
			goto error;
		}
	} else {
		handle->ust64_consumer = -1;
	}
	if (kernel_channel_rotate_pipe) {
		handle->kernel_consumer =
				lttng_pipe_release_readfd(
					kernel_channel_rotate_pipe);
		if (handle->kernel_consumer < 0) {
			goto error;
		}
	} else {
		handle->kernel_consumer = -1;
	}
	if (client_rotate_pipe) {
		handle->client_rotate_pipe =
				lttng_pipe_release_readfd(
					client_rotate_pipe);
		if (handle->client_rotate_pipe < 0) {
			goto error;
		}
	} else {
		handle->client_rotate_pipe = -1;
	}
	handle->thread_quit_pipe = thread_quit_pipe;
	handle->rotate_timer_pipe = rotate_timer_pipe;

end:
	return handle;
error:
	rotation_thread_handle_destroy(handle);
	return NULL;
}

static
int init_poll_set(struct lttng_poll_event *poll_set,
		struct rotation_thread_handle *handle)
{
	int ret;

	/*
	 * Create pollset with size 6:
	 *	- sessiond quit pipe
	 *	- consumerd (32-bit user space) channel rotate pipe,
	 *	- consumerd (64-bit user space) channel rotate pipe,
	 *	- consumerd (kernel) channel rotate pipe,
	 *	- sessiond rotate pending pipe,
	 *	- sessiond thread manage client pipe (for session size polling).
	 */
	ret = lttng_poll_create(poll_set, 6, LTTNG_CLOEXEC);
	if (ret < 0) {
		goto end;
	}

	ret = lttng_poll_add(poll_set, handle->thread_quit_pipe,
			LPOLLIN | LPOLLERR);
	if (ret < 0) {
		ERR("[rotation-thread] Failed to add thread_quit_pipe fd to pollset");
		goto error;
	}
	ret = lttng_poll_add(poll_set, handle->rotate_timer_pipe,
			LPOLLIN | LPOLLERR);
	if (ret < 0) {
		ERR("[rotation-thread] Failed to add rotate_pending fd to pollset");
		goto error;
	}
	ret = lttng_poll_add(poll_set, handle->ust32_consumer,
			LPOLLIN | LPOLLERR);
	if (ret < 0) {
		ERR("[rotation-thread] Failed to add ust-32 channel rotation pipe fd to pollset");
		goto error;
	}
	ret = lttng_poll_add(poll_set, handle->ust64_consumer,
			LPOLLIN | LPOLLERR);
	if (ret < 0) {
		ERR("[rotation-thread] Failed to add ust-64 channel rotation pipe fd to pollset");
		goto error;
	}
	ret = lttng_poll_add(poll_set, handle->kernel_consumer,
			LPOLLIN | LPOLLERR);
	if (ret < 0) {
		ERR("[rotation-thread] Failed to add kernel channel rotation pipe fd to pollset");
		goto error;
	}
	ret = lttng_poll_add(poll_set, handle->client_rotate_pipe,
			LPOLLIN | LPOLLERR);
	if (ret < 0) {
		ERR("[rotation-thread] Failed to add kernel channel rotation pipe fd to pollset");
		goto error;
	}

end:
	return ret;
error:
	lttng_poll_clean(poll_set);
	return ret;
}

static
void fini_thread_state(struct rotation_thread_state *state)
{
	lttng_poll_clean(&state->events);
	if (notification_channel) {
		lttng_notification_channel_destroy(notification_channel);
	}
}

static
int init_thread_state(struct rotation_thread_handle *handle,
		struct rotation_thread_state *state)
{
	int ret;

	memset(state, 0, sizeof(*state));
	lttng_poll_init(&state->events);

	ret = init_poll_set(&state->events, handle);
	if (ret) {
		goto end;
	}

	channel_pending_rotate_ht = cds_lfht_new(DEFAULT_HT_SIZE,
			1, 0, CDS_LFHT_AUTO_RESIZE | CDS_LFHT_ACCOUNTING, NULL);
	if (!channel_pending_rotate_ht) {
		ret = -1;
	}

end:
	return 0;
}

static
int handle_channel_rotation_pipe(int fd, uint32_t revents,
		struct rotation_thread_handle *handle,
		struct rotation_thread_state *state)
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
			", domain %d\n", key, domain);

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
		ERR("[rotation-thread] Session %" PRIu64 " not found",
				channel_info->session_id);
		ret = -1;
		goto end_unlock;
	}

	if (--session->nr_chan_rotate_pending == 0) {
		time_t now = time(NULL);

		if (now == (time_t) -1) {
			session->rotate_status = LTTNG_ROTATE_ERROR;
			ret = LTTNG_ERR_ROTATE_NOT_AVAILABLE;
			goto end;
		}
		session_lock(session);

		ret = rename_complete_chunk(session, now);
		if (ret < 0) {
			ERR("Failed to rename completed rotation chunk");
			session_unlock(session);
			goto end;
		}
		session->rotate_pending = false;
		session->rotate_status = LTTNG_ROTATE_COMPLETED;
		session->last_chunk_start_ts = session->current_chunk_start_ts;
		if (session->rotate_pending_relay) {
			ret = sessiond_timer_rotate_pending_start(
					session,
					DEFAULT_ROTATE_PENDING_RELAY_TIMER);
			if (ret) {
				ERR("Enabling rotate pending timer");
				ret = -1;
				session_unlock(session);
				goto end;
			}
		}
		session_unlock(session);
	}

	channel_rotation_info_destroy(channel_info);

	ret = 0;

end_unlock:
	session_unlock_list();
	rcu_read_unlock();
end:
	return ret;
}

static
int rotate_pending_relay_timer(struct ltt_session *session)
{
	int ret;

	DBG("[rotation-thread] Check rotate pending on session %" PRIu64,
			session->id);
	ret = relay_rotate_pending(session, session->rotate_count - 1);
	if (ret < 0) {
		ERR("[rotation-thread] Check relay rotate pending");
		goto end;
	}
	if (ret == 0) {
		DBG("[rotation-thread] Rotation completed on the relay for "
				"session %" PRIu64, session->id);
		/*
		 * Stop the timer and clear the queue, the timers are currently
		 * ignored because of the rotate_pending_relay_check_in_progress
		 * flag.
		 */
		sessiond_timer_rotate_pending_stop(session);
		/*
		 * Now we can clear the pending flag in the session. New
		 * rotations can start now.
		 */
		session->rotate_pending_relay = false;
	} else if (ret == 1) {
		DBG("[rotation-thread] Rotation still pending on the relay for "
				"session %" PRIu64, session->id);
	}
	/*
	 * Allow the timer thread to send other notifications when needed.
	 */
	session->rotate_pending_relay_check_in_progress = false;

	ret = 0;

end:
	return ret;
}

static
int rotate_timer(struct ltt_session *session)
{
	int ret;

	DBG("[rotation-thread] Rotate timer on session %" PRIu64, session->id);

	/*
	 * If the session is stopped, we need to cancel this timer.
	 */
	session_lock(session);
	if (!session->active && session->rotate_timer_enabled) {
		sessiond_rotate_timer_stop(session);
	}

	ret = cmd_rotate_session(session, NULL);
	session_unlock(session);
	if (ret == -LTTNG_ERR_ROTATE_PENDING) {
		ret = 0;
		goto end;
	} else if (ret != LTTNG_OK) {
		ret = -1;
		goto end;
	}

	ret = 0;

end:
	return ret;
}

static
int handle_rotate_timer_pipe(int fd, uint32_t revents,
		struct rotation_thread_handle *handle,
		struct rotation_thread_state *state)
{
	int ret = 0;
	struct ltt_session *session;
	struct sessiond_rotation_timer timer_data;

	if (revents & (LPOLLERR | LPOLLHUP | LPOLLRDHUP)) {
		ret = lttng_poll_del(&state->events, fd);
		if (ret) {
			ERR("[rotation-thread] Failed to remove consumer "
					"rotate pending pipe from poll set");
		}
		goto end;
	}

	memset(&timer_data, 0, sizeof(struct sessiond_rotation_timer));

	do {
		ret = read(fd, &timer_data, sizeof(timer_data));
	} while (ret == -1 && errno == EINTR);
	if (ret != sizeof(timer_data)) {
		ERR("[rotation-thread] Failed to read from pipe (fd = %i)",
				fd);
		ret = -1;
		goto end;
	}

	rcu_read_lock();
	session_lock_list();
	session = session_find_by_id(timer_data.session_id);
	if (!session) {
		ERR("[rotation-thread] Session %" PRIu64 " not found",
				timer_data.session_id);
		/*
		 * This is a non-fatal error, and we cannot report it to the
		 * user (timer), so just print the error and continue the
		 * processing.
		 */
		ret = 0;
		goto end_unlock;
	}

	if (timer_data.signal == LTTNG_SESSIOND_SIG_ROTATE_PENDING) {
		ret = rotate_pending_relay_timer(session);
	} else if (timer_data.signal == LTTNG_SESSIOND_SIG_ROTATE_TIMER) {
		ret = rotate_timer(session);
	} else {
		ERR("Unknown signal in rotate timer");
		ret = -1;
	}

end_unlock:
	session_unlock_list();
	rcu_read_unlock();
end:
	return ret;
}

static
int subscribe_session_usage(struct ltt_session *session, uint64_t size)
{
	int ret;
	enum lttng_condition_status condition_status;
	enum lttng_notification_channel_status nc_status;

	session->condition = lttng_condition_session_usage_consumed_create();
	if (!session->condition) {
		ERR("Create condition object");
		ret = -1;
		goto end;
	}

	condition_status = lttng_condition_session_usage_consumed_set_threshold(
			session->condition, size);
	if (condition_status != LTTNG_CONDITION_STATUS_OK) {
		ERR("Could not set threshold");
		ret = -1;
		goto end;
	}

	condition_status = lttng_condition_session_usage_set_session_name(
			session->condition, session->name);
	if (condition_status != LTTNG_CONDITION_STATUS_OK) {
		ERR("Could not set session name");
		ret = -1;
		goto end;
	}

	session->action = lttng_action_notify_create();
	if (!session->action) {
		ERR("Could not create action notify");
		ret = -1;
		goto end;
	}

	session->trigger = lttng_trigger_create(session->condition,
			session->action);
	if (!session->trigger) {
		ERR("Could not create trigger");
		ret = -1;
		goto end;
	}

	ret = lttng_register_trigger(session->trigger);
	if (ret < 0 && ret != -LTTNG_ERR_TRIGGER_EXISTS) {
		ERR("Register trigger, %s\n", lttng_strerror(ret));
		ret = -1;
		goto end;
	}

	nc_status = lttng_notification_channel_subscribe(
			notification_channel, session->condition);
	if (nc_status != LTTNG_NOTIFICATION_CHANNEL_STATUS_OK) {
		ERR("Could not subscribe\n");
		ret = -1;
		goto end;
	}

	ret = 0;

end:
	return ret;
}

static
void unsubscribe_session_usage(struct ltt_session *session)
{
	lttng_unregister_trigger(session->trigger);

	if (lttng_notification_channel_unsubscribe(notification_channel,
				session->condition)) {
		ERR("Session unsubscribe error");
	}
	lttng_trigger_destroy(session->trigger);
	lttng_condition_destroy(session->condition);
	lttng_action_destroy(session->action);
}

int handle_condition(
		const struct lttng_condition *condition,
		const struct lttng_evaluation *evaluation)
{
	int ret = 0;
	const char *condition_session_name = NULL;
	enum lttng_condition_type condition_type;
	enum lttng_evaluation_status evaluation_status;
	uint64_t consumed;
	struct ltt_session *session;

	condition_type = lttng_condition_get_type(condition);

	if (condition_type != LTTNG_CONDITION_TYPE_SESSION_USAGE_CONSUMED) {
		ret = 1;
		printf("error: condition type and session usage type are not the same\n");
		goto end;
	}

	/* Fetch info to test */
	ret = lttng_condition_session_usage_get_session_name(condition,
			&condition_session_name);
	if (ret) {
		printf("error: session name could not be fetched\n");
		ret = 1;
		goto end;
	}
	evaluation_status = lttng_evaluation_session_usage_get_consumed(evaluation,
			&consumed);
	if (evaluation_status != LTTNG_EVALUATION_STATUS_OK) {
		ERR("Failed to get evaluation");
		ret = -1;
		goto end;
	}

	session = session_find_by_name(condition_session_name);
	if (!session) {
		ret = -1;
		ERR("Session not found");
		goto end;
	}

	printf("notification: %lu\n", consumed);
	unsubscribe_session_usage(session);
	subscribe_session_usage(session, consumed + session->rotate_size);
	ret = 0;

end:
	return ret;
}

/*
 * Handle messages coming from the sessiond (thread_manage_client).
 */
static
int handle_manage_client(int fd, uint32_t revents,
		struct rotation_thread_handle *handle,
		struct rotation_thread_state *state,
		struct lttng_poll_event *poll_set)
{
	int ret = 0;
	struct rotation_thread_msg msg;

	if (revents & (LPOLLERR | LPOLLHUP | LPOLLRDHUP)) {
		ret = lttng_poll_del(&state->events, fd);
		if (ret) {
			ERR("[rotation-thread] Failed to remove manage client pipe");
			goto end;
		}
		goto end;
	}

	/*
	 * First time, we create the notification channel and add it to the
	 * poll set.
	 */
	if (!notification_channel) {
		notification_channel = lttng_notification_channel_create(
				lttng_session_daemon_notification_endpoint);
		if (!notification_channel) {
			printf("error: Could not create notification channel\n");
			ret = 1;
			goto end;
		}
		ret = lttng_poll_add(poll_set, notification_channel->socket,
				LPOLLIN | LPOLLERR);
		if (ret < 0) {
			ERR("[rotation-thread] Failed to add notification fd to pollset");
			goto end;
		}
	}

	ret = lttng_read(fd, &msg, sizeof(msg));
	if (ret < sizeof(msg)) {
		PERROR("Read manage client data");
		ret = -1;
		goto end;
	}

	switch (msg.cmd) {
	case ROTATION_THREAD_SUBSCRIBE_SESSION_USAGE:
	{
		uint64_t size = msg.subscribe_session_usage.size;
		struct ltt_session *session = msg.subscribe_session_usage.session;

		assert(session);

		if (!session->rotate_size && size != -1ULL) {
			ret = subscribe_session_usage(session, size);
			if (ret) {
				ERR("[rotation-thread] Subscribe session usage");
				goto end;
			}
			session->rotate_size = size;
		} else if (session->rotate_size && size == -1ULL) {
			unsubscribe_session_usage(session);
			session->rotate_size = 0;
		} else {
			ERR("[rotation-thread] Invalid size parameter, current rotate_size: %"
					PRIu64 ", new size: %" PRIu64, session->rotate_size, size);
			ret = -1;
			goto end;
		}
		break;
	}
	default:
		ERR("Wrong command");
		ret = -1;
		goto end;
	}

	ret = 0;
	
end:
	return ret;
}

static
int handle_sampling_notification(int fd, uint32_t revents,
		struct rotation_thread_handle *handle,
		struct rotation_thread_state *state)
{
	int ret;
	struct lttng_notification *notification;
	enum lttng_notification_channel_status status;
	const struct lttng_evaluation *notification_evaluation;
	const struct lttng_condition *notification_condition;

	/* Receive the next notification. */
	status = lttng_notification_channel_get_next_notification(
			notification_channel,
			&notification);

	switch (status) {
	case LTTNG_NOTIFICATION_CHANNEL_STATUS_OK:
		break;
	case LTTNG_NOTIFICATION_CHANNEL_STATUS_NOTIFICATIONS_DROPPED:
		/* Not an error, we will wait for the next one */
		ret = 0;
		goto end;;
	case LTTNG_NOTIFICATION_CHANNEL_STATUS_CLOSED:
		ERR("Notification channel was closed\n");
		ret = -1;
		goto end;
	default:
		/* Unhandled conditions / errors. */
		ERR("Unknown notification channel status\n");
		ret = -1;
		goto end;
	}

	notification_condition = lttng_notification_get_condition(notification);
	notification_evaluation = lttng_notification_get_evaluation(notification);

	ret = handle_condition(notification_condition, notification_evaluation);

end:
	lttng_notification_destroy(notification);
	if (ret != 0) {
		goto end;
	}


	return ret;
}

void *thread_rotation(void *data)
{
	int ret;
	struct rotation_thread_handle *handle = data;
	struct rotation_thread_state state;

	DBG("[rotation-thread] Started rotation thread");

	if (!handle) {
		ERR("[rotation-thread] Invalid thread context provided");
		goto end;
	}

	rcu_register_thread();
	rcu_thread_online();

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

			if (fd == handle->thread_quit_pipe) {
				DBG("[rotation-thread] Quit pipe activity");
				goto exit;
			} else if (fd == handle->rotate_timer_pipe) {
				ret = handle_rotate_timer_pipe(fd, revents,
						handle, &state);
				if (ret) {
					ERR("[rotation-thread] Rotate timer");
					goto error;
				}
			} else if (fd == handle->ust32_consumer ||
					fd == handle->ust64_consumer ||
					fd == handle->kernel_consumer) {
				ret = handle_channel_rotation_pipe(fd,
						revents, handle, &state);
				if (ret) {
					ERR("[rotation-thread] Exit main loop");
					goto error;
				}
			} else if (fd == handle->client_rotate_pipe) {
				ret = handle_manage_client(fd, revents, handle, &state,
						&state.events);
				if (ret) {
					ERR("[rotation-thread] manage client");
					goto error;
				}
			} else if (fd == notification_channel->socket) {
				ret = handle_sampling_notification(fd, revents, handle, &state);
				if (ret) {
					ERR("[rotation-thread] handle sample");
					goto error;
				}
			}
		}
	}
exit:
error:
	fini_thread_state(&state);
	health_unregister(health_sessiond);
	rcu_thread_offline();
	rcu_unregister_thread();
end:
	return NULL;
}
