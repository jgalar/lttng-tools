/*
 * Copyright (C) 2017 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#include <lttng/trigger/trigger-internal.h>
#include <lttng/condition/condition-internal.h>
#include <lttng/action/action-internal.h>
#include <common/error.h>
#include <common/dynamic-array.h>
#include <assert.h>
#include <inttypes.h>

static void lttng_trigger_set_internal_object_ownership(
		struct lttng_trigger *trigger)
{
	/*
	 * This is necessary to faciliate the object destroy phase. A trigger
	 * created by a client does not OWN the internal objects (condition and
	 * action) but when a trigger object is created on the sessiond side or
	 * for listing triggers (mostly via create_from_buffer) the object is
	 * the owner of the internal objects. Hence, we set the ownership bool
	 * to true, in such case, to facilitate object lifetime management and
	 * internal ownership.
	 *
	 * TODO: I'm open to any other solution
	 */
	assert(trigger);
	trigger->owns_internal_objects = true;
}

LTTNG_HIDDEN
bool lttng_trigger_validate(struct lttng_trigger *trigger)
{
	bool valid;

	if (!trigger) {
		valid = false;
		goto end;
	}

	valid = lttng_condition_validate(trigger->condition) &&
			lttng_action_validate(trigger->action);
end:
	return valid;
}

struct lttng_trigger *lttng_trigger_create(
		struct lttng_condition *condition,
		struct lttng_action *action)
{
	struct lttng_trigger *trigger = NULL;

	if (!condition || !action) {
		goto end;
	}

	trigger = zmalloc(sizeof(struct lttng_trigger));
	if (!trigger) {
		goto end;
	}

	urcu_ref_init(&trigger->ref);
	trigger->owns_internal_objects = false;
	trigger->firing_policy.type = LTTNG_TRIGGER_FIRE_EVERY_N;
	trigger->firing_policy.threshold = 1;
	trigger->condition = condition;
	trigger->action = action;

	trigger->creds.set = false;

end:
	return trigger;
}

struct lttng_condition *lttng_trigger_get_condition(
		struct lttng_trigger *trigger)
{
	return trigger ? trigger->condition : NULL;
}

LTTNG_HIDDEN
const struct lttng_condition *lttng_trigger_get_const_condition(
		const struct lttng_trigger *trigger)
{
	return trigger->condition;
}

struct lttng_action *lttng_trigger_get_action(
		struct lttng_trigger *trigger)
{
	return trigger ? trigger->action : NULL;
}

LTTNG_HIDDEN
const struct lttng_action *lttng_trigger_get_const_action(
		const struct lttng_trigger *trigger)
{
	return trigger->action;
}

static void trigger_destroy_ref(struct urcu_ref *ref)
{
	struct lttng_trigger *trigger =
			container_of(ref, struct lttng_trigger, ref);

	if (trigger->owns_internal_objects) {
		struct lttng_action *action = lttng_trigger_get_action(trigger);
		struct lttng_condition *condition =
				lttng_trigger_get_condition(trigger);

		assert(action);
		assert(condition);
		lttng_action_destroy(action);
		lttng_condition_destroy(condition);
	}

	free(trigger->name);
	free(trigger);
}

void lttng_trigger_destroy(struct lttng_trigger *trigger)
{
	lttng_trigger_put(trigger);
}

LTTNG_HIDDEN
ssize_t lttng_trigger_create_from_buffer(
		const struct lttng_buffer_view *src_view,
		struct lttng_trigger **trigger)
{
	ssize_t ret, offset = 0, condition_size, action_size, name_size = 0;
	enum lttng_trigger_status status;
	struct lttng_condition *condition = NULL;
	struct lttng_action *action = NULL;
	const struct lttng_trigger_comm *trigger_comm;
	struct lttng_buffer_view condition_view;
	struct lttng_buffer_view action_view;
	struct lttng_buffer_view name_view;
	const char *name = NULL;
	unsigned long long firing_threshold;
	enum lttng_trigger_firing_policy_type firing_policy;

