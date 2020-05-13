/*
 * Copyright (C) 2017 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
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

#include <lttng/condition/event-rule.h>
#include <lttng/condition/condition-internal.h>
#include <lttng/condition/event-rule-internal.h>
#include <lttng/event-rule/event-rule-internal.h>
#include <lttng/event-field-value-internal.h>
#include <lttng/event-expr-internal.h>
#include <lttng/event-expr.h>
#include <lttng/lttng-error.h>
#include <common/macros.h>
#include <common/error.h>
#include <common/event-expr-to-bytecode.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <msgpack.h>

#define IS_EVENT_RULE_CONDITION(condition) ( \
	lttng_condition_get_type(condition) == LTTNG_CONDITION_TYPE_EVENT_RULE_HIT \
	)

static
bool is_event_rule_evaluation(const struct lttng_evaluation *evaluation)
{
	enum lttng_condition_type type = lttng_evaluation_get_type(evaluation);

	return type == LTTNG_CONDITION_TYPE_EVENT_RULE_HIT;
}


static
bool lttng_condition_event_rule_validate(
		const struct lttng_condition *condition);
static
int lttng_condition_event_rule_serialize(
		const struct lttng_condition *condition,
		struct lttng_payload *payload);
static
bool lttng_condition_event_rule_is_equal(const struct lttng_condition *_a,
		const struct lttng_condition *_b);
static
void lttng_condition_event_rule_destroy(
		struct lttng_condition *condition);


static
bool lttng_condition_event_rule_validate(
		const struct lttng_condition *condition)
{
	bool valid = false;
	struct lttng_condition_event_rule *event_rule;

	if (!condition) {
		goto end;
	}

	event_rule = container_of(condition,
			struct lttng_condition_event_rule, parent);
	if (!event_rule->rule) {
		ERR("Invalid session event_rule condition: a rule must be set.");
		goto end;
	}

	valid = lttng_event_rule_validate(event_rule->rule);
end:
	return valid;
}

/*
 * Serializes the C string `str` into `buf`.
 *
 * Encoding is the length of `str` plus one (for the null character),
 * and then the string, including its null character.
 */
static
int serialize_cstr(const char *str, struct lttng_dynamic_buffer *buf)
{
	int ret;
	uint32_t len = strlen(str) + 1;

	/* Serialize the length, including the null character */
	DBG("Serializing C string's length (including null character): "
			"%" PRIu32, len);
	ret = lttng_dynamic_buffer_append(buf, &len, sizeof(len));
	if (ret) {
		goto end;
	}

	/* Serialize the string */
	DBG("Serializing C string: \"%s\"", str);
	ret = lttng_dynamic_buffer_append(buf, str, len);
	if (ret) {
		goto end;
	}

end:
	return ret;
}

/*
 * Serializes the event expression `expr` into `buf`.
 */
static
int serialize_event_expr(const struct lttng_event_expr *expr,
		struct lttng_payload *payload)
{
	uint8_t type;
	int ret;

	/* Serialize the expression's type */
	DBG("Serializing event expression's type: %d", expr->type);
	type = expr->type;
	ret = lttng_dynamic_buffer_append(&payload->buffer, &type, sizeof(type));
	if (ret) {
		goto end;
	}

	/* Serialize the expression */
	switch (expr->type) {
	case LTTNG_EVENT_EXPR_TYPE_EVENT_PAYLOAD_FIELD:
	case LTTNG_EVENT_EXPR_TYPE_CHANNEL_CONTEXT_FIELD:
	{
		const struct lttng_event_expr_field *field_expr =
				container_of(expr,
					const struct lttng_event_expr_field,
					parent);

		/* Serialize the field name */
		DBG("Serializing field event expression's field name: \"%s\"",
				field_expr->name);
		ret = serialize_cstr(field_expr->name, &payload->buffer);
		if (ret) {
			goto end;
		}

		break;
	}
	case LTTNG_EVENT_EXPR_TYPE_APP_SPECIFIC_CONTEXT_FIELD:
	{
		const struct lttng_event_expr_app_specific_context_field *field_expr =
				container_of(expr,
					const struct lttng_event_expr_app_specific_context_field,
					parent);

		/* Serialize the provider name */
		DBG("Serializing app-specific context field event expression's "
				"provider name: \"%s\"",
				field_expr->provider_name);
		ret = serialize_cstr(field_expr->provider_name, &payload->buffer);
		if (ret) {
			goto end;
		}

		/* Serialize the type name */
		DBG("Serializing app-specific context field event expression's "
				"type name: \"%s\"",
				field_expr->provider_name);
		ret = serialize_cstr(field_expr->type_name, &payload->buffer);
		if (ret) {
			goto end;
		}

		break;
	}
	case LTTNG_EVENT_EXPR_TYPE_ARRAY_FIELD_ELEMENT:
	{
		const struct lttng_event_expr_array_field_element *elem_expr =
				container_of(expr,
					const struct lttng_event_expr_array_field_element,
					parent);
		uint32_t index = elem_expr->index;

		/* Serialize the index */
		DBG("Serializing array field element event expression's "
				"index: %u", elem_expr->index);
		ret = lttng_dynamic_buffer_append(&payload->buffer, &index, sizeof(index));
		if (ret) {
			goto end;
		}

		/* Serialize the parent array field expression */
		DBG("Serializing array field element event expression's "
				"parent array field event expression.");
		ret = serialize_event_expr(elem_expr->array_field_expr, payload);
		if (ret) {
			goto end;
		}

		break;
	}
	default:
		break;
	}

end:
	return ret;
}

