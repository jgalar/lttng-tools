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

#ifndef LTTNG_EVENT_RULE_H
#define LTTNG_EVENT_RULE_H

#ifdef __cplusplus
extern "C" {
#endif

struct lttng_event_rule;

enum lttng_event_rule_type {
	LTTNG_EVENT_RULE_TYPE_UNKNOWN = -1,
	LTTNG_EVENT_RULE_TYPE_TRACEPOINT = 100,
	LTTNG_EVENT_RULE_TYPE_SYSCALL = 101,
	LTTNG_EVENT_RULE_TYPE_KPROBE = 102,
	LTTNG_EVENT_RULE_TYPE_KRETPROBE = 103,
	LTTNG_EVENT_RULE_TYPE_UPROBE = 104,
};


enum lttng_event_rule_status {
	LTTNG_EVENT_RULE_STATUS_OK = 0,
	LTTNG_EVENT_RULE_STATUS_ERROR = -1,
	LTTNG_EVENT_RULE_STATUS_UNKNOWN = -2,
	LTTNG_EVENT_RULE_STATUS_INVALID = -3,
	LTTNG_EVENT_RULE_STATUS_UNSET = -4,
	LTTNG_EVENT_RULE_STATUS_UNSUPPORTED = -5,
};

/**
 * Event rule describe a set of criteria to be used as a discriminant in regards
 * to a set of events.
 *
 * Event rule have the following base properties:
 *   - the instrumentation type for the event rule,
 *       - tracepoint
 *       - syscall
 *       - probe
 *       - userspace-probe
 *   - the domain the event rule covers.
 */

/*
 * Get the event rule type.
 *
 * Returns the type of an event rule on success, LTTNG_EVENT_RULE_UNKNOWN on
 * error.
 */
extern enum lttng_event_rule_type lttng_event_rule_get_type(
		const struct lttng_event_rule *event_rule);

/*
 * Destroy (release) a event_rule object.
 */
extern void lttng_event_rule_destroy(struct lttng_event_rule *rule);

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_EVENT_RULE_H */
