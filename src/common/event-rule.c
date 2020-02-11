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
#include <lttng/event-rule/event-rule-kretprobe-internal.h>
#include <lttng/event-rule/event-rule-uprobe-internal.h>
#include <lttng/event-rule/event-rule-syscall-internal.h>
#include <lttng/event-rule/event-rule-tracepoint-internal.h>
#include <common/macros.h>
#include <common/error.h>
#include <common/dynamic-buffer.h>
#include <common/buffer-view.h>
#include <stdbool.h>
#include <assert.h>

enum lttng_event_rule_type lttng_event_rule_get_type(
		const struct lttng_event_rule *event_rule)
{
	return event_rule ? event_rule->type : LTTNG_EVENT_RULE_TYPE_UNKNOWN;
}

LTTNG_HIDDEN
enum lttng_domain_type lttng_event_rule_get_domain_type(
		const struct lttng_event_rule *event_rule)
{
	enum lttng_domain_type domain_type;

	switch (lttng_event_rule_get_type(event_rule)) {
	case LTTNG_EVENT_RULE_TYPE_TRACEPOINT:
		(void) lttng_event_rule_tracepoint_get_domain_type(event_rule, &domain_type);
		break;
	case LTTNG_EVENT_RULE_TYPE_SYSCALL:
	case LTTNG_EVENT_RULE_TYPE_KPROBE:
	case LTTNG_EVENT_RULE_TYPE_KRETPROBE:
	case LTTNG_EVENT_RULE_TYPE_UPROBE:
		domain_type = LTTNG_DOMAIN_KERNEL;
		break;
	case LTTNG_EVENT_RULE_TYPE_UNKNOWN:
		domain_type = LTTNG_DOMAIN_NONE;
		break;
	}

	return domain_type;
}

static
void lttng_event_rule_release(struct urcu_ref *ref)
{

	struct lttng_event_rule *event_rule = container_of(ref, typeof(*event_rule),
			ref);

	assert(event_rule->destroy);
	event_rule->destroy(event_rule);
}

void lttng_event_rule_destroy(struct lttng_event_rule *event_rule)
{
	lttng_event_rule_put(event_rule);
}

LTTNG_HIDDEN
bool lttng_event_rule_validate(const struct lttng_event_rule *event_rule)
{
	bool valid;

	if (!event_rule) {
		valid = false;
		goto end;
	}

	if (!event_rule->validate) {
		/* Sub-class guarantees that it can never be invalid. */
		valid = true;
		goto end;
	}

	valid = event_rule->validate(event_rule);
end:
	return valid;
}

LTTNG_HIDDEN
int lttng_event_rule_serialize(const struct lttng_event_rule *event_rule,
		struct lttng_dynamic_buffer *buf,
		int *fd_to_send)
{
	int ret;
	struct lttng_event_rule_comm event_rule_comm = { 0 };

	if (!event_rule) {
		ret = -1;
		goto end;
	}

	event_rule_comm.event_rule_type = (int8_t) event_rule->type;

	ret = lttng_dynamic_buffer_append(buf, &event_rule_comm,
			sizeof(event_rule_comm));
	if (ret) {
		goto end;
	}

	ret = event_rule->serialize(event_rule, buf, fd_to_send);
	if (ret) {
		goto end;
	}
end:
	return ret;
}

LTTNG_HIDDEN
bool lttng_event_rule_is_equal(const struct lttng_event_rule *a,
		const struct lttng_event_rule *b)
{
	bool is_equal = false;

	if (!a || !b) {
		goto end;
	}

	if (a->type != b->type) {
		goto end;
	}

	if (a == b) {
		is_equal = true;
		goto end;
	}

	is_equal = a->equal ? a->equal(a, b) : true;
end:
	return is_equal;
}

LTTNG_HIDDEN
ssize_t lttng_event_rule_create_from_buffer(
		const struct lttng_buffer_view *buffer,
		struct lttng_event_rule **event_rule)
{
	ssize_t ret, event_rule_size = 0;
	const struct lttng_event_rule_comm *event_rule_comm;
	event_rule_create_from_buffer_cb create_from_buffer = NULL;

	if (!buffer || !event_rule) {
		ret = -1;
		goto end;
	}

	DBG("Deserializing event_rule from buffer");
	event_rule_comm = (const struct lttng_event_rule_comm *) buffer->data;
	event_rule_size += sizeof(*event_rule_comm);

