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

#include <lttng/trigger/trigger-internal.h>
#include <lttng/condition/condition-internal.h>
#include <lttng/action/action-internal.h>
#include <assert.h>

LTTNG_HIDDEN
bool lttng_trigger_validate(struct lttng_trigger *trigger)
{
	bool valid;

	if (!trigger) {
		valid = false;
		goto end;
	}

	valid = lttng_condition_validate(trigger->condition) &&
			lttng_action_validate(trigger->action);
end:
	return valid;
}

struct lttng_trigger *lttng_trigger_create(
		struct lttng_condition *condition,
		struct lttng_action *action)
{
	struct lttng_trigger *trigger = NULL;

	if (!condition || !action) {
		goto end;
	}

	trigger = zmalloc(sizeof(struct lttng_trigger));
	if (!trigger) {
		goto end;
	}

	trigger->condition = condition;
	trigger->action = action;
end:
	return trigger;
}

void lttng_trigger_destroy(struct lttng_trigger *trigger)
{
	if (!trigger) {
		return;
	}

	lttng_condition_destroy(trigger->condition);
	lttng_action_destroy(trigger->action);
	free(trigger);
}

LTTNG_HIDDEN
ssize_t lttng_trigger_create_from_buffer(const char *buf,
		struct lttng_trigger **trigger)
{
	ssize_t ret, trigger_size;
	struct lttng_condition *condition = NULL;
	struct lttng_action *action = NULL;

	if (!buf || !trigger) {
		ret = -1;
		goto end;
	}

	ret = lttng_condition_create_from_buffer(buf, &condition);
	if (ret < 0) {
		goto end;
	}

	trigger_size = ret;
	buf += ret;
	ret = lttng_action_create_from_buffer(buf, &action);
	if (ret < 0) {
		goto end;
	}

	trigger_size += ret;
	*trigger = lttng_trigger_create(condition, action);
	if (!*trigger) {
		goto error;
	}
	ret = trigger_size;
end:
	return ret;
error:
	lttng_condition_destroy(condition);
	lttng_action_destroy(action);
	return ret;
}

/*
 * Returns the size of a trigger's condition and action.
 * Both elements are stored contiguously, see their "*_comm" structure
 * for the detailed format.
 */
LTTNG_HIDDEN
ssize_t lttng_trigger_serialize(struct lttng_trigger *trigger, char *buf)
{
	ssize_t action_size, condition_size, ret;

	if (!trigger) {
		ret = -1;
		goto end;
	}

	condition_size = lttng_condition_serialize(trigger->condition, NULL);
	if (condition_size < 0) {
		ret = -1;
		goto end;
	}

	action_size = lttng_action_serialize(trigger->action, NULL);
	if (action_size < 0) {
		ret = -1;
		goto end;
	}

	ret = action_size + condition_size;
	if (!buf) {
		goto end;
	}

	condition_size = lttng_condition_serialize(trigger->condition, buf);
	if (condition_size < 0) {
		ret = -1;
		goto end;
	}

	buf += condition_size;
	action_size = lttng_action_serialize(trigger->action, buf);
	if (action_size < 0) {
		ret = -1;
		goto end;
	}
end:
	return ret;
}
