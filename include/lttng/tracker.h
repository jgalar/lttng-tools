/*
 * Copyright (C) 2019 Jonathan Rajotte-Julien <jonathan.rajotte-julien@efficios.com>
 * Copyright (C) 2020 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#ifndef LTTNG_TRACKER_H
#define LTTNG_TRACKER_H

#include <lttng/domain.h>
#include <lttng/lttng-error.h>
#include <lttng/constant.h>
#include <lttng/session.h>

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Process attribute tracked by a tracker.
 */
enum lttng_process_attr {
	/* Kernel space domain only. */
	LTTNG_PROCESS_ATTR_PROCESS_ID = 0,
	/* Kernel and user space domains. */
	LTTNG_PROCESS_ATTR_VIRTUAL_PROCESS_ID = 1,
	/* Kernel space domain only. */
	LTTNG_PROCESS_ATTR_USER_ID = 2,
	/* Kernel and user space domains. */
	LTTNG_PROCESS_ATTR_VIRTUAL_USER_ID = 3,
	/* Kernel space domain only. */
	LTTNG_PROCESS_ATTR_GROUP_ID = 4,
	/* Kernel and user space domains. */
	LTTNG_PROCESS_ATTR_VIRTUAL_GROUP_ID = 5,
};

/*
 * Tracking (filtering) policy of a process attribute tracker.
 */
enum lttng_tracking_policy {
	/*
	 * Track all possible process attribute value of a given type
	 * (i.e. no filtering).
	 * This is the default state of a process attribute tracker.
	 */
	LTTNG_TRACKING_POLICY_INCLUDE_ALL = 0,
	/* Exclude all possible process attribute values of a given type. */
	LTTNG_TRACKING_POLICY_EXCLUDE_ALL = 1,
	/* Track a list of possible process attribute values. */
	LTTNG_TRACKING_POLICY_INCLUDE_LIST = 2,
};

/*
 * Type of a process attribute value.
 *
 * This allows the use of the matching accessor given the type of a value.
 */
enum lttng_process_attr_value_type {
	LTTNG_PROCESS_ATTR_VALUE_TYPE_INVALID = -1,
	LTTNG_PROCESS_ATTR_VALUE_TYPE_PID = 0,
	LTTNG_PROCESS_ATTR_VALUE_TYPE_UID = 1,
	LTTNG_PROCESS_ATTR_VALUE_TYPE_USER_NAME = 2,
	LTTNG_PROCESS_ATTR_VALUE_TYPE_GID = 3,
	LTTNG_PROCESS_ATTR_VALUE_TYPE_GROUP_NAME = 4,
};

enum lttng_process_attr_tracker_handle_status {
	LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_ERROR = -3,
	LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_COMMUNICATION_ERROR = -2,
	LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_INVALID = -1,
	LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_OK = 0,
	LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_ALREADY_PRESENT = 1,
	LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_NOT_PRESENT = 2,
};

enum lttng_process_attr_value_status {
	/* Invalid accessor used with to access a process attribute value. */
	LTTNG_PROCESS_ATTR_VALUE_STATUS_INVALID_TYPE = -1,
	LTTNG_PROCESS_ATTR_VALUE_STATUS_OK = 0,
};

enum lttng_process_attr_values_status {
	LTTNG_PROCESS_ATTR_VALUES_STATUS_INVALID = -1,
	LTTNG_PROCESS_ATTR_VALUES_STATUS_OK = 0,
};

/*
 * A process attribute tracker handle.
 *
 * A process attribute tracker is an _include list_ of process attributes.
 * Tracked processes are allowed to emit events, provided those events are
 * targeted by enabled event rules.
 *
 * An LTTng session is created with a number of process attribute
 * trackers by default. The process attributes that can be tracked vary by
 * domain (see enum lttng_process_attr).
 *
 * Trackers are per-domain (user and kernel space) and allow the filtering
 * of events based on a process's attributes.
 */
struct lttng_process_attr_tracker_handle;

/* A set of process attribute values. */
struct lttng_process_attr_values;

/*
 * Get a handle to one of the process attribute trackers of a session's domain.
 *
 * Returns LTTNG_OK and a process attribute tracker handle on success,
 * or an lttng_error_code on error.
 *
 * The tracker's ownership is transfered to the caller. Use
 * lttng_process_attr_tracker_handle_destroy() to dispose of it.
 */
extern enum lttng_error_code lttng_session_get_tracker_handle(
		const char *session_name,
		enum lttng_domain_type domain,
		enum lttng_process_attr process_attr,
		struct lttng_process_attr_tracker_handle **out_tracker);

/*
 * Destroy a process attribute tracker handle.
 */
