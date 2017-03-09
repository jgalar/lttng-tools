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
#include <lttng/condition/condition-internal.h>
#include <lttng/condition/evaluation-internal.h>
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

LTTNG_HIDDEN
ssize_t lttng_notification_serialize(struct lttng_notification *notification,
		char *buf)
{
	ssize_t ret, condition_size, evaluation_size, offset = 0;
	struct lttng_notification_comm notification_comm;

	if (!notification) {
		ret = -1;
		goto end;
	}

	offset += sizeof(notification_comm);
	condition_size = lttng_condition_serialize(notification->condition,
			buf ? (buf + offset) : NULL);
	if (condition_size < 0) {
		ret = condition_size;
		goto end;
	}
	offset += condition_size;

	evaluation_size = lttng_evaluation_serialize(notification->evaluation,
			buf ? (buf + offset) : NULL);
	if (evaluation_size < 0) {
		ret = evaluation_size;
		goto end;
	}
	offset += evaluation_size;

	if (buf) {
		notification_comm.length =
				(uint32_t) (condition_size + evaluation_size);
		memcpy(buf, &notification_comm, sizeof(notification_comm));
	}
	ret = offset;
end:
	return ret;

}

LTTNG_HIDDEN
ssize_t lttng_notification_create_from_buffer(const char *buf,
		struct lttng_notification **notification)
{
	ssize_t ret, offset = 0, condition_size, evaluation_size;
	struct lttng_notification_comm *notification_comm;
	struct lttng_condition *condition;
	struct lttng_evaluation *evaluation;

	if (!buf || !notification) {
		ret = -1;
		goto end;
	}

	notification_comm = (struct lttng_notification_comm *) buf;
	offset += sizeof(*notification_comm);

	/* struct lttng_condition */
	condition_size = lttng_condition_create_from_buffer(buf + offset,
			&condition);
	if (condition_size < 0) {
		ret = condition_size;
		goto end;
	}
	offset += condition_size;

	/* struct lttng_evaluation */
	evaluation_size = lttng_evaluation_create_from_buffer(buf + offset,
			&evaluation);
	if (evaluation_size < 0) {
		ret = evaluation_size;
		goto end;
	}
	offset += evaluation_size;

	/* Unexpected size of inner-elements; the buffer is corrupted. */
	if ((ssize_t) notification_comm->length !=
			condition_size + evaluation_size) {
		ret = -1;
		goto error;
	}

	*notification = lttng_notification_create(condition, evaluation);
	if (!*notification) {
		goto error;
	}
	ret = offset;
end:
	return ret;
error:
	lttng_condition_destroy(condition);
	lttng_evaluation_destroy(evaluation);
	return ret;
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
