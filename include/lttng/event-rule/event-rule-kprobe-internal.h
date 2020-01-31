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

#ifndef LTTNG_EVENT_RULE_KPROBE_INTERNAL_H
#define LTTNG_EVENT_RULE_KPROBE_INTERNAL_H

#include <lttng/event-rule/event-rule-kprobe.h>
#include <lttng/event-rule/event-rule-internal.h>
#include <common/buffer-view.h>
#include <common/macros.h>

struct lttng_event_rule_kprobe {
	struct lttng_event_rule parent;
	char *name;
	struct {
		uint64_t address;
		uint64_t offset;
		char *symbol_name;
		bool set;
	} probe;
};

struct lttng_event_rule_kprobe_comm {
	uint32_t name_len;
	uint32_t probe_symbol_name_len;
	uint64_t probe_address;
	uint64_t probe_offset;
	/* name, source symbol_name */
	char payload[];
} LTTNG_PACKED;

LTTNG_HIDDEN
ssize_t lttng_event_rule_kprobe_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_event_rule **rule);

LTTNG_HIDDEN
uint64_t lttng_event_rule_kprobe_get_address(
		const struct lttng_event_rule *rule);

LTTNG_HIDDEN
uint64_t lttng_event_rule_kprobe_get_offset(
		const struct lttng_event_rule *rule);

LTTNG_HIDDEN
const char *lttng_event_rule_kprobe_get_symbol_name(
		const struct lttng_event_rule *rule);

#endif /* LTTNG_EVENT_RULE_KPROBE_INTERNAL_H */