extern void lttng_process_attr_tracker_handle_handle_destroy(
		struct lttng_process_attr_tracker_handle *tracker);

/*
 * Get the tracking policy of a process attribute tracker.
 *
 * Returns the LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_OK and the tracking
 * policy of a process attribute tracker on success,
 * LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_INVALID on error.
 */
extern enum lttng_process_attr_tracker_handle_status
lttng_process_tracker_handle_get_tracking_policy(
		const struct lttng_process_attr_tracker_handle *tracker,
		enum lttng_tracking_policy *policy);

/*
 * Set the tracking policy of a process attribute tracker.
 *
 * If the tracking policy of a tracker was already
 * LTTNG_TRACKING_POLICY_INCLUDE_LIST, the include list is re-initialized to
 * contain no values.
 *
 * Returns the LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_INVALID on error.
 */
extern enum lttng_process_attr_tracker_handle_status
lttng_process_attr_tracker_handle_set_tracking_policy(
		const struct lttng_process_attr_tracker_handle *tracker,
		enum lttng_tracking_policy policy);

/*
 * Add a numerical PID to the process ID process attribute tracker include list.
 *
 * Returns LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_ALREADY_PRESENT if it was already
 * present in the include list, and
 * LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_INVALID if an invalid tracker
 * argument was provided.
 */
extern enum lttng_process_attr_tracker_handle_status
lttng_process_attr_process_id_tracker_handle_add_pid(
		const struct lttng_process_attr_tracker_handle
				*process_id_tracker,
		pid_t pid);

