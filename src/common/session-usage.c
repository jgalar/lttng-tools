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
#include <lttng/condition/session-usage-internal.h>
#include <common/macros.h>
#include <common/error.h>
#include <assert.h>
#include <math.h>
#include <float.h>
#include <time.h>

#define IS_USAGE_CONDITION(condition) ( \
	lttng_condition_get_type(condition) == LTTNG_CONDITION_TYPE_SESSION_USAGE_CONSUMED \
	)

static
bool is_usage_evaluation(const struct lttng_evaluation *evaluation)
{
	enum lttng_condition_type type = lttng_evaluation_get_type(evaluation);

	return type == LTTNG_CONDITION_TYPE_SESSION_USAGE_CONSUMED;
}

static
void lttng_condition_session_usage_destroy(struct lttng_condition *condition)
{
	struct lttng_condition_session_usage *usage;

	usage = container_of(condition, struct lttng_condition_session_usage,
			parent);

	free(usage->session_name);
	free(usage);
}

static
bool lttng_condition_session_usage_validate(
		const struct lttng_condition *condition)
{
	bool valid = false;
	struct lttng_condition_session_usage *usage;

	if (!condition) {
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_session_usage,
			parent);
	if (!usage->session_name) {
		ERR("Invalid buffer condition: a target session name must be set.");
		goto end;
	}
	if (!usage->consumed_threshold_bytes.set) {
		ERR("Invalid session condition: a threshold must be set.");
		goto end;
	}

	valid = true;
end:
	return valid;
}

static
ssize_t lttng_condition_session_usage_serialize(
		const struct lttng_condition *condition, char *buf)
{
	struct lttng_condition_session_usage *usage;
	ssize_t ret, size;
	size_t session_name_len;

	if (!condition || !IS_USAGE_CONDITION(condition)) {
		ret = -1;
		goto end;
	}

	DBG("Serializing session usage condition");
	usage = container_of(condition, struct lttng_condition_session_usage,
			parent);
	size = sizeof(struct lttng_condition_session_usage_comm);
	session_name_len = strlen(usage->session_name) + 1;
	if (session_name_len > LTTNG_NAME_MAX) {
		ret = -1;
		goto end;
	}
	size += session_name_len;
	if (buf) {
		struct lttng_condition_session_usage_comm usage_comm = {
			.consumed_threshold_bytes = usage->consumed_threshold_bytes.value,
			.session_name_len = session_name_len,
		};

		memcpy(buf, &usage_comm, sizeof(usage_comm));
		buf += sizeof(usage_comm);
		memcpy(buf, usage->session_name, session_name_len);
		buf += session_name_len;
	}
	ret = size;
end:
	return ret;
}

static
bool lttng_condition_session_usage_is_equal(const struct lttng_condition *_a,
		const struct lttng_condition *_b)
{
	bool is_equal = false;
	struct lttng_condition_session_usage *a, *b;

	a = container_of(_a, struct lttng_condition_session_usage, parent);
	b = container_of(_b, struct lttng_condition_session_usage, parent);

	if (a->consumed_threshold_bytes.set && b->consumed_threshold_bytes.set) {
		uint64_t a_value, b_value;

		a_value = a->consumed_threshold_bytes.value;
		b_value = b->consumed_threshold_bytes.value;
		if (a_value != b_value) {
			goto end;
		}
	}

	if ((a->session_name && !b->session_name) ||
			(!a->session_name && b->session_name)) {
		goto end;
	}

	is_equal = true;
end:
	return is_equal;
}

struct lttng_condition *lttng_condition_session_usage_consumed_create(void)
{
	struct lttng_condition_session_usage *condition;

	condition = zmalloc(sizeof(struct lttng_condition_session_usage));
	if (!condition) {
		return NULL;
	}

	lttng_condition_init(&condition->parent, LTTNG_CONDITION_TYPE_SESSION_USAGE_CONSUMED);
	condition->parent.validate = lttng_condition_session_usage_validate;
	condition->parent.serialize = lttng_condition_session_usage_serialize;
	condition->parent.equal = lttng_condition_session_usage_is_equal;
	condition->parent.destroy = lttng_condition_session_usage_destroy;
	return &condition->parent;
}

