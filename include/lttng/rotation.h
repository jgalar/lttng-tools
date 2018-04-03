/*
 * Copyright (C) 2017 - Julien Desfossez <jdesfossez@efficios.com>
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

#ifndef LTTNG_ROTATION_H
#define LTTNG_ROTATION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Return codes for lttng_rotation_handle_get_state()
 */
enum lttng_rotation_state {
	/*
	 * Session has not been rotated.
	 */
	LTTNG_ROTATION_STATE_NO_ROTATION = 0,
	/*
	 * Rotation is ongoing, but has not been completed yet.
	 */
	LTTNG_ROTATION_STATE_ONGOING = 1,
	/*
	 * Rotation has been completed and the resulting chunk
	 * can now safely be read.
	 */
	LTTNG_ROTATION_STATE_COMPLETED = 2,
	/*
	 * The rotation has expired.
	 *
	 * The information associated with a given rotation is eventually
	 * purged by the session daemon. In such a case, the attributes of
	 * the rotation, such as its path, may no longer be available.
	 *
	 * Note that this state does not guarantee the the rotation was
	 * completed successfully.
	 */
	LTTNG_ROTATION_STATE_EXPIRED = 3,
	/*
	 * The rotation could not be completed due to an error.
	 */
	LTTNG_ROTATION_STATE_ERROR = 4,
};

enum lttng_rotation_status {
	LTTNG_ROTATION_STATUS_OK = 0,
	/* Information not available. */
	LTTNG_ROTATION_STATUS_UNAVAILABLE = 1,
	/* Generic error. */
	LTTNG_ROTATION_STATUS_ERROR = -1,
	/* Invalid parameters provided. */
	LTTNG_ROTATION_STATUS_INVALID = -2,
};

enum lttng_rotation_schedule_type {
	LTTNG_ROTATION_SCHEDULE_TYPE_SIZE_THRESHOLD = 0,
	LTTNG_ROTATION_SCHEDULE_TYPE_PERIODIC = 1,
};

enum lttng_trace_archive_location_type {
	LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_LOCAL = 0,
	LTTNG_TRACE_ARCHIVE_LOCATION_TYPE_RELAY = 1,
};

enum lttng_trace_archive_location_status {
	LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_OK = 0,
	LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_INVALID = -1,
	LTTNG_TRACE_ARCHIVE_LOCATION_STATUS_ERROR = -2,
};

/*
 * Descriptor of an immediate session rotation to be performed as soon as
 * possible by the tracers.
 */
struct lttng_rotation_immediate_descriptor;

/*
 * Descriptor of a session rotation schedule to add to a session.
 */
struct lttng_rotation_schedule_descriptor;

/*
 * Handle used to represent a specific rotation.
 */
struct lttng_rotation_handle;

/*
 * Location of a trace archive.
 */
struct lttng_trace_archive_location;

/*
 * A set of lttng_rotation_schedule_descriptors.
 */
struct lttng_rotation_schedule_descriptors;

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

/*
 * Return a newly allocated immediate session rotation descriptor object or NULL
 * on error.
 */
extern struct lttng_rotation_immediate *
lttng_rotation_immediate_descriptor_create(void);

/*
 * Set the name of the session to rotate immediately.
 *
 * The session_name parameter is copied to the immediate session rotation
 * descriptor.
 */
extern enum lttng_rotation_status
lttng_rotation_immediate_descriptor_set_session_name(
		struct lttng_rotation_immediate_descriptor *descriptor,
		const char *session_name);

/*
 * Destroy an immediate session rotation descriptor object.
 */
extern void lttng_rotation_immediate_descriptor_destroy(
		struct lttng_rotation_immediate_descriptor *descriptor);

/*
 * Rotate the output of a session.
 *
 * On success, a session rotation handle is allocated and can be used to monitor
 * the progress of the rotation using lttng_rotation_get_state().
 * The handle must be destroyed by the caller using
 * lttng_rotation_handle_destroy().
 *
 * Return 0 if the rotate action was successfully initiated or a negative LTTng
 * error code on error.
 */
