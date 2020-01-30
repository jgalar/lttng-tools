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
#include <common/runas.h>
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
	free(tracepoint->internal_filter.filter);
	free(tracepoint->internal_filter.bytecode);
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
	size_t pattern_len, filter_expression_len, exclusions_len;
	struct lttng_event_rule_tracepoint *tracepoint;
	struct lttng_event_rule_tracepoint_comm tracepoint_comm;

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule)) {
		ret = -1;
		goto end;
	}

	DBG("Serializing tracepoint event rule");
	tracepoint = container_of(
			rule, struct lttng_event_rule_tracepoint, parent);

	pattern_len = strlen(tracepoint->pattern) + 1;

	if (tracepoint->filter_expression != NULL) {
		filter_expression_len =
				strlen(tracepoint->filter_expression) + 1;
	} else {
		filter_expression_len = 0;
	}

	exclusions_len = 0;
	for (int i = 0; i < tracepoint->exclusions.count; i++) {
		/* Payload */
		exclusions_len += strlen(tracepoint->exclusions.values[i]) + 1;
		/* Bound check */
		exclusions_len += sizeof(uint32_t);
	}

	tracepoint_comm.domain_type = (int8_t) tracepoint->domain;
	tracepoint_comm.loglevel_type = (int8_t) tracepoint->loglevel.type;
	tracepoint_comm.loglevel_value = tracepoint->loglevel.value;
	tracepoint_comm.pattern_len = pattern_len;
	tracepoint_comm.filter_expression_len = filter_expression_len;
	tracepoint_comm.exclusions_count = tracepoint->exclusions.count;
	tracepoint_comm.exclusions_len = exclusions_len;

	ret = lttng_dynamic_buffer_append(
			buf, &tracepoint_comm, sizeof(tracepoint_comm));
	if (ret) {
		goto end;
	}
	ret = lttng_dynamic_buffer_append(buf, tracepoint->pattern,
			pattern_len);
	if (ret) {
		goto end;
	}
	ret = lttng_dynamic_buffer_append(buf, tracepoint->filter_expression,
			filter_expression_len);
	if (ret) {
		goto end;
	}

	size_t exclusions_appended = 0;
	for (int i = 0; i < tracepoint->exclusions.count; i++) {
		size_t len;
		len = strlen(tracepoint->exclusions.values[i]) + 1;
		/* Append bound check, does not include the '\0' */
		ret = lttng_dynamic_buffer_append(buf, &len, sizeof(uint32_t));
		if (ret) {
			goto end;
		}
		exclusions_appended += sizeof(uint32_t);

		/* Include the '\0' in the payload */
		ret = lttng_dynamic_buffer_append(buf, tracepoint->exclusions.values[i],
				len);
		if (ret) {
			goto end;
		}
		exclusions_appended += len;
	}

	assert(exclusions_len == exclusions_appended);

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

/*
 * On success ret is 0;
 *
 * On error ret is negative.
 *
 * An event with NO loglevel and the name is * will return NULL.
 */
