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

#include <lttng/condition/evaluation-internal.h>
#include <common/macros.h>
#include <stdbool.h>
#include <assert.h>

enum lttng_condition_type lttng_evaluation_get_type(
		struct lttng_evaluation *evaluation)
{
	return evaluation ? evaluation->type : LTTNG_CONDITION_TYPE_UNKNOWN;
}

void lttng_evaluation_destroy(struct lttng_evaluation *evaluation)
{
	if (!evaluation) {
		return;
	}

	assert(evaluation->destroy);
	evaluation->destroy(evaluation);
}
