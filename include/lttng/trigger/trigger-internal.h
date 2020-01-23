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

struct lttng_triggers {
	struct lttng_trigger **array;
	unsigned int count;
};

struct lttng_trigger_comm {
	/* length excludes its own length. */
	uint32_t name_length /* Includes '\0' */;
	uint32_t length;
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

/*
 * Allocate a new list of lttng_trigger.
 * The returned object must be freed via lttng_triggers_destroy.
 */
LTTNG_HIDDEN
struct lttng_triggers *lttng_triggers_create(unsigned int count);

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
 * TODO:
 */
LTTNG_HIDDEN
int lttng_triggers_set_pointer_of_index(
		const struct lttng_triggers *triggers, unsigned int index, struct lttng_trigger *trigger);

/*
 * Serialize a trigger collection to a lttng_dynamic_buffer.
 * Return LTTNG_OK on success, negative lttng error code on error.
 */
LTTNG_HIDDEN
int lttng_triggers_serialize(const struct lttng_triggers *triggers,
		struct lttng_dynamic_buffer *buffer);

/*
 * Free only the collection structure, not the triggers 
 * TODO: this is needed only on sessiond side, the use of refcount on the
 * trigger object would mostly resolve the need for this.
 */
LTTNG_HIDDEN
void lttng_triggers_destroy_array(struct lttng_triggers *triggers);

LTTNG_HIDDEN
ssize_t lttng_triggers_create_from_buffer(const struct lttng_buffer_view *view,
		struct lttng_triggers **triggers);


#endif /* LTTNG_TRIGGER_INTERNAL_H */