static int generate_agent_filter(
		const struct lttng_event_rule *rule,
		char **_agent_filter)
{
	int err;
	int ret = 0;
	char *agent_filter = NULL;
	const char *pattern;
	const char *filter;
	enum lttng_loglevel_type loglevel_type;
	enum lttng_event_rule_status status;

	assert(rule);
	assert(_agent_filter);

	status = lttng_event_rule_tracepoint_get_pattern(rule, &pattern);
	if (status != LTTNG_EVENT_RULE_STATUS_OK) {
		ret = -1;
		goto end;
	}

	status = lttng_event_rule_tracepoint_get_filter(rule, &filter);
	if (status == LTTNG_EVENT_RULE_STATUS_UNSET) {
		filter = NULL;
	} else if (status != LTTNG_EVENT_RULE_STATUS_OK) {
		ret = -1;
		goto end;
	}

	status = lttng_event_rule_tracepoint_get_loglevel_type(rule, &loglevel_type);
	if (status != LTTNG_EVENT_RULE_STATUS_OK) {
		ret = -1;
		goto end;
	}

	/* Don't add filter for the '*' event. */
	if (strcmp(pattern, "*") != 0) {
		if (filter) {
			err = asprintf(&agent_filter, "(%s) && (logger_name == \"%s\")", filter,
					pattern);
		} else {
			err = asprintf(&agent_filter, "logger_name == \"%s\"", pattern);
		}
		if (err < 0) {
			PERROR("asprintf");
			ret = -1;
			goto end;
		}
	}

	if (loglevel_type != LTTNG_EVENT_LOGLEVEL_ALL) {
		char *op;
		int loglevel_value;

		status = lttng_event_rule_tracepoint_get_loglevel(rule, &loglevel_value);
		if (status != LTTNG_EVENT_RULE_STATUS_OK) {
			ret = -1;
			goto end;
		}

		if (loglevel_type == LTTNG_EVENT_LOGLEVEL_RANGE) {
			op = ">=";
		} else {
			op = "==";
		}

		if (filter || agent_filter) {
			char *new_filter;

			err = asprintf(&new_filter, "(%s) && (int_loglevel %s %d)",
					agent_filter ? agent_filter : filter, op,
					loglevel_value);
			if (agent_filter) {
				free(agent_filter);
			}
			agent_filter = new_filter;
		} else {
			err = asprintf(&agent_filter, "int_loglevel %s %d", op,
					loglevel_value);
		}
		if (err < 0) {
			PERROR("asprintf");
			ret = -1;
			goto end;
		}
	}

	*_agent_filter = agent_filter;
	agent_filter = NULL;

end:
	free(agent_filter);
	return ret;
}

static
enum lttng_error_code lttng_event_rule_tracepoint_populate(struct lttng_event_rule *rule, uid_t uid, gid_t gid)
{
	int ret;
	enum lttng_error_code ret_code;
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_domain_type domain_type;
	enum lttng_event_rule_status status;
	const char *filter;
	struct lttng_filter_bytecode *bytecode = NULL;

	assert(rule);

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);

	status = lttng_event_rule_tracepoint_get_filter(rule, &filter);
	if (status == LTTNG_EVENT_RULE_STATUS_UNSET) {
		filter = NULL;
	} else if (status != LTTNG_EVENT_RULE_STATUS_OK) {
		ret_code = LTTNG_ERR_FILTER_INVAL;
		goto end;
	}

	if (filter && filter[0] == '\0') {
		ret_code = LTTNG_ERR_FILTER_INVAL;
		goto error;
	}

	status = lttng_event_rule_tracepoint_get_domain_type(rule, &domain_type);
	if (status !=  LTTNG_EVENT_RULE_STATUS_OK) {
		ret_code = LTTNG_ERR_UNK;
		goto error;
	}

	switch (domain_type) {
	case LTTNG_DOMAIN_LOG4J:
	case LTTNG_DOMAIN_JUL:
	case LTTNG_DOMAIN_PYTHON:
	{
		char *agent_filter;
		ret = generate_agent_filter(rule, &agent_filter);
		if (ret) {
			ret_code = LTTNG_ERR_FILTER_INVAL;
			goto error;
		}
		tracepoint->internal_filter.filter = agent_filter;
		break;
	}
	default:
	{
		if (filter) {
			tracepoint->internal_filter.filter = strdup(filter);
			if (tracepoint->internal_filter.filter == NULL) {
				ret_code = LTTNG_ERR_NOMEM;
				goto error;
			}
		} else {
			tracepoint->internal_filter.filter = NULL;
		}
		break;
	}
	}

	if (tracepoint->internal_filter.filter == NULL) {
		ret_code = LTTNG_OK;
		goto end;
	}

	ret = run_as_generate_filter_bytecode(tracepoint->internal_filter.filter, uid, gid, &bytecode);
	if (ret) {
		ret_code = LTTNG_ERR_FILTER_INVAL;
		goto end;
	}

	tracepoint->internal_filter.bytecode = bytecode;
	bytecode = NULL;
	ret_code = LTTNG_OK;

