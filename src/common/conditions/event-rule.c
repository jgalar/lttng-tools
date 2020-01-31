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

#include <lttng/condition/condition-internal.h>
#include <lttng/condition/event-rule-internal.h>
#include <lttng/event-rule/event-rule-internal.h>
#include <common/macros.h>
#include <common/error.h>
#include <assert.h>
#include <stdbool.h>

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
		struct lttng_dynamic_buffer *buf,
		int *fd_to_send);
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

static
int lttng_condition_event_rule_serialize(
		const struct lttng_condition *condition,
		struct lttng_dynamic_buffer *buf,
		int *fd_to_send)
{
	int ret;
	size_t header_offset, size_before_payload;
	struct lttng_condition_event_rule *event_rule;
	struct lttng_condition_event_rule_comm event_rule_comm = { 0 };
	struct lttng_condition_event_rule_comm *header;

	if (!condition || !IS_EVENT_RULE_CONDITION(condition)) {
		ret = -1;
		goto end;
	}

	DBG("Serializing event rule condition");
	event_rule = container_of(condition, struct lttng_condition_event_rule,
			parent);

	header_offset = buf->size;
	ret = lttng_dynamic_buffer_append(buf, &event_rule_comm,
			sizeof(event_rule_comm));
	if (ret) {
		goto end;
	}

	size_before_payload = buf->size;
	ret = lttng_event_rule_serialize(event_rule->rule, buf, fd_to_send);
	if (ret) {
		goto end;
	}

	/* Update payload size */
	header = (struct lttng_condition_event_rule_comm *) ((char *) buf->data + header_offset);
	header->length = buf->size - size_before_payload;

end:
	return ret;
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
	free(event_rule);
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
	parent = &condition->parent;
end:
	return parent;
}

LTTNG_HIDDEN
ssize_t lttng_condition_event_rule_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_condition **_condition)
{
	ssize_t offset, event_rule_size;
	const struct lttng_condition_event_rule_comm *comm;
	struct lttng_condition *condition = NULL;
	struct lttng_event_rule *event_rule = NULL;
	struct lttng_buffer_view event_rule_view;

	if (!view || !_condition) {
		goto error;
	}

	if (view->size < sizeof(*comm)) {
		ERR("Failed to initialize from malformed event rule condition: buffer too short to contain header");
		goto error;
	}

	comm = (const struct lttng_condition_event_rule_comm *) view->data;
	offset = sizeof(*comm);

	/* Struct lttng_event_rule */
	event_rule_view = lttng_buffer_view_from_view(view, offset, -1);
	event_rule_size = lttng_event_rule_create_from_buffer(&event_rule_view, &event_rule);
	if (event_rule_size < 0 || !event_rule) {
		goto error;
	}

	if ((size_t) comm->length != event_rule_size) {
		goto error;
	}
	
	/* Move to the end */
	offset += comm->length;

	condition = lttng_condition_event_rule_create(event_rule);
	if (!condition) {
		goto error;
	}

	/* Ownership passed on condition event rule create */
	event_rule = NULL;

	*_condition = condition;
	condition = NULL;
	goto end;

error:
	offset = -1;

end:
	lttng_event_rule_destroy(event_rule);
	lttng_condition_destroy(condition);
	return offset;
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

LTTNG_HIDDEN
ssize_t lttng_evaluation_event_rule_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_evaluation **_evaluation)
{
	ssize_t ret, offset = 0;
	const char *name;
	struct lttng_evaluation *evaluation = NULL;
	const struct lttng_evaluation_event_rule_comm *comm = 
		(const struct lttng_evaluation_event_rule_comm *) view->data;
	struct lttng_buffer_view current_view;

	if (!_evaluation) {
		ret = -1;
		goto error;
	}

	if (view->size < sizeof(*comm)) {
		ret = -1;
		goto error;
	}

	/* Map the name, view of the payload */
	offset += sizeof(*comm);
	current_view = lttng_buffer_view_from_view(view, offset, comm->trigger_name_length);
	name = current_view.data;
	if (!name) {
		ret = -1;
		goto error;
	}

	if (comm->trigger_name_length == 1 ||
			name[comm->trigger_name_length - 1] != '\0' ||
			strlen(name) != comm->trigger_name_length - 1) {
		/*
		 * Check that the name is not NULL, is NULL-terminated, and
		 * does not contain a NULL before the last byte.
		 */
		ret = -1;
		goto error;
	}

	offset += comm->trigger_name_length;

	evaluation = lttng_evaluation_event_rule_create(name);
	if (!evaluation) {
		ret = -1;
		goto error;
	}

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
		struct lttng_dynamic_buffer *buf)
{
	int ret = 0;
	struct lttng_evaluation_event_rule *hit;
	struct lttng_evaluation_event_rule_comm comm;

	hit = container_of(evaluation, struct lttng_evaluation_event_rule,
			parent);
	comm.trigger_name_length = strlen(hit->name) + 1;
	ret = lttng_dynamic_buffer_append(buf, &comm, sizeof(comm));
	if (ret) {
		goto end;
	}
	ret = lttng_dynamic_buffer_append(buf, hit->name, comm.trigger_name_length);
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
	free(hit);
}

LTTNG_HIDDEN
struct lttng_evaluation *lttng_evaluation_event_rule_create(const char *trigger_name)
{
	struct lttng_evaluation_event_rule *hit;

	hit = zmalloc(sizeof(struct lttng_evaluation_event_rule));
	if (!hit) {
		goto end;
	}

	/* TODO errir handling */
	hit->name = strdup(trigger_name);

	hit->parent.type = LTTNG_CONDITION_TYPE_EVENT_RULE_HIT;
	hit->parent.serialize = lttng_evaluation_event_rule_serialize;
	hit->parent.destroy = lttng_evaluation_event_rule_destroy;
end:
	return &hit->parent;
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
