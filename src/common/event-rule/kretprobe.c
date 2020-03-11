/*
 * Copyright (C) 2019 Jonathan Rajotte <jonathan.rajotte-julien@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#include <assert.h>
#include <common/error.h>
#include <common/macros.h>
#include <common/payload-view.h>
#include <common/runas.h>
#include <lttng/event-rule/event-rule-internal.h>
#include <lttng/event-rule/kretprobe-internal.h>

#define IS_KRETPROBE_EVENT_RULE(rule) \
	(lttng_event_rule_get_type(rule) == LTTNG_EVENT_RULE_TYPE_KRETPROBE)

static void lttng_event_rule_kretprobe_destroy(struct lttng_event_rule *rule)
{
	struct lttng_event_rule_kretprobe *kretprobe;

	kretprobe = container_of(
			rule, struct lttng_event_rule_kretprobe, parent);

	/*
	 * TODO
	 */
	free(kretprobe);
}

static bool lttng_event_rule_kretprobe_validate(
		const struct lttng_event_rule *rule)
{
	/* TODO */
	return false;
}

static int lttng_event_rule_kretprobe_serialize(
		const struct lttng_event_rule *rule,
		struct lttng_payload *payload)
{
	/* TODO */
	return -1;
}

static bool lttng_event_rule_kretprobe_is_equal(
		const struct lttng_event_rule *_a,
		const struct lttng_event_rule *_b)
{
	/* TODO */
	return false;
}

static enum lttng_error_code
lttng_event_rule_kretprobe_generate_filter_bytecode(
		struct lttng_event_rule *rule, uid_t uid, gid_t gid)
{
	/* Nothing to do */
	return LTTNG_OK;
}

static const char *lttng_event_rule_kretprobe_get_filter(
		const struct lttng_event_rule *rule)
{
	/* Not supported */
	return NULL;
}

static const struct lttng_filter_bytecode *
lttng_event_rule_kretprobe_get_filter_bytecode(
		const struct lttng_event_rule *rule)
{
	/* Not supported */
	return NULL;
}

static struct lttng_event_exclusion *
lttng_event_rule_kretprobe_generate_exclusions(
		const struct lttng_event_rule *rule)
{
	/* Not supported */
	return NULL;
}

struct lttng_event_rule *lttng_event_rule_kretprobe_create()
{
	struct lttng_event_rule_kretprobe *rule;

	rule = zmalloc(sizeof(struct lttng_event_rule_kretprobe));
	if (!rule) {
		return NULL;
	}

	lttng_event_rule_init(&rule->parent, LTTNG_EVENT_RULE_TYPE_KRETPROBE);
	rule->parent.validate = lttng_event_rule_kretprobe_validate;
	rule->parent.serialize = lttng_event_rule_kretprobe_serialize;
	rule->parent.equal = lttng_event_rule_kretprobe_is_equal;
	rule->parent.destroy = lttng_event_rule_kretprobe_destroy;
	rule->parent.generate_filter_bytecode =
			lttng_event_rule_kretprobe_generate_filter_bytecode;
	rule->parent.get_filter = lttng_event_rule_kretprobe_get_filter;
	rule->parent.get_filter_bytecode =
			lttng_event_rule_kretprobe_get_filter_bytecode;
	rule->parent.generate_exclusions =
			lttng_event_rule_kretprobe_generate_exclusions;
	return &rule->parent;
}

LTTNG_HIDDEN
ssize_t lttng_event_rule_kretprobe_create_from_payload(
		struct lttng_payload_view *view,
		struct lttng_event_rule **_event_rule)
{
	/* TODO */
	return -1;
}

enum lttng_event_rule_status lttng_event_rule_kretprobe_set_source(
		struct lttng_event_rule *rule, const char *source)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_kretprobe_set_name(
		struct lttng_event_rule *rule, const char *name)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

enum lttng_event_rule_status lttng_event_rule_kretprobe_get_name(
		const struct lttng_event_rule *rule, const char **name)
{
	return LTTNG_EVENT_RULE_STATUS_UNSUPPORTED;
}

LTTNG_HIDDEN
uint64_t lttng_event_rule_kretprobe_get_address(
		const struct lttng_event_rule *rule)
{
	assert("Not implemented" && 0);
}

LTTNG_HIDDEN
uint64_t lttng_event_rule_kretprobe_get_offset(
		const struct lttng_event_rule *rule)
{
	assert("Not implemented" && 0);
}

LTTNG_HIDDEN
const char *lttng_event_rule_kretprobe_get_symbol_name(
		const struct lttng_event_rule *rule)
{
	assert("Not implemented" && 0);
}
