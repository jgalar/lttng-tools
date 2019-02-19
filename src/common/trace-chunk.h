/*
 * Copyright (C) 2019 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License, version 2.1 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef LTTNG_TRACE_CHUNK_H
#define LTTNG_TRACE_CHUNK_H

#include <common/macros.h>
#include <common/credentials.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct lttng_trace_chunk;

enum lttng_trace_chunk_status {
        LTTNG_TRACE_CHUNK_STATUS_OK,
	LTTNG_TRACE_CHUNK_STATUS_NONE,
	LTTNG_TRACE_CHUNK_STATUS_INVALID_ARGUMENT,
	LTTNG_TRACE_CHUNK_STATUS_ERROR,
};

LTTNG_HIDDEN
struct lttng_trace_chunk *lttng_trace_chunk_create_anonymous(void);

LTTNG_HIDDEN
struct lttng_trace_chunk *lttng_trace_chunk_create(
		uint64_t chunk_id,
		time_t chunk_creation_time);

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_get_id(
		const struct lttng_trace_chunk *chunk, uint64_t *id);

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_get_creation_timestamp(
		const struct lttng_trace_chunk *chunk, time_t *creation_ts);

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_get_credentials(
		const struct lttng_trace_chunk *chunk,
		struct lttng_credentials *credentials);

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_set_credentials(
		struct lttng_trace_chunk *chunk,
		const struct lttng_credentials *credentials);

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_set_credentials_current_user(
		struct lttng_trace_chunk *chunk);

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_set_session_output_directory(
		struct lttng_trace_chunk *chunk,
		const char *session_output_directory_path,
		bool create_directory);

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_create_subdirectory(
		const struct lttng_trace_chunk *chunk,
		const char *subdirectory_path);

/* Returns true on success. */
LTTNG_HIDDEN
bool lttng_trace_chunk_get(struct lttng_trace_chunk *chunk);

LTTNG_HIDDEN
void lttng_trace_chunk_put(struct lttng_trace_chunk *chunk);

#endif /* LTTNG_TRACE_CHUNK_H */
