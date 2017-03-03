/*
 * Copyright (C) 2017 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
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
#include <lttng/notification/channel-internal.h>
#include <lttng/notification/notification-internal.h>
#include <lttng/condition/condition-internal.h>
#include <lttng/condition/buffer-usage-internal.h>
#include <common/error.h>
#include <common/config/session-config.h>
#include <common/defaults.h>
#include <common/utils.h>
#include <common/align.h>
#include <common/time.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>

#include "notification-thread.h"
#include "lttng-sessiond.h"
#include "health-sessiond.h"

#include <urcu/list.h>

#define CLIENT_RECEPTION_BUFFER_SIZE	(4 * PAGE_SIZE)
#define SIMULATION_TIMER_INTERVAL_NS 	2 * NSEC_PER_SEC
#define SIMULATION_TIMER_SIGNAL		SIGRTMIN + 10

static int simulation_timer_event_fd = -1;
static timer_t simulation_timer;

struct client {
	int socket;
	struct cds_list_head list_node;
	/*
	 * Conditions to which the client is registered.
	 */
	struct cds_list_head condition_list;
};

static struct cds_list_head client_list;
static struct cds_list_head trigger_list;
static char *client_reception_buffer;

/*
 * The simulation timer will alternate between "buffers" between full and
 * empty values, firing all low/high usage triggers in alternance.
 */
static pthread_mutex_t simulation_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t simulation_buffer_use_bytes;
static double simulation_buffer_use_ratio = 0.0;
static uint64_t simulation_buffer_capacity = UINT32_MAX;

/*
 * Destroy the thread data previously created by the init function.
 */
void notification_destroy_data(struct notification_thread_data *data)
{
	int ret;

	if (!data) {
		goto end;
	}

	if (data->cmd_queue.event_fd < 0) {
		goto end;
	}
	ret = close(data->cmd_queue.event_fd);
	if (ret < 0) {
		PERROR("close notification command queue event_fd");
	}

	pthread_mutex_destroy(&data->cmd_queue.lock);
	/* TODO: purge queue and mark commands as cancelled. */
end:
	free(data);
}

/*
 * Initialize the thread's data. This MUST be called before the notification
 * thread is started.
 */
struct notification_thread_data *notification_init_data(void)
{
	int ret;
	struct notification_thread_data *data;

	data = zmalloc(sizeof(*data));
	if (!data) {
		goto end;
	}

	data->cmd_queue.event_fd = eventfd(0, EFD_CLOEXEC);
	if (data->cmd_queue.event_fd < 0) {
		PERROR("eventfd notification command queue");
		goto error;
	}
	CDS_INIT_LIST_HEAD(&data->cmd_queue.list);
	ret = pthread_mutex_init(&data->cmd_queue.lock, NULL);
	if (ret) {
		goto error;
	}
end:
	return data;
error:
	notification_destroy_data(data);
	return NULL;
}

static
void simulation_timer_thread(union sigval val)
{
	int ret;
	uint64_t counter = 1;

	pthread_mutex_lock(&simulation_lock);
	if (simulation_buffer_use_bytes == 0) {
		simulation_buffer_use_bytes = UINT32_MAX;
		simulation_buffer_use_ratio = 1.0;
	} else {
		simulation_buffer_use_bytes = 0;
		simulation_buffer_use_ratio = 0.0;
	}
	pthread_mutex_unlock(&simulation_lock);
	ret = write(simulation_timer_event_fd, &counter, sizeof(counter));
	if (ret < 0) {
		PERROR("writer simulation timer event fd");
	}
}

