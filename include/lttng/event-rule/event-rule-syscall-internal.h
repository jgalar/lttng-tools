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

#ifndef LTTNG_EVENT_RULE_SYSCALL_INTERNAL_H
#define LTTNG_EVENT_RULE_SYSCALL_INTERNAL_H

#include <lttng/event-rule/event-rule-syscall.h>
#include <lttng/event-rule/event-rule-internal.h>
#include <common/buffer-view.h>
#include <common/macros.h>

struct lttng_event_rule_syscall {
	struct lttng_event_rule parent;
	char *pattern;
	char *filter_expression;
	/* internal use only */
	struct {
		char *filter;
		struct lttng_filter_bytecode *bytecode;
	} internal_filter;
};

struct lttng_event_rule_syscall_comm {
	uint32_t pattern_len;
	uint32_t filter_expression_len;
	/* pattern, filter expression */
	char payload[];
} LTTNG_PACKED;

LTTNG_HIDDEN
ssize_t lttng_event_rule_syscall_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_event_rule **rule);

#endif /* LTTNG_EVENT_RULE_SYSCALL_INTERNAL_H */
