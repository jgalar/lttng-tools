#ifndef COMMON_SNAPSHOT_H
#define COMMON_SNAPSHOT_H

/*
 * Copyright (C) 2020 - EfficiOS, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "common/macros.h"

#include <stdbool.h>

struct lttng_buffer_view;
struct lttng_dynamic_buffer;
struct lttng_snapshot_output;

LTTNG_HIDDEN
bool lttng_snapshot_output_validate(const struct lttng_snapshot_output *output);

LTTNG_HIDDEN
bool lttng_snapshot_output_is_equal(
		const struct lttng_snapshot_output *a,
		const struct lttng_snapshot_output *b);

LTTNG_HIDDEN
int lttng_snapshot_output_serialize(
		const struct lttng_snapshot_output *output,
		struct lttng_dynamic_buffer *buf);

LTTNG_HIDDEN
ssize_t lttng_snapshot_output_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_snapshot_output **output_p);

#endif /* COMMON_SNAPSHOT_H */