static
int simulation_timer_start(void)
{
	int ret;
	struct sigevent sev;
	struct itimerspec its;

	ret = eventfd(0, EFD_CLOEXEC);
	if (ret < 0) {
		PERROR("eventfd simulation timer event fd");
		goto error;
	}
	simulation_timer_event_fd = ret;

	sev.sigev_notify = SIGEV_THREAD;
	sev.sigev_value.sival_ptr = NULL;
	sev.sigev_notify_function = simulation_timer_thread;
	sev.sigev_notify_attributes = NULL;

	/*
	 * Valgrind indicates a leak when timer_create() is used
	 * in the "SIGEV_THREAD" mode. This bug has been known to upstream glibc
	 * since 2009, but no fix has been implemented so far.
	 */
	ret = timer_create(CLOCK_MONOTONIC, &sev, &simulation_timer);
	if (ret < 0) {
		PERROR("timer_create simulation timer");
		goto error;
	}

	its.it_value.tv_sec = SIMULATION_TIMER_INTERVAL_NS / NSEC_PER_SEC;
	its.it_value.tv_nsec = (SIMULATION_TIMER_INTERVAL_NS % NSEC_PER_SEC);
	its.it_interval.tv_sec = its.it_value.tv_sec;
	its.it_interval.tv_nsec = its.it_value.tv_nsec;

	ret = timer_settime(simulation_timer, 0, &its, NULL);
	if (ret < 0) {
		PERROR("timer_settime simulation timer");
		goto error;
	}

	return 0;
error:
	return -1;
}

static
void simulation_timer_stop(void)
{
	int ret;

	ret = timer_delete(simulation_timer);
	if (ret == -1) {
		PERROR("timer_delete simulation timer");
	}
}

static
char *get_notification_channel_sock_path(void)
{
	int ret;
	bool is_root = !getuid();
	char *sock_path;

	sock_path = zmalloc(LTTNG_PATH_MAX);
	if (!sock_path) {
		goto error;
	}

	if (is_root) {
		ret = snprintf(sock_path, LTTNG_PATH_MAX,
				DEFAULT_GLOBAL_NOTIFICATION_CHANNEL_UNIX_SOCK);
		if (ret < 0) {
			goto error;
		}
	} else {
		char *home_path = utils_get_home_dir();

		if (!home_path) {
			ERR("Can't get HOME directory for socket creation");
			goto error;
		}

		ret = snprintf(sock_path, LTTNG_PATH_MAX,
				DEFAULT_HOME_NOTIFICATION_CHANNEL_UNIX_SOCK,
				home_path);
		if (ret < 0) {
			goto error;
		}
	}

	return sock_path;
error:
	free(sock_path);
	return NULL;
}

static
void notification_channel_socket_destroy(int fd)
{
	int ret;
	char *sock_path = get_notification_channel_sock_path();

	DBG("[notification-thread] Destroying notification channel socket");

	if (sock_path) {
		ret = unlink(sock_path);
		free(sock_path);
		if (ret < 0) {
			PERROR("unlink notification channel socket");
		}
	}

	ret = close(fd);
	if (ret) {
		PERROR("close notification channel socket");
	}
}