LTTNG_HIDDEN
struct lttng_capture_descriptor *
lttng_condition_event_rule_get_internal_capture_descriptor_at_index(
		const struct lttng_condition *condition, unsigned int index)
{
	const struct lttng_condition_event_rule *event_rule_cond =
			container_of(condition,
				const struct lttng_condition_event_rule,
				parent);
	struct lttng_capture_descriptor *desc = NULL;
	unsigned int count;
	enum lttng_condition_status status;

	if (!condition || !IS_EVENT_RULE_CONDITION(condition)) {
		goto end;
	}

	status = lttng_condition_event_rule_get_capture_descriptor_count(
			condition, &count);
	if (status != LTTNG_CONDITION_STATUS_OK) {
		goto end;
	}

	if (index >= count) {
		goto end;
	}

	desc = lttng_dynamic_pointer_array_get_pointer(
			&event_rule_cond->capture_descriptors, index);
end:
	return desc;
}

static
int lttng_condition_event_rule_serialize(
		const struct lttng_condition *condition,
		struct lttng_payload *payload)
{
	int ret;
	struct lttng_condition_event_rule *event_rule;
	enum lttng_condition_status status;
	uint32_t capture_descr_count;
	uint32_t i;

	if (!condition || !IS_EVENT_RULE_CONDITION(condition)) {
		ret = -1;
		goto end;
	}

	DBG("Serializing event rule condition");
	event_rule = container_of(condition, struct lttng_condition_event_rule,
			parent);

	DBG("Serializing event rule condition's event rule");
	ret = lttng_event_rule_serialize(event_rule->rule, payload);
	if (ret) {
		goto end;
	}

	status = lttng_condition_event_rule_get_capture_descriptor_count(
			condition, &capture_descr_count);
	if (status != LTTNG_CONDITION_STATUS_OK) {
		ret = -1;
		goto end;
	};

	DBG("Serializing event rule condition's capture descriptor count: %" PRIu32,
			capture_descr_count);
	ret = lttng_dynamic_buffer_append(&payload->buffer, &capture_descr_count,
			sizeof(capture_descr_count));
	if (ret) {
		goto end;
	}

	for (i = 0; i < capture_descr_count; i++) {
		const struct lttng_capture_descriptor *desc =
				lttng_condition_event_rule_get_internal_capture_descriptor_at_index(
						condition, i);

		DBG("Serializing event rule condition's capture descriptor %" PRIu32,
				i);
		ret = serialize_event_expr(desc->event_expression, payload);
		if (ret) {
			goto end;
		}

		/*
		 * Appending the internal index payload linked with the
		 * descriptor.
		 * TODO: might want to move to an englobing object to describe a
		 * capture descriptor publicly.
		 */
		ret = lttng_dynamic_buffer_append(&payload->buffer, &desc->capture_index,
				sizeof(desc->capture_index));
		if (ret) {
			goto end;
		}
	}

end:
	return ret;
}

static
bool capture_descriptors_are_equal(
		const struct lttng_condition *condition_a,
		const struct lttng_condition *condition_b)
{
	bool is_equal = true;
	unsigned int capture_descr_count_a;
	unsigned int capture_descr_count_b;
	size_t i;
	enum lttng_condition_status status;

	status = lttng_condition_event_rule_get_capture_descriptor_count(
			condition_a, &capture_descr_count_a);
	if (status != LTTNG_CONDITION_STATUS_OK) {
		goto not_equal;
	}

	status = lttng_condition_event_rule_get_capture_descriptor_count(
			condition_b, &capture_descr_count_b);
	if (status != LTTNG_CONDITION_STATUS_OK) {
		goto not_equal;
	}

	if (capture_descr_count_a != capture_descr_count_b) {
		goto not_equal;
	}

	for (i = 0; i < capture_descr_count_a; i++) {
		const struct lttng_event_expr *expr_a =
				lttng_condition_event_rule_get_capture_descriptor_at_index(
					condition_a,
					i);
		const struct lttng_event_expr *expr_b =
				lttng_condition_event_rule_get_capture_descriptor_at_index(
					condition_b,
					i);

		if (!lttng_event_expr_is_equal(expr_a, expr_b)) {
			goto not_equal;
		}
	}

	goto end;

not_equal:
	is_equal = false;

end:
	return is_equal;
}

static
bool lttng_condition_event_rule_is_equal(const struct lttng_condition *_a,
		const struct lttng_condition *_b)
{
	bool is_equal = false;
	struct lttng_condition_event_rule *a, *b;

	a = container_of(_a, struct lttng_condition_event_rule, parent);
	b = container_of(_b, struct lttng_condition_event_rule, parent);

	/* Both session names must be set or both must be unset. */
	if ((a->rule && !b->rule) ||
			(!a->rule && b->rule)) {
		WARN("Comparing session event_rule conditions with uninitialized rule.");
		goto end;
	}

	is_equal = lttng_event_rule_is_equal(a->rule, b->rule);
	if (!is_equal) {
		goto end;
	}

	is_equal = capture_descriptors_are_equal(_a, _b);

end:
	return is_equal;
}

static
void lttng_condition_event_rule_destroy(
		struct lttng_condition *condition)
{
	struct lttng_condition_event_rule *event_rule;

