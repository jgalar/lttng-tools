/*
 * Copyright (C) 2018 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
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

#ifndef LTTNG_LOCATION_H
#define LTTNG_LOCATION_H

#ifdef __cplusplus
extern "C" {
#endif

enum lttng_trace_archive_location_type {
	LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_UNKNOWN = 0,
	LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_LOCAL = 1,
	LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_RELAY = 2,
};

enum lttng_trace_archive_location_status {
	LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_OK = 0,
	LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_INVALID = -1,
	LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_ERROR = -2,
};

/*
 * Location of a trace archive.
 */
struct lttng_trace_archive_location;

/*
 * Get a trace archive location's type.
 */
extern enum lttng_trace_archive_location_type
lttng_trace_archive_location_get_type(
		struct lttng_trace_archive_location *location);

/*
 * Get the absolute path of a local trace archive location.
 *
 * absolute_path is dynamically allocated and must be free'd by the caller.
 */
extern enum lttng_trace_archive_location_status
lttng_trace_archive_location_local_get_absolute_path(
		struct lttng_trace_archive_location *location,
		char **absolute_path);

/*
 * Get the URI of the relay daemon associated to this trace archive location.
 *
 * relay_uri is dynamically allocated and must be free'd by the caller.
 */
extern enum lttng_trace_archive_location_status
lttng_trace_archive_location_relay_get_uri(
		struct lttng_trace_archive_location *location,
		char **relay_uri);

/*
 * Get path relative to the relay daemon's current output path.
 *
 * relative_path is dynamically allocated and must be free'd by the caller.
 */
extern enum lttng_trace_archive_location_status
lttng_trace_archive_location_relay_get_relative_path(
		struct lttng_trace_archive_location *location,
		char **relative_path);

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_LOCATION_H */