static
int notification_channel_socket_create(void)
{
	int fd = -1, ret;
	char *sock_path = get_notification_channel_sock_path();

	DBG("[notification-thread] Creating notification channel UNIX socket at %s",
			sock_path);

	ret = lttcomm_create_unix_sock(sock_path);
	if (ret < 0) {
		ERR("[notification-thread] Failed to create notification socket");
		goto error;
	}
	fd = ret;

	ret = chmod(sock_path, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
	if (ret < 0) {
		ERR("Set file permissions failed: %s", sock_path);
		PERROR("chmod notification channel socket");
		goto error;
	}

	DBG("[notification-thread] Notification channel UNIX socket created (fd = %i)",
			fd);
	free(sock_path);
	return fd;
error:
	if (fd >= 0 && close(fd) < 0) {
		PERROR("close notification channel socket");
	}
	free(sock_path);
	return ret;
}

static
int handle_new_connection(int socket)
{
	int ret;
	struct client *client;

	DBG("[notification-thread] Handling new notification channel client connection");

	client = zmalloc(sizeof(*client));
	if (!client) {
		goto error;
	}

	ret = lttcomm_accept_unix_sock(socket);
	if (ret < 0) {
		ERR("[notification-thread] Failed to accept new notification channel client connection");
		goto error;
	}

	client->socket = ret;
	CDS_INIT_LIST_HEAD(&client->condition_list);

	/* FIXME handle creds. */
	ret = lttcomm_setsockopt_creds_unix_sock(socket);
	if (ret < 0) {
		ERR("[notification-thread] Failed to set socket options on new notification channel client socket");
		goto error;
	}

	cds_list_add(&client->list_node, &client_list);
	return client->socket;
error:
	free(client);
	return -1;
}

static
int send_command_reply(int socket,
		enum lttng_notification_channel_status status)
{
	ssize_t ret;
	struct lttng_notification_channel_command_reply reply = {
		.status = (int8_t) status,
	};

	DBG("[notification-thread] Send command reply (%i)", (int) status);

	ret = lttcomm_send_unix_sock(socket, &reply, sizeof(reply));
	if (ret < 0) {
		ERR("[notification-thread] Failed to send command reply");
		goto error;
	}
	return 0;
error:
	return -1;
}

static
struct client *get_client_from_fd(int fd)
{
	struct client *client;

	cds_list_for_each_entry(client, &client_list, list_node) {
		if (client->socket == fd) {
			return client;
		}
	}
	return NULL;
}

static
int handle_notification_channel_client(int socket)
{
	ssize_t ret;
	size_t received = 0;
	struct client *client = get_client_from_fd(socket);
	struct lttng_condition *condition;
	struct lttng_notification_channel_command command;
	enum lttng_notification_channel_status status =
			LTTNG_NOTIFICATION_CHANNEL_STATUS_OK;
	struct lttng_trigger *trigger;

	assert(client);

	/* Receive command header. */
	do
	{
		ret = lttcomm_recv_unix_sock(socket, ((char *) &command) + received,
				sizeof(command) - received);
		if (ret <= 0) {
			ERR("[notification-thread] Failed to receive channel command from client (received %zu bytes)", received);
			goto error_no_reply;
		}
		received += ret;
	} while (received < sizeof(command));

	received = 0;
	if (command.size >= CLIENT_RECEPTION_BUFFER_SIZE) {
		ERR("[notification-thread] Notification channel client attempted to send condition larger (%u bytes) than client reception buffer (%u bytes)",
				command.size,
				(unsigned int) CLIENT_RECEPTION_BUFFER_SIZE);
		goto error_no_reply;
	}

	do
	{
		ret = lttcomm_recv_unix_sock(socket,
				client_reception_buffer + received,
				command.size - received);
		if (ret <= 0) {
			ERR("[notification-thread] Failed to receive condition from client");
			goto error_no_reply;
		}
		received += ret;
	} while (received < sizeof(command));

	ret = lttng_condition_create_from_buffer(client_reception_buffer,
			&condition);
	if (ret < 0 || ret < command.size) {
		ERR("[notification-thread] Malformed condition received from client");
		goto error_no_reply;
	}

	DBG("[notification-thread] Successfully received condition from notification channel client");

	/*
	 * A client may only listen for a condition that is currently associated
	 * with a trigger known to the system.
	 */
	DBG("[notification-thread] Comparing registered condition to known trigger conditions");
	cds_list_for_each_entry(trigger, &trigger_list, list_node) {
		struct lttng_condition *trigger_condition =
				lttng_trigger_get_condition(trigger);

		if (!trigger_condition) {
			ERR("[notification-thread] lttng_trigger_get_condition returned NULL");
			status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
			goto end;
		}

		if (lttng_condition_is_equal(trigger_condition, condition)) {
			/* Matching condition found. */
			DBG("[notification-thread] Found a matching condition, accepting client subscription request");
			cds_list_add(&condition->list_node,
					&client->condition_list);
			goto end;
		}
	}

	/* No match found, refuse the subscription. */
	DBG("[notification-thread] No matching condition found, refusing client subscription request");
	status = LTTNG_NOTIFICATION_CHANNEL_STATUS_UNKNOWN;
end:
	if (send_command_reply(socket, status)) {
		goto error_no_reply;
	}
	return 0;
error_no_reply:
	return -1;
}

static
void client_destroy(struct client *client)
{
	struct lttng_condition *condition, *tmp;

	cds_list_for_each_entry_safe(condition, tmp, &client->condition_list,
			list_node) {
		cds_list_del(&condition->list_node);
		lttng_condition_destroy(condition);
	}

	(void) lttcomm_close_unix_sock(client->socket);
	free(client);
}

static
void clean_up_notification_channel_client(int socket)
{
	struct client *client;

	DBG("[notification-thread] Searching for client data for clean-up");
	cds_list_for_each_entry(client, &client_list, list_node) {
		if (client->socket == socket) {
			DBG("[notification-thread] Client data found for clean-up");
			cds_list_del(&client->list_node);
			client_destroy(client);
			return;
		}
	}
	ERR("[notification-thread] Failed to clean-up client data");
}

static
void activate_triggers(struct cds_list_head *new_triggers_list)
{
	struct lttng_trigger *trigger, *tmp;

	DBG("[notification-thread] Moving triggers from new list to activated trigger set");
	cds_list_for_each_entry_safe(trigger, tmp, new_triggers_list, list_node) {
		cds_list_del(&trigger->list_node);
		cds_list_add(&trigger->list_node, &trigger_list);
	}
}

static
void clean_up_triggers(void)
{
	struct lttng_trigger *trigger, *tmp;

	DBG("[notification-thread] Cleaning up triggers");
	cds_list_for_each_entry_safe(trigger, tmp, &trigger_list, list_node) {
		DBG("[notification-thread] Destroying trigger");
		cds_list_del(&trigger->list_node);
		lttng_trigger_destroy(trigger);
	}
}

static
struct lttng_evaluation *evaluate_buffer_usage_condition(
		struct lttng_condition *_condition)
{
	uint64_t threshold;
	struct lttng_evaluation *evaluation = NULL;
	struct lttng_condition_buffer_usage *condition = container_of(
			_condition, struct lttng_condition_buffer_usage,
			parent);

	if (condition->threshold_bytes.set) {
		threshold = condition->threshold_bytes.value;
	} else {
		/* Threshold was expressed as a ratio. */
		threshold = (uint64_t) (condition->threshold_ratio.value *
				(double) simulation_buffer_capacity);
	}

	if (condition->parent.type ==
			LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW) {
		if (simulation_buffer_use_bytes <= threshold) {
			evaluation = lttng_evaluation_buffer_usage_create(
					condition->parent.type,
					simulation_buffer_use_bytes,
					simulation_buffer_capacity);
		}
	} else {
		if (simulation_buffer_use_bytes >= threshold) {
			evaluation = lttng_evaluation_buffer_usage_create(
					condition->parent.type,
					simulation_buffer_use_bytes,
					simulation_buffer_capacity);
		}
	}
	return evaluation;
}

static
void notify_client(struct client *client, struct lttng_condition *condition,
		struct lttng_evaluation *evaluation)
{
	ssize_t notification_size, ret;
	char *notification_buffer;
	struct lttng_notification *notification;

	notification = lttng_notification_create(condition, evaluation);
	if (!notification) {
		ERR("[notification-thread] Failed to create client notification");
		return;
	}

	notification_size = lttng_notification_serialize(notification, NULL);
	if (notification_size < 0) {
		ERR("[notification-thread] Failed to get size of serialized notification");
		return;
	}

	notification_buffer = zmalloc(notification_size);
	if (!notification_buffer) {
		ERR("[notification-thread] Failed to allocate notification serialization buffer");
	}

	ret = lttng_notification_serialize(notification, notification_buffer);
	if (ret != notification_size) {
		ERR("[notification-thread] Failed to serialize notification");
		return;
	}

	ret = lttcomm_send_unix_sock(client->socket, notification_buffer,
			notification_size);
	if (ret < 0) {
		ERR("[notification-thread] Failed to send notification to client");
		return;
	}
}

static
void evaluate_client_conditions(void)
{
	struct client *client;

	DBG("[notification-thread] Evaluating client conditions");
	cds_list_for_each_entry(client, &client_list, list_node) {
		struct lttng_condition *condition;
		cds_list_for_each_entry(condition, &client->condition_list,
				list_node) {
			struct lttng_evaluation *evaluation = NULL;
			switch (lttng_condition_get_type(condition)) {
			case LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW:
			case LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH:
				evaluation = evaluate_buffer_usage_condition(
						condition);
				break;
			default:
				ERR("[notification-thread] Unknown condition type encountered in evaluation");
				abort();
			}

			if (evaluation) {
				DBG("[notification-thread] Condition evaluated to true");
				notify_client(client, condition, evaluation);
				lttng_evaluation_destroy(evaluation);
			}
		}
	}
	DBG("[notification-thread] Client conditions evaluated");
}

/*
 * This thread services notification channel clients and received notifications
 * from various lttng-sessiond components over a command queue.
 */
void *thread_notification(void *data)
{
	int ret;
	struct lttng_poll_event events;
	int notification_channel_socket;
	struct notification_thread_data *ctx = data;

	DBG("[notification-thread] Started notification thread");

	CDS_INIT_LIST_HEAD(&client_list);
	CDS_INIT_LIST_HEAD(&trigger_list);

	simulation_timer_start();

	if (!ctx) {
		ERR("[notification-thread] Invalid thread context provided");
		goto end;
	}

	rcu_register_thread();
	rcu_thread_online();

	health_register(health_sessiond, HEALTH_SESSIOND_TYPE_NOTIFICATION);
	health_code_update();

	client_reception_buffer = zmalloc(CLIENT_RECEPTION_BUFFER_SIZE);
	if (!client_reception_buffer) {
		ERR("[notification-thread] Failed to allocate client reception buffer");
		goto end;
	}

	ret = notification_channel_socket_create();
	if (ret < 0) {
		goto end;
	}
	notification_channel_socket = ret;

	/*
	 * Create pollset with size 2, quit pipe and notification channel
	 * socket, and the command queue event fd.
	 */
	ret = sessiond_set_thread_pollset(&events, 3);
	if (ret < 0) {
		goto error_poll_create;
	}

	/* Add notification channel socket to poll set. */
	ret = lttng_poll_add(&events, notification_channel_socket,
			LPOLLIN | LPOLLERR | LPOLLHUP | LPOLLRDHUP);
	if (ret < 0) {
		ERR("[notification-thread] Failed to add notification channel socket to pollset");
		goto error;
	}

	ret = lttng_poll_add(&events, ctx->cmd_queue.event_fd,
			LPOLLIN | LPOLLERR);
	if (ret < 0) {
		ERR("[notification-thread] Failed to add notification command queue event fd to pollset");
		goto error;
	}

	ret = lttng_poll_add(&events, simulation_timer_event_fd,
			LPOLLIN | LPOLLERR);
	if (ret < 0) {
		ERR("[notification-thread] Failed to add timer event fd to pollset");
		goto error;
	}

	DBG("[notification-thread] Listening on notification channel socket");
	ret = lttcomm_listen_unix_sock(notification_channel_socket);
	if (ret < 0) {
		ERR("[notification-thread] Listen failed on notification channel socket");
		goto error;
	}

	while (true) {
		int fd_count, i;

		health_poll_entry();
		DBG("[notification-thread] Entering poll wait");
		ret = lttng_poll_wait(&events, -1);
		DBG("[notification-thread] Poll wait returned (%i)", ret);
		health_poll_exit();
		if (ret < 0) {
			/*
			 * Restart interrupted system call.
			 */
			if (errno == EINTR) {
				continue;
			}
			ERR("[notification-thread] Error encountered during lttng_poll_wait (%i)", ret);
			goto error;
		}

		fd_count = ret;
		for (i = 0; i < fd_count; i++) {
			int fd = LTTNG_POLL_GETFD(&events, i);
			uint32_t revents = LTTNG_POLL_GETEV(&events, i);

			DBG("[notification-thread] Handling fd (%i) activity (%u)", fd, revents);

			/* Thread quit pipe has been closed. Killing thread. */
			if (sessiond_check_thread_quit_pipe(fd, revents)) {
				DBG("[notification-thread] Quit pipe signaled, exiting.");
				goto exit;
			}

			if (fd == notification_channel_socket) {
				if (revents & LPOLLIN) {
					int new_socket;

					ret = handle_new_connection(
							notification_channel_socket);
					if (ret < 0) {
						continue;
					}
					new_socket = ret;

					ret = lttng_poll_add(&events, new_socket,
							LPOLLIN | LPOLLERR |
							LPOLLHUP | LPOLLRDHUP);
					if (ret < 0) {
						ERR("[notification-thread] Failed to add notification channel client socket to pollset");
						goto error;
					}
					DBG("[notification-thread] Added new notification channel client socket to poll set");
				} else if (revents &
						(LPOLLERR | LPOLLHUP | LPOLLRDHUP)) {
					ERR("[notification-thread] Notification socket poll error");
					goto error;
				} else {
					ERR("[notification-thread] Unexpected poll events %u for notification socket %i", revents, fd);
					goto error;
				}
			} else if (fd == ctx->cmd_queue.event_fd) {
				/*
				 * Handling of internaly-generated events to
				 * evaluate against the set of active
				 * conditions.
				 */
				uint64_t counter;

				DBG("[notification-thread] Event received on command queue event fd");
				ret = read(fd, &counter, sizeof(counter));
				if (ret < 0) {
					ERR("read on command queue event fd");
				}

				pthread_mutex_lock(&ctx->cmd_queue.lock);
				activate_triggers(&ctx->cmd_queue.list);
				pthread_mutex_unlock(&ctx->cmd_queue.lock);
			} else if (fd == simulation_timer_event_fd) {
				/*
				 * Place-holder timer to simulate activity in
				 * the system.
				 */
				uint64_t counter;

				DBG("[notification-thread] Simulation timer fired");
				ret = read(fd, &counter, sizeof(counter));
				if (ret < 0) {
					ERR("read on simulation timer event fd");
				}

				pthread_mutex_lock(&simulation_lock);
				evaluate_client_conditions();
				pthread_mutex_unlock(&simulation_lock);
			} else {
				if (revents & (LPOLLERR | LPOLLHUP | LPOLLRDHUP)) {
					/*
					 * It doesn't matter if a command was
					 * pending on the client socket at this
					 * point since it now has now way to
					 * receive the notifications to which
					 * it was subscribing or unsubscribing.
					 */
					DBG("[notification-thread] Closing client connection (fd = %i)", fd);
					clean_up_notification_channel_client(fd);
				} else if (revents & LPOLLIN) {
					ret = handle_notification_channel_client(fd);
					if (ret) {
						DBG("[notification-thread] Closing client connection following error");
						clean_up_notification_channel_client(fd);
					}
				} else {
					DBG("[notification-thread] Unexpected poll events %u for notification socket %i", revents, fd);
				}
			}
		}
	}
exit:

error:
	lttng_poll_clean(&events);
error_poll_create:
	notification_channel_socket_destroy(notification_channel_socket);
	health_unregister(health_sessiond);
	rcu_thread_offline();
	rcu_unregister_thread();
	free(client_reception_buffer);
	clean_up_triggers();
end:
	simulation_timer_stop();
	return NULL;
}