	switch ((enum lttng_event_rule_type) event_rule_comm->event_rule_type) {
	case LTTNG_EVENT_RULE_TYPE_TRACEPOINT:
		create_from_buffer = lttng_event_rule_tracepoint_create_from_buffer;
		break;
	case LTTNG_EVENT_RULE_TYPE_KPROBE:
		create_from_buffer = lttng_event_rule_kprobe_create_from_buffer;
		break;
	case LTTNG_EVENT_RULE_TYPE_KRETPROBE:
		create_from_buffer = lttng_event_rule_kretprobe_create_from_buffer;
		break;
	case LTTNG_EVENT_RULE_TYPE_UPROBE:
		create_from_buffer = lttng_event_rule_uprobe_create_from_buffer;
		break;
	case LTTNG_EVENT_RULE_TYPE_SYSCALL:
		create_from_buffer = lttng_event_rule_syscall_create_from_buffer;
		break;
	default:
		ERR("Attempted to create event rule of unknown type (%i)",
				(int) event_rule_comm->event_rule_type);
		ret = -1;
		goto end;
	}

	if (create_from_buffer) {
		const struct lttng_buffer_view view =
				lttng_buffer_view_from_view(buffer,
					sizeof(*event_rule_comm), -1);

		ret = create_from_buffer(&view, event_rule);
		if (ret < 0) {
			goto end;
		}
		event_rule_size += ret;

	} else {
		abort();
	}

	ret = event_rule_size;
end:
	return ret;
}

LTTNG_HIDDEN
void lttng_event_rule_init(struct lttng_event_rule *event_rule,
		enum lttng_event_rule_type type)
{
	urcu_ref_init(&event_rule->ref);
	event_rule->type = type;
}

LTTNG_HIDDEN
bool lttng_event_rule_get(struct lttng_event_rule *event_rule)
{
	return urcu_ref_get_unless_zero(&event_rule->ref);
}

LTTNG_HIDDEN
void lttng_event_rule_put(struct lttng_event_rule *event_rule)
{
	if (!event_rule) {
		return;
	}
	assert(event_rule->ref.refcount);
	urcu_ref_put(&event_rule->ref, lttng_event_rule_release);
}

LTTNG_HIDDEN
enum lttng_error_code lttng_event_rule_populate(struct lttng_event_rule *rule, uid_t uid, gid_t gid)
{
	assert(rule->populate);
	return rule->populate(rule, uid, gid);
}

/* If not present return NULL
 * Caller DO NOT own the returned object
 */
LTTNG_HIDDEN
const char *lttng_event_rule_get_filter(const struct lttng_event_rule *rule)
{
	assert(rule->get_filter);
	return rule->get_filter(rule);
}

/* If not present return NULL
 * Caller DO NOT own the returned object
 */
LTTNG_HIDDEN
const struct lttng_filter_bytecode *lttng_event_rule_get_filter_bytecode(
		const struct lttng_event_rule *rule)
{
	assert(rule->get_filter_bytecode);
	return rule->get_filter_bytecode(rule);
}

/*
 * If not present return NULL
 * Caller OWN the returned object
 * TODO: should this be done another way?
 */
LTTNG_HIDDEN
struct lttng_event_exclusion *lttng_event_rule_generate_exclusions(
		struct lttng_event_rule *rule)
{
	assert(rule->generate_exclusions);
	return rule->generate_exclusions(rule);
}

LTTNG_HIDDEN
struct lttng_event *lttng_event_rule_generate_lttng_event(
		const struct lttng_event_rule *rule)
{
	assert(rule->generate_lttng_event);
	return rule->generate_lttng_event(rule);
}

LTTNG_HIDDEN
bool lttng_event_rule_is_agent(const struct lttng_event_rule *rule)
{
	bool ret = false;
	enum lttng_domain_type type = lttng_event_rule_get_domain_type(rule);

	switch (type) {
	case LTTNG_DOMAIN_JUL:
	case LTTNG_DOMAIN_LOG4J:
	case LTTNG_DOMAIN_PYTHON:
		ret = true;
		break;
	case LTTNG_DOMAIN_UST:
	case LTTNG_DOMAIN_KERNEL:
		ret = false;
		break;
	default:
		assert(0);
	};

	return ret;
}
