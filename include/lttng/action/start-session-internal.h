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

#ifndef LTTNG_ACTION_START_SESSION_INTERNAL_H
#define LTTNG_ACTION_START_SESSION_INTERNAL_H

#include <sys/types.h>

#include <common/macros.h>

struct lttng_action;
struct lttng_buffer_view;

/*
 * Create a "start session" action from a buffer view.
 *
 * On success, return the number of bytes consumed from `view`, and the created
 * action in `*action`.  On failure, return -1.
 */
LTTNG_HIDDEN
extern ssize_t lttng_action_start_session_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_action **action);

#endif /* LTTNG_ACTION_START_SESSION_INTERNAL_H */
