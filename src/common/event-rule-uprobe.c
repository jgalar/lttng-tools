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
#include <lttng/event-rule/event-rule-uprobe-internal.h>
#include <common/macros.h>
#include <common/error.h>
#include <common/runas.h>
#include <assert.h>

#define IS_UPROBE_EVENT_RULE(rule) ( \
	lttng_event_rule_get_type(rule) == LTTNG_EVENT_RULE_TYPE_UPROBE \
	)

static
void lttng_event_rule_uprobe_destroy(struct lttng_event_rule *rule)
{
	struct lttng_event_rule_uprobe *uprobe;

	uprobe = container_of(rule, struct lttng_event_rule_uprobe,
			parent);

	/*
	 * TODO
	 */
	free(uprobe);
}

static
bool lttng_event_rule_uprobe_validate(
		const struct lttng_event_rule *rule)
{
	/* TODO */
	return false;
}

static
int lttng_event_rule_uprobe_serialize(
		const struct lttng_event_rule *rule,
		struct lttng_dynamic_buffer *buf)
{
	return -1;
}

static
bool lttng_event_rule_uprobe_is_equal(const struct lttng_event_rule *_a,
		const struct lttng_event_rule *_b)
{
	/* TODO */
	return false;
}

static
enum lttng_error_code lttng_event_rule_uprobe_populate(struct lttng_event_rule *rule, uid_t uid, gid_t gid)
{
	int ret;
	enum lttng_error_code ret_code;
	struct lttng_event_rule_uprobe *uprobe;
	enum lttng_event_rule_status status;
	const char *filter;
	struct lttng_filter_bytecode *bytecode = NULL;

	assert(rule);

	uprobe = container_of(rule, struct lttng_event_rule_uprobe,
			parent);

	/* Generate the filter bytecode */
	status = lttng_event_rule_uprobe_get_filter(rule, &filter);
	if (status == LTTNG_EVENT_RULE_STATUS_UNSET) {
		filter = NULL;
	} else if (status != LTTNG_EVENT_RULE_STATUS_OK) {
		ret = -1;
		goto end;
	}

	if (filter && filter[0] == '\0') {
		ret_code = LTTNG_ERR_FILTER_INVAL;
		goto error;
	}

	if (filter == NULL) {
		/* Nothing to do */
		ret = LTTNG_OK;
		goto end;
	}

	uprobe->internal_filter.filter = strdup(filter);
	if (uprobe->internal_filter.filter == NULL) {
		ret_code = LTTNG_ERR_NOMEM;
		goto end;
	}

	ret = run_as_generate_filter_bytecode(uprobe->internal_filter.filter, uid, gid, &bytecode);
	if (ret) {
		ret_code = LTTNG_ERR_FILTER_INVAL;
	}

	uprobe->internal_filter.bytecode = bytecode;
	bytecode = NULL;

error:
end:
	free(bytecode);
	return ret_code;
}

struct lttng_event_rule *lttng_event_rule_uprobe_create()
{
	struct lttng_event_rule_uprobe *rule;

	rule = zmalloc(sizeof(struct lttng_event_rule_uprobe));
	if (!rule) {
		return NULL;
	}

	lttng_event_rule_init(&rule->parent, LTTNG_EVENT_RULE_TYPE_UPROBE);
	rule->parent.validate = lttng_event_rule_uprobe_validate;
	rule->parent.serialize = lttng_event_rule_uprobe_serialize;
	rule->parent.equal = lttng_event_rule_uprobe_is_equal;
	rule->parent.destroy = lttng_event_rule_uprobe_destroy;
	rule->parent.populate = lttng_event_rule_uprobe_populate;
	return &rule->parent;
}

LTTNG_HIDDEN
ssize_t lttng_event_rule_uprobe_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_event_rule **_event_rule)
{
	/* TODO */
	return -1;
}

enum lttng_event_rule_status lttng_event_rule_uprobe_set_source(
		struct lttng_event_rule *rule, const char *source)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_uprobe_set_name(
		struct lttng_event_rule *rule, const char *name)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_uprobe_get_name(
		const struct lttng_event_rule *rule, const char *name)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_uprobe_set_filter(
		struct lttng_event_rule *rule, const char *expression)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_uprobe_get_filter(
		const struct lttng_event_rule *rule, const char **expression)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}
