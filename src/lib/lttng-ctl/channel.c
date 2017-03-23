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
#include <common/defaults.h>
#include <common/error.h>
#include <common/dynamic-buffer.h>
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
	pthread_mutex_init(&channel->lock, NULL);

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
	struct lttng_notification_comm comm;
	struct lttng_notification *notification = NULL;
	struct lttng_dynamic_buffer reception_buffer;
	struct lttng_notification_channel_message msg;
	enum lttng_notification_channel_status status =
			LTTNG_NOTIFICATION_CHANNEL_STATUS_OK;

	lttng_dynamic_buffer_init(&reception_buffer);

	if (!channel || !_notification) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID;
		goto end;
	}
	pthread_mutex_lock(&channel->lock);

	ret = lttcomm_recv_unix_sock(channel->socket, &msg, sizeof(msg));
	if (ret <= 0) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto end_unlock;
	}
	if (msg.size > DEFAULT_MAX_NOTIFICATION_CLIENT_MESSAGE_PAYLOAD_SIZE) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto end_unlock;
	}
	if (msg.type != LTTNG_NOTIFICATION_CHANNEL_MESSAGE_TYPE_NOTIFICATION) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto end_unlock;
	}

	ret = lttcomm_recv_unix_sock(channel->socket, &comm, sizeof(comm));
	if (ret < sizeof(comm)) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto end_unlock;
	}

	ret = lttng_dynamic_buffer_set_size(&reception_buffer,
			comm.length + sizeof(comm));
	if (ret) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto end_unlock;
	}

	memcpy(reception_buffer.data, &comm, sizeof(comm));
	ret = lttcomm_recv_unix_sock(channel->socket,
			reception_buffer.data + sizeof(comm),
			comm.length);
	if (ret < (ssize_t) comm.length) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto end_unlock;
	}

	ret = lttng_notification_create_from_buffer(reception_buffer.data,
			&notification);
	if (ret != (sizeof(comm) + (ssize_t) comm.length)) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto error;
	}
	*_notification = notification;
end_unlock:
	pthread_mutex_unlock(&channel->lock);
end:
	lttng_dynamic_buffer_reset(&reception_buffer);
	return status;
error:
	lttng_notification_destroy(notification);
	goto end_unlock;
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
	struct lttng_notification_channel_message cmd_message = {
		.type = type,
	};
	struct lttng_notification_channel_message reply_message;
	struct lttng_notification_channel_command_reply reply;

	if (!channel) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID;
		goto end;
	}

	pthread_mutex_lock(&channel->lock);
	socket = channel->socket;
	if (!lttng_condition_validate(condition)) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID;
		goto end_unlock;
	}

	ret = lttng_condition_serialize(condition, NULL);
	if (ret < 0) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID;
		goto end_unlock;
	}
	assert(ret < UINT32_MAX);
	cmd_message.size = (uint32_t) ret;
	command_size = ret + sizeof(
			struct lttng_notification_channel_message);
	command_buffer = zmalloc(command_size);
	if (!command_buffer) {
		goto end_unlock;
	}

	memcpy(command_buffer, &cmd_message, sizeof(cmd_message));
	ret = lttng_condition_serialize(condition,
			command_buffer + sizeof(cmd_message));
	if (ret < 0) {
		goto end_unlock;
	}

	ret = lttcomm_send_unix_sock(socket, command_buffer, command_size);
	if (ret < 0) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto end_unlock;
	}

	/* Receive command reply header. */
	do {
		ret = lttcomm_recv_unix_sock(socket,
				((char *) &reply_message) + received,
				sizeof(reply_message) - received);
		if (ret <= 0) {
			status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
			goto end_unlock;
		}
		received += ret;
	} while (received < sizeof(reply_message));
	if (reply_message.type !=
			LTTNG_NOTIFICATION_CHANNEL_MESSAGE_TYPE_COMMAND_REPLY ||
			reply_message.size != sizeof(reply)) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
		goto end_unlock;
	}

	/* Receive command reply payload. */
	received = 0;
	do {
		ret = lttcomm_recv_unix_sock(socket,
				((char *) &reply) + received,
				sizeof(reply) - received);
		if (ret <= 0) {
			status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ERROR;
			goto end_unlock;
		}
		received += ret;
	} while (received < sizeof(reply));
	status = (enum lttng_notification_channel_status) reply.status;
end_unlock:
	pthread_mutex_unlock(&channel->lock);
end:
	free(command_buffer);
	return status;
}

enum lttng_notification_channel_status lttng_notification_channel_subscribe(
		struct lttng_notification_channel *channel,
		struct lttng_condition *condition)
{
	return send_command(channel,
			LTTNG_NOTIFICATION_CHANNEL_MESSAGE_TYPE_SUBSCRIBE,
			condition);
}

enum lttng_notification_channel_status lttng_notification_channel_unsubscribe(
		struct lttng_notification_channel *channel,
		struct lttng_condition *condition)
{
	return send_command(channel,
			LTTNG_NOTIFICATION_CHANNEL_MESSAGE_TYPE_UNSUBSCRIBE,
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
	pthread_mutex_destroy(&channel->lock);
	free(channel);
}

