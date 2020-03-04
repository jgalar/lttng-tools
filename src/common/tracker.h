/*
 * Copyright (C) 2019 Jonathan Rajotte-Julien <jonathan.rajotte-julien@efficios.com>
 * Copyright (C) 2020 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#ifndef LTTNG_TRACKER_INTERNAL_H
#define LTTNG_TRACKER_INTERNAL_H

#include <lttng/tracker.h>

#include <common/buffer-view.h>
#include <common/dynamic-buffer.h>
#include <common/macros.h>

LTTNG_HIDDEN
int lttng_process_attr_tracker_serialize(
		const struct lttng_process_attr_tracker *tracker,
		struct lttng_dynamic_buffer *buffer);

LTTNG_HIDDEN
ssize_t lttng_process_attr_tracker_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_process_attr_tracker **_tracker);

#endif /* LTTNG_TRACKER_INTERNAL_H */
