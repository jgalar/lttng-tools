/*
 * Copyright (C) 2019 Jonathan Rajotte-Julien <jonathan.rajotte-julien@efficios.com>
 * Copyright (C) 2020 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#include <lttng/tracker.h>
#include <common/tracker.h>
#include <common/dynamic-array.h>
#include <common/optional.h>

struct lttng_process_attr_value
{
	enum lttng_process_attr_value_type type;
	union {
		pid_t pid;
		pid_t vpid;
		uid_t uid;
		uid_t vuid;
		gid_t gid;
		gid_t vgid;
		char *user_name;
		char *group_name;
	} value;
};

struct lttng_process_attr_values
{
	struct lttng_dynamic_array values;
};

struct lttng_process_attr_tracker_id {
};

/*
struct lttng_process_attr_tracker
{
	enum lttng_process_attr target;
	enum lttng_tracking_policy policy;
	LTTNG_OPTIONAL(struct lttng_process_attr_values) include_list;
};
*/
enum lttng_process_attr_tracker_status lttng_tracker_get_tracking_policy(
		const struct lttng_process_attr_tracker *tracker,
		enum lttng_tracking_policy *policy)
{
	enum lttng_process_attr_tracker_status status = LTTNG_PROCESS_ATTR_TRACKER_STATUS_OK;

	if (!tracker || !policy) {
		status = LTTNG_PROCESS_ATTR_TRACKER_STATUS_INVALID;
		goto end;
	}
	*policy = tracker->policy;
 end:
	return status;
}

enum lttng_process_attr_tracker_status lttng_tracker_set_tracking_policy(
		const struct lttng_process_attr_tracker *tracker,
		enum lttng_tracking_policy *policy)
{
	enum lttng_process_attr_tracker_status status = LTTNG_PROCESS_ATTR_TRACKER_STATUS_OK;

	if (!tracker || !policy) {
		status = LTTNG_PROCESS_ATTR_TRACKER_STATUS_INVALID;
		goto end;
	}
	*policy = tracker->policy;
 end:
	return status;
}

#define DEFINE_TRACKER_ADD_VALUE_FUNC(process_attr_name, value_type_name,                     \
		value_type, value_union_field_name)                                           \
	enum lttng_process_attr_tracker_status                                                \
			lttng_process_attr_tracker_##process_attr_name_add_##value_type_name( \
					const struct lttng_process_attr_tracker               \
							*tracker,                             \
					value_type value)                                     \
	{                                                                                     \
		enum lttng_process_attr_tracker_status status =                               \
				LTTNG_PROCESS_ATTR_TRACKER_STATUS_OK;                         \
		return status;                                                                \
	}

DEFINE_TRACKER_ADD_VALUE_FUNC(user_id, uid, uid_t, uid);