	event_rule = container_of(condition,
			struct lttng_condition_event_rule, parent);


	lttng_event_rule_destroy(event_rule->rule);
	lttng_dynamic_pointer_array_reset(&event_rule->capture_descriptors);
	free(event_rule);
}

static
void destroy_capture_descriptor(void *ptr)
{
	struct lttng_capture_descriptor *desc =
			(struct lttng_capture_descriptor *) ptr;
	lttng_event_expr_destroy(desc->event_expression);
	free(desc);
}

struct lttng_condition *lttng_condition_event_rule_create(
		struct lttng_event_rule *rule)
{
	struct lttng_condition *parent = NULL;
	struct lttng_condition_event_rule *condition = NULL;

	if (!rule) {
		goto end;
	}

	condition = zmalloc(sizeof(struct lttng_condition_event_rule));
	if (!condition) {
		return NULL;
	}

	lttng_condition_init(&condition->parent, LTTNG_CONDITION_TYPE_EVENT_RULE_HIT);
	condition->parent.validate = lttng_condition_event_rule_validate,
	condition->parent.serialize = lttng_condition_event_rule_serialize,
	condition->parent.equal = lttng_condition_event_rule_is_equal,
	condition->parent.destroy = lttng_condition_event_rule_destroy,

	condition->rule = rule;
	lttng_dynamic_pointer_array_init(&condition->capture_descriptors,
			destroy_capture_descriptor);

	parent = &condition->parent;
end:
	return parent;
}

static
int64_t int_from_buffer(const struct lttng_buffer_view *view, size_t size,
		size_t *offset)
{
	int64_t ret;

	if (*offset + size > view->size) {
		ret = -1;
		goto end;
	}

	switch (size) {
	case 1:
		ret = (int64_t) view->data[*offset];
		break;
	case sizeof(int32_t):
	{
		int32_t s32;

		memcpy(&s32, &view->data[*offset], sizeof(s32));
		ret = (int64_t) s32;
		break;
	}
	case sizeof(ret):
		memcpy(&ret, &view->data[*offset], sizeof(ret));
		break;
	default:
		abort();
	}

	*offset += size;

end:
	return ret;
}
static
uint64_t uint_from_buffer(const struct lttng_buffer_view *view, size_t size,
		size_t *offset)
{
	uint64_t ret;

	if (*offset + size > view->size) {
		ret = UINT64_C(-1);
		goto end;
	}

	switch (size) {
	case 1:
		ret = (uint64_t) view->data[*offset];
		break;
	case sizeof(uint32_t):
	{
		uint32_t u32;

		memcpy(&u32, &view->data[*offset], sizeof(u32));
		ret = (uint64_t) u32;
		break;
	}
	case sizeof(ret):
		memcpy(&ret, &view->data[*offset], sizeof(ret));
		break;
	default:
		abort();
	}

	*offset += size;

end:
	return ret;
}

static
const char *str_from_buffer(const struct lttng_buffer_view *view,
		size_t *offset)
{
	uint64_t len;
	const char *ret;

	len = uint_from_buffer(view, sizeof(uint32_t), offset);
	if (len == UINT64_C(-1)) {
		goto error;
	}

	ret = &view->data[*offset];

	if (!lttng_buffer_view_contains_string(view, ret, len)) {
		goto error;
	}

	*offset += len;
	goto end;

error:
	ret = NULL;

end:
	return ret;
}

static
struct lttng_event_expr *event_expr_from_payload(
		struct lttng_payload_view *view, size_t *offset)
{
	struct lttng_event_expr *expr = NULL;
	const char *str;
	uint64_t type;

	type = uint_from_buffer(&view->buffer, sizeof(uint8_t), offset);
	if (type == UINT64_C(-1)) {
		goto error;
	}

	switch (type) {
	case LTTNG_EVENT_EXPR_TYPE_EVENT_PAYLOAD_FIELD:
		str = str_from_buffer(&view->buffer, offset);
		if (!str) {
			goto error;
		}

		expr = lttng_event_expr_event_payload_field_create(str);
		break;
	case LTTNG_EVENT_EXPR_TYPE_CHANNEL_CONTEXT_FIELD:
		str = str_from_buffer(&view->buffer, offset);
		if (!str) {
			goto error;
		}

		expr = lttng_event_expr_channel_context_field_create(str);
		break;
	case LTTNG_EVENT_EXPR_TYPE_APP_SPECIFIC_CONTEXT_FIELD:
	{
		const char *provider_name;
		const char *type_name;

		provider_name = str_from_buffer(&view->buffer, offset);
		if (!provider_name) {
			goto error;
		}

		type_name = str_from_buffer(&view->buffer, offset);
		if (!type_name) {
			goto error;
		}

		expr = lttng_event_expr_app_specific_context_field_create(
				provider_name, type_name);
		break;
	}
	case LTTNG_EVENT_EXPR_TYPE_ARRAY_FIELD_ELEMENT:
	{
		struct lttng_event_expr *array_field_expr;
		uint64_t index;

		index = uint_from_buffer(&view->buffer, sizeof(uint32_t), offset);
		if (index == UINT64_C(-1)) {
			goto error;
		}

		/* Array field expression is the encoded after this */
		array_field_expr = event_expr_from_payload(view, offset);
		if (!array_field_expr) {
			goto error;
		}

		/* Move ownership of `array_field_expr` to new expression */
		expr = lttng_event_expr_array_field_element_create(
				array_field_expr, (unsigned int) index);
		if (!expr) {
			/* `array_field_expr` not moved: destroy it */
			lttng_event_expr_destroy(array_field_expr);
		}

		break;
	}
	default:
		abort();
	}

