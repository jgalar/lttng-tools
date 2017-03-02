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
#include <common/error.h>
#include <common/config/session-config.h>
#include <common/defaults.h>
#include <common/utils.h>
#include <sys/eventfd.h>
#include <sys/stat.h>

#include "notification-thread.h"
#include "lttng-sessiond.h"
#include "health-sessiond.h"

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
	cds_wfcq_init(&data->cmd_queue.head, &data->cmd_queue.tail);
end:
	return data;
error:
	notification_destroy_data(data);
	return NULL;
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
		puts(home_path);

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

	if (!ctx) {
		ERR("[notification-thread] Invalid thread context provided");
		goto end;
	}

	rcu_register_thread();
	rcu_thread_online();

	health_register(health_sessiond, HEALTH_SESSIOND_TYPE_NOTIFICATION);
	health_code_update();

	notification_channel_socket = notification_channel_socket_create();

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

			/* Thread quit pipe has been closed. Killing thread. */
			if (sessiond_check_thread_quit_pipe(fd, revents)) {
				DBG("[notification-thread] Quit pipe signaled, exiting.");
				goto exit;
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
end:
	return NULL;
}
