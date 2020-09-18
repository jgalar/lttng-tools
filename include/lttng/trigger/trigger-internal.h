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
	/*
	 * Internal use only
	 * The unique token passed to the tracer to identify a event-rule
	 * notification
	 */
	LTTNG_OPTIONAL(uint64_t) tracer_token;
};

struct lttng_trigger_comm {
	/* length excludes its own length. */
	uint32_t name_length /* Includes '\0' */;
	uint32_t length;
	/* A name, condition and action object follow. */
	char payload[];
} LTTNG_PACKED;

LTTNG_HIDDEN
ssize_t lttng_trigger_create_from_payload(struct lttng_payload_view *view,
		struct lttng_trigger **trigger);

LTTNG_HIDDEN
int lttng_trigger_serialize(struct lttng_trigger *trigger,
		struct lttng_payload *payload);

LTTNG_HIDDEN
const struct lttng_condition *lttng_trigger_get_const_condition(
		const struct lttng_trigger *trigger);

LTTNG_HIDDEN
const struct lttng_action *lttng_trigger_get_const_action(
		const struct lttng_trigger *trigger);

LTTNG_HIDDEN
bool lttng_trigger_validate(struct lttng_trigger *trigger);

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

LTTNG_HIDDEN
const struct lttng_credentials *lttng_trigger_get_credentials(
		const struct lttng_trigger *trigger);

LTTNG_HIDDEN
void lttng_trigger_set_credentials(
		struct lttng_trigger *trigger,
		const struct lttng_credentials *creds);

#endif /* LTTNG_TRIGGER_INTERNAL_H */