static
ssize_t init_condition_from_buffer(struct lttng_condition *condition,
		const struct lttng_buffer_view *src_view)
{
	ssize_t ret, condition_size;
	enum lttng_condition_status status;
	const struct lttng_condition_session_usage_comm *condition_comm;
	const char *session_name;
	struct lttng_buffer_view names_view;

	if (src_view->size < sizeof(*condition_comm)) {
		ERR("Failed to initialize from malformed condition buffer: buffer too short to contain header");
		ret = -1;
		goto end;
	}

	condition_comm = (const struct lttng_condition_session_usage_comm *) src_view->data;
	names_view = lttng_buffer_view_from_view(src_view,
			sizeof(*condition_comm), -1);

	if (condition_comm->session_name_len > LTTNG_NAME_MAX) {
		ERR("Failed to initialize from malformed condition buffer: name exceeds LTTNG_MAX_NAME");
		ret = -1;
		goto end;
	}

	if (names_view.size < condition_comm->session_name_len) {
		ERR("Failed to initialize from malformed condition buffer: buffer too short to contain element names");
		ret = -1;
		goto end;
	}

	status = lttng_condition_session_usage_consumed_set_threshold(condition,
			condition_comm->consumed_threshold_bytes);
	if (status != LTTNG_CONDITION_STATUS_OK) {
		ERR("Failed to initialize session usage condition threshold");
		ret = -1;
		goto end;
	}

	session_name = names_view.data;
	if (*(session_name + condition_comm->session_name_len - 1) != '\0') {
		ERR("Malformed session name encountered in condition buffer");
		ret = -1;
		goto end;
	}

	status = lttng_condition_session_usage_set_session_name(condition,
			session_name);
	if (status != LTTNG_CONDITION_STATUS_OK) {
		ERR("Failed to set buffer usage session name");
		ret = -1;
		goto end;
	}

	if (!lttng_condition_validate(condition)) {
		ret = -1;
		goto end;
	}

	condition_size = sizeof(*condition_comm) +
			(ssize_t) condition_comm->session_name_len;
	ret = condition_size;
end:
	return ret;
}

LTTNG_HIDDEN
ssize_t lttng_condition_session_usage_consumed_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_condition **_condition)
{
	ssize_t ret;
	struct lttng_condition *condition =
			lttng_condition_session_usage_consumed_create();

	if (!_condition || !condition) {
		ret = -1;
		goto error;
	}

	ret = init_condition_from_buffer(condition, view);
	if (ret < 0) {
		goto error;
	}

	*_condition = condition;
	return ret;
error:
	lttng_condition_destroy(condition);
	return ret;
}

static
struct lttng_evaluation *create_evaluation_from_buffer(
		enum lttng_condition_type type,
		const struct lttng_buffer_view *view)
{
	const struct lttng_evaluation_session_usage_comm *comm =
			(const struct lttng_evaluation_session_usage_comm *) view->data;
	struct lttng_evaluation *evaluation = NULL;

	if (view->size < sizeof(*comm)) {
		goto end;
	}

	evaluation = lttng_evaluation_session_usage_consumed_create(type,
			comm->session_consumed);
end:
	return evaluation;
}

LTTNG_HIDDEN
ssize_t lttng_evaluation_session_usage_consumed_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_evaluation **_evaluation)
{
	ssize_t ret;
	struct lttng_evaluation *evaluation = NULL;

	if (!_evaluation) {
		ret = -1;
		goto error;
	}

	evaluation = create_evaluation_from_buffer(
			LTTNG_CONDITION_TYPE_SESSION_USAGE_CONSUMED, view);
	if (!evaluation) {
		ret = -1;
		goto error;
	}

	*_evaluation = evaluation;
	ret = sizeof(struct lttng_evaluation_session_usage_comm);
	return ret;
error:
	lttng_evaluation_destroy(evaluation);
	return ret;
}