	goto end;

error:
	lttng_event_expr_destroy(expr);
	expr = NULL;

end:
	return expr;
}

LTTNG_HIDDEN
ssize_t lttng_condition_event_rule_create_from_payload(
		struct lttng_payload_view *view,
		struct lttng_condition **_condition)
{
	ssize_t consumed_length;
	size_t offset = 0;
	ssize_t size;
	uint64_t capture_descr_count;
	uint64_t i;
	struct lttng_condition *condition = NULL;
	struct lttng_event_rule *event_rule = NULL;

	if (!view || !_condition) {
		goto error;
	}

	/* Struct lttng_event_rule */
	{
		struct lttng_payload_view event_rule_view =
				lttng_payload_view_from_view(view, offset, -1);
		size = lttng_event_rule_create_from_payload(
				&event_rule_view, &event_rule);
	}

	if (size < 0 || !event_rule) {
		goto error;
	}

	/* Create condition (no capture descriptors yet) at this point */
	condition = lttng_condition_event_rule_create(event_rule);
	if (!condition) {
		goto error;
	}

	/* Ownership moved to `condition` */
	event_rule = NULL;

	/* Capture descriptor count */
	assert(size >= 0);
	offset += (size_t) size;
	capture_descr_count = uint_from_buffer(&view->buffer, sizeof(uint32_t), &offset);
	if (capture_descr_count == UINT64_C(-1)) {
		goto error;
	}

	/* Capture descriptors */
	for (i = 0; i < capture_descr_count; i++) {
		enum lttng_condition_status status;
		struct lttng_capture_descriptor *desc;
		struct lttng_event_expr *expr = event_expr_from_payload(
				view, &offset);
		int32_t payload_index = int_from_buffer(&view->buffer, sizeof(int32_t),
				&offset);

		if (!expr) {
			goto error;
		}

		/* Move ownership of `expr` to `condition` */
		status = lttng_condition_event_rule_append_capture_descriptor(
				condition, expr);
		if (status != LTTNG_CONDITION_STATUS_OK) {
			/* `expr` not moved: destroy it */
			lttng_event_expr_destroy(expr);
			goto error;
		}

		/*
		 * Set the internal payload object for the descriptor. This can
		 * be used by liblttng-ctl to access capture msgpack payload on
		 * the client side.
		 */
		desc = lttng_condition_event_rule_get_internal_capture_descriptor_at_index(condition, i);
		if (desc == NULL) {
			goto error;
		}
		desc->capture_index = payload_index;
	}

	consumed_length = (ssize_t) offset;
	*_condition = condition;
	condition = NULL;
	goto end;

error:
	consumed_length = -1;

end:
	lttng_event_rule_destroy(event_rule);
	lttng_condition_destroy(condition);
	return consumed_length;
}

LTTNG_HIDDEN
enum lttng_condition_status
lttng_condition_event_rule_get_rule_no_const(
		const struct lttng_condition *condition,
		struct lttng_event_rule **rule)
{
	struct lttng_condition_event_rule *event_rule;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !IS_EVENT_RULE_CONDITION(condition) || !rule) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	event_rule = container_of(condition, struct lttng_condition_event_rule,
			parent);
	if (!event_rule->rule) {
		status = LTTNG_CONDITION_STATUS_UNSET;
		goto end;
	}
	*rule = event_rule->rule;
end:
	return status;
}

enum lttng_condition_status
lttng_condition_event_rule_get_rule(
		const struct lttng_condition *condition,
		const struct lttng_event_rule **rule)
{
	struct lttng_event_rule *no_const_rule = NULL;
	enum lttng_condition_status status;

	status = lttng_condition_event_rule_get_rule_no_const(condition, &no_const_rule);
	*rule = no_const_rule;
	return status;
}

enum lttng_condition_status
lttng_condition_event_rule_append_capture_descriptor(
		struct lttng_condition *condition,
		struct lttng_event_expr *expr)
{
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;
	struct lttng_condition_event_rule *event_rule_cond =
			container_of(condition,
				struct lttng_condition_event_rule, parent);
	int ret;
	struct lttng_capture_descriptor *descriptor = NULL;
	const struct lttng_event_rule *rule = NULL;

	/* Only accept l-values */
	if (!condition || !IS_EVENT_RULE_CONDITION(condition) || !expr ||
			!lttng_event_expr_is_lvalue(expr)) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	status = lttng_condition_event_rule_get_rule(event_rule_cond, &rule);
	if (status != LTTNG_CONDITION_STATUS_OK) {
		goto end;
	}

	switch(lttng_event_rule_get_type(rule)) {
	case LTTNG_EVENT_RULE_TYPE_TRACEPOINT:
	case LTTNG_EVENT_RULE_TYPE_SYSCALL:
		/* Supported */
		status = LTTNG_CONDITION_STATUS_OK;
		break;
	case LTTNG_EVENT_RULE_TYPE_UNKNOWN:
		status = LTTNG_CONDITION_STATUS_INVALID;
		break;
	default:
		status = LTTNG_CONDITION_STATUS_UNSUPPORTED;
		break;
	}