/*
 * Remove a numerical PID from the process ID process attribute tracker include
 * list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_NOT_PRESENT if it was not present
 * in the include list, and LTTNG_PROCESS_ATTR_STATUS_INVALID if
 * an invalid tracker argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_process_id_tracker_handle_remove_pid(
		const struct lttng_process_attr_tracker_handle
				*process_id_tracker,
		pid_t pid);

/*
 * Add a numerical PID to the virtual process ID process attribute tracker
 * include list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_ALREADY_PRESENT if it was already
 * present in the include list, and
 * LTTNG_PROCESS_ATTR_STATUS_INVALID if an invalid tracker
 * argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_virtual_process_id_tracker_handle_add_pid(
		const struct lttng_process_attr_tracker_handle
				*process_id_tracker,
		pid_t vpid);

/*
 * Remove a numerical PID from the virtual process ID process attribute tracker
 * include list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_NOT_PRESENT if it was not present
 * in the include list, and LTTNG_PROCESS_ATTR_STATUS_INVALID if
 * an invalid tracker argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_virtual_process_id_tracker_handle_remove_pid(
		const struct lttng_process_attr_tracker_handle
				*process_id_tracker,
		pid_t vpid);

/*
 * Add a numerical UID to the user ID process attribute tracker include list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_ALREADY_PRESENT if it was already
 * present in the include list, and
 * LTTNG_PROCESS_ATTR_STATUS_INVALID if an invalid tracker
 * argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_user_id_tracker_handle_add_uid(
		const struct lttng_process_attr_tracker_handle *user_id_tracker,
		uid_t uid);

/*
 * Remove a numerical UID from the user ID process attribute tracker include
 * list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_NOT_PRESENT if it was not present
 * in the include list, and LTTNG_PROCESS_ATTR_STATUS_INVALID if
 * an invalid tracker argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_user_id_tracker_handle_remove_uid(
		const struct lttng_process_attr_tracker_handle *user_id_tracker,
		uid_t uid);

/*
 * Add a user name to the user ID process attribute tracker include list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_ALREADY_PRESENT if it was already
 * present in the include list, and
 * LTTNG_PROCESS_ATTR_STATUS_INVALID if an invalid tracker
 * argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_user_id_tracker_handle_add_user_name(
		const struct lttng_process_attr_tracker_handle *user_id_tracker,
		const char *user_name);

/*
 * Remove a user name from the user ID process attribute tracker include
 * list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_NOT_PRESENT if it was not present
 * in the include list, and LTTNG_PROCESS_ATTR_STATUS_INVALID if
 * an invalid tracker argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_user_id_tracker_handle_remove_user_name(
		const struct lttng_process_attr_tracker_handle *user_id_tracker,
		const char *user_name);

/*
 * Add a numerical UID to the virtual user ID process attribute tracker
 * include list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_ALREADY_PRESENT if it was already
 * present in the include list, and
 * LTTNG_PROCESS_ATTR_STATUS_INVALID if an invalid tracker
 * argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_virtual_user_id_tracker_handle_add_uid(
		const struct lttng_process_attr_tracker_handle *user_id_tracker,
		uid_t vuid);

/*
 * Remove a numerical UID from the virtual user ID process attribute tracker
 * include list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_NOT_PRESENT if it was not present
 * in the include list, and LTTNG_PROCESS_ATTR_STATUS_INVALID if
 * an invalid tracker argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_virtual_user_id_tracker_handle_remove_uid(
		const struct lttng_process_attr_tracker_handle *user_id_tracker,
		uid_t vuid);

/*
 * Add a user name to the virtual user ID process attribute tracker include
 * list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_ALREADY_PRESENT if it was already
 * present in the include list, and
 * LTTNG_PROCESS_ATTR_STATUS_INVALID if an invalid tracker
 * argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_virtual_user_id_tracker_handle_add_user_name(
		const struct lttng_process_attr_tracker_handle *user_id_tracker,
		const char *virtual_user_name);

/*
 * Remove a user name from the virtual user ID process attribute tracker
 * include list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_NOT_PRESENT if it was not present
 * in the include list, and LTTNG_PROCESS_ATTR_STATUS_INVALID if
 * an invalid tracker argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_virtual_user_id_tracker_handle_remove_user_name(
		const struct lttng_process_attr_tracker_handle *user_id_tracker,
		const char *virtual_user_name);

/*
 * Add a numerical UID to the group ID process attribute tracker include list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_ALREADY_PRESENT if it was already
 * present in the include list, and
 * LTTNG_PROCESS_ATTR_STATUS_INVALID if an invalid tracker
 * argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_group_id_tracker_handle_add_gid(
		const struct lttng_process_attr_tracker_handle *group_id_tracker,
		gid_t gid);

/*
 * Remove a numerical GID from the group ID process attribute tracker include
 * list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_NOT_PRESENT if it was not present
 * in the include list, and LTTNG_PROCESS_ATTR_STATUS_INVALID if
 * an invalid tracker argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_group_id_tracker_handle_remove_gid(
		const struct lttng_process_attr_tracker_handle *group_id_tracker,
		gid_t gid);

/*
 * Add a group name to the group ID process attribute tracker include list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_ALREADY_PRESENT if it was already
 * present in the include list, and
 * LTTNG_PROCESS_ATTR_STATUS_INVALID if an invalid tracker
 * argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_group_id_tracker_handle_add_group_name(
		const struct lttng_process_attr_tracker_handle *group_id_tracker,
		const char *group_name);

/*
 * Remove a group name from the group ID process attribute tracker include
 * list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_NOT_PRESENT if it was not present
 * in the include list, and LTTNG_PROCESS_ATTR_STATUS_INVALID if
 * an invalid tracker argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_group_id_tracker_handle_remove_group_name(
		const struct lttng_process_attr_tracker_handle *group_id_tracker,
		const char *group_name);

/*
 * Add a numerical UID to the virtual group ID process attribute tracker
 * include list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_ALREADY_PRESENT if it was already
 * present in the include list, and
 * LTTNG_PROCESS_ATTR_STATUS_INVALID if an invalid tracker
 * argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_virtual_group_id_tracker_handle_add_gid(
		const struct lttng_process_attr_tracker_handle *group_id_tracker,
		gid_t vgid);

/*
 * Remove a numerical GID from the virtual group ID process attribute tracker
 * include list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_NOT_PRESENT if it was not present
 * in the include list, and LTTNG_PROCESS_ATTR_STATUS_INVALID if
 * an invalid tracker argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_virtual_group_id_tracker_handle_remove_gid(
		const struct lttng_process_attr_tracker_handle *group_id_tracker,
		gid_t vgid);

/*
 * Add a group name to the virtual group ID process attribute tracker include
 * list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_ALREADY_PRESENT if it was already
 * present in the include list, and
 * LTTNG_PROCESS_ATTR_STATUS_INVALID if an invalid tracker
 * argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_virtual_group_id_tracker_handle_add_group_name(
		const struct lttng_process_attr_tracker_handle *group_id_tracker,
		const char *virtual_group_name);

/*
 * Remove a group name from the virtual group ID process attribute tracker
 * include list.
 *
 * Returns LTTNG_PROCESS_ATTR_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_STATUS_NOT_PRESENT if it was not present
 * in the include list, and LTTNG_PROCESS_ATTR_STATUS_INVALID if
 * an invalid tracker argument was provided.
 */
extern enum lttng_process_attr_status
lttng_process_attr_virtual_group_id_tracker_handle_remove_group_name(
		const struct lttng_process_attr_tracker_handle *group_id_tracker,
		const char *virtual_group_name);