enum lttng_condition_status
lttng_condition_session_usage_consumed_get_threshold(
		const struct lttng_condition *condition,
		uint64_t *consumed_threshold_bytes)
{
	struct lttng_condition_session_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !IS_USAGE_CONDITION(condition) || !consumed_threshold_bytes) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_session_usage,
			parent);
	if (!usage->consumed_threshold_bytes.set) {
		status = LTTNG_CONDITION_STATUS_UNSET;
		goto end;
	}
	*consumed_threshold_bytes = usage->consumed_threshold_bytes.value;
end:
	return status;
}

enum lttng_condition_status
lttng_condition_session_usage_consumed_set_threshold(
		struct lttng_condition *condition, uint64_t consumed_threshold_bytes)
{
	struct lttng_condition_session_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !IS_USAGE_CONDITION(condition)) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_session_usage,
			parent);
	usage->consumed_threshold_bytes.set = true;
	usage->consumed_threshold_bytes.value = consumed_threshold_bytes;
end:
	return status;
}

enum lttng_condition_status
lttng_condition_session_usage_get_session_name(
		const struct lttng_condition *condition,
		const char **session_name)
{
	struct lttng_condition_session_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !IS_USAGE_CONDITION(condition) || !session_name) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_session_usage,
			parent);
	if (!usage->session_name) {
		status = LTTNG_CONDITION_STATUS_UNSET;
		goto end;
	}
	*session_name = usage->session_name;
end:
	return status;
}

enum lttng_condition_status
lttng_condition_session_usage_set_session_name(
		struct lttng_condition *condition, const char *session_name)
{
	char *session_name_copy;
	struct lttng_condition_session_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !IS_USAGE_CONDITION(condition) || !session_name ||
			strlen(session_name) == 0) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_session_usage,
			parent);
	session_name_copy = strdup(session_name);
	if (!session_name_copy) {
		status = LTTNG_CONDITION_STATUS_ERROR;
		goto end;
	}

	if (usage->session_name) {
		free(usage->session_name);
	}
	usage->session_name = session_name_copy;
end:
	return status;
}

static
ssize_t lttng_evaluation_session_usage_serialize(
		struct lttng_evaluation *evaluation, char *buf)
{
	ssize_t ret;
	struct lttng_evaluation_session_usage *usage;

	usage = container_of(evaluation, struct lttng_evaluation_session_usage,
			parent);
	if (buf) {
		struct lttng_evaluation_session_usage_comm comm = {
			.session_consumed = usage->session_consumed,
		};

		memcpy(buf, &comm, sizeof(comm));
	}

	ret = sizeof(struct lttng_evaluation_session_usage_comm);
	return ret;
}

static
void lttng_evaluation_session_usage_destroy(
		struct lttng_evaluation *evaluation)
{
	struct lttng_evaluation_session_usage *usage;

	usage = container_of(evaluation, struct lttng_evaluation_session_usage,
			parent);
	free(usage);
}

LTTNG_HIDDEN
struct lttng_evaluation *lttng_evaluation_session_usage_consumed_create(
		enum lttng_condition_type type, uint64_t consumed)
{
	struct lttng_evaluation_session_usage *usage;

	usage = zmalloc(sizeof(struct lttng_evaluation_session_usage));
	if (!usage) {
		goto end;
	}

	usage->parent.type = type;
	usage->session_consumed = consumed;
	usage->parent.serialize = lttng_evaluation_session_usage_serialize;
	usage->parent.destroy = lttng_evaluation_session_usage_destroy;
end:
	return &usage->parent;
}

enum lttng_evaluation_status
lttng_evaluation_session_usage_get_consumed(
		const struct lttng_evaluation *evaluation,
		uint64_t *session_consumed)
{
	struct lttng_evaluation_session_usage *usage;
	enum lttng_evaluation_status status = LTTNG_EVALUATION_STATUS_OK;

	if (!evaluation || !is_usage_evaluation(evaluation) || !session_consumed) {
		status = LTTNG_EVALUATION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(evaluation, struct lttng_evaluation_session_usage,
			parent);
	*session_consumed = usage->session_consumed;
end:
	return status;
}
