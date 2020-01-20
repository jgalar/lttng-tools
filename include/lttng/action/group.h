/*
 * Copyright (C) 2019 EfficiOS, inc.
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

#ifndef LTTNG_ACTION_GROUP_H
#define LTTNG_ACTION_GROUP_H

struct lttng_action;
struct lttng_action_group;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create a newly allocated action group object.
 *
 * Returns a new action group on success, NULL on failure. This action group
 * must be destroyed using lttng_action_group_destroy().
 */
extern struct lttng_action *lttng_action_group_create(void);

/*
 * Add an action to an lttng_action object of type LTTNG_ACTION_GROUP.
 *
 * The group takes ownership of the action.
 */
extern enum lttng_action_status lttng_action_group_add_action(
		struct lttng_action *group, struct lttng_action *action);

extern enum lttng_action_status lttng_action_group_get_count(
		const struct lttng_action *group, unsigned int *count);

extern const struct lttng_action *lttng_action_group_get_at_index_const(
		const struct lttng_action *group,
		unsigned int index);

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_ACTION_GROUP_H */
