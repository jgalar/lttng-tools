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
	/* Nothing to do */
	return LTTNG_OK;
}

static
char *lttng_event_rule_uprobe_get_filter(struct lttng_event_rule *rule)
{
	/* Unsupported */
	return NULL;
}

static
struct lttng_filter_bytecode *lttng_event_rule_uprobe_get_filter_bytecode(struct lttng_event_rule *rule)
{
	/* Unsupported */
	return NULL;
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
	rule->parent.get_filter = lttng_event_rule_uprobe_get_filter;
	rule->parent.get_filter_bytecode = lttng_event_rule_uprobe_get_filter_bytecode;
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
