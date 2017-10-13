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

#ifndef LTTNG_CONDITION_SESSION_USAGE_INTERNAL_H
#define LTTNG_CONDITION_SESSION_USAGE_INTERNAL_H

#include <lttng/condition/session-usage.h>
#include <lttng/condition/condition-internal.h>
#include <lttng/condition/evaluation-internal.h>
#include <lttng/domain.h>
#include "common/session-view.h"

struct lttng_condition_session_usage {
	struct lttng_condition parent;
	struct {
		bool set;
		uint64_t value;
	} consumed_threshold_bytes;
	char *session_name;
};

struct lttng_condition_session_usage_comm {
	uint64_t consumed_threshold;
	/* Length includes the trailing \0. */
	uint32_t session_name_len;
	char session_names[];
} LTTNG_PACKED;

struct lttng_evaluation_session_usage {
	struct lttng_evaluation parent;
	uint64_t session_consumed;
};

struct lttng_evaluation_session_usage_comm {
	uint64_t session_consumed;
} LTTNG_PACKED;

LTTNG_HIDDEN
struct lttng_evaluation *lttng_evaluation_session_usage_create(
		enum lttng_condition_type type, uint64_t consumed);

LTTNG_HIDDEN
ssize_t lttng_condition_session_usage_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_condition **condition);

LTTNG_HIDDEN
ssize_t lttng_evaluation_session_usage_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_evaluation **evaluation);

#endif /* LTTNG_CONDITION_SESSION_USAGE_INTERNAL_H */
