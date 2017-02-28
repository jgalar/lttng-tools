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
#include <lttng/condition/condition.h>
#include <lttng/action/action.h>
#include <assert.h>

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
