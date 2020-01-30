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

#ifndef LTTNG_EVENT_RULE_SYSCALL_H
#define LTTNG_EVENT_RULE_SYSCALL_H

#include <lttng/event-rule/event-rule.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TODO:
 */
extern struct lttng_event_rule *lttng_event_rule_syscall_create(void);

/*
 * Set the pattern of a syscall event rule.
 *
 * Pattern can contains wildcard '*'. See man lttng-enable-event.
 *
 * Return LTTNG_EVENT_RULE_STATUS_OK on success, LTTNG_EVENT_RULE_STATUS_INVALID
 * if invalid parameters are passed.
 */
extern enum lttng_event_rule_status lttng_event_rule_syscall_set_pattern(
		struct lttng_event_rule *rule, const char *pattern);

/*
 * Get the pattern of a tracecpoint event rule.
 *
 * The caller does not assume the ownership of the returned pattern. The
 * pattern shall only only be used for the duration of the event rule's
 * lifetime, or before a different pattern is set.
 *
 * Returns LTTNG_EVENT_RULE_STATUS_OK and a pointer to the event rule's pattern
 * on success, LTTNG_EVENT_RULE_STATUS_INVALID if an invalid
 * parameter is passed, or LTTNG_EVENT_RULE_STATUS_UNSET if a pattern
 * was not set prior to this call.
 */
extern enum lttng_event_rule_status lttng_event_rule_syscall_get_pattern(
		const struct lttng_event_rule *rule, const char **pattern);

/*
 * Set the filter expression of a syscall event rule.
 *
 * The expression is copied.
 *
 * Return LTTNG_EVENT_RULE_STATUS_OK on success, LTTNG_EVENT_RULE_STATUS_INVALID
 * if invalid parameters are passed.
 */
extern enum lttng_event_rule_status lttng_event_rule_syscall_set_filter(
		struct lttng_event_rule *rule, const char *expression);

/*
 * Get the filter expression of a syscall event rule.
 *
 * The caller does not assume the ownership of the returned filter expression.
 * The filter expression shall only only be used for the duration of the event
 * rule's lifetime, or before a different filter expression is set.
 *
 * Returns LTTNG_EVENT_RULE_STATUS_OK and a pointer to the event rule's filter
 * expression on success, LTTNG_EVENT_RULE_STATUS_INVALID if an invalid
 * parameter is passed, or LTTNG_EVENT_RULE_STATUS_UNSET if a filter expression
 * was not set prior to this call.
 */
extern enum lttng_event_rule_status lttng_event_rule_syscall_get_filter(
		const struct lttng_event_rule *rule, const char **expression);

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_EVENT_RULE_H */