error:
end:
	free(bytecode);
	return ret_code;

}

static char *lttng_event_rule_tracepoint_get_internal_filter(struct lttng_event_rule *rule)
{
	struct lttng_event_rule_tracepoint *tracepoint;
	assert(rule);

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	return tracepoint->internal_filter.filter;
}

static struct lttng_filter_bytecode *lttng_event_rule_tracepoint_get_internal_filter_bytecode(struct lttng_event_rule *rule)
{
	struct lttng_event_rule_tracepoint *tracepoint;
	assert(rule);

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	return tracepoint->internal_filter.bytecode;
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
	rule->parent.populate = lttng_event_rule_tracepoint_populate;
	rule->parent.get_filter = lttng_event_rule_tracepoint_get_internal_filter;
	rule->parent.get_filter_bytecode = lttng_event_rule_tracepoint_get_internal_filter_bytecode;

	rule->domain = domain_type;
	rule->loglevel.type = LTTNG_EVENT_LOGLEVEL_ALL;

	return &rule->parent;
}

LTTNG_HIDDEN
ssize_t lttng_event_rule_tracepoint_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_event_rule **_event_rule)
{
	ssize_t ret, offset = 0;
	enum lttng_event_rule_status status;
	enum lttng_domain_type domain_type;
	enum lttng_loglevel_type loglevel_type;
	const struct lttng_event_rule_tracepoint_comm *tracepoint_comm;
	const char *pattern;
	const char *filter_expression = NULL;
	const char **exclusions =  NULL;
	const uint32_t *exclusion_len;
	const char *exclusion;
	struct lttng_buffer_view current_view;
	struct lttng_event_rule *rule = NULL;

	if (!_event_rule) {
		ret = -1;
		goto end;
	}

	if (view->size < sizeof(*tracepoint_comm)) {
		ERR("Failed to initialize from malformed event rule tracepoint: buffer too short to contain header");
		ret = -1;
		goto end;
	}

	current_view = lttng_buffer_view_from_view(view, offset, sizeof(*tracepoint_comm));
	tracepoint_comm = (typeof(tracepoint_comm)) current_view.data;

	if(!tracepoint_comm) {
		ret = -1;
		goto end;
	}

	if (tracepoint_comm->domain_type <= LTTNG_DOMAIN_NONE ||
			tracepoint_comm->domain_type > LTTNG_DOMAIN_PYTHON) {
		/* Invalid domain value. */
		ERR("Invalid domain type value (%i) found in tracepoint_comm buffer",
				(int) tracepoint_comm->domain_type);
		ret = -1;
		goto end;
	}

	domain_type = (enum lttng_domain_type) tracepoint_comm->domain_type;
	rule = lttng_event_rule_tracepoint_create(domain_type);
	if (!rule) {
		ERR("Failed to create event rule tracepoint");
		ret = -1;
		goto end;
	}

	loglevel_type = (enum lttng_loglevel_type)
					tracepoint_comm->loglevel_type;
	switch (loglevel_type) {
	case LTTNG_EVENT_LOGLEVEL_ALL:
		status = lttng_event_rule_tracepoint_set_loglevel_all(rule);
		break;
	case LTTNG_EVENT_LOGLEVEL_RANGE:
		status = lttng_event_rule_tracepoint_set_loglevel_range(
				rule, (enum lttng_loglevel_type) tracepoint_comm
						      ->loglevel_value);
		break;
	case LTTNG_EVENT_LOGLEVEL_SINGLE:
		status = lttng_event_rule_tracepoint_set_loglevel(
				rule, (enum lttng_loglevel_type) tracepoint_comm
						      ->loglevel_value);
		break;
	default:
		ERR("Failed to set event rule tracepoint loglevel: unknown loglevel type");
		ret = -1;
		goto end;
	}
	if (status != LTTNG_EVENT_RULE_STATUS_OK) {
		ERR("Failed to set event rule tracepoint loglevel");
	}

	/* Skip to payload */
	offset += current_view.size;

	/* Map the pattern */
	current_view = lttng_buffer_view_from_view(view, offset, tracepoint_comm->pattern_len);
	pattern = current_view.data;
	if (!pattern) {
		ret = -1;
		goto end;
	}

	if (tracepoint_comm->pattern_len == 1 ||
			pattern[tracepoint_comm->pattern_len - 1] != '\0' ||
			strlen(pattern) != tracepoint_comm->pattern_len - 1) {
		/*
		 * Check that the pattern is not NULL, is NULL-terminated, and
		 * does not contain a NULL before the last byte.
		 */
		ret = -1;
		goto end;
	}

	/* Skip after the pattern */
	offset += tracepoint_comm->pattern_len;

	if (!tracepoint_comm->filter_expression_len) {
		goto skip_filter_expression;
	}

	/* Map the filter_expression */
	current_view = lttng_buffer_view_from_view(view, offset, tracepoint_comm->filter_expression_len);
	filter_expression = current_view.data;
	if (!filter_expression) {
		ret = -1;
		goto end;
	}

	if (tracepoint_comm->filter_expression_len == 1 ||
			filter_expression[tracepoint_comm->filter_expression_len -
					  1] != '\0' ||
			strlen(filter_expression) !=
					tracepoint_comm->filter_expression_len -
							1) {
		/*
		 * Check that the filter expression is not NULL, is
		 * NULL-terminated, and does not contain a NULL before the last
		 * byte.
		 */
		ret = -1;
		goto end;
	}

	/* Skip after the pattern */
	offset += tracepoint_comm->filter_expression_len;

skip_filter_expression:

	if (!tracepoint_comm->exclusions_count) {
		goto skip_exclusions;
	}

	exclusions = zmalloc(sizeof(*exclusions) * tracepoint_comm->exclusions_count);
	if (!exclusions) {
		ret = -1;
		goto end;
	}

	for (int i = 0; i < tracepoint_comm->exclusions_count; i++) {
		current_view = lttng_buffer_view_from_view(view, offset, sizeof(*exclusion_len));
		exclusion_len = (typeof(exclusion_len)) current_view.data;
		if (!exclusion_len) {
			ret = -1;
			goto end;
		}

		offset += sizeof(*exclusion_len);
		current_view = lttng_buffer_view_from_view(view, offset, *exclusion_len);
		exclusion = current_view.data;
		if (*exclusion_len == 1 ||
				exclusion[*exclusion_len - 1] != '\0' ||
				strlen(exclusion) != *exclusion_len - 1) {
			/*
			 * Check that the exclusion is not NULL, is
			 * NULL-terminated, and does not contain a NULL before
			 * the last byte.
			 */
			ret = -1;
			goto end;
		}
		exclusions[i] = exclusion;
		/* Skip to next exclusion */
		offset += *exclusion_len;
	}

skip_exclusions:
	status = lttng_event_rule_tracepoint_set_pattern(rule, pattern);
	if (status != LTTNG_EVENT_RULE_STATUS_OK) {
		ERR("Failed to set event rule tracepoint pattern");
		ret = -1;
		goto end;
	}

	if (filter_expression) {
		status = lttng_event_rule_tracepoint_set_filter(
				rule, filter_expression);
		if (status != LTTNG_EVENT_RULE_STATUS_OK) {
			ERR("Failed to set event rule tracepoint pattern");
			ret = -1;
			goto end;
		}
	}

	if (exclusions) {
		status = lttng_event_rule_tracepoint_set_exclusions(rule,
				tracepoint_comm->exclusions_count, exclusions);
		if (status != LTTNG_EVENT_RULE_STATUS_OK) {
			ERR("Failed to set event rule tracepoint exclusions");
			ret = -1;
			goto end;
		}
	}

	*_event_rule = rule;
	rule = NULL;
	ret = offset;
end:
	free(exclusions);
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
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule) || !expression) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	if (!tracepoint->filter_expression) {
		status = LTTNG_EVENT_RULE_STATUS_UNSET;
		goto end;
	}

	*expression = tracepoint->filter_expression;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_set_loglevel(
		struct lttng_event_rule *rule, int level)
{	
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	/*
	 * TODO/QUESTION: do we validate the passed level based on the domain?
	 * What if no domain is set yet? Should we move the domain to the
	 * "create" api call to enforce the domain type?
	 */
	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule)) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	tracepoint->loglevel.value = level;
	tracepoint->loglevel.type = LTTNG_EVENT_LOGLEVEL_SINGLE;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_set_loglevel_range(
		struct lttng_event_rule *rule, int level)
{
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	/*
	 * TODO/QUESTION: do we validate the passed level based on the domain?
	 * What if no domain is set yet? Should we move the domain to the
	 * "create" api call to enforce the domain type?
	 */
	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule)) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	tracepoint->loglevel.value = level;
	tracepoint->loglevel.type = LTTNG_EVENT_LOGLEVEL_RANGE;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_set_loglevel_all(
		struct lttng_event_rule *rule)
{
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule)) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	tracepoint->loglevel.type = LTTNG_EVENT_LOGLEVEL_ALL;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_loglevel_type(
		const struct lttng_event_rule *rule, enum lttng_loglevel_type *type)
{
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule) || !type) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	*type = tracepoint->loglevel.type;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_loglevel(
		const struct lttng_event_rule *rule, int *level)
{
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule) || !level) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	if (tracepoint->loglevel.type == LTTNG_EVENT_LOGLEVEL_ALL) {
		status = LTTNG_EVENT_RULE_STATUS_UNSET;
		goto end;
	}
	*level = tracepoint->loglevel.value;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_set_exclusions(
		struct lttng_event_rule *rule,
		unsigned int count,
		const char **exclusions)
{
	char **exclusions_copy = NULL;
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;
	enum lttng_domain_type domain_type;

	/* TODO: validate that the passed exclusions are valid? */

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule) || count == 0 ||
			!exclusions) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(
			rule, struct lttng_event_rule_tracepoint, parent);

	(void) lttng_event_rule_tracepoint_get_domain_type(rule, &domain_type);

	switch (domain_type) {
	case LTTNG_DOMAIN_KERNEL:
	case LTTNG_DOMAIN_JUL:
	case LTTNG_DOMAIN_LOG4J:
	case LTTNG_DOMAIN_PYTHON:
		status = LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
		goto end;
	case LTTNG_DOMAIN_UST:
		/* Exclusions supported */
		break;
	default:
		assert(0);
	}

	exclusions_copy = zmalloc(sizeof(char *) * count);
	if (!exclusions_copy) {
		status = LTTNG_EVENT_RULE_STATUS_ERROR;
		goto end;
	}

	/* Perform the copy locally */
	for (int i = 0; i < count; i++) {
		exclusions_copy[i] = strdup(exclusions[i]);
		if (!exclusions_copy[i]) {
			status = LTTNG_EVENT_RULE_STATUS_ERROR;
			goto end;
		}
	}

	if (tracepoint->exclusions.count != 0) {
		for (int i = 0; i < tracepoint->exclusions.count; i++) {
			free(tracepoint->exclusions.values[i]);
		}
		free(tracepoint->exclusions.values);
	}

	tracepoint->exclusions.values = exclusions_copy;
	tracepoint->exclusions.count = count;
	exclusions_copy = NULL;
end:
	if (exclusions_copy) {
		for (int i = 0; i < count; i++) {
			free(exclusions_copy[i]);
		}
		free(exclusions_copy);
	}
	return status;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_exclusions_count(
		struct lttng_event_rule *rule, unsigned int *count)
{
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule) || !count) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	*count = tracepoint->exclusions.count;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_tracepoint_get_exclusion_at_index(
		struct lttng_event_rule *rule,
		unsigned int index,
		const char **exclusion)
{
	struct lttng_event_rule_tracepoint *tracepoint;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_TRACEPOINT_EVENT_RULE(rule) || !exclusion) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	tracepoint = container_of(rule, struct lttng_event_rule_tracepoint,
			parent);
	if (index >= tracepoint->exclusions.count) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}
	*exclusion = tracepoint->exclusions.values[index];
end:
	return status;
}
