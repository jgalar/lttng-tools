/*
 * Copyright (C) 2017 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License, version 2.1 only,
 * as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <lttng/notification/notification-internal.h>
#include <lttng/notification/channel-internal.h>
#include <lttng/condition/condition-internal.h>
#include <lttng/endpoint.h>
#include <common/error.h>
#include <common/utils.h>
#include <common/defaults.h>
#include <assert.h>
#include "lttng-ctl-helper.h"

struct lttng_notification_channel *lttng_notification_channel_create(
		struct lttng_endpoint *endpoint)
{
	int fd, ret;
	bool is_in_tracing_group = false, is_root = false;
	char *sock_path = NULL;
	struct lttng_notification_channel *channel = NULL;

	if (!endpoint ||
			endpoint != lttng_session_daemon_notification_endpoint) {
		goto end;
	}

	sock_path = zmalloc(LTTNG_PATH_MAX);
	if (!sock_path) {
		goto end;
	}

	channel = zmalloc(sizeof(struct lttng_notification_channel));
	if (!channel) {
		goto end;
	}
	channel->socket = -1;

	is_root = (getuid() == 0);
	if (!is_root) {
		is_in_tracing_group = lttng_check_tracing_group();
	}

	if (is_root || is_in_tracing_group) {
		lttng_ctl_copy_string(sock_path,
				DEFAULT_GLOBAL_NOTIFICATION_CHANNEL_UNIX_SOCK,
				LTTNG_PATH_MAX);
		ret = lttcomm_connect_unix_sock(sock_path);
		if (ret >= 0) {
			fd = ret;
			goto set_fd;
		}
	}

	/* Fallback to local session daemon. */
	ret = snprintf(sock_path, LTTNG_PATH_MAX,
			DEFAULT_HOME_NOTIFICATION_CHANNEL_UNIX_SOCK,
			utils_get_home_dir());
	if (ret < 0 || ret >= LTTNG_PATH_MAX) {
		goto error;
	}

	ret = lttcomm_connect_unix_sock(sock_path);
	if (ret < 0) {
		goto error;
	}
	fd = ret;

set_fd:
	channel->socket = fd;

	/* FIXME send creds */
end:
	free(sock_path);
	return channel;
error:
	lttng_notification_channel_destroy(channel);
	channel = NULL;
	goto end;
}

enum lttng_notification_channel_status
lttng_notification_channel_get_next_notification(
		struct lttng_notification_channel *channel,
		struct lttng_notification **_notification)
{
	ssize_t ret;
	char *notification_buffer = NULL;
	struct lttng_notification_comm comm;
	struct lttng_notification *notification = NULL;
	enum lttng_notification_channel_status status =
			LTTNG_NOTIFICATION_CHANNEL_STATUS_OK;

	if (!channel || !_notification) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID;
		goto end;
	}

	ret = lttcomm_recv_unix_sock(channel->socket, &comm, sizeof(comm));
	if (ret < sizeof(comm)) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto end;
	}

	notification_buffer = zmalloc(comm.length + sizeof(comm));
	if (!notification_buffer) {
		goto end;
	}

	memcpy(notification_buffer, &comm, sizeof(comm));
	ret = lttcomm_recv_unix_sock(channel->socket,
			notification_buffer + sizeof(comm),
			comm.length);
	if (ret < (ssize_t) comm.length) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto end;
	}

	ret = lttng_notification_create_from_buffer(notification_buffer,
			&notification);
	if (ret != (sizeof(comm) + (ssize_t) comm.length)) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto error;
	}
	*_notification = notification;
end:
	free(notification_buffer);
	return status;
error:
	lttng_notification_destroy(notification);
	goto end;
}

static
enum lttng_notification_channel_status send_command(
		struct lttng_notification_channel *channel,
		enum lttng_notification_channel_message_type type,
		struct lttng_condition *condition)
{
	int socket;
	ssize_t command_size, ret;
	size_t received = 0;
	enum lttng_notification_channel_status status =
			LTTNG_NOTIFICATION_CHANNEL_STATUS_OK;
	char *command_buffer = NULL;
	struct lttng_notification_channel_message cmd = {
		.type = type,
	};
	struct lttng_notification_channel_command_reply reply;

	if (!channel) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID;
		goto end;
	}

	socket = channel->socket;
	if (!lttng_condition_validate(condition)) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID;
		goto end;
	}

	ret = lttng_condition_serialize(condition, NULL);
	if (ret < 0) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID;
		goto end;
	}
	assert(ret < UINT32_MAX);
	cmd.size = (uint32_t) ret;
	command_size = ret + sizeof(
			struct lttng_notification_channel_message);
	command_buffer = zmalloc(command_size);
	if (!command_buffer) {
		goto end;
	}

	memcpy(command_buffer, &cmd, sizeof(cmd));
	ret = lttng_condition_serialize(condition,
			command_buffer + sizeof(cmd));
	if (ret < 0) {
		goto end;
	}

	ret = lttcomm_send_unix_sock(socket, command_buffer, command_size);
	if (ret < 0) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto end;
	}

	/* Receive command reply. */
	do
	{
		ret = lttcomm_recv_unix_sock(socket,
				((char *) &reply) + received,
				sizeof(reply) - received);
		if (ret <= 0) {
			status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
			goto end;
		}
		received += ret;
	} while (received < sizeof(reply));
	status = (enum lttng_notification_channel_status) reply.status;
end:
	free(command_buffer);
	return status;
}

enum lttng_notification_channel_status lttng_notification_channel_subscribe(
		struct lttng_notification_channel *channel,
		struct lttng_condition *condition)
{
	return send_command(channel,
			LTTNG_NOTIFICATION_CHANNEL_COMMAND_TYPE_SUBSCRIBE,
			condition);
}

enum lttng_notification_channel_status lttng_notification_channel_unsubscribe(
		struct lttng_notification_channel *channel,
		struct lttng_condition *condition)
{
	return send_command(channel,
			LTTNG_NOTIFICATION_CHANNEL_COMMAND_TYPE_UNSUBSCRIBE,
			condition);
}

void lttng_notification_channel_destroy(
		struct lttng_notification_channel *channel)
{
	if (!channel) {
		return;
	}

	if (channel->socket >= 0) {
		(void) lttcomm_close_unix_sock(channel->socket);
	}
	free(channel);
}

