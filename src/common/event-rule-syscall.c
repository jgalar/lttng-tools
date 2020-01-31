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
#include <lttng/event-rule/event-rule-syscall-internal.h>
#include <common/macros.h>
#include <common/error.h>
#include <common/runas.h>
#include <assert.h>

#define IS_SYSCALL_EVENT_RULE(rule) ( \
	lttng_event_rule_get_type(rule) == LTTNG_EVENT_RULE_TYPE_SYSCALL \
	)

static
void lttng_event_rule_syscall_destroy(struct lttng_event_rule *rule)
{
	struct lttng_event_rule_syscall *syscall;

	if (rule == NULL) {
		return;
	}

	syscall = container_of(rule, struct lttng_event_rule_syscall,
			parent);

	free(syscall->pattern);
	free(syscall->filter_expression);
	free(syscall->internal_filter.filter);
	free(syscall->internal_filter.bytecode);
	free(syscall);
}

static
bool lttng_event_rule_syscall_validate(
		const struct lttng_event_rule *rule)
{
	bool valid = false;
	struct lttng_event_rule_syscall *syscall;

	if (!rule) {
		goto end;
	}

	syscall = container_of(rule, struct lttng_event_rule_syscall, parent);

	/* Required field */
	if (!syscall->pattern) {
		ERR("Invalid syscall event rule: a pattern must be set.");
		goto end;
	}

	valid = true;
end:
	return valid;
}

static
int lttng_event_rule_syscall_serialize(
		const struct lttng_event_rule *rule,
		struct lttng_dynamic_buffer *buf,
		int *fd_to_send)
{
	int ret;
	size_t pattern_len, filter_expression_len;
	struct lttng_event_rule_syscall *syscall;
	struct lttng_event_rule_syscall_comm syscall_comm;

	if (!rule || !IS_SYSCALL_EVENT_RULE(rule)) {
		ret = -1;
		goto end;
	}

	DBG("Serializing syscall event rule");
	syscall = container_of(rule, struct lttng_event_rule_syscall, parent);

	pattern_len = strlen(syscall->pattern) + 1;

	if (syscall->filter_expression != NULL) {
		filter_expression_len = strlen(syscall->filter_expression) + 1;
	} else {
		filter_expression_len = 0;
	}

	syscall_comm.pattern_len = pattern_len;
	syscall_comm.filter_expression_len = filter_expression_len;

	ret = lttng_dynamic_buffer_append(
			buf, &syscall_comm, sizeof(syscall_comm));
	if (ret) {
		goto end;
	}
	ret = lttng_dynamic_buffer_append(buf, syscall->pattern, pattern_len);
	if (ret) {
		goto end;
	}
	ret = lttng_dynamic_buffer_append(
			buf, syscall->filter_expression, filter_expression_len);
	if (ret) {
		goto end;
	}

	if (fd_to_send) {
		/* Nothing to send */
		*fd_to_send = -1;
	}
end:
	return ret;
}

static
bool lttng_event_rule_syscall_is_equal(const struct lttng_event_rule *_a,
		const struct lttng_event_rule *_b)
{
	bool is_equal = false;
	struct lttng_event_rule_syscall *a, *b;

	a = container_of(_a, struct lttng_event_rule_syscall, parent);
	b = container_of(_b, struct lttng_event_rule_syscall, parent);

	if (!!a->filter_expression != !!b->filter_expression) {
		goto end;
	}

	/* Long check */
	/* syscall is invalid if this is not true */
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

	is_equal = true;
end:
	return is_equal;
}

static
enum lttng_error_code lttng_event_rule_syscall_populate(struct lttng_event_rule *rule, uid_t uid, gid_t gid)
{
	int ret;
	enum lttng_error_code ret_code = LTTNG_OK;
	struct lttng_event_rule_syscall *syscall;
	enum lttng_event_rule_status status;
	const char *filter;
	struct lttng_filter_bytecode *bytecode = NULL;

	assert(rule);

	syscall = container_of(rule, struct lttng_event_rule_syscall,
			parent);