	if (status != LTTNG_CONDITION_STATUS_OK) {
		goto end;
	}

	descriptor = malloc(sizeof(*descriptor));
	if (descriptor == NULL) {
		status = LTTNG_CONDITION_STATUS_ERROR;
		goto end;
	}

	descriptor->capture_index = -1;
	descriptor->event_expression = expr;

	ret = lttng_dynamic_pointer_array_add_pointer(
			&event_rule_cond->capture_descriptors, descriptor);
	if (ret) {
		status = LTTNG_CONDITION_STATUS_ERROR;
		goto end;
	}

	/* Ownership is transfered to the internal capture_descriptors array */
	descriptor = NULL;
end:
	free(descriptor);
	return status;
}

enum lttng_condition_status
lttng_condition_event_rule_get_capture_descriptor_count(
		const struct lttng_condition *condition, unsigned int *count)
{
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;
	const struct lttng_condition_event_rule *event_rule_cond =
			container_of(condition,
				const struct lttng_condition_event_rule,
				parent);

	if (!condition || !IS_EVENT_RULE_CONDITION(condition) || !count) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	*count = lttng_dynamic_pointer_array_get_count(
			&event_rule_cond->capture_descriptors);

end:
	return status;
}

const struct lttng_event_expr *
lttng_condition_event_rule_get_capture_descriptor_at_index(
		const struct lttng_condition *condition, unsigned int index)
{
	const struct lttng_event_expr *expr = NULL;
	const struct lttng_capture_descriptor *desc = NULL;

	desc = lttng_condition_event_rule_get_internal_capture_descriptor_at_index(
			condition, index);
	if (desc == NULL) {
		goto end;
	}
	expr = desc->event_expression;

end:
	return expr;
}

LTTNG_HIDDEN
ssize_t lttng_evaluation_event_rule_create_from_payload(
		const struct lttng_condition_event_rule *condition,
		struct lttng_payload_view *view,
		struct lttng_evaluation **_evaluation)
{
	ssize_t ret, offset = 0;
	const char *name;
	struct lttng_evaluation *evaluation = NULL;
	const struct lttng_evaluation_event_rule_comm *comm =
			(const struct lttng_evaluation_event_rule_comm *)
					view->buffer.data;
	uint32_t capture_payload_size;
	const char *capture_payload = NULL;

	if (!_evaluation) {
		ret = -1;
		goto error;
	}

	if (view->buffer.size < sizeof(*comm)) {
		ret = -1;
		goto error;
	}

	/* Map the name, view of the payload */
	offset += sizeof(*comm);
	{
		struct lttng_payload_view current_view =
				lttng_payload_view_from_view(view, offset,
						comm->trigger_name_length);
		name = current_view.buffer.data;
		if (!name) {
			ret = -1;
			goto error;
		}

		if (!lttng_buffer_view_contains_string(&current_view.buffer,
				    name, comm->trigger_name_length)) {
			ret = -1;
			goto error;
		}
	}

	offset += comm->trigger_name_length;
	{
		struct lttng_payload_view current_view = lttng_payload_view_from_view(view, offset, -1);

		if (current_view.buffer.size < sizeof(capture_payload_size)) {
			ret = -1;
			goto error;
		}

		memcpy(&capture_payload_size, current_view.buffer.data,
				sizeof(capture_payload_size));
	}
	offset += sizeof(capture_payload_size);

	if (capture_payload_size > 0) {
		struct lttng_payload_view current_view = lttng_payload_view_from_view(view, offset, -1);

		if (current_view.buffer.size < capture_payload_size) {
			ret = -1;
			goto error;
		}

		capture_payload = current_view.buffer.data;
	}

	evaluation = lttng_evaluation_event_rule_create(condition, name,
			capture_payload, capture_payload_size, true);
	if (!evaluation) {
		ret = -1;
		goto error;
	}

	offset += capture_payload_size;
	*_evaluation = evaluation;
	evaluation = NULL;
	ret = offset;

error:
	lttng_evaluation_destroy(evaluation);
	return ret;
}

static
int lttng_evaluation_event_rule_serialize(
		const struct lttng_evaluation *evaluation,
		struct lttng_payload *payload)
{
	int ret = 0;
	struct lttng_evaluation_event_rule *hit;
	struct lttng_evaluation_event_rule_comm comm;
	uint32_t capture_payload_size;

	hit = container_of(
			evaluation, struct lttng_evaluation_event_rule, parent);
	comm.trigger_name_length = strlen(hit->name) + 1;
	ret = lttng_dynamic_buffer_append(
			&payload->buffer, &comm, sizeof(comm));
	if (ret) {
		goto end;
	}
	ret = lttng_dynamic_buffer_append(
			&payload->buffer, hit->name, comm.trigger_name_length);
	if (ret) {
		goto end;
	}

	capture_payload_size = (uint32_t) hit->capture_payload.size;
	ret = lttng_dynamic_buffer_append(&payload->buffer, &capture_payload_size,
			sizeof(capture_payload_size));
	if (ret) {
		goto end;
	}

	ret = lttng_dynamic_buffer_append(&payload->buffer, hit->capture_payload.data,
			hit->capture_payload.size);
	if (ret) {
		goto end;
	}

end:
	return ret;
}

