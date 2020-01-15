/*
 * Copyright (C) 2019 EfficiOS, Inc.
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

#include <assert.h>
#include <common/error.h>
#include <common/macros.h>
#include <lttng/action/action-internal.h>
#include <lttng/action/group-internal.h>
#include <lttng/action/group.h>

struct lttng_action_group {
	struct lttng_action parent;

	/* Owned by this. */
	struct lttng_action **elements;
	size_t n_elements;
	size_t n_elements_allocated;
};

struct lttng_action_group_comm {
	uint32_t n_elements;

	/*
	 * Variable data: each element serialized sequentially.
	 */
	char data[];
} LTTNG_PACKED;

static struct lttng_action_group *action_group_from_action(
		const struct lttng_action *action)
{
	assert(action);

	return container_of(action, struct lttng_action_group, parent);
}

static bool lttng_action_group_validate(struct lttng_action *action)
{
	size_t i;
	struct lttng_action_group *action_group;
	bool valid;

	assert(lttng_action_get_type(action) == LTTNG_ACTION_TYPE_GROUP);

	action_group = action_group_from_action(action);

	for (i = 0; i < action_group->n_elements; i++) {
		struct lttng_action *child = action_group->elements[i];

		assert(child);

		if (!lttng_action_validate(child)) {
			valid = false;
			goto end;
		}
	}

	valid = true;

end:
	return valid;
}

static bool lttng_action_group_is_equal(const struct lttng_action *_a, const struct lttng_action *_b)
{
	bool is_equal = false;
	struct lttng_action_group *a, *b;
	size_t i;

	assert(lttng_action_get_type_const(_a) == LTTNG_ACTION_TYPE_GROUP);
	assert(lttng_action_get_type_const(_b) == LTTNG_ACTION_TYPE_GROUP);

	a = action_group_from_action(_a);
	b = action_group_from_action(_b);

	if (a->n_elements != b->n_elements) {
		goto end;
	}

	for (i = 0; i < a->n_elements; i++) {
		struct lttng_action *child_a = a->elements[i];
		struct lttng_action *child_b = b->elements[i];

		assert(child_a);
		assert(child_b);

		if (!lttng_action_is_equal(child_a, child_b)) {
			goto end;
		}
	}

	is_equal = true;
end:
	return is_equal;
}

static int lttng_action_group_serialize(
		struct lttng_action *action, struct lttng_dynamic_buffer *buf)
{
	struct lttng_action_group *action_group;
	struct lttng_action_group_comm comm;
	int ret;
	size_t i;

	assert(action);
	assert(buf);
	assert(lttng_action_get_type(action) == LTTNG_ACTION_TYPE_GROUP);

	action_group = action_group_from_action(action);

	DBG("Serializing action group");

	comm.n_elements = action_group->n_elements;

	ret = lttng_dynamic_buffer_append(buf, &comm, sizeof(comm));
	if (ret) {
		ret = -1;
		goto end;
	}

	for (i = 0; i < action_group->n_elements; i++) {
		struct lttng_action *child = action_group->elements[i];

		assert(child);

		ret = lttng_action_serialize(child, buf);
		if (ret) {
			goto end;
		}
	}

	ret = 0;

end:
	return ret;
}

static void lttng_action_group_destroy(struct lttng_action *action)
{
	struct lttng_action_group *action_group;
	size_t i;

	if (!action) {
		goto end;
	}

	action_group = action_group_from_action(action);

	for (i = 0; i < action_group->n_elements; i++) {
		struct lttng_action *child = action_group->elements[i];

		assert(child);

		lttng_action_destroy(child);
	}

	free(action_group->elements);
	free(action_group);

end:
	return;
}

