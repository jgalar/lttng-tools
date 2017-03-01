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
#include <lttng/condition/condition.h>
#include <lttng/condition/evaluation.h>
#include <assert.h>

LTTNG_HIDDEN
struct lttng_notification *lttng_notification_create(
		struct lttng_condition *condition,
		struct lttng_evaluation *evaluation)
{
	struct lttng_notification *notification = NULL;

	if (!condition || !evaluation) {
		goto end;
	}

	notification = zmalloc(sizeof(struct lttng_notification));
	if (!notification) {
		goto end;
	}

	notification->condition = condition;
	notification->evaluation = evaluation;
end:
	return notification;
}

void lttng_notification_destroy(struct lttng_notification *notification)
{
	if (!notification) {
		return;
	}

	lttng_condition_destroy(notification->condition);
	lttng_evaluation_destroy(notification->evaluation);
	free(notification);
}

struct lttng_condition *lttng_notification_get_condition(
		struct lttng_notification *notification)
{
	return notification ? notification->condition : NULL;
}

struct lttng_evaluation *lttng_notification_get_evaluation(
		struct lttng_notification *notification)
{
	return notification ? notification->evaluation : NULL;
}
