/*
 * Copyright (C) 2019 Jonathan Rajotte <jonathan.rajotte-julien@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#ifndef LTTNG_EVENT_RULE_KRETPROBE_INTERNAL_H
#define LTTNG_EVENT_RULE_KRETPROBE_INTERNAL_H

#include <common/payload-view.h>
#include <common/macros.h>
#include <lttng/event-rule/event-rule-internal.h>
#include <lttng/event-rule/kretprobe.h>

struct lttng_event_rule_kretprobe {
	struct lttng_event_rule parent;
	char *name;
	struct {
		uint64_t address;
		uint64_t offset;
		char *symbol_name;
	} probe;
};

struct lttng_event_rule_kretprobe_comm {
	/* Includes terminator `\0`. */
	uint32_t name_len;
	/* Includes terminator `\0`. */
	uint32_t probe_symbol_len;
	/*
	 * name, probe symbol_name.
	 * Null-terminated.
	 */
	char payload[];
} LTTNG_PACKED;

LTTNG_HIDDEN
ssize_t lttng_event_rule_kretprobe_create_from_payload(
		struct lttng_payload_view *view,
		struct lttng_event_rule **rule);

LTTNG_HIDDEN
uint64_t lttng_event_rule_kretprobe_get_address(
		const struct lttng_event_rule *rule);

LTTNG_HIDDEN
uint64_t lttng_event_rule_kretprobe_get_offset(
		const struct lttng_event_rule *rule);

LTTNG_HIDDEN
const char *lttng_event_rule_kretprobe_get_symbol_name(
		const struct lttng_event_rule *rule);

#endif /* LTTNG_EVENT_RULE_KRETPROBE_INTERNAL_H */