/*
 * Get the process attribute values that are part of a tracker's include list.
 *
 * The values returned are a snapshot of the values that are part of the
 * tracker's include list at the moment of the invocation; it is not updated
 * as entries are added or removed.
 *
 * The values remain valid until the tracker is destroyed or its policy is
 * changed.
 *
 * Returns LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_TRACKER_HANDLE_STATUS_INVALID if the tracker's policy is
 * not LTTNG_TRACKING_POLICY_INCLUDE_LIST.
 */
extern enum lttng_process_attr_tracker_handle_status
lttng_process_attr_tracker_handle_get_include_list(
		const struct lttng_process_attr_tracker_handle *tracker,
		const struct lttng_process_attr_values **values);

/*
 * Get the count of values within a set of process attribute values.
 *
 * Returns LTTNG_PROCESS_ATTR_VALUES_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_VALUES_STATUS_INVALID if an invalid argument is provided.
 */
extern enum lttng_process_attr_values_status
lttng_process_attr_values_get_count(
		const struct lttng_process_attr_values *values,
		unsigned int *count);

/*
 * Get a process attribute value at a given index.
 *
 * Returns a process attribute value type on success,
 * LTTNG_PROCESS_ATTR_VALUE_TYPE_INVALID if an invalid argument is provided.
 */
extern enum lttng_process_attr_value_type
lttng_process_attr_values_get_type_at_index(
		const struct lttng_process_attr_values *values,
		unsigned int index);

/*
 * Get the value of a process ID process attribute value.
 *
 * Returns LTTNG_PROCESS_ATTR_VALUE_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_VALUE_STATUS_INVALID_TYPE if the process attribute value
 * is not a process ID.
 */
extern enum lttng_process_attr_value_status
lttng_process_attr_values_get_pid_at_index(
		struct lttng_process_attr_values *values, pid_t *pid);

/*
 * Get the value of a user ID process attribute value.
 *
 * Returns LTTNG_PROCESS_ATTR_VALUE_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_VALUE_STATUS_INVALID_TYPE if the process attribute value
 * is not a user ID.
 */
extern enum lttng_process_attr_value_status
lttng_process_attr_values_get_uid_at_index(
		struct lttng_process_attr_values *values, uid_t *uid);

/*
 * Get the value of a user name process attribute value.
 *
 * Returns LTTNG_PROCESS_ATTR_VALUE_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_VALUE_STATUS_INVALID_TYPE if the process attribute value
 * is not a user name.
 */
extern enum lttng_process_attr_value_status
lttng_process_attr_values_get_user_name_at_index(
		struct lttng_process_attr_values *values,
		const char **user_name);

/*
 * Get the value of a group ID process attribute value.
 *
 * Returns LTTNG_PROCESS_ATTR_VALUE_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_VALUE_STATUS_INVALID_TYPE if the process attribute value
 * is not a group ID.
 */
extern enum lttng_process_attr_value_status
lttng_process_attr_values_get_gid_at_index(
		struct lttng_process_attr_values *values, gid_t *gid);

/*
 * Get the value of a group name process attribute value.
 *
 * Returns LTTNG_PROCESS_ATTR_VALUE_STATUS_OK on success,
 * LTTNG_PROCESS_ATTR_VALUE_STATUS_INVALID_TYPE if the process attribute value
 * is not a group name.
 */
extern enum lttng_process_attr_value_status
lttng_process_attr_values_get_group_name_at_index(
		struct lttng_process_attr_values *values,
		const char **group_name);


/* The following entry points are deprecated. */

/*
 * Deprecated: see `lttng_list_tracker_ids`.
 *
 * List tracked PIDs.
 *
 * `enabled` indicates whether or not the PID tracker is enabled.
 *
 * `pids` is set to an allocated array of PIDs currently being tracked. On
 * success, `pids` must be freed by the caller.
 *
 * `nr_pids` is set to the number of entries contained in the `pids` array.
 *
 * Returns 0 on success, else a negative LTTng error code.
 */
extern int lttng_list_tracker_pids(struct lttng_handle *handle,
		int *enabled,
		int32_t **pids,
		size_t *nr_pids);

/*
 * Deprecated: see `lttng_track_id`.
 *
 * Add PID to session tracker.
 *
 * A pid argument >= 0 adds the PID to the session's PID tracker.
 * A pid argument of -1 means "track all PIDs".
 *
 * Returns 0 on success, else a negative LTTng error code.
 */
extern int lttng_track_pid(struct lttng_handle *handle, int pid);

/*
 * Deprecated: see `lttng_untrack_id`.
 *
 * Remove PID from session tracker.
 *
 * A pid argument >= 0 removes the PID from the session's PID tracker.
 * A pid argument of -1 means "untrack all PIDs".
 *
 * Returns 0 on success, else a negative LTTng error code.
 */
extern int lttng_untrack_pid(struct lttng_handle *handle, int pid);

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_TRACKER_H */