extern int lttng_rotate_session(
		struct lttng_rotation_immediate_descriptor *descriptor,
		struct lttng_rotation_handle **rotation_handle);

/*
 * Destroy an lttng_rotation_handle.
 */
extern void lttng_rotation_handle_destroy(
		struct lttng_rotation_handle *rotation_handle);

/*
 * Return a newly allocated size-based session rotation descriptor or NULL on
 * error.
 */
extern struct lttng_rotation_schedule *
lttng_rotation_schedule_size_threshold_create(void);

/*
 * Set a session rotation schedule's size threshold.
 */
extern enum lttng_rotation_status
lttng_rotation_schedule_size_threshold_set_threshold(
		uint64_t size_threshold_bytes);

/*
 * Return a newly allocated periodic session rotation descriptor or NULL on
 * error.
 */
extern struct lttng_rotation_schedule *
lttng_rotation_schedule_periodic_create(void);

/*
 * Set a time-based session rotation schedule's period.
 */
extern enum lttng_rotation_status lttng_rotation_schedule_periodic_set_period(
		uint64_t period_us);

/*
 * Destroy a session rotation schedule object.
 */
extern void lttng_rotation_schedule_descriptor_destroy(
		struct lttng_rotation_schedule_descriptor *schedule);

/*
 * Get the current state of the rotation referenced by the handle.
 *
 * This will issue a request to the session daemon on every call. Hence,
 * the result of this call may change over time.
 */
extern enum lttng_rotation_status lttng_rotation_handle_get_state(
		struct lttng_rotation_handle *rotation_handle,
		enum lttng_rotation_state *rotation_state);

/*
 * Get the location of the rotation's resulting archive.
 *
 * The rotation must be completed in order for this call to succeed.
 * The location returned remains owned by the rotation handle.
 *
 * Note that location will not be set in case of error, or if the session
 * rotation handle has expired.
 */
extern enum lttng_rotation_status lttng_rotation_handle_get_archive_location(
		struct lttng_rotation_handle *rotation_handle,
		struct lttng_trace_archive_location **location);

/*
 * Add a session rotation schedule to a session.
 *
 * Note that the current implementation currently limits the schedules
 * associated with a given session to one per type of schedule.
 *
 * Returns LTTNG_OK on success, or a negative lttng error code on error.
 */
extern int lttng_session_add_rotation_schedule(
		const char *session_name,
		struct lttng_rotation_schedule_descriptor *schedule);

/*
 * Remove a session rotation schedule from a session.
 *
 * Returns LTTNG_OK on success, or a negative lttng error code on error.
 */
extern int lttng_session_remove_rotation_schedule(
		const char *session_name,
		struct lttng_rotation_schedule_descriptor *schedule);

/*
 * Destroy a set of rotation schedules. Pointers to any descriptor contained
 * in this set become invalid after this call.
 */
extern void lttng_rotation_schedule_descriptors_destroy(
		struct lttng_rotation_schedule_descriptors *descriptors);

/*
 * Get the number of descriptors in this set.
 *
 * Returns the number of descriptors on success, or a negative lttng error code
 * on error.
 */
extern int lttng_rotation_schedule_descriptors_get_count(
		struct lttng_rotation_schedule_descriptors *descriptors);

/*
 * Get a schedule descriptor from the set at a given index.
 *
 * Note that the set maintains the ownership of the returned schedule
 * descriptor. It must not be destroyed by the user.
 *
 * Returns a rotation schedule descriptor, or NULL on error.
 */
extern struct lttng_rotation_schedule_descriptor *
lttng_rotation_schedule_descriptors_get_at_index(
		struct lttng_rotation_schedule_descriptors *descriptors,
		int index);

/*
 * Get the rotation schedules associated with a given session.
 *
 * Returns LTTNG_OK on success, or a negative lttng error code on error.
 */
extern int lttng_session_list_rotation_schedule_descriptors(
		const char *session_name,
		struct lttng_rotation_schedule_descriptors **descriptors);

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_ROTATION_H */
