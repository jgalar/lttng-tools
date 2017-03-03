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
#include <common/error.h>
#include <assert.h>

static
bool is_usage_condition(struct lttng_condition *condition)
{
	enum lttng_condition_type type = lttng_condition_get_type(condition);

	return type == LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW ||
			type == LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH;
}

static
bool is_usage_evaluation(struct lttng_evaluation *evaluation)
{
	enum lttng_condition_type type = lttng_evaluation_get_type(evaluation);

	return type == LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW ||
			type == LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH;
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
		ERR("Invalid buffer condition: a target session name must be set.");
		goto end;
	}
	if (!usage->channel_name) {
		ERR("Invalid buffer condition: a target channel name must be set.");
		goto end;
	}
	if (!usage->threshold_percent.set && !usage->threshold_bytes.set) {
		ERR("Invalid buffer condition: a threshold must be set.");
		goto end;
	}

	valid = true;
end:
	return valid;
}

static
ssize_t lttng_condition_buffer_usage_serialize(struct lttng_condition *condition,
		char *buf)
{
	struct lttng_condition_buffer_usage *usage;
	ssize_t size;
	size_t session_name_len, channel_name_len;

	if (!condition || !is_usage_condition(condition)) {
		size = -1;
		goto end;
	}

	DBG("Serializing buffer usage condition");
	usage = container_of(condition, struct lttng_condition_buffer_usage,
			parent);
	size = sizeof(struct lttng_condition_buffer_usage_comm);
	session_name_len = strlen(usage->session_name) + 1;
	channel_name_len = strlen(usage->channel_name) + 1;
	size += session_name_len + channel_name_len;
	if (buf) {
		struct lttng_condition_buffer_usage_comm usage_comm = {
			.threshold_set_in_bytes = usage->threshold_bytes.set ? 1 : 0,
			.session_name_len = session_name_len,
			.channel_name_len = channel_name_len,
			.domain_type = (int8_t) usage->domain.type,
		};

		if (usage->threshold_bytes.set) {
			usage_comm.threshold.bytes =
					usage->threshold_bytes.value;
		} else {
			usage_comm.threshold.percent =
					usage->threshold_percent.value;
		}

		memcpy(buf, &usage_comm, sizeof(usage_comm));
		buf += sizeof(usage_comm);
		memcpy(buf, usage->session_name, session_name_len);
		buf += session_name_len;
		memcpy(buf, usage->channel_name, channel_name_len);
		buf += channel_name_len;
	}
end:
	return size;
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

	lttng_condition_init(&condition->parent, type);
	condition->parent.validate = lttng_condition_buffer_usage_validate;
	condition->parent.serialize = lttng_condition_buffer_usage_serialize;
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

static
ssize_t init_from_buffer(struct lttng_condition *condition, const char *buf)
{
	ssize_t ret, condition_size;
	enum lttng_condition_status status;
	enum lttng_domain_type domain_type;
	struct lttng_condition_buffer_usage_comm *condition_comm =
			(struct lttng_condition_buffer_usage_comm *) buf;
	const char *session_name, *channel_name;

	if (condition_comm->threshold_set_in_bytes) {
		status = lttng_condition_buffer_usage_set_threshold(condition,
				condition_comm->threshold.bytes);
	} else {
		status = lttng_condition_buffer_usage_set_threshold_percentage(
				condition, condition_comm->threshold.percent);
	}
	if (status != LTTNG_CONDITION_STATUS_OK) {
		ERR("Failed to initialize buffer usage condition threshold");
		ret = -1;
		goto end;
	}

	if (condition_comm->domain_type <= LTTNG_DOMAIN_NONE ||
			condition_comm->domain_type > LTTNG_DOMAIN_PYTHON) {
		/* Invalid domain value. */
		ERR("Invalid domain type value (%i) found in condition buffer",
				(int) condition_comm->domain_type);
		ret = -1;
		goto end;
	}

	domain_type = (enum lttng_domain_type) condition_comm->domain_type;
	status = lttng_condition_buffer_usage_set_domain_type(condition,
			domain_type);
	if (status != LTTNG_CONDITION_STATUS_OK) {
		ERR("Failed to set buffer usage condition domain");
		ret = -1;
		goto end;
	}

	session_name = buf + sizeof(struct lttng_condition_buffer_usage_comm);
	channel_name = session_name + condition_comm->session_name_len;

	status = lttng_condition_buffer_usage_set_session_name(condition,
			session_name);
	if (status != LTTNG_CONDITION_STATUS_OK) {
		ERR("Failed to set buffer usage session name");
		ret = -1;
		goto end;
	}

	status = lttng_condition_buffer_usage_set_channel_name(condition,
			channel_name);
	if (status != LTTNG_CONDITION_STATUS_OK) {
		ERR("Failed to set buffer usage channel name");
		ret = -1;
		goto end;
	}

	if (!lttng_condition_validate(condition)) {
		ret = -1;
		goto end;
	}

	condition_size = sizeof(*condition_comm);
	condition_size += (ssize_t) condition_comm->session_name_len;
	condition_size += (ssize_t) condition_comm->channel_name_len;
	ret = condition_size;
end:
	return ret;
}

LTTNG_HIDDEN
ssize_t lttng_condition_buffer_usage_low_create_from_buffer(const char *buf,
		struct lttng_condition **_condition)
{
	ssize_t ret;
	struct lttng_condition *condition =
			lttng_condition_buffer_usage_low_create();

	if (!_condition || !condition) {
		ret = -1;
		goto error;
	}

	DBG("Initializing low buffer usage condition from buffer");
	ret = init_from_buffer(condition, buf);
	if (ret < 0) {
		goto error;
	}

	*_condition = condition;
	return ret;
error:
	lttng_condition_destroy(condition);
	return ret;
}

LTTNG_HIDDEN
ssize_t lttng_condition_buffer_usage_high_create_from_buffer(const char *buf,
		struct lttng_condition **_condition)
{
	ssize_t ret;
	struct lttng_condition *condition =
			lttng_condition_buffer_usage_high_create();

	if (!_condition || !condition) {
		ret = -1;
		goto error;
	}

	DBG("Initializing high buffer usage condition from buffer");
	ret = init_from_buffer(condition, buf);
	if (ret < 0) {
		goto error;
	}

	*_condition = condition;
	return ret;
error:
	lttng_condition_destroy(condition);
	return ret;
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

static
void lttng_evaluation_buffer_usage_destroy(
		struct lttng_evaluation *evaluation)
{
	struct lttng_evaluation_buffer_usage *usage;

	usage = container_of(evaluation, struct lttng_evaluation_buffer_usage,
			parent);
	free(usage);
}

LTTNG_HIDDEN
struct lttng_evaluation *lttng_evaluation_buffer_usage_create(uint64_t use,
		uint64_t capacity)
{
	struct lttng_evaluation_buffer_usage *usage;

	usage = zmalloc(sizeof(struct lttng_evaluation_buffer_usage));
	if (!usage) {
		goto end;
	}

	usage->buffer_use = use;
	usage->buffer_capacity = capacity;
	usage->parent.destroy = lttng_evaluation_buffer_usage_destroy;
end:
	return &usage->parent;
}

/*
 * Get the sampled buffer usage which caused the associated condition to
 * evaluate to "true".
 */
enum lttng_evaluation_status
lttng_evaluation_buffer_usage_get_usage_percentage(
		struct lttng_evaluation *evaluation, double *usage_percent)
{
	struct lttng_evaluation_buffer_usage *usage;
	enum lttng_evaluation_status status = LTTNG_EVALUATION_STATUS_OK;

	if (!evaluation || !is_usage_evaluation(evaluation) || !usage_percent) {
		status = LTTNG_EVALUATION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(evaluation, struct lttng_evaluation_buffer_usage,
			parent);
	*usage_percent = (double) usage->buffer_use /
			(double) usage->buffer_capacity;
end:
	return status;
}

enum lttng_evaluation_status
lttng_evaluation_buffer_usage_get_usage(struct lttng_evaluation *evaluation,
	        uint64_t *usage_bytes)
{
	struct lttng_evaluation_buffer_usage *usage;
	enum lttng_evaluation_status status = LTTNG_EVALUATION_STATUS_OK;

	if (!evaluation || !is_usage_evaluation(evaluation) || !usage_bytes) {
		status = LTTNG_EVALUATION_STATUS_INVALID;
		goto end;
	}

	usage = container_of(evaluation, struct lttng_evaluation_buffer_usage,
			parent);
	*usage_bytes = usage->buffer_use;
end:
	return status;
}