	/* Generate the filter bytecode */
	status = lttng_event_rule_syscall_get_filter(rule, &filter);
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

	if (filter == NULL) {
		/* Nothing to do */
		ret_code = LTTNG_OK;
		goto end;
	}

	syscall->internal_filter.filter = strdup(filter);
	if (syscall->internal_filter.filter == NULL) {
		ret_code = LTTNG_ERR_NOMEM;
		goto end;
	}

	ret = run_as_generate_filter_bytecode(syscall->internal_filter.filter, uid, gid, &bytecode);
	if (ret) {
		ret_code = LTTNG_ERR_FILTER_INVAL;
	}

	syscall->internal_filter.bytecode = bytecode;
	bytecode = NULL;

error:
end:
	free(bytecode);
	return ret_code;
}

static char *lttng_event_rule_syscall_get_internal_filter(
		struct lttng_event_rule *rule)
{
	struct lttng_event_rule_syscall *syscall;
	assert(rule);

	syscall = container_of(rule, struct lttng_event_rule_syscall, parent);
	return syscall->internal_filter.filter;
}

static struct lttng_filter_bytecode *
lttng_event_rule_syscall_get_internal_filter_bytecode(
		struct lttng_event_rule *rule)
{
	struct lttng_event_rule_syscall *syscall;
	assert(rule);

	syscall = container_of(rule, struct lttng_event_rule_syscall, parent);
	return syscall->internal_filter.bytecode;
}

struct lttng_event_rule *lttng_event_rule_syscall_create()
{
	struct lttng_event_rule_syscall *rule;

	rule = zmalloc(sizeof(struct lttng_event_rule_syscall));
	if (!rule) {
		return NULL;
	}

	lttng_event_rule_init(&rule->parent, LTTNG_EVENT_RULE_TYPE_SYSCALL);
	rule->parent.validate = lttng_event_rule_syscall_validate;
	rule->parent.serialize = lttng_event_rule_syscall_serialize;
	rule->parent.equal = lttng_event_rule_syscall_is_equal;
	rule->parent.destroy = lttng_event_rule_syscall_destroy;
	rule->parent.populate = lttng_event_rule_syscall_populate;
	rule->parent.get_filter = lttng_event_rule_syscall_get_internal_filter;
	rule->parent.get_filter_bytecode =
			lttng_event_rule_syscall_get_internal_filter_bytecode;
	return &rule->parent;
}