static
bool msgpack_str_is_equal(const struct msgpack_object *obj, const char *str)
{
	bool is_equal = true;

	assert(obj->type == MSGPACK_OBJECT_STR);

	if (obj->via.str.size != strlen(str)) {
		is_equal = false;
		goto end;
	}

	if (strncmp(obj->via.str.ptr, str, obj->via.str.size) != 0) {
		is_equal = false;
		goto end;
	}

end:
	return is_equal;
}

static
const msgpack_object *get_msgpack_map_obj(const struct msgpack_object *map_obj,
		const char *name)
{
	const msgpack_object *ret = NULL;
	size_t i;

	assert(map_obj->type == MSGPACK_OBJECT_MAP);

	for (i = 0; i < map_obj->via.map.size; i++) {
		const struct msgpack_object_kv *kv = &map_obj->via.map.ptr[i];

		assert(kv->key.type == MSGPACK_OBJECT_STR);

		if (msgpack_str_is_equal(&kv->key, name)) {
			ret = &kv->val;
			goto end;
		}
	}

end:
	return ret;
}

static
void lttng_evaluation_event_rule_destroy(
		struct lttng_evaluation *evaluation)
{
	struct lttng_evaluation_event_rule *hit;

	hit = container_of(evaluation, struct lttng_evaluation_event_rule,
			parent);
	free(hit->name);
	lttng_dynamic_buffer_reset(&hit->capture_payload);
	if (hit->captured_values) {
		lttng_event_field_value_destroy(hit->captured_values);
	}
	free(hit);
}

static
int event_field_value_from_obj(const msgpack_object *obj,
		struct lttng_event_field_value **field_val)
{
	assert(obj);
	assert(field_val);
	int ret = 0;

	switch (obj->type) {
	case MSGPACK_OBJECT_NIL:
		/* Unavailable */
		*field_val = NULL;
		goto end;
	case MSGPACK_OBJECT_POSITIVE_INTEGER:
		*field_val = lttng_event_field_value_uint_create(
				obj->via.u64);
		break;
	case MSGPACK_OBJECT_NEGATIVE_INTEGER:
		*field_val = lttng_event_field_value_int_create(
				obj->via.i64);
		break;
	case MSGPACK_OBJECT_FLOAT32:
	case MSGPACK_OBJECT_FLOAT64:
		*field_val = lttng_event_field_value_real_create(
				obj->via.f64);
		break;
	case MSGPACK_OBJECT_STR:
		*field_val = lttng_event_field_value_string_create_with_size(
				obj->via.str.ptr, obj->via.str.size);
		break;
	case MSGPACK_OBJECT_ARRAY:
	{
		size_t i;

		*field_val = lttng_event_field_value_array_create();
		if (!*field_val) {
			goto error;
		}

		for (i = 0; i < obj->via.array.size; i++) {
			const msgpack_object *elem_obj = &obj->via.array.ptr[i];
			struct lttng_event_field_value *elem_field_val;

			ret = event_field_value_from_obj(elem_obj,
					&elem_field_val);

			if (ret) {
				goto error;
			}

			if (elem_field_val) {
				ret = lttng_event_field_value_array_append(
						*field_val, elem_field_val);
			} else {
				ret = lttng_event_field_value_array_append_unavailable(
						*field_val);
			}

			if (ret) {
				lttng_event_field_value_destroy(elem_field_val);
				goto error;
			}
		}

		break;
	}
	case MSGPACK_OBJECT_MAP:
	{
		/*
		 * As of this version, the only valid map object is
		 * for an enumeration value, for example:
		 *
		 *     type: enum
		 *     value: 177
		 *     labels:
		 *     - Labatt 50
		 *     - Molson Dry
		 *     - Carling Black Label
		 */
		const msgpack_object *inner_obj;
		size_t label_i;

		inner_obj = get_msgpack_map_obj(obj, "type");
		if (!inner_obj) {
			ERR("Missing `type` entry in map object.");
			goto error;
		}

		if (inner_obj->type != MSGPACK_OBJECT_STR) {
			ERR("Map object's `type` entry is not a string (it's a %d).",
					inner_obj->type);
			goto error;
		}

		if (!msgpack_str_is_equal(inner_obj, "enum")) {
			ERR("Map object's `type` entry: expecting `enum`.");
			goto error;
		}

		inner_obj = get_msgpack_map_obj(obj, "value");
		if (!inner_obj) {
			ERR("Missing `value` entry in map object.");
			goto error;
		}

		if (inner_obj->type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
			*field_val = lttng_event_field_value_enum_uint_create(
					inner_obj->via.u64);
		} else if (inner_obj->type == MSGPACK_OBJECT_NEGATIVE_INTEGER) {
			*field_val = lttng_event_field_value_enum_int_create(
					inner_obj->via.i64);
		} else {
			ERR("Map object's `value` entry is not an integer (it's a %d).",
					inner_obj->type);
			goto error;
		}

		if (!*field_val) {
			goto error;
		}

		inner_obj = get_msgpack_map_obj(obj, "labels");
		if (!inner_obj) {
			/* No labels */
			goto end;
		}

		if (inner_obj->type != MSGPACK_OBJECT_ARRAY) {
			ERR("Map object's `labels` entry is not an array (it's a %d).",
					inner_obj->type);
			goto error;
		}

		for (label_i = 0; label_i < inner_obj->via.array.size;
				label_i++) {
			int iret;
			const msgpack_object *elem_obj =
					&inner_obj->via.array.ptr[label_i];

			if (elem_obj->type != MSGPACK_OBJECT_STR) {
				ERR("Map object's `labels` entry's type is not a string (it's a %d).",
						elem_obj->type);
				goto error;
			}

			iret = lttng_event_field_value_enum_append_label_with_size(
					*field_val, elem_obj->via.str.ptr,
					elem_obj->via.str.size);
			if (iret) {
				goto error;
			}
		}

		break;
	}
	default:
		ERR("Unexpected object type %d.", obj->type);
		goto error;
	}

