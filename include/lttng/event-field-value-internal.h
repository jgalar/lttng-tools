/*
 * Copyright (C) 2020 Philippe Proulx <pproulx@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#ifndef LTTNG_EVENT_FIELD_VALUE_INTERNAL_H
#define LTTNG_EVENT_FIELD_VALUE_INTERNAL_H

#include <assert.h>
#include <stdint.h>
#include <lttng/event-field-value.h>
#include <common/dynamic-array.h>

struct lttng_event_field_value {
	enum lttng_event_field_value_type type;
};

/*
 * `LTTNG_EVENT_FIELD_VALUE_TYPE_UNSIGNED_INT`.
 */
struct lttng_event_field_value_uint {
	struct lttng_event_field_value parent;
	uint64_t val;
};

/*
 * `LTTNG_EVENT_FIELD_VALUE_TYPE_SIGNED_INT`.
 */
struct lttng_event_field_value_int {
	struct lttng_event_field_value parent;
	int64_t val;
};

/*
 * `LTTNG_EVENT_FIELD_VALUE_TYPE_UNSIGNED_ENUM` and
 * `LTTNG_EVENT_FIELD_VALUE_TYPE_SIGNED_ENUM` (base).
 */
struct lttng_event_field_value_enum {
	struct lttng_event_field_value parent;

	/*
	 * Array of `char *` (owned by this).
	 */
	struct lttng_dynamic_pointer_array labels;
};

/*
 * `LTTNG_EVENT_FIELD_VALUE_TYPE_UNSIGNED_ENUM`.
 */
struct lttng_event_field_value_enum_uint {
	struct lttng_event_field_value_enum parent;
	uint64_t val;
};

/*
 * `LTTNG_EVENT_FIELD_VALUE_TYPE_SIGNED_ENUM`.
 */
struct lttng_event_field_value_enum_int {
	struct lttng_event_field_value_enum parent;
	int64_t val;
};

/* `LTTNG_EVENT_FIELD_VALUE_TYPE_REAL` */
struct lttng_event_field_value_real {
	struct lttng_event_field_value parent;
	double val;
};

/* `LTTNG_EVENT_FIELD_VALUE_TYPE_STRING` */
struct lttng_event_field_value_string {
	struct lttng_event_field_value parent;

	/* Owned by this */
	char *val;
};

/* `LTTNG_EVENT_FIELD_VALUE_TYPE_STRING` */
struct lttng_event_field_value_array {
	struct lttng_event_field_value parent;

	/*
	 * Array of `struct lttng_event_field_value *` (owned by this).
	 *
	 * A `NULL` element means it's unavailable
	 * (`LTTNG_EVENT_FIELD_VALUE_STATUS_UNAVAILABLE` status).
	 */
	struct lttng_dynamic_pointer_array elems;
};

/*
 * NOTE JORAJ: This was previously public. The only slight problem with that is
 * that as of today (2020-05-26) there is no plan/sessiond for the tracer to
 * actually provide this information. This was already known in [1]. For now
 * this code have no value since it only confuse the end user. At upstreaming
 * time we will need to decide if we want to remove all code pertaining to enum
 * label, at least on the lttng-tools API.
 *
 * [1] https://support.efficios.com/issues/792
 *
 * Sets `*count` to the number of labels of the enumeration event field
 * value `field_val`.
 *
 * Returns:
 *
 * `LTTNG_EVENT_FIELD_VALUE_STATUS_OK`:
 *     Success.
 *
 * `LTTNG_EVENT_FIELD_VALUE_STATUS_INVALID`:
 *     * `field_val` is `NULL`.
 *     * The type of `field_val` is not
 *       `LTTNG_EVENT_FIELD_VALUE_TYPE_UNSIGNED_ENUM` or
 *       `LTTNG_EVENT_FIELD_VALUE_TYPE_SIGNED_ENUM`.
 *     * `count` is `NULL`.
 */
LTTNG_HIDDEN
enum lttng_event_field_value_status
lttng_event_field_value_enum_get_label_count(
		const struct lttng_event_field_value *field_val,
		unsigned int *count);

/*
 * NOTE JORAJ: see NOTE JORAJ off lttng_event_field_value_enum_get_label_count
 *
 * Returns the label at index `index` of the enumeration event field
 * value `field_val`, or `NULL` if:
 *
 * * `field_val` is `NULL`.
 * * The type of `field_val` is not
 *   `LTTNG_EVENT_FIELD_VALUE_TYPE_UNSIGNED_ENUM` or
 *   `LTTNG_EVENT_FIELD_VALUE_TYPE_SIGNED_ENUM`.
 * * `index` is greater than or equal to the label count of `field_val`,
 *   as returned by lttng_event_field_value_enum_get_label_count().
 */
LTTNG_HIDDEN
const char *lttng_event_field_value_enum_get_label_at_index(
		const struct lttng_event_field_value *field_val,
		unsigned int index);

LTTNG_HIDDEN
struct lttng_event_field_value *lttng_event_field_value_uint_create(
		uint64_t val);

LTTNG_HIDDEN
struct lttng_event_field_value *lttng_event_field_value_int_create(
		int64_t val);

LTTNG_HIDDEN
struct lttng_event_field_value *lttng_event_field_value_enum_uint_create(
		uint64_t val);

LTTNG_HIDDEN
struct lttng_event_field_value *lttng_event_field_value_enum_int_create(
		int64_t val);

LTTNG_HIDDEN
struct lttng_event_field_value *lttng_event_field_value_real_create(double val);

LTTNG_HIDDEN
struct lttng_event_field_value *lttng_event_field_value_string_create(
		const char *val);

LTTNG_HIDDEN
struct lttng_event_field_value *lttng_event_field_value_string_create_with_size(
		const char *val, size_t size);

LTTNG_HIDDEN
struct lttng_event_field_value *lttng_event_field_value_array_create(void);

LTTNG_HIDDEN
int lttng_event_field_value_enum_append_label(
		struct lttng_event_field_value *field_val, const char *label);

LTTNG_HIDDEN
int lttng_event_field_value_enum_append_label_with_size(
		struct lttng_event_field_value *field_val, const char *label,
		size_t size);

LTTNG_HIDDEN
int lttng_event_field_value_array_append(
		struct lttng_event_field_value *array_field_val,
		struct lttng_event_field_value *field_val);

LTTNG_HIDDEN
int lttng_event_field_value_array_append_unavailable(
		struct lttng_event_field_value *array_field_val);

LTTNG_HIDDEN
void lttng_event_field_value_destroy(struct lttng_event_field_value *field_val);

#endif /* LTTNG_EVENT_FIELD_VALUE_INTERNAL_H */
