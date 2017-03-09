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

#ifndef LTTNG_NOTIFICATION_INTERNAL_H
#define LTTNG_NOTIFICATION_INTERNAL_H

#include <lttng/notification/notification.h>
#include <common/macros.h>
#include <stdint.h>

struct lttng_notification {
	struct lttng_condition *condition;
	struct lttng_evaluation *evaluation;
};

struct lttng_notification_comm {
	/* length excludes its own length. */
	uint32_t length;
	/* A condition and evaluation object follow. */
	char payload[];
} LTTNG_PACKED;

LTTNG_HIDDEN
struct lttng_notification *lttng_notification_create(
		struct lttng_condition *condition,
		struct lttng_evaluation *evaluation);

LTTNG_HIDDEN
ssize_t lttng_notification_serialize(struct lttng_notification *notification,
		char *buf);

LTTNG_HIDDEN
ssize_t lttng_notification_create_from_buffer(const char *buf,
		struct lttng_notification **notification);

#endif /* LTTNG_NOTIFICATION_INTERNAL_H */
