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

#ifndef LTTNG_NOTIFICATION_CHANNEL_INTERNAL_H
#define LTTNG_NOTIFICATION_CHANNEL_INTERNAL_H

#include <lttng/notification/channel.h>
#include <common/macros.h>
#include <stdint.h>
#include <stdbool.h>

enum lttng_notification_channel_message_type {
	LTTNG_NOTIFICATION_CHANNEL_COMMAND_TYPE_SUBSCRIBE = 0,
	LTTNG_NOTIFICATION_CHANNEL_COMMAND_TYPE_UNSUBSCRIBE = 1,
	LTTNG_NOTIFICATION_CHANNEL_COMMAND_REPLY = 2,
	LTTNG_NOTIFICATION_CHANNEL_NOTIFICATION = 3,
};

struct lttng_notification_channel_message {
	/* enum lttng_notification_channel_message_type */
	int8_t type;
	/* size of the payload following this field */
	uint32_t size;
	char payload[];
} LTTNG_PACKED;

struct lttng_notification_channel_command_reply {
	/* enum lttng_notification_channel_status */
	int8_t status;
} LTTNG_PACKED;

struct lttng_notification_channel {
	/* FIXME Add mutex to protect the socket from concurrent uses. */
	int socket;
};

#endif /* LTTNG_NOTIFICATION_CHANNEL_INTERNAL_H */
