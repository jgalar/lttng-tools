/*
 * Copyright (C) 2017 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#ifndef LTTNG_TRIGGER_INTERNAL_H
#define LTTNG_TRIGGER_INTERNAL_H

#include <lttng/trigger/trigger.h>
#include <common/credentials.h>
#include <common/dynamic-array.h>
#include <common/macros.h>
#include <common/optional.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <urcu/ref.h>

struct lttng_payload;
struct lttng_payload_view;

struct lttng_trigger {
	/* Reference counting is only exposed to internal users. */
	struct urcu_ref ref;

	struct lttng_condition *condition;
	struct lttng_action *action;
	char *name;
	LTTNG_OPTIONAL(struct lttng_credentials) creds;
	struct {
		enum lttng_trigger_firing_policy_type type;
		uint64_t threshold;
		uint64_t current_count;
	} firing_policy;
	/*
	 * Internal use only
	 * The unique token passed to the tracer to identify a event-rule
	 * notification
	 */
	LTTNG_OPTIONAL(uint64_t) tracer_token;

};

struct lttng_triggers {
	struct lttng_dynamic_pointer_array array;
};

struct lttng_trigger_comm {
	/* length excludes its own length. */
	uint32_t name_length /* Includes '\0' */;
	uint32_t length;
	uint8_t policy_type;
	uint64_t policy_threshold;
	/* A name, condition and action object follow. */
	char payload[];
} LTTNG_PACKED;

struct lttng_triggers_comm {
	uint32_t count;
	uint32_t length;
	/* Count * lttng_trigger_comm structure */
	char payload[];
};

LTTNG_HIDDEN
ssize_t lttng_trigger_create_from_payload(struct lttng_payload_view *view,
		struct lttng_trigger **trigger);

LTTNG_HIDDEN
int lttng_trigger_serialize(const struct lttng_trigger *trigger,
		struct lttng_payload *payload);

LTTNG_HIDDEN
bool lttng_trigger_validate(const struct lttng_trigger *trigger);

LTTNG_HIDDEN
int lttng_trigger_assign_name(struct lttng_trigger *dst,
		const struct lttng_trigger *src);

LTTNG_HIDDEN
void lttng_trigger_set_tracer_token(
		struct lttng_trigger *trigger, uint64_t token);

LTTNG_HIDDEN
uint64_t lttng_trigger_get_tracer_token(const struct lttng_trigger *trigger);

LTTNG_HIDDEN
int lttng_trigger_generate_name(struct lttng_trigger *trigger, uint64_t offset);

LTTNG_HIDDEN
bool lttng_trigger_is_equal(const struct lttng_trigger *a,
		const struct lttng_trigger *b);

LTTNG_HIDDEN
void lttng_trigger_get(struct lttng_trigger *trigger);

LTTNG_HIDDEN
void lttng_trigger_put(struct lttng_trigger *trigger);

/*
 * Allocate a new list of lttng_trigger.
 * The returned object must be freed via lttng_triggers_destroy.
 */
LTTNG_HIDDEN
struct lttng_triggers *lttng_triggers_create(void);

/*
 * Return the non-const pointer of an element at index "index" of a
 * lttng_triggers.
 *
 * The ownership of the lttng_triggers element is NOT transfered.
 * The returned object can NOT be freed via lttng_trigger_destroy.
 */
LTTNG_HIDDEN
struct lttng_trigger *lttng_triggers_get_pointer_of_index(
		const struct lttng_triggers *triggers, unsigned int index);

/*
 * Add a trigger to the triggers object.
 */
LTTNG_HIDDEN
int lttng_triggers_add(
		struct lttng_triggers *triggers, struct lttng_trigger *trigger);

/*
 * Serialize a trigger collection to a lttng_payload object.
 * Return LTTNG_OK on success, negative lttng error code on error.
 */
LTTNG_HIDDEN
int lttng_triggers_serialize(const struct lttng_triggers *triggers,
		struct lttng_payload *payload);

LTTNG_HIDDEN
ssize_t lttng_triggers_create_from_payload(struct lttng_payload_view *view,
		struct lttng_triggers **triggers);

LTTNG_HIDDEN
const struct lttng_credentials *lttng_trigger_get_credentials(
		const struct lttng_trigger *trigger);

LTTNG_HIDDEN
void lttng_trigger_set_credentials(
		struct lttng_trigger *trigger,
		const struct lttng_credentials *creds);

LTTNG_HIDDEN
bool lttng_trigger_is_ready_to_fire(
		struct lttng_trigger *trigger);

/*
 * Return the type of any uderlying domain requirement. If no particular
 * requirement is needed return LTTNG_DOMAIN_NONE.
 */
LTTNG_HIDDEN
enum lttng_domain_type lttng_trigger_get_underlying_domain_type_restriction(
		const struct lttng_trigger *trigger);

#endif /* LTTNG_TRIGGER_INTERNAL_H */
