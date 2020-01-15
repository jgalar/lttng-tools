/*
 * Copyright (C) 2019 EfficiOS, Inc.
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

#ifndef LTTNG_ACTION_GROUP_INTERNAL_H
#define LTTNG_ACTION_GROUP_INTERNAL_H

#include <sys/types.h>

#include <common/macros.h>

struct lttng_action;
struct lttng_buffer_view;

/*
 * Create an action group from a buffer view.
 *
 * On success, return the number of bytes consumed from `view`, and the created
 * group in `*group`.  On failure, return -1.
 */
LTTNG_HIDDEN
extern ssize_t lttng_action_group_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_action **group);


LTTNG_HIDDEN
enum lttng_action_status lttng_action_group_get_count(
		const struct lttng_action *group, unsigned int *count);

LTTNG_HIDDEN
const struct lttng_action *lttng_action_group_get_at_index(
		const struct lttng_action *group,
		unsigned int index);

#endif /* LTTNG_ACTION_GROUP_INTERNAL_H */
