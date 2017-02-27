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

#include <lttng/condition/condition-internal.h>
#include <common/macros.h>
#include <stdbool.h>
#include <assert.h>

enum lttng_condition_type lttng_condition_get_type(
		struct lttng_condition *condition)
{
	return condition ? condition->type : LTTNG_CONDITION_TYPE_UNKNOWN;
}

void lttng_condition_destroy(struct lttng_condition *condition)
{
	if (!condition) {
		return;
	}

	assert(condition->destroy);
	condition->destroy(condition);
}

LTTNG_HIDDEN
bool lttng_condition_validate(struct lttng_condition *condition)
{
	bool valid;

	if (!condition) {
		valid = false;
		goto end;
	}

	if (!condition->validate) {
		/* Sub-class guarantees that it can never be invalid. */
		valid = true;
		goto end;
	}

	valid = condition->validate(condition);
end:
	return valid;
}
