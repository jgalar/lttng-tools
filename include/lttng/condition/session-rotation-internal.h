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

#ifndef LTTNG_CONDITION_SESSION_ROTATION_INTERNAL_H
#define LTTNG_CONDITION_SESSION_ROTATION_INTERNAL_H

#include <lttng/condition/session-rotation.h>
#include <lttng/condition/condition-internal.h>
#include <lttng/condition/evaluation-internal.h>
#include "common/buffer-view.h"

struct lttng_condition_session_rotation {
	struct lttng_condition parent;
	char *session_name;
};

struct lttng_condition_session_rotation_comm {
	/* Length includes the trailing \0. */
	uint32_t session_name_len;
	char session_name[];
} LTTNG_PACKED;

struct lttng_evaluation_session_rotation {
	struct lttng_evaluation parent;
	uint64_t id;
	struct lttng_trace_chunk_archive_location *location;
	/*
	 * The ownership of location depends on the way it the evaluation was
	 * created. The evaluation owns the location if it was obtained from a
	 * notification (and thus created by lttng_evaluation_create_from_buffer)
	 * as the user may never access location, thus never getting a chance to
	 * free it.
	 *
	 * However, when the _private_ lttng_evaluation_session_rotation_create()
	 * function is used, no ownership of the location is assumed by
	 * the evaluation object. The main reason for this change in
	 * behavior is that internal users of this API only use the object
	 * to use its serialization facilities.
	 */
	bool owns_location;
};

struct lttng_evaluation_session_rotation_comm {
	uint64_t id;
	/* lttng_trace_chunk_archive_location_comm follows. */
	char location[];
} LTTNG_PACKED;

struct lttng_trace_chunk_archive_location {
	enum lttng_trace_chunk_archive_location_type type;
	char *path;
};

struct lttng_trace_chunk_archive_location_comm {
	/* Matches enum lttng_trace_chunk_archive_location_type */
	uint8_t type;
	/* Length includes the trailing \0. */
	uint32_t path_len;
	char path[];
} LTTNG_PACKED;

LTTNG_HIDDEN
ssize_t lttng_condition_session_rotation_ongoing_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_condition **condition);

LTTNG_HIDDEN
ssize_t lttng_condition_session_rotation_completed_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_condition **condition);

LTTNG_HIDDEN
struct lttng_evaluation *lttng_evaluation_session_rotation_ongoing_create(
		uint64_t id);

LTTNG_HIDDEN
struct lttng_evaluation *lttng_evaluation_session_rotation_completed_create(
		uint64_t id,
		struct lttng_trace_chunk_archive_location *location);

LTTNG_HIDDEN
ssize_t lttng_evaluation_session_rotation_ongoing_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_evaluation **evaluation);

LTTNG_HIDDEN
ssize_t lttng_evaluation_session_rotation_completed_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_evaluation **evaluation);

LTTNG_HIDDEN
struct lttng_trace_chunk_archive_location *
lttng_trace_chunk_archive_location_local_create(const char *path);

LTTNG_HIDDEN
struct lttng_trace_chunk_archive_location *
lttng_trace_chunk_archive_location_relayd_create(const char *path);

LTTNG_HIDDEN
void lttng_trace_chunk_archive_location_destroy(
		struct lttng_trace_chunk_archive_location *location);

#endif /* LTTNG_CONDITION_SESSION_ROTATION_INTERNAL_H */
