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

#ifndef LTTNG_EVENT_RULE_TRACEPOINT_H
#define LTTNG_EVENT_RULE_TRACEPOINT_H

#include <lttng/event-rule/event-rule.h>
#include <lttng/domain.h>
#include <lttng/event.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TODO:
 */
extern struct lttng_event_rule *lttng_event_rule_tracepoint_create(
		enum lttng_domain_type domain);

/*
 * Set the pattern of a tracepoint event rule.
 *
 * Pattern can contains wildcard '*'. See man lttng-enable-event.
 *
 * Return LTTNG_EVENT_RULE_STATUS_OK on success, LTTNG_EVENT_RULE_STATUS_INVALID
 * if invalid parameters are passed.
 */
extern enum lttng_event_rule_status lttng_event_rule_tracepoint_set_pattern(
		struct lttng_event_rule *rule, const char *pattern);

/*
 * Get the pattern of a tracepoint event rule.
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
extern enum lttng_event_rule_status lttng_event_rule_tracepoint_get_pattern(
		const struct lttng_event_rule *rule, const char **pattern);

/*
 * Get the domain type of a tracecpoint event rule.
 *
 * Returns LTTNG_EVENT_RULE_STATUS_OK and sets the domain type output parameter
 * on success, LTTNG_EVENT_RULE_STATUS_INVALID if an invalid parameter is
 * passed, or LTTNG_EVENT_RULE_STATUS_UNSET if a pattern was not set prior to
 * this call.
 */
extern enum lttng_event_rule_status lttng_event_rule_tracepoint_get_domain_type(
		const struct lttng_event_rule *rule,
		enum lttng_domain_type *type);

/*
 * Set the filter expression of a tracepoint event rule.
 *
 * The expression is copied.
 *
 * Return LTTNG_EVENT_RULE_STATUS_OK on success, LTTNG_EVENT_RULE_STATUS_INVALID
 * if invalid parameters are passed.
 */
extern enum lttng_event_rule_status lttng_event_rule_tracepoint_set_filter(
		struct lttng_event_rule *rule, const char *expression);

/*
 * Get the filter expression of a tracepoint event rule.
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
extern enum lttng_event_rule_status lttng_event_rule_tracepoint_get_filter(
		const struct lttng_event_rule *rule, const char **expression);

/*
 * Set the single loglevel of a tracepoint event rule.
 *
 * Return LTTNG_EVENT_RULE_STATUS_OK on success, LTTNG_EVENT_RULE_STATUS_INVALID
 * if invalid parameters are passed.
 */
extern enum lttng_event_rule_status lttng_event_rule_tracepoint_set_loglevel(
		struct lttng_event_rule *rule, int level);

/*
 * Set the loglevel range of a tracepoint event rule.
 *
 * Return LTTNG_EVENT_RULE_STATUS_OK on success, LTTNG_EVENT_RULE_STATUS_INVALID
 * if invalid parameters are passed.
 */
extern enum lttng_event_rule_status
lttng_event_rule_tracepoint_set_loglevel_range(
		struct lttng_event_rule *rule, int level);

/*
 * Set the loglevel to all of a tracepoint event rule.
 *
 * Return LTTNG_EVENT_RULE_STATUS_OK on success, LTTNG_EVENT_RULE_STATUS_INVALID
 * if invalid parameters are passed.
 */
extern enum lttng_event_rule_status
lttng_event_rule_tracepoint_set_loglevel_all(struct lttng_event_rule *rule);

/*
 * Get the loglevel type of a tracepoint event rule.
 *
 * Returns LTTNG_EVENT_RULE_STATUS_OK and sets the loglevel type output
 * parameter on success, LTTNG_EVENT_RULE_STATUS_INVALID if an invalid parameter
 * is passed, or LTTNG_EVENT_RULE_STATUS_UNSET if a loglevel was not set prior
 * to this call.
 */
extern enum lttng_event_rule_status
lttng_event_rule_tracepoint_get_loglevel_type(
		const struct lttng_event_rule *rule, enum lttng_loglevel_type *type);

/*
 * Get the loglevel of a tracepoint event rule.
 *
 * Returns LTTNG_EVENT_RULE_STATUS_OK and sets the loglevel output parameter
 * on success, LTTNG_EVENT_RULE_STATUS_INVALID if an invalid parameter is
 * passed, or LTTNG_EVENT_RULE_STATUS_UNSET if a loglevel was not set prior to
 * this call.
 */
extern enum lttng_event_rule_status lttng_event_rule_tracepoint_get_loglevel(
		const struct lttng_event_rule *rule, int *level);

/*
 * Set the exclusions property of a event rule.
 *
 * The passed exclusions will be copied to the event_rule.
 *
 * Returns LTTNG_EVENT_RULE_STATUS_OK on success,
 * LTTNG_EVENT_RULE_STATUS_INVALID if invalid parameters are passed, or
 * LTTNG_EVENT_RULE_STATUS_UNSUPPORTED if this property is not supported by the
 * domain.
 */
extern enum lttng_event_rule_status lttng_event_rule_tracepoint_set_exclusions(
		struct lttng_event_rule *rule,
		unsigned int count,
		const char **exclusions);

/*
 * Get the exclusions property count of a event rule.
 *
 * Returns LTTNG_EVENT_RULE_STATUS_OK and sets the count output parameter
 * on success, LTTNG_EVENT_RULE_STATUS_INVALID if an invalid parameter is
 * passed, or LTTNG_EVENT_RULE_STATUS_UNSET if a domain type was not set prior
 * to this call.
 */
extern enum lttng_event_rule_status
lttng_event_rule_tracepoint_get_exclusions_count(
		struct lttng_event_rule *rule, unsigned int *count);

/*
 * TODO:
 * */
extern enum lttng_event_rule_status
lttng_event_rule_tracepoint_get_exclusion_at_index(
		struct lttng_event_rule *rule,
		unsigned int index,
		const char **exclusion);

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_EVENT_RULE_TRACEPOINT_H */
