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
#include <lttng/condition/buffer-usage-internal.h>
#include <common/macros.h>
#include <assert.h>

static
bool is_usage_condition(struct lttng_condition *condition)
{
	enum lttng_condition_type type = lttng_condition_get_type(condition);

	return type == LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW ||
			type == LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH;
}

static
bool is_usage_condition(struct lttng_condition *condition)
{
	return is_low_usage_condition(condition) ||
			is_high_usage_condition(condition);
}

static
void lttng_condition_buffer_usage_destroy(struct lttng_condition *condition)
{
	struct lttng_condition_buffer_usage *usage;

	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);

	free(usage->session_name);
	free(usage->channel_name);
	free(usage);
}

static
bool lttng_condition_buffer_usage_validate(struct lttng_condition *condition)
{
	bool valid = false;
	struct lttng_condition_buffer_usage *usage;

	if (!condition) {
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);
	if (!usage->session_name) {
		goto end;
	}
	if (!usage->channel_name) {
		goto end;
	}
	if (!usage->threshold_percent.set && !usage->threshold_bytes.set) {
		goto end;
	}

	valid = true;
end:
	return valid;
}

static
struct lttng_condition *lttng_condition_buffer_usage_create(
		enum lttng_condition_type type)
{
	struct lttng_condition_buffer_usage *condition;

	condition = zmalloc(sizeof(struct lttng_condition_buffer_usage));
	if (!condition) {
		goto end;
	}

	condition->parent.type = type;
	condition->parent.validate = lttng_condition_buffer_usage_validate;
	condition->parent.destroy = lttng_condition_buffer_usage_destroy;
end:
	return &condition->parent;
}

struct lttng_condition *lttng_condition_buffer_usage_low_create(void)
{
	return lttng_condition_buffer_usage_create(
			LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW);
}

struct lttng_condition *lttng_condition_buffer_usage_high_create(void)
{
	return lttng_condition_buffer_usage_create(
			LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH);
}

enum lttng_condition_status
lttng_condition_buffer_usage_get_threshold_percentage(
		struct lttng_condition *condition, double *threshold_percent)
{
	struct lttng_condition_buffer_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !is_usage_condition(condition) ||
			!threshold_percent) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);
	if (!usage->threshold_percent.set) {
		status = LTTNG_CONDITION_STATUS_UNSET;
		goto end;
	}
	*threshold_percent = usage->threshold_percent.value;
end:
	return status;
}

/* threshold_percent expressed as [0.0, 1.0]. */
enum lttng_condition_status
lttng_condition_buffer_usage_set_threshold_percentage(
		struct lttng_condition *condition, double threshold_percent)
{
	struct lttng_condition_buffer_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !is_usage_condition(condition) ||
		threshold_percent < 0.0 || threshold_percent > 1.0) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);
	usage->threshold_percent.set = true;
	usage->threshold_bytes.set = false;
	usage->threshold_percent.value = threshold_percent;
end:
	return status;
}

enum lttng_condition_status
lttng_condition_buffer_usage_get_threshold(
		struct lttng_condition *condition, uint64_t *threshold_bytes)
{
	struct lttng_condition_buffer_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !is_usage_condition(condition) || !threshold_bytes) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);
	if (!usage->threshold_bytes.set) {
		status = LTTNG_CONDITION_STATUS_UNSET;
		goto end;
	}
	*threshold_bytes = usage->threshold_bytes.value;
end:
	return status;
}

enum lttng_condition_status
lttng_condition_buffer_usage_set_threshold(
		struct lttng_condition *condition, uint64_t threshold_bytes)
{
	struct lttng_condition_buffer_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !is_usage_condition(condition)) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);
	usage->threshold_percent.set = false;
	usage->threshold_bytes.set = true;
	usage->threshold_bytes.value = threshold_bytes;
end:
	return status;
}

enum lttng_condition_status
lttng_condition_buffer_usage_get_session_name(
		struct lttng_condition *condition, const char **session_name)
{
	struct lttng_condition_buffer_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !is_usage_condition(condition) || !session_name) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);
	if (!usage->session_name) {
		status = LTTNG_CONDITION_STATUS_UNSET;
		goto end;
	}
	*session_name = usage->session_name;
end:
	return status;
}

extern enum lttng_condition_status
lttng_condition_buffer_usage_set_session_name(
		struct lttng_condition *condition, const char *session_name)
{
	struct lttng_condition_buffer_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !is_usage_condition(condition) || !session_name ||
			strlen(session_name) == 0) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);
	usage->session_name = strdup(session_name);
	if (!usage->session_name) {
		status = LTTNG_CONDITION_STATUS_ERROR;
		goto end;
	}
end:
	return status;
}

enum lttng_condition_status
lttng_condition_buffer_usage_get_channel_name(
		struct lttng_condition *condition, const char **channel_name)
{
	struct lttng_condition_buffer_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !is_usage_condition(condition) || !channel_name) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);
	if (!usage->channel_name) {
		status = LTTNG_CONDITION_STATUS_UNSET;
		goto end;
	}
	*channel_name = usage->channel_name;
end:
	return status;
}

extern enum lttng_condition_status
lttng_condition_buffer_usage_set_channel_name(
		struct lttng_condition *condition, const char *channel_name)
{
	struct lttng_condition_buffer_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !is_usage_condition(condition) || !channel_name ||
			strlen(channel_name) == 0) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);
	usage->channel_name = strdup(channel_name);
	if (!usage->channel_name) {
		status = LTTNG_CONDITION_STATUS_ERROR;
		goto end;
	}
end:
	return status;
}

extern enum lttng_condition_status
lttng_condition_buffer_usage_get_domain_type(
		struct lttng_condition *condition, enum lttng_domain_type *type)
{
	struct lttng_condition_buffer_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !is_usage_condition(condition) || !type) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);
	if (!usage->domain.set) {
		status = LTTNG_CONDITION_STATUS_UNSET;
		goto end;
	}
	*type = usage->domain.type;
end:
	return status;
}

extern enum lttng_condition_status
lttng_condition_buffer_usage_set_domain_type(
		struct lttng_condition *condition, enum lttng_domain_type type)
{
	struct lttng_condition_buffer_usage *usage;
	enum lttng_condition_status status = LTTNG_CONDITION_STATUS_OK;

	if (!condition || !is_usage_condition(condition) ||
			type == LTTNG_DOMAIN_NONE) {
		status = LTTNG_CONDITION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);
	usage->domain.set = true;
	usage->domain.type = type;
end:
	return status;
}