	if (!src_view || !trigger) {
		ret = -1;
		goto end;
	}

	/* lttng_trigger_comm header */
	trigger_comm = (const struct lttng_trigger_comm *) src_view->data;
	offset += sizeof(*trigger_comm);

	firing_policy = trigger_comm->policy_type;
	firing_threshold = trigger_comm->policy_threshold;
	if (trigger_comm->name_length != 0) {
		name_view = lttng_buffer_view_from_view(
				src_view, offset, trigger_comm->name_length);
		name = name_view.data;
		if (trigger_comm->name_length == 1 ||
				name[trigger_comm->name_length - 1] != '\0' ||
				strlen(name) != trigger_comm->name_length - 1) {
			ret = -1;
			goto end;
		}
		offset += trigger_comm->name_length;
		name_size = trigger_comm->name_length;
	}

	condition_view = lttng_buffer_view_from_view(src_view, offset, -1);

	/* struct lttng_condition */
	condition_size = lttng_condition_create_from_buffer(&condition_view,
			&condition);
	if (condition_size < 0) {
		ret = condition_size;
		goto end;
	}
	offset += condition_size;

	/* struct lttng_action */
	action_view = lttng_buffer_view_from_view(src_view, offset, -1);
	action_size = lttng_action_create_from_buffer(&action_view, &action);
	if (action_size < 0) {
		ret = action_size;
		goto end;
	}
	offset += action_size;

	/* Unexpected size of inner-elements; the buffer is corrupted. */
	if ((ssize_t) trigger_comm->length != condition_size + action_size + name_size) {
		ret = -1;
		goto error;
	}

	*trigger = lttng_trigger_create(condition, action);
	if (!*trigger) {
		ret = -1;
		goto error;
	}

	/* Take ownership of the internal object from there */
	lttng_trigger_set_internal_object_ownership(*trigger);
	condition = NULL;
	action = NULL;

	if (name) {
		status = lttng_trigger_set_name(*trigger, name);
		if (status != LTTNG_TRIGGER_STATUS_OK) {
			ret = -1;
			goto end;
		}
	}

	status = lttng_trigger_set_firing_policy(*trigger, firing_policy, firing_threshold);
	if (status != LTTNG_TRIGGER_STATUS_OK) {
		ret = -1;
		goto end;
	}

	ret = offset;
end:
	return ret;
error:
	lttng_condition_destroy(condition);
	lttng_action_destroy(action);
	return ret;
}

/*
 * Both elements are stored contiguously, see their "*_comm" structure
 * for the detailed format.
 */
LTTNG_HIDDEN
int lttng_trigger_serialize(struct lttng_trigger *trigger,
		struct lttng_dynamic_buffer *buf,
		int *fd_to_send)
{
	int ret;
	size_t header_offset, size_before_payload, size_name;
	struct lttng_trigger_comm trigger_comm = { 0 };
	struct lttng_trigger_comm *header;

	header_offset = buf->size;

	if (trigger->name != NULL) {
		size_name = strlen(trigger->name) + 1;
	} else {
		size_name = 0;
	}

	trigger_comm.name_length = size_name;
	trigger_comm.policy_type = (uint8_t) trigger->firing_policy.type;
	trigger_comm.policy_threshold = (uint64_t) trigger->firing_policy.threshold;

	ret = lttng_dynamic_buffer_append(buf, &trigger_comm,
			sizeof(trigger_comm));
	if (ret) {
		goto end;
	}

	size_before_payload = buf->size;

	/* Trigger name */
	ret = lttng_dynamic_buffer_append(buf, trigger->name, size_name);
	if (ret) {
		goto end;
	}

	ret = lttng_condition_serialize(trigger->condition, buf, fd_to_send);
	if (ret) {
		goto end;
	}

	ret = lttng_action_serialize(trigger->action, buf);
	if (ret) {
		goto end;
	}

	/* Update payload size. */
	header = (struct lttng_trigger_comm *) ((char *) buf->data + header_offset);
	header->length = buf->size - size_before_payload;
end:
	return ret;
}