	if (!*field_val) {
		goto error;
	}

	goto end;

error:
	lttng_event_field_value_destroy(*field_val);
	*field_val = NULL;
	ret = -1;

end:
	return ret;
}

static
struct lttng_event_field_value *event_field_value_from_capture_payload(
		const struct lttng_condition_event_rule *condition,
		const char *capture_payload, size_t capture_payload_size)
{
	struct lttng_event_field_value *ret = NULL;
	msgpack_unpacked unpacked;
	msgpack_unpack_return unpack_return;
	const msgpack_object *root_obj;
	const msgpack_object_array *root_array_obj;
	size_t i;
	size_t count;

	assert(condition);
	assert(capture_payload);

	/* Initialize value */
	msgpack_unpacked_init(&unpacked);

	/* Decode */
	unpack_return = msgpack_unpack_next(&unpacked, capture_payload,
			capture_payload_size, NULL);
	if (unpack_return != MSGPACK_UNPACK_SUCCESS) {
		ERR("msgpack_unpack_next() failed to decode the "
				"MessagePack-encoded capture payload "
				"(size %zu); returned %d.",
				capture_payload_size, unpack_return);
		goto error;
	}

	/* Get root array */
	root_obj = &unpacked.data;

	if (root_obj->type != MSGPACK_OBJECT_ARRAY) {
		ERR("Expecting an array as the root object; got type %d.",
				root_obj->type);
		goto error;
	}

	root_array_obj = &root_obj->via.array;

	/* Create an empty root array event field value */
	ret = lttng_event_field_value_array_create();
	if (!ret) {
		goto error;
	}

	/*
	 * For each capture descriptor in the condition object:
	 *
	 * 1. Get its corresponding captured field value MessagePack
	 *    object.
	 *
	 * 2. Create a corresponding event field value.
	 *
	 * 3. Append it to `ret` (the root array event field value).
	 */
	count = lttng_dynamic_pointer_array_get_count(
			&condition->capture_descriptors);
	assert(count > 0);

	for (i = 0; i < count; i++) {
		const struct lttng_capture_descriptor *capture_descriptor =
				lttng_condition_event_rule_get_internal_capture_descriptor_at_index(
						&condition->parent, i);
		const msgpack_object *elem_obj;
		struct lttng_event_field_value *elem_field_val;
		int iret;

		assert(capture_descriptor);
		assert(capture_descriptor->capture_index >= 0);

		if (capture_descriptor->capture_index >= root_array_obj->size) {
			ERR("Root array object of size %u does not have enough "
					"elements for the capture index %u "
					"(for capture descriptor #%zu).",
					(unsigned int) root_array_obj->size,
					(unsigned int) capture_descriptor->capture_index,
					i);
			goto error;
		}

		elem_obj = &root_array_obj->ptr[(size_t) capture_descriptor->capture_index];
		iret = event_field_value_from_obj(elem_obj,
				&elem_field_val);
		if (iret) {
			goto error;
		}

		if (elem_field_val) {
			iret = lttng_event_field_value_array_append(ret,
					elem_field_val);
		} else {
			iret = lttng_event_field_value_array_append_unavailable(
					ret);
		}

		if (iret) {
			lttng_event_field_value_destroy(elem_field_val);
			goto error;
		}
	}

	goto end;

error:
	lttng_event_field_value_destroy(ret);
	ret = NULL;

end:
	msgpack_unpacked_destroy(&unpacked);
	return ret;
}

LTTNG_HIDDEN
struct lttng_evaluation *lttng_evaluation_event_rule_create(
		const struct lttng_condition_event_rule *condition,
		const char *trigger_name,
		const char *capture_payload, size_t capture_payload_size,
		bool decode_capture_payload)
{
	struct lttng_evaluation_event_rule *hit;

	hit = zmalloc(sizeof(struct lttng_evaluation_event_rule));
	if (!hit) {
		goto error;
	}

	/* TODO errir handling */
	hit->name = strdup(trigger_name);
	lttng_dynamic_buffer_init(&hit->capture_payload);

	if (capture_payload) {
		lttng_dynamic_buffer_append(&hit->capture_payload,
				capture_payload, capture_payload_size);

		if (decode_capture_payload) {
			hit->captured_values =
					event_field_value_from_capture_payload(
						condition,
						capture_payload,
						capture_payload_size);
			if (!hit->captured_values) {
				ERR("Failed to decode the capture payload (size %zu).",
						capture_payload_size);
				goto error;
			}
		}
	}

	hit->parent.type = LTTNG_CONDITION_TYPE_EVENT_RULE_HIT;
	hit->parent.serialize = lttng_evaluation_event_rule_serialize;
	hit->parent.destroy = lttng_evaluation_event_rule_destroy;
	goto end;

error:
	lttng_evaluation_event_rule_destroy(&hit->parent);
	hit = NULL;

end:
	return &hit->parent;
}