ssize_t lttng_action_group_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_action **p_action)
{
	ssize_t consumed_len;
	struct lttng_action_group_comm *comm;
	struct lttng_action *group;
	struct lttng_action *child_action = NULL;
	enum lttng_action_status status;
	size_t i;

	group = lttng_action_group_create();
	if (!group) {
		consumed_len = -1;
		goto end;
	}

	comm = (struct lttng_action_group_comm *) view->data;

	consumed_len = sizeof(struct lttng_action_group_comm);

	for (i = 0; i < comm->n_elements; i++) {
		ssize_t consumed_len_child;
		struct lttng_buffer_view child_view;

		child_view = lttng_buffer_view_from_view(
				view, consumed_len, view->size - consumed_len);
		consumed_len_child = lttng_action_create_from_buffer(
				&child_view, &child_action);

		status = lttng_action_group_add_action(group, child_action);
		if (status != LTTNG_ACTION_STATUS_OK) {
			consumed_len = -1;
			goto end;
		}
		child_action = NULL;

		consumed_len += consumed_len_child;
	}

	*p_action = group;
	group = NULL;

end:
	lttng_action_group_destroy(group);
	lttng_action_destroy(child_action);

	return consumed_len;
}

struct lttng_action *lttng_action_group_create(void)
{
	struct lttng_action_group *action_group;
	struct lttng_action *action;

	action_group = zmalloc(sizeof(struct lttng_action_group));
	if (!action_group) {
		action = NULL;
		goto end;
	}

	action = &action_group->parent;

	lttng_action_init(action, LTTNG_ACTION_TYPE_GROUP,
			lttng_action_group_validate,
			lttng_action_group_serialize,
			lttng_action_group_is_equal,
			lttng_action_group_destroy);

	action_group->n_elements = 0;
	action_group->n_elements_allocated = 10;
	action_group->elements = zmalloc(10 * sizeof(struct lttng_action *));
	if (!action_group->elements) {
		goto error;
	}

	goto end;

error:
	lttng_action_group_destroy(action);
	action = NULL;

end:
	return action;
}

enum lttng_action_status lttng_action_group_add_action(
		struct lttng_action *group, struct lttng_action *action)
{
	struct lttng_action_group *action_group;
	enum lttng_action_status status;

	if (!group ||
			(lttng_action_get_type(group) !=
					LTTNG_ACTION_TYPE_GROUP) ||
			!action) {
		status = LTTNG_ACTION_STATUS_INVALID;
		goto end;
	}

	/*
	 * Don't allow adding groups in groups for now, since we're afraid of
	 * cycles.
	 */
	if (lttng_action_get_type(action) == LTTNG_ACTION_TYPE_GROUP) {
		status = LTTNG_ACTION_STATUS_INVALID;
		goto end;
	}

	action_group = action_group_from_action(group);

	if (action_group->n_elements == action_group->n_elements_allocated) {
		// TODO: handle resize.
		status = LTTNG_ACTION_STATUS_ERROR;
		goto end;
	}

	action_group->elements[action_group->n_elements] = action;
	action_group->n_elements++;

	status = LTTNG_ACTION_STATUS_OK;
end:
	return status;
}

LTTNG_HIDDEN
enum lttng_action_status lttng_action_group_get_count(
		const struct lttng_action *group, unsigned int *count)
{
	struct lttng_action_group *action_group;
	enum lttng_action_status status = LTTNG_ACTION_STATUS_OK;

	if (!group || (lttng_action_get_type_const(group) !=
				      LTTNG_ACTION_TYPE_GROUP)) {
		status = LTTNG_ACTION_STATUS_INVALID;
		*count = 0;
		goto end;
	}

	action_group = action_group_from_action(group);


	*count = action_group->n_elements;
end:
	return status;
}

LTTNG_HIDDEN
const struct lttng_action *lttng_action_group_get_at_index(
		const struct lttng_action *group,
		unsigned int index)
{
	struct lttng_action_group *action_group;
	const struct lttng_action *action = NULL;

	if (!group || (lttng_action_get_type_const(group) !=
				      LTTNG_ACTION_TYPE_GROUP)) {
		goto end;
	}

	action_group = action_group_from_action(group);

	if (index >= action_group->n_elements) {
		goto end;
	}

	action = action_group->elements[index];
end:
	return action;
}
