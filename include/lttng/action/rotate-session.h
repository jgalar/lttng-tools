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

#ifndef LTTNG_ACTION_ROTATE_SESSION_H
#define LTTNG_ACTION_ROTATE_SESSION_H

struct lttng_action;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create a newly allocated rotate-session action object.
 *
 * Returns a new action on success, NULL on failure. This action must be
 * destroyed using lttng_action_destroy().
 */
extern struct lttng_action *lttng_action_rotate_session_create(void);

/*
 * Set the session name of an lttng_action object of type
 * LTTNG_ACTION_TYPE_ROTATE_SESSION.
 */
extern enum lttng_action_status lttng_action_rotate_session_set_session_name(
		struct lttng_action *action, const char *session_name);

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_ACTION_ROTATE_SESSION_H */
