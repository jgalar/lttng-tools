/*
 * Copyright (C) 2019 - Jonathan Rajotte-Julien <jonathan.rajotte-julien@efficios.com>
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

#ifndef LTTNG_EVENT_RULE_TRACEPOINT_INTERNAL_H
#define LTTNG_EVENT_RULE_TRACEPOINT_INTERNAL_H

#include <lttng/event-rule/event-rule-tracepoint.h>
#include <lttng/event-rule/event-rule-internal.h>
#include <lttng/domain.h>
#include <lttng/event.h>
#include <common/buffer-view.h>
#include <common/macros.h>

struct lttng_event_rule_tracepoint {
	struct lttng_event_rule parent;

	/* Domain */
	enum lttng_domain_type domain;

	/* Name pattern */
	char *pattern;

	/* Filter */
	char *filter_expression;

	/* Loglevel */
	struct {
		enum lttng_loglevel_type type;
		int value;
	} loglevel;

	/* Exclusions */

	struct {
		char **values;
		unsigned int count;
	} exclusions;

	/* internal use only */
	struct {
		char *filter;
		struct lttng_filter_bytecode *bytecode;
	} internal_filter;
};

struct lttng_event_rule_tracepoint_comm {
	/* enum lttng_domain_type */
	int8_t domain_type;
	/* enum lttng_event_logleven_type */
	int8_t loglevel_type;
	int32_t loglevel_value;
	uint32_t pattern_len;
	uint32_t filter_expression_len;
	uint32_t exclusions_count;
	uint32_t exclusions_len;
	/*
	 * pattern, filter expression and exclusions each terminating with '\0'
	 */
	char payload[];
} LTTNG_PACKED;

LTTNG_HIDDEN
ssize_t lttng_event_rule_tracepoint_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_event_rule **rule);

#endif /* LTTNG_EVENT_RULE_TRACEPOINT_INTERNAL_H */