enum lttng_trigger_status lttng_trigger_set_name(struct lttng_trigger *trigger, const char* name)
{
	char *name_copy = NULL;
	enum lttng_trigger_status status = LTTNG_TRIGGER_STATUS_OK;

	if (!trigger || !name ||
			strlen(name) == 0) {
		status = LTTNG_TRIGGER_STATUS_INVALID;
		goto end;
	}

	name_copy = strdup(name);
	if (!name_copy) {
		status = LTTNG_TRIGGER_STATUS_ERROR;
		goto end;
	}

	if (trigger->name) {
		free(trigger->name);
	}

	trigger->name = name_copy;
	name_copy = NULL;
end:
	return status;
}

enum lttng_trigger_status lttng_trigger_get_name(const struct lttng_trigger *trigger, const char **name)
{
	enum lttng_trigger_status status = LTTNG_TRIGGER_STATUS_OK;

	if (!trigger || !name) {
		status = LTTNG_TRIGGER_STATUS_INVALID;
		goto end;
	}

	if (!trigger->name) {
		status = LTTNG_TRIGGER_STATUS_UNSET;
	}

	*name = trigger->name;
end:
	return status;
}

LTTNG_HIDDEN
int lttng_trigger_assign(struct lttng_trigger *dst,
		const struct lttng_trigger *src)
{
	int ret = 0;
	enum lttng_trigger_status status;
	/* todo some validation */

	status = lttng_trigger_set_name(dst, src->name);
	if (status != LTTNG_TRIGGER_STATUS_OK) {
		ret = -1;
		ERR("Failed to set name for trigger");
		goto end;
	}
end:
	return ret;
}

LTTNG_HIDDEN
void lttng_trigger_set_key(struct lttng_trigger *trigger, uint64_t key)
{
	assert(trigger);
	trigger->key.value = key;
	trigger->key.set = true;
}

LTTNG_HIDDEN
uint64_t lttng_trigger_get_key(struct lttng_trigger *trigger)
{
	assert(trigger);

	assert(trigger->key.set == true);
	return trigger->key.value;
}

LTTNG_HIDDEN
int lttng_trigger_generate_name(struct lttng_trigger *trigger, uint64_t offset)
{
	int ret = 0;
	char *generated_name = NULL;
	assert(trigger->key.set);

	ret = asprintf(&generated_name, "TJORAJ%" PRIu64 "", trigger->key.value + offset);
	if (ret < 0) {
		ERR("Failed to generate trigger name");
		ret = -1;
		goto end;
	}

	if (trigger->name) {
		free(trigger->name);
	}
	trigger->name = generated_name;
end:
	return ret;
}

LTTNG_HIDDEN
bool lttng_trigger_is_equal(
		const struct lttng_trigger *a, const struct lttng_trigger *b)
{
	/* TODO: Optimization: for now a trigger with a firing policy that is
	 * not the same even if the conditions and actions is the same is
	 * treated as a "completely" different trigger. In a perfect world we
	 * would simply add a supplemental counter internally (sessiond side) to
	 * remove overhead on the tracer side.
	 */
	if (a->firing_policy.type != b->firing_policy.type) {
		return false;
	}

	if (a->firing_policy.threshold != b->firing_policy.threshold) {
		return false;
	}

	/*
	 * Name is not taken into account since it is cosmetic only
	 */
	if (!lttng_condition_is_equal(a->condition, b->condition)) {
		return false;
	}
	if (!lttng_action_is_equal(a->action, b->action)) {
		return false;
	}

	return true;
}

static void delete_trigger_array_element(void *ptr)
{
	struct lttng_trigger *trigger = ptr;
	lttng_trigger_destroy(trigger);
}

