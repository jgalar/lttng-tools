/*
 * Copyright (C) 2017 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#ifndef LTTNG_TRIGGER_INTERNAL_H
#define LTTNG_TRIGGER_INTERNAL_H

#include <lttng/trigger/trigger.h>
#include <common/macros.h>
#include <common/buffer-view.h>
#include <common/dynamic-buffer.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

struct lttng_trigger {
	struct lttng_condition *condition;
	struct lttng_action *action;
	char *name;
	struct { /* Internal use only */
		bool set;
		uint64_t value;
	} key;
};

struct lttng_trigger_comm {
	/* length excludes its own length. */
	uint32_t name_length /* Includes '\0' */;
	uint32_t length;
	/* A name, condition and action object follow. */
	char payload[];
} LTTNG_PACKED;

LTTNG_HIDDEN
ssize_t lttng_trigger_create_from_buffer(const struct lttng_buffer_view *view,
		struct lttng_trigger **trigger);

LTTNG_HIDDEN
int lttng_trigger_serialize(struct lttng_trigger *trigger,
		struct lttng_dynamic_buffer *buf);

LTTNG_HIDDEN
const struct lttng_condition *lttng_trigger_get_const_condition(
		const struct lttng_trigger *trigger);

LTTNG_HIDDEN
const struct lttng_action *lttng_trigger_get_const_action(
		const struct lttng_trigger *trigger);

LTTNG_HIDDEN
bool lttng_trigger_validate(struct lttng_trigger *trigger);

LTTNG_HIDDEN
int lttng_trigger_assign(struct lttng_trigger *dst,
		const struct lttng_trigger *src);

LTTNG_HIDDEN
bool lttng_trigger_is_equal(const struct lttng_trigger *a,
		const struct lttng_trigger *b);

LTTNG_HIDDEN
void lttng_trigger_set_key(struct lttng_trigger *trigger, uint64_t key);

LTTNG_HIDDEN
int lttng_trigger_generate_name(struct lttng_trigger *trigger, uint64_t offset);

#endif /* LTTNG_TRIGGER_INTERNAL_H */
