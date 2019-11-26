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

	/* Required field */
	if (!tracepoint->pattern) {
		ERR("Invalid tracepoint event rule: a pattern must be set.");
		goto end;
	}
	if (tracepoint->domain == LTTNG_DOMAIN_NONE) {
		ERR("Invalid tracepoint event rule: a domain must be set.");
		goto end;
	}

	/* QUESTION: do we validate inside state on validate or during set of
	 * each component */

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

	/* Quick checks */
	if (a->domain != b->domain) {
		goto end;
	}

	if (a->exclusions.count != b->exclusions.count) {
		goto end;
	}

	if (!!a->filter_expression != !!b->filter_expression) {
		goto end;
	}

	/* Long check */
	/* Tracepoint is invalid if this is not true */
	assert(a->pattern);
	assert(b->pattern);
	if (strcmp(a->pattern, b->pattern)) {
		goto end;
	}

	if (a->filter_expression && b->filter_expression) {
		if (strcmp(a->filter_expression, b->filter_expression)) {
			goto end;
		}
	}

	if (a->loglevel.type != b->loglevel.type) {
		goto end;
	}

	if (a->loglevel.value != b->loglevel.value) {
		goto end;
	}

	for (int i = 0; i < a->exclusions.count; i++) {
		if (strcmp(a->exclusions.values[i], b->exclusions.values[i])) {
			goto end;
		}
	}

	is_equal = true;
end:
	return is_equal;
}

struct lttng_event_rule *lttng_event_rule_tracepoint_create(enum lttng_domain_type domain_type)
{
	struct lttng_event_rule_tracepoint *rule;

	if (domain_type == LTTNG_DOMAIN_NONE) {
		return NULL;
	}

	rule = zmalloc(sizeof(struct lttng_event_rule_tracepoint));
	if (!rule) {
		return NULL;
	}

	lttng_event_rule_init(&rule->parent, LTTNG_EVENT_RULE_TYPE_TRACEPOINT);
	rule->parent.validate = lttng_event_rule_tracepoint_validate;
	rule->parent.serialize = lttng_event_rule_tracepoint_serialize;
	rule->parent.equal = lttng_event_rule_tracepoint_is_equal;
	rule->parent.destroy = lttng_event_rule_tracepoint_destroy;

	rule->domain = domain_type;
	rule->loglevel.type = LTTNG_EVENT_LOGLEVEL_ALL;

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
	char *pattern_copy = NULL;
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule) || !pattern ||
			strlen(pattern) == 0) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	pattern_copy = strdup(pattern);
	if (!pattern_copy) {
		status = LTTNG_EVENT_RULE_STATUS_ERROR;
		goto end;
	}

	if (tracepoint->pattern) {
		free(tracepoint->pattern);
	}

	tracepoint->pattern = pattern_copy;
	pattern_copy = NULL;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_pattern(
		const struct lttng_event_rule *rule, const char **pattern)
{
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule) || !pattern) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	if (!tracepoint->pattern) {
		status = LTTNG_EVENT_RULE_STATUS_UNSET;
		goto end;
	}

	*pattern = tracepoint->pattern;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_domain_type(
		const struct lttng_event_rule *rule,
		enum lttng_domain_type *type)
{
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule) || !type) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	*type = tracepoint->domain;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_set_filter(
		struct lttng_event_rule *rule, const char *expression)
{
	char *expression_copy = NULL;
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	/* TODO: validate that the passed expression is valid */

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule) || !expression ||
			strlen(expression) == 0) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(
			rule, struct lttng_event_rule_tracepoint, parent);
	expression_copy = strdup(expression);
	if (!expression_copy) {
		status = LTTNG_EVENT_RULE_STATUS_ERROR;
		goto end;
	}

	if (tracepoint->filter_expression) {
		free(tracepoint->filter_expression);
	}

	tracepoint->filter_expression = expression_copy;
	expression_copy = NULL;
end:
	return status;
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