LTTNG_HIDDEN
struct lttng_triggers *lttng_triggers_create(void)
{
	struct lttng_triggers *triggers = NULL;

	triggers = zmalloc(sizeof(*triggers));
	if (!triggers) {
		goto error;
	}

	lttng_dynamic_pointer_array_init(&triggers->array, delete_trigger_array_element);

	return triggers;
error:
	free(triggers);
	return NULL;
}

LTTNG_HIDDEN
struct lttng_trigger *lttng_triggers_get_pointer_of_index(
		const struct lttng_triggers *triggers, unsigned int index)
{
	assert(triggers);
	if (index >= lttng_dynamic_pointer_array_get_count(&triggers->array)) {
		return NULL;
	}
	return lttng_dynamic_pointer_array_get_pointer(&triggers->array, index);
}

LTTNG_HIDDEN
int lttng_triggers_add(
		struct lttng_triggers *triggers, struct lttng_trigger *trigger)
{
	assert(triggers);
	assert(trigger);

	return lttng_dynamic_pointer_array_add_pointer(&triggers->array, trigger);
}

const struct lttng_trigger *lttng_triggers_get_at_index(
		const struct lttng_triggers *triggers, unsigned int index)
{
	assert(triggers);
	return lttng_triggers_get_pointer_of_index(triggers, index);
}

enum lttng_trigger_status lttng_triggers_get_count(const struct lttng_triggers *triggers, unsigned int *count)
{
	enum lttng_trigger_status status = LTTNG_TRIGGER_STATUS_OK;

	if (!triggers || !count) {
		status = LTTNG_TRIGGER_STATUS_INVALID;
		goto end;
	}

	*count = lttng_dynamic_pointer_array_get_count(&triggers->array);
end:
	return status;
}

void lttng_triggers_destroy(struct lttng_triggers *triggers)
{
	if (!triggers) {
		return;
	}

	lttng_dynamic_pointer_array_reset(&triggers->array);
	free(triggers);
}

int lttng_triggers_serialize(const struct lttng_triggers *triggers,
		struct lttng_dynamic_buffer *buffer)
{
	int ret;
	unsigned int count;
	size_t header_offset, size_before_payload;
	struct lttng_triggers_comm triggers_comm = { 0 };
	struct lttng_triggers_comm *header;
	struct lttng_trigger *trigger;
	enum lttng_trigger_status status;

	header_offset = buffer->size;

	status = lttng_triggers_get_count(triggers, &count);
	if (status != LTTNG_TRIGGER_STATUS_OK) {
		ret = LTTNG_ERR_INVALID;
		goto end;
	}

	triggers_comm.count = count;

	ret = lttng_dynamic_buffer_append(buffer, &triggers_comm,
			sizeof(triggers_comm));
	if (ret) {
		goto end;
	}

	size_before_payload = buffer->size;

	for (int i = 0; i < count; i++) {
		trigger = lttng_triggers_get_pointer_of_index(triggers, i);
		if (!trigger) {
			assert(0);
		}

		ret = lttng_trigger_serialize(trigger, buffer, NULL);
		if (ret) {
			goto end;
		}
	}

	/* Update payload size. */
	header = (struct lttng_triggers_comm *) ((char *) buffer->data + header_offset);
	header->length = buffer->size - size_before_payload;
end:
	return ret;
}

LTTNG_HIDDEN
ssize_t lttng_triggers_create_from_buffer(
		const struct lttng_buffer_view *src_view,
		struct lttng_triggers **triggers)
{
	ssize_t ret, offset = 0, trigger_size, triggers_size = 0;
	const struct lttng_triggers_comm *triggers_comm;
	struct lttng_buffer_view trigger_view;
	struct lttng_triggers *local_triggers = NULL;

	if (!src_view || !triggers) {
		ret = -1;
		goto error;
	}

