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

#include <lttng/location-internal.h>
#include <common/macros.h>
#include <stdlib.h>

static
struct lttng_trace_archive_location *lttng_trace_archive_location_create(
		enum lttng_trace_archive_location_type type)
{
	struct lttng_trace_archive_location *location;

	location = zmalloc(sizeof(*location));
	if (!location) {
		goto end;
	}

	location->type = type;
end:
	return location;
}

LTTNG_HIDDEN
void lttng_trace_archive_location_destroy(
		struct lttng_trace_archive_location *location)
{
	switch (location->type) {
	case LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_LOCAL:
		free(location->types.local.absolute_path);
		break;
	case LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_RELAY:
		free(location->types.relay.uri);
		free(location->types.relay.relative_path);
		break;
	default:
		abort();
	}

	free(location);
}

LTTNG_HIDDEN
struct lttng_trace_archive_location *lttng_trace_archive_location_local_create(
		const char *absolute_path)
{
	struct lttng_trace_archive_location *location = NULL;

	if (!absolute_path) {
		goto end;
	}

	location = lttng_trace_archive_location_create(
			LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_LOCAL);
	if (!location) {
		goto end;
	}

	location->types.local.absolute_path = strdup(absolute_path);
	if (!location->types.local.absolute_path) {
		goto error;
	}

end:
	return location;
error:
	lttng_trace_archive_location_destroy(location);
	return NULL;
}

LTTNG_HIDDEN
struct lttng_trace_archive_location *lttng_trace_archive_location_relay_create(
		const char *uri, const char *relative_path)
{
	struct lttng_trace_archive_location *location = NULL;

	if (!uri || !relative_path) {
		goto end;
	}

	location = lttng_trace_archive_location_create(
			LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_RELAY);
	if (!location) {
		goto end;
	}

	location->types.relay.uri = strdup(uri);
	if (!location->types.relay.uri) {
		goto error;
	}
	location->types.relay.relative_path = strdup(relative_path);
	if (!location->types.relay.relative_path) {
		goto error;
	}

end:
	return location;
error:
	lttng_trace_archive_location_destroy(location);
	return NULL;
}

enum lttng_trace_archive_location_type lttng_trace_archive_location_get_type(
		struct lttng_trace_archive_location *location)
{
	enum lttng_trace_archive_location_type type;

	if (!location) {
		type = LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_UNKNOWN;
		goto end;
	}

	type = location->type;
end:
	return type;
}

enum lttng_trace_archive_location_status
lttng_trace_archive_location_local_get_absolute_path(
		struct lttng_trace_archive_location *location,
		char **absolute_path)
{
	enum lttng_trace_archive_location_status status =
			LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_OK;

	if (!location || !absolute_path ||
			location->type != LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_LOCAL) {
		status = LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_INVALID;
		goto end;
	}

	*absolute_path = strdup(location->types.local.absolute_path);
	if (!*absolute_path) {
		status = LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_ERROR;
		goto end;
	}
end:
	return status;
}

enum lttng_trace_archive_location_status
lttng_trace_archive_location_relay_get_uri(
		struct lttng_trace_archive_location *location,
		char **relay_uri)
{
	enum lttng_trace_archive_location_status status =
			LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_OK;

	if (!location || !relay_uri ||
			location->type != LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_RELAY) {
		status = LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_INVALID;
		goto end;
	}

	*relay_uri = strdup(location->types.relay.uri);
	if (!*relay_uri) {
		status = LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_ERROR;
		goto end;
	}
end:
	return status;
}

enum lttng_trace_archive_location_status
lttng_trace_archive_location_relay_get_relative_path(
		struct lttng_trace_archive_location *location,
		char **relative_path)
{
	enum lttng_trace_archive_location_status status =
			LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_OK;

	if (!location || !relative_path ||
			location->type != LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_RELAY) {
		status = LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_INVALID;
		goto end;
	}

	*relative_path = strdup(location->types.relay.relative_path);
	if (!*relative_path) {
		status = LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_ERROR;
		goto end;
	}
end:
	return status;
}