enum lttng_evaluation_status lttng_evaluation_get_captured_values(
		const struct lttng_evaluation *evaluation,
		const struct lttng_event_field_value **field_val)
{
	struct lttng_evaluation_event_rule *hit;
	enum lttng_evaluation_status status = LTTNG_EVALUATION_STATUS_OK;

	if (!evaluation || !is_event_rule_evaluation(evaluation) ||
			!field_val) {
		status = LTTNG_EVALUATION_STATUS_INVALID;
		goto end;
	}

	hit = container_of(evaluation, struct lttng_evaluation_event_rule,
			parent);
	if (!hit->captured_values) {
		status = LTTNG_EVALUATION_STATUS_INVALID;
		goto end;
	}

	*field_val = hit->captured_values;

end:
	return status;
}

enum lttng_evaluation_status
lttng_evaluation_event_rule_get_trigger_name(
		const struct lttng_evaluation *evaluation,
		const char **name)
{
	struct lttng_evaluation_event_rule *hit;
	enum lttng_evaluation_status status = LTTNG_EVALUATION_STATUS_OK;

	if (!evaluation || !is_event_rule_evaluation(evaluation) || !name) {
		status = LTTNG_EVALUATION_STATUS_INVALID;
		goto end;
	}

	hit = container_of(evaluation, struct lttng_evaluation_event_rule,
			parent);
	*name = hit->name;
end:
	return status;
}

LTTNG_HIDDEN
enum lttng_error_code
lttng_condition_event_rule_generate_capture_descriptor_bytecode_set(
		struct lttng_condition *condition,
		struct lttng_dynamic_pointer_array *bytecode_set)
{
	enum lttng_error_code ret;
	enum lttng_condition_status status;
	unsigned int capture_count;
	const struct lttng_condition_event_rule_capture_bytecode_element *set_element;
	struct lttng_capture_descriptor *local_capture_desc;
	ssize_t set_count;
	struct lttng_condition_event_rule_capture_bytecode_element *set_element_to_append =
			NULL;
	struct lttng_bytecode *bytecode = NULL;

	if (!condition || !IS_EVENT_RULE_CONDITION(condition) ||
			!bytecode_set) {
		ret = LTTNG_ERR_FATAL;
		goto end;
	}

	status = lttng_condition_event_rule_get_capture_descriptor_count(
			condition, &capture_count);
	if (status != LTTNG_CONDITION_STATUS_OK) {
		ret = LTTNG_ERR_FATAL;
		goto end;
	}

	/*
	 * O(n^2), don't care. This code path is not hot.
	 * Before inserting into the set, validate that the expression is not
	 * already present in it.
	 */
	for (unsigned int i = 0; i < capture_count; i++) {
		int found_in_set = false;

		set_count = lttng_dynamic_pointer_array_get_count(bytecode_set);

		local_capture_desc =
				lttng_condition_event_rule_get_internal_capture_descriptor_at_index(
						condition, i);
		if (local_capture_desc == NULL) {
			ret = LTTNG_ERR_FATAL;
			goto end;
		}

		/*
		 * Iterate over the set to check if already present in the set.
		 */
		for (ssize_t j = 0; j < set_count; j++) {
			set_element = (struct lttng_condition_event_rule_capture_bytecode_element
							*)
					lttng_dynamic_pointer_array_get_pointer(
							bytecode_set, j);
			if (set_element == NULL) {
				ret = LTTNG_ERR_FATAL;
				goto end;
			}

			if (!lttng_event_expr_is_equal(
					    local_capture_desc->event_expression,
					    set_element->expression)) {
				/* Check against next set element */
				continue;
			}

			/*
			 * Already present in the set, assign the
			 * capture index of the capture descriptor for
			 * future use.
			 */
			found_in_set = true;
			local_capture_desc->capture_index = j;
			/* Exit inner loop */
			break;
		}

		if (found_in_set) {
			/* Process next local capture descriptor */
			continue;
		}

		/*
		 * Not found in the set.
		 * Insert the capture descriptor in the set.
		 */
		set_element_to_append = malloc(sizeof(*set_element_to_append));
		if (set_element_to_append == NULL) {
			ret = LTTNG_ERR_NOMEM;
			goto end;
		}

		/* Generate the bytecode */
		status = lttng_event_expr_to_bytecode(
				local_capture_desc->event_expression,
				&bytecode);
		if (status < 0 || bytecode == NULL) {
			/* TODO: return pertinent capture related error code */
			ret = LTTNG_ERR_FILTER_INVAL;
			goto end;
		}

		set_element_to_append->bytecode = bytecode;

		/* 
		 * Ensure the lifetime of the event expression.
		 * Our reference will be put on condition destroy.
		 */
		lttng_event_expr_get(local_capture_desc->event_expression);
		set_element_to_append->expression =
				local_capture_desc->event_expression;

		ret = lttng_dynamic_pointer_array_add_pointer(
				bytecode_set, set_element_to_append);
		if (ret < 0) {
			ret = LTTNG_ERR_NOMEM;
			goto end;
		}

		/* Ownership tranfered to the bytecode set */
		set_element_to_append = NULL;
		bytecode = NULL;

		/* Assign the capture descriptor for future use */
		local_capture_desc->capture_index = set_count;
	}

	/* Everything went better than expected */
	ret = LTTNG_OK;

end:
	free(set_element_to_append);
	free(bytecode);
	return ret;
}
