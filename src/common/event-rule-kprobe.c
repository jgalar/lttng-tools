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
#include <lttng/event-rule/event-rule-kprobe-internal.h>
#include <common/macros.h>
#include <common/error.h>
#include <common/runas.h>
#include <assert.h>

#define IS_KPROBE_EVENT_RULE(rule) ( \
	lttng_event_rule_get_type(rule) == LTTNG_EVENT_RULE_TYPE_KPROBE \
	)

static
void lttng_event_rule_kprobe_destroy(struct lttng_event_rule *rule)
{
	struct lttng_event_rule_kprobe *kprobe;

	kprobe = container_of(rule, struct lttng_event_rule_kprobe,
			parent);

	/*
	 * TODO
	 */
	free(kprobe);
}

static
bool lttng_event_rule_kprobe_validate(
		const struct lttng_event_rule *rule)
{
	/* TODO */
	return false;
}

static
int lttng_event_rule_kprobe_serialize(
		const struct lttng_event_rule *rule,
		struct lttng_dynamic_buffer *buf)
{
	/* TODO */
	return -1;
}

static
bool lttng_event_rule_kprobe_is_equal(const struct lttng_event_rule *_a,
		const struct lttng_event_rule *_b)
{
	/* Return false */
	return false;
}

static
enum lttng_error_code lttng_event_rule_kprobe_populate(struct lttng_event_rule *rule, uid_t uid, gid_t gid)
{
	int ret;
	enum lttng_error_code ret_code;
	struct lttng_event_rule_kprobe *kprobe;
	enum lttng_event_rule_status status;
	const char *filter;
	struct lttng_filter_bytecode *bytecode = NULL;

	assert(rule);

	kprobe = container_of(rule, struct lttng_event_rule_kprobe,
			parent);

	/* Generate the filter bytecode */
	status = lttng_event_rule_kprobe_get_filter(rule, &filter);
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

	kprobe->internal_filter.filter = strdup(filter);
	if (kprobe->internal_filter.filter == NULL) {
		ret_code = LTTNG_ERR_NOMEM;
		goto end;
	}

	ret = run_as_generate_filter_bytecode(kprobe->internal_filter.filter, uid, gid, &bytecode);
	if (ret) {
		ret_code = LTTNG_ERR_FILTER_INVAL;
	}

	kprobe->internal_filter.bytecode = bytecode;
	bytecode = NULL;

error:
end:
	free(bytecode);
	return ret_code;
}

struct lttng_event_rule *lttng_event_rule_kprobe_create()
{
	struct lttng_event_rule_kprobe *rule;

	rule = zmalloc(sizeof(struct lttng_event_rule_kprobe));
	if (!rule) {
		return NULL;
	}

	lttng_event_rule_init(&rule->parent, LTTNG_EVENT_RULE_TYPE_KPROBE);
	rule->parent.validate = lttng_event_rule_kprobe_validate;
	rule->parent.serialize = lttng_event_rule_kprobe_serialize;
	rule->parent.equal = lttng_event_rule_kprobe_is_equal;
	rule->parent.destroy = lttng_event_rule_kprobe_destroy;
	rule->parent.populate = lttng_event_rule_kprobe_populate;
	return &rule->parent;
}

LTTNG_HIDDEN
ssize_t lttng_event_rule_kprobe_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_event_rule **_event_rule)
{
	/* TODO */
	return -1;
}

enum lttng_event_rule_status lttng_event_rule_kprobe_set_source(
		struct lttng_event_rule *rule, const char *source)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_kprobe_set_name(
		struct lttng_event_rule *rule, const char *name)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_kprobe_get_name(
		const struct lttng_event_rule *rule, const char **name)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_kprobe_set_filter(
		struct lttng_event_rule *rule, const char *expression)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_kprobe_get_filter(
		const struct lttng_event_rule *rule, const char **expression)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

LTTNG_HIDDEN
uint64_t lttng_event_rule_kprobe_get_address(
		const struct lttng_event_rule *rule)
{
	assert("Not implemented" && 0);
}

LTTNG_HIDDEN
uint64_t lttng_event_rule_kprobe_get_offset(
		const struct lttng_event_rule *rule)
{
	assert("Not implemented" && 0);
}

LTTNG_HIDDEN
const char *lttng_event_rule_kprobe_get_symbol_name(
		const struct lttng_event_rule *rule)
{
	assert("Not implemented" && 0);
}
