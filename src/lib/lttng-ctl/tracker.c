/*
 * Copyright (C) 2020 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#include "lttng/domain.h"
#include "lttng/lttng-error.h"
#include <lttng/tracker.h>
#include <common/tracker.h>
#include <common/sessiond-comm/sessiond-comm.h>

struct lttng_process_attr_tracker_handle
{
	char *session_name;
	enum lttng_domain_type domain;
	enum lttng_process_attr process_attr;
};

struct lttng_process_attr_value_comm
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

void lttng_process_attr_tracker_handle_destroy(
		struct lttng_process_attr_tracker_handle *tracker)
{
	if (!tracker) {
		return;
	}

	free(tracker->session_name);
	free(tracker);
}

enum lttng_error_code lttng_session_get_tracker(const char *session_name,
		enum lttng_domain_type domain,
		enum lttng_process_attr process_attr,
		struct lttng_process_attr_tracker_handle **out_tracker)
{
	int ret;
	enum lttng_error_code code = LTTNG_OK;
	struct lttng_process_attr_tracker_handle *tracker;

	if (domain == LTTNG_DOMAIN_UST || domain == LTTNG_DOMAIN_KERNEL) {
		code = LTTNG_ERR_UNSUPPORTED_DOMAIN;
		goto error;
	}
	if (!session_name) {
		code = LTTNG_ERR_INVALID;
		goto error;
	}

	tracker = zmalloc(sizeof(*tracker));
	if (!tracker) {
		code = LTTNG_ERR_NOMEM;
		goto error;
	}

	tracker->session_name = strdup(session_name);
	if (!tracker->session_name) {
		code = LTTNG_ERR_NOMEM;
		goto error;
	}

	/* TODO Validate session exists. */

	return code;
error:
	lttng_process_attr_tracker_handle_destroy(tracker);
	return code;
}