LTTNG_HIDDEN
ssize_t lttng_event_rule_syscall_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_event_rule **_event_rule)
{
	ssize_t ret, offset = 0;
	enum lttng_event_rule_status status;
	const struct lttng_event_rule_syscall_comm *syscall_comm;
	const char *pattern;
	const char *filter_expression = NULL;
	struct lttng_buffer_view current_view;
	struct lttng_event_rule *rule = NULL;

	if (!_event_rule) {
		ret = -1;
		goto end;
	}

	if (view->size < sizeof(*syscall_comm)) {
		ERR("Failed to initialize from malformed event rule syscall: buffer too short to contain header");
		ret = -1;
		goto end;
	}

	current_view = lttng_buffer_view_from_view(
			view, offset, sizeof(*syscall_comm));
	syscall_comm = (typeof(syscall_comm)) current_view.data;

	if (!syscall_comm) {
		ret = -1;
		goto end;
	}

	rule = lttng_event_rule_syscall_create();
	if (!rule) {
		ERR("Failed to create event rule syscall");
		ret = -1;
		goto end;
	}

	/* Skip to payload */
	offset += current_view.size;

	/* Map the pattern */
	current_view = lttng_buffer_view_from_view(
			view, offset, syscall_comm->pattern_len);
	pattern = current_view.data;
	if (!pattern) {
		ret = -1;
		goto end;
	}

	if (syscall_comm->pattern_len == 1 ||
			pattern[syscall_comm->pattern_len - 1] != '\0' ||
			strlen(pattern) != syscall_comm->pattern_len - 1) {
		/*
		 * Check that the pattern is not NULL, is NULL-terminated, and
		 * does not contain a NULL before the last byte.
		 */
		ret = -1;
		goto end;
	}

	/* Skip after the pattern */
	offset += syscall_comm->pattern_len;

	if (!syscall_comm->filter_expression_len) {
		goto skip_filter_expression;
	}

	/* Map the filter_expression */
	current_view = lttng_buffer_view_from_view(
			view, offset, syscall_comm->filter_expression_len);
	filter_expression = current_view.data;
	if (!filter_expression) {
		ret = -1;
		goto end;
	}

	if (syscall_comm->filter_expression_len == 1 ||
			filter_expression[syscall_comm->filter_expression_len -
					  1] != '\0' ||
			strlen(filter_expression) !=
					syscall_comm->filter_expression_len -
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
	offset += syscall_comm->filter_expression_len;

skip_filter_expression:

	status = lttng_event_rule_syscall_set_pattern(rule, pattern);
	if (status != LTTNG_EVENT_RULE_STATUS_OK) {
		ERR("Failed to set event rule syscall pattern");
		ret = -1;
		goto end;
	}

	if (filter_expression) {
		status = lttng_event_rule_syscall_set_filter(
				rule, filter_expression);
		if (status != LTTNG_EVENT_RULE_STATUS_OK) {
			ERR("Failed to set event rule syscall pattern");
			ret = -1;
			goto end;
		}
	}

	*_event_rule = rule;
	rule = NULL;
	ret = offset;
end:
	lttng_event_rule_destroy(rule);
	return ret;
}

enum lttng_event_rule_status lttng_event_rule_syscall_set_pattern(
		struct lttng_event_rule *rule, const char *pattern)
{
	char *pattern_copy = NULL;
	struct lttng_event_rule_syscall *syscall;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_SYSCALL_EVENT_RULE(rule) || !pattern ||
			strlen(pattern) == 0) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	syscall = container_of(rule, struct lttng_event_rule_syscall, parent);
	pattern_copy = strdup(pattern);
	if (!pattern_copy) {
		status = LTTNG_EVENT_RULE_STATUS_ERROR;
		goto end;
	}

	if (syscall->pattern) {
		free(syscall->pattern);
	}

	syscall->pattern = pattern_copy;
	pattern_copy = NULL;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_syscall_get_pattern(
		const struct lttng_event_rule *rule, const char **pattern)
{
	struct lttng_event_rule_syscall *syscall;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_SYSCALL_EVENT_RULE(rule) || !pattern) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	syscall = container_of(rule, struct lttng_event_rule_syscall, parent);
	if (!syscall->pattern) {
		status = LTTNG_EVENT_RULE_STATUS_UNSET;
		goto end;
	}

	*pattern = syscall->pattern;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_syscall_set_filter(
		struct lttng_event_rule *rule, const char *expression)
{
	char *expression_copy = NULL;
	struct lttng_event_rule_syscall *syscall;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	/* TODO: validate that the passed expression is valid */

	if (!rule || !IS_SYSCALL_EVENT_RULE(rule) || !expression ||
			strlen(expression) == 0) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	syscall = container_of(rule, struct lttng_event_rule_syscall, parent);
	expression_copy = strdup(expression);
	if (!expression_copy) {
		status = LTTNG_EVENT_RULE_STATUS_ERROR;
		goto end;
	}

	if (syscall->filter_expression) {
		free(syscall->filter_expression);
	}

	syscall->filter_expression = expression_copy;
	expression_copy = NULL;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_syscall_get_filter(
		const struct lttng_event_rule *rule, const char **expression)
{
	struct lttng_event_rule_syscall *syscall;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_SYSCALL_EVENT_RULE(rule) || !expression) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	syscall = container_of(rule, struct lttng_event_rule_syscall, parent);
	if (!syscall->filter_expression) {
		status = LTTNG_EVENT_RULE_STATUS_UNSET;
		goto end;
	}

	*expression = syscall->filter_expression;
end:
	return status;
}
