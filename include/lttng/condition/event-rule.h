/*
 * Copyright (C) 2019 - Jonathan Rajotte <jonathan.rajotte-julien@efficios.com>
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

#ifndef LTTNG_CONDITION_EVENT_RULE_H
#define LTTNG_CONDITION_EVENT_RULE_H

#include <lttng/event-rule/event-rule.h>
#include <lttng/condition/condition.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * TODO: overall desciption of an event rule condition.
 * */

/*
 * Create a newly allocated event rule condition.
 *
 * Rule will be copied internally.
 *
 * Returns a new condition on success, NULL on failure. This condition must be
 * destroyed using lttng_condition_destroy().
 */
extern struct lttng_condition *lttng_condition_event_rule_create(
		struct lttng_event_rule *rule);

/*
 * Get the rule property of a event rule condition.
 *
 * The caller does not assume the ownership of the returned rule. The
 * rule shall only be used for the duration of the condition's
 * lifetime.
 *
 * Returns LTTNG_CONDITION_STATUS_OK and a pointer to the condition's rule
 * on success, LTTNG_CONDITION_STATUS_INVALID if an invalid
 * parameter is passed. */
extern enum lttng_condition_status lttng_condition_event_rule_get_rule(
		const struct lttng_condition *condition, const struct lttng_event_rule **rule);

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_CONDITION_BUFFER_USAGE_H */