	/* lttng_trigger_comms header */
	triggers_comm = (const struct lttng_triggers_comm *) src_view->data;
	offset += sizeof(*triggers_comm);

	local_triggers = lttng_triggers_create();
	if (!local_triggers) {
		ret = -1;
		goto error;
	}

	for (int i = 0; i < triggers_comm->count; i++) {
		struct lttng_trigger *trigger = NULL;
		trigger_view = lttng_buffer_view_from_view(src_view, offset, -1);
		trigger_size = lttng_trigger_create_from_buffer(&trigger_view,
				&trigger);
		if (trigger_size  < 0) {
			lttng_trigger_destroy(trigger);
			ret = trigger_size;
			goto error;
		}
		
		/* Pass ownership of the trigger to the collection */
		ret = lttng_triggers_add(local_triggers, trigger);
		if (ret < 0) {
			assert(0);
		}
		trigger = NULL;

		offset += trigger_size;
		triggers_size += trigger_size;
	}

	/* Unexpected size of inner-elements; the buffer is corrupted. */
	if ((ssize_t) triggers_comm->length != triggers_size) {
		ret = -1;
		goto error;
	}

	/* Pass ownership to caller */
	*triggers = local_triggers;
	local_triggers = NULL;

	ret = offset;
error:

	lttng_triggers_destroy(local_triggers);
	return ret;
}

LTTNG_HIDDEN
const struct lttng_credentials *lttng_trigger_get_credentials(
		const struct lttng_trigger *trigger)
{
	assert(trigger->creds.set);
	return &(trigger->creds.credentials);
}

LTTNG_HIDDEN
void lttng_trigger_set_credentials(
		struct lttng_trigger *trigger, uid_t uid, gid_t gid)
{
	trigger->creds.credentials.uid = uid;
	trigger->creds.credentials.gid = gid;
	trigger->creds.set = true;
}

enum lttng_trigger_status lttng_trigger_set_firing_policy(
		struct lttng_trigger *trigger,
		enum lttng_trigger_firing_policy_type policy_type,
		unsigned long long threshold)
{
	enum lttng_trigger_status ret = LTTNG_TRIGGER_STATUS_OK;
	assert(trigger);

	if (threshold < 1) {
		ret = LTTNG_TRIGGER_STATUS_INVALID;
		goto end;
	}

	trigger->firing_policy.type = policy_type;
	trigger->firing_policy.threshold = threshold;

end:
	return ret;
}

LTTNG_HIDDEN
bool lttng_trigger_is_ready_to_fire(struct lttng_trigger *trigger)
{
	assert(trigger);
	bool ready_to_fire = false;

	trigger->firing_policy.current_count++;

	switch (trigger->firing_policy.type) {
	case LTTNG_TRIGGER_FIRE_EVERY_N:
		if (trigger->firing_policy.current_count == trigger->firing_policy.threshold) {
			trigger->firing_policy.current_count = 0;
			ready_to_fire = true;
		}
		break;
	case LTTNG_TRIGGER_FIRE_ONCE_AFTER_N:
		if (trigger->firing_policy.current_count == trigger->firing_policy.threshold) {
			/* TODO: remove the trigger of at least deactivate it on
			 * the tracers side to remove any work overhead on the
			 * traced application or kernel since the trigger will
			 * never fire again.
			 * Still this branch should be left here since event
			 * could still be in the pipe. These will be discarded.
			 */
			ready_to_fire = true;
		}
		break;
	default:
		assert(0);
	};

	return ready_to_fire;
}

LTTNG_HIDDEN
void lttng_trigger_get(struct lttng_trigger *trigger)
{
	urcu_ref_get(&trigger->ref);
}

LTTNG_HIDDEN
void lttng_trigger_put(struct lttng_trigger *trigger)
{
	if (!trigger) {
		return;
	}

	urcu_ref_put(&trigger->ref , trigger_destroy_ref);
}
