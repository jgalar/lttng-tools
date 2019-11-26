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

#include <lttng/event-rule/event-rule-internal.h>
#include <lttng/event-rule/event-rule-tracepoint-internal.h>
#include <common/macros.h>
#include <common/error.h>
#include <assert.h>

#define IS_TRACEPOINT_EVENT_RULE(rule) ( \
	lttng_event_rule_get_type(rule) == LTTNG_EVENT_RULE_TYPE_TRACEPOINT \
	)

static
void lttng_event_rule_tracepoint_destroy(struct lttng_event_rule *rule)
{
	struct lttng_event_rule_tracepoint *tracepoint;

	if (rule == NULL) {
		return;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);

	free(tracepoint->pattern);
	free(tracepoint->filter_expression);
	for(int i = 0; i < tracepoint->exclusions.count; i++) {
		free(tracepoint->exclusions.values[i]);
	}
	free(tracepoint->exclusions.values);
	free(tracepoint);
}

static
bool lttng_event_rule_tracepoint_validate(
		const struct lttng_event_rule *rule)
{
	bool valid = false;
	struct lttng_event_rule_tracepoint *tracepoint;

	if (!rule) {
		goto end;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);

	/*
	 * TODO
	 */

	valid = true;
end:
	return valid;
}

static
int lttng_event_rule_tracepoint_serialize(
		const struct lttng_event_rule *rule,
		struct lttng_dynamic_buffer *buf)
{
	int ret;
	struct lttng_event_rule_tracepoint *tracepoint;

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule)) {
		ret = -1;
		goto end;
	}

	DBG("Serializing tracepoint event rule");
	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);

	/* TODO */
end:
	return ret;
}

static
bool lttng_event_rule_tracepoint_is_equal(const struct lttng_event_rule *_a,
		const struct lttng_event_rule *_b)
{
	bool is_equal = false;
	struct lttng_event_rule_tracepoint *a, *b;

	a = container_of(_a, struct lttng_event_rule_tracepoint, parent);
	b = container_of(_b, struct lttng_event_rule_tracepoint, parent);

	/* TODO */
	is_equal = true;
end:
	return is_equal;
}

struct lttng_event_rule *lttng_event_rule_tracepoint_create(enum lttng_domain_type domain_type)
{
	struct lttng_event_rule_tracepoint *rule;

	rule = zmalloc(sizeof(struct lttng_event_rule_tracepoint));
	if (!rule) {
		return NULL;
	}

	lttng_event_rule_init(&rule->parent, LTTNG_EVENT_RULE_TYPE_TRACEPOINT);
	rule->parent.validate = lttng_event_rule_tracepoint_validate;
	rule->parent.serialize = lttng_event_rule_tracepoint_serialize;
	rule->parent.equal = lttng_event_rule_tracepoint_is_equal;
	rule->parent.destroy = lttng_event_rule_tracepoint_destroy;
	return &rule->parent;
}

LTTNG_HIDDEN
ssize_t lttng_event_rule_tracepoint_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_event_rule **_event_rule)
{
	ssize_t ret;
	struct lttng_event_rule *rule =
			lttng_event_rule_tracepoint_create();

	if (!_event_rule || !rule) {
		ret = -1;
		goto error;
	}

	/* TODO */

	*_event_rule = rule;
	return ret;
error:
	lttng_event_rule_destroy(rule);
	return ret;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_set_pattern(
		struct lttng_event_rule *rule, const char *pattern)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_pattern(
		const struct lttng_event_rule *rule, const char **pattern)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_domain_type(
		const struct lttng_event_rule *rule,
		enum lttng_domain_type *type)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_set_filter(
		struct lttng_event_rule *rule, const char *expression)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_filter(
		const struct lttng_event_rule *rule, const char **expression)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_set_loglevel(
		struct lttng_event_rule *rule, int level)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_set_loglevel_range(
		struct lttng_event_rule *rule, int level)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_set_loglevel_all(
		struct lttng_event_rule *rule)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_loglevel_type(
		const struct lttng_event_rule *rule, enum lttng_loglevel_type *type)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_loglevel(
		const struct lttng_event_rule *rule, int *level)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_set_exclusions(
		struct lttng_event_rule *rule,
		unsigned int count,
		const char **exclusions)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_exclusions_count(
		struct lttng_event_rule *rule, unsigned int *count)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_exclusion_at_index(
		struct lttng_event_rule *rule,
		unsigned int index,
		const char **exclusion)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}
