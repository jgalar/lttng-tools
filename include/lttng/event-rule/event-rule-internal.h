/*
 * Copyright (C) 2019 - Jonathan Rajotte-Julien
 * <jonathan.rajotte-julien@efficios.com>
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

#ifndef LTTNG_EVENT_RULE_INTERNAL_H
#define LTTNG_EVENT_RULE_INTERNAL_H

#include <common/buffer-view.h>
#include <common/dynamic-buffer.h>
#include <common/macros.h>
#include <lttng/event.h>
#include <lttng/event-rule/event-rule.h>
#include <lttng/lttng-error.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <urcu/ref.h>

typedef void (*event_rule_destroy_cb)(struct lttng_event_rule *event_rule);
typedef bool (*event_rule_validate_cb)(
		const struct lttng_event_rule *event_rule);
typedef int (*event_rule_serialize_cb)(
		const struct lttng_event_rule *event_rule,
		struct lttng_dynamic_buffer *buf,
		int *fd_to_send);
typedef bool (*event_rule_equal_cb)(const struct lttng_event_rule *a,
		const struct lttng_event_rule *b);
typedef ssize_t (*event_rule_create_from_buffer_cb)(
		const struct lttng_buffer_view *view,
		struct lttng_event_rule **event_rule);
typedef enum lttng_error_code (*event_rule_populate_cb)(
		struct lttng_event_rule *event_rule, uid_t uid, gid_t gid);
typedef const char *(*event_rule_get_filter_cb)(
		const struct lttng_event_rule *event_rule);
typedef const struct lttng_filter_bytecode *(
		*event_rule_get_filter_bytecode_cb)(
		const struct lttng_event_rule *event_rule);
typedef struct lttng_event_exclusion *(*event_rule_generate_exclusions_cb)(
		struct lttng_event_rule *event_rule);
typedef struct lttng_event *(*event_rule_generate_lttng_event_cb)(
		const struct lttng_event_rule *event_rule);

struct lttng_event_rule {
	struct urcu_ref ref;
	enum lttng_event_rule_type type;
	event_rule_validate_cb validate;
	event_rule_serialize_cb serialize;
	event_rule_equal_cb equal;
	event_rule_destroy_cb destroy;
	event_rule_populate_cb populate;
	/* Optional */
	event_rule_get_filter_cb get_filter;
	event_rule_get_filter_bytecode_cb get_filter_bytecode;
	event_rule_generate_exclusions_cb generate_exclusions;
	event_rule_generate_lttng_event_cb generate_lttng_event;
};

struct lttng_event_rule_comm {
	/* enum lttng_event_rule_type */
	int8_t event_rule_type;
	char payload[];
};

LTTNG_HIDDEN
void lttng_event_rule_init(struct lttng_event_rule *event_rule,
		enum lttng_event_rule_type type);

LTTNG_HIDDEN
bool lttng_event_rule_validate(const struct lttng_event_rule *event_rule);

LTTNG_HIDDEN
ssize_t lttng_event_rule_create_from_buffer(
		const struct lttng_buffer_view *buffer,
		struct lttng_event_rule **event_rule);

LTTNG_HIDDEN
int lttng_event_rule_serialize(const struct lttng_event_rule *event_rule,
		struct lttng_dynamic_buffer *buf,
		int *fd_to_send);

LTTNG_HIDDEN
bool lttng_event_rule_is_equal(const struct lttng_event_rule *a,
		const struct lttng_event_rule *b);

LTTNG_HIDDEN
bool lttng_event_rule_get(struct lttng_event_rule *rule);

LTTNG_HIDDEN
void lttng_event_rule_put(struct lttng_event_rule *rule);

LTTNG_HIDDEN
enum lttng_domain_type lttng_event_rule_get_domain_type(
		const struct lttng_event_rule *rule);

LTTNG_HIDDEN
enum lttng_error_code lttng_event_rule_populate(
		struct lttng_event_rule *rule, uid_t uid, gid_t gid);

/* If not present return NULL
 * Caller DO NOT own the returned object
 */
LTTNG_HIDDEN
const char *lttng_event_rule_get_filter(const struct lttng_event_rule *rule);

/* If not present return NULL
 * Caller DO NOT own the returned object
 */
LTTNG_HIDDEN
const struct lttng_filter_bytecode *lttng_event_rule_get_filter_bytecode(
		const struct lttng_event_rule *rule);

/*
 * If not present return NULL
 * Caller OWN the returned object
 * TODO: should this be done another way?
 */
LTTNG_HIDDEN
struct lttng_event_exclusion *lttng_event_rule_generate_exclusions(
		struct lttng_event_rule *rule);

/*
 * This is compatibility helper, allowing us to generate a sessiond side (not
 * communication) struct lttng_event object. This facilitate integration with
 * current code.
 * Caller OWN the returned object
 */
LTTNG_HIDDEN
struct lttng_event *lttng_event_rule_generate_lttng_event(
		const struct lttng_event_rule *rule);

/* Quick helper to test if the event rule apply to an agent domain */
LTTNG_HIDDEN
bool lttng_event_rule_is_agent(const struct lttng_event_rule *rule);

#endif /* LTTNG_EVENT_RULE_INTERNAL_H */
