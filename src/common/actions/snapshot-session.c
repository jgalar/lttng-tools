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
#include <lttng/action/snapshot-session-internal.h>
#include <lttng/action/snapshot-session.h>

struct lttng_action_snapshot_session {
	struct lttng_action parent;

	/* Owned by this. */
	char *session_name;

	/* Owned by this. */
	char *snapshot_name;
};

struct lttng_action_snapshot_session_comm {
	/* Includes the trailing \0. */
	uint32_t session_name_len;

	/* Includes the trailing \0. */
	uint32_t snapshot_name_len;

	/*
	 * Variable data:
	 *
	 *  - session name (null terminated)
	 *  - snapshot_name_len (null terminated), if snapshot_name_len is
	 *    greater than 0.
	 */
	char data[];
} LTTNG_PACKED;

static struct lttng_action_snapshot_session *
action_snapshot_session_from_action(struct lttng_action *action)
{
	assert(action);

	return container_of(
			action, struct lttng_action_snapshot_session, parent);
}

static bool lttng_action_snapshot_session_validate(struct lttng_action *action)
{
	bool valid;
	struct lttng_action_snapshot_session *action_snapshot_session;

	if (!action) {
		valid = false;
		goto end;
	}

	action_snapshot_session = action_snapshot_session_from_action(action);

	/* A non-empty session name is mandatory. */
	if (!action_snapshot_session->session_name ||
			strlen(action_snapshot_session->session_name) == 0) {
		valid = false;
		goto end;
	}

	/* If a snapshot name is provided, it must not be an empty string. */
	if (action_snapshot_session->snapshot_name &&
			strlen(action_snapshot_session->snapshot_name) == 0) {
		valid = false;
		goto end;
	}

	valid = true;
end:
	return valid;
}

static int lttng_action_snapshot_session_serialize(
		struct lttng_action *action, struct lttng_dynamic_buffer *buf)
{
	struct lttng_action_snapshot_session *action_snapshot_session;
	struct lttng_action_snapshot_session_comm comm;
	size_t session_name_len, snapshot_name_len;
	int ret;

	assert(action);
	assert(buf);

	action_snapshot_session = action_snapshot_session_from_action(action);

	assert(action_snapshot_session->session_name);

	DBG("Serializing snapshot session action: session-name: %s",
			action_snapshot_session->session_name);

	session_name_len = strlen(action_snapshot_session->session_name) + 1;
	comm.session_name_len = session_name_len;

	snapshot_name_len = (action_snapshot_session->snapshot_name ?
					     (strlen(action_snapshot_session->snapshot_name) +
							     1) :
					     0);
	comm.snapshot_name_len = snapshot_name_len;

	ret = lttng_dynamic_buffer_append(buf, &comm, sizeof(comm));
	if (ret) {
		ret = -1;
		goto end;
	}

	ret = lttng_dynamic_buffer_append(buf,
			action_snapshot_session->session_name,
			session_name_len);
	if (ret) {
		ret = -1;
		goto end;
	}

	ret = lttng_dynamic_buffer_append(buf,
			action_snapshot_session->snapshot_name,
			snapshot_name_len);
	if (ret) {
		ret = -1;
		goto end;
	}

	ret = 0;
end:
	return ret;
}

static void lttng_action_snapshot_session_destroy(struct lttng_action *action)
{
	struct lttng_action_snapshot_session *action_snapshot_session;

	if (!action) {
		goto end;
	}

	action_snapshot_session = action_snapshot_session_from_action(action);

	free(action_snapshot_session->session_name);
	free(action_snapshot_session->snapshot_name);
	free(action_snapshot_session);

end:
	return;
}

ssize_t lttng_action_snapshot_session_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_action **p_action)
{
	ssize_t consumed_len;
	struct lttng_action_snapshot_session_comm *comm;
	const char *variable_data;
	struct lttng_action *action;
	enum lttng_action_status status;

	action = lttng_action_snapshot_session_create();
	if (!action) {
		consumed_len = -1;
		goto end;
	}

	comm = (struct lttng_action_snapshot_session_comm *) view->data;
	variable_data = (const char *) &comm->data;

	if (!lttng_buffer_view_validate_string(
			    view, variable_data, comm->session_name_len)) {
		consumed_len = -1;
		goto end;
	}

	status = lttng_action_snapshot_session_set_session_name(
			action, variable_data);
	if (status != LTTNG_ACTION_STATUS_OK) {
		consumed_len = -1;
		goto end;
	}

	variable_data += comm->session_name_len;

	if (comm->snapshot_name_len > 0) {
		if (!lttng_buffer_view_validate_string(view, variable_data,
				    comm->snapshot_name_len)) {
			consumed_len = -1;
			goto end;
		}

		status = lttng_action_snapshot_session_set_snapshot_name(
				action, variable_data);
		if (status != LTTNG_ACTION_STATUS_OK) {
			consumed_len = -1;
			goto end;
		}
	}

	consumed_len = sizeof(struct lttng_action_snapshot_session_comm) +
		       comm->session_name_len + comm->snapshot_name_len;
	*p_action = action;
	action = NULL;

end:
	lttng_action_snapshot_session_destroy(action);

	return consumed_len;
}

struct lttng_action *lttng_action_snapshot_session_create(void)
{
	struct lttng_action *action;

	action = zmalloc(sizeof(struct lttng_action_snapshot_session));
	if (!action) {
		goto end;
	}

	lttng_action_init(action, LTTNG_ACTION_TYPE_SNAPSHOT_SESSION,
			lttng_action_snapshot_session_validate,
			lttng_action_snapshot_session_serialize,
			lttng_action_snapshot_session_destroy);

end:
	return action;
}

extern enum lttng_action_status lttng_action_snapshot_session_set_session_name(
		struct lttng_action *action, const char *session_name)
{
	struct lttng_action_snapshot_session *action_snapshot_session;
	enum lttng_action_status status;

	if (!action || !session_name || strlen(session_name) == 0) {
		status = LTTNG_ACTION_STATUS_INVALID;
		goto end;
	}

	action_snapshot_session = action_snapshot_session_from_action(action);

	free(action_snapshot_session->session_name);

	action_snapshot_session->session_name = strdup(session_name);
	if (!action_snapshot_session->session_name) {
		status = LTTNG_ACTION_STATUS_ERROR;
		goto end;
	}

	status = LTTNG_ACTION_STATUS_OK;
end:
	return status;
}

extern enum lttng_action_status lttng_action_snapshot_session_set_snapshot_name(
		struct lttng_action *action, const char *snapshot_name)
{
	struct lttng_action_snapshot_session *action_snapshot_session;
	enum lttng_action_status status;

	if (!action || (snapshot_name && strlen(snapshot_name) == 0)) {
		status = LTTNG_ACTION_STATUS_INVALID;
		goto end;
	}

	action_snapshot_session = action_snapshot_session_from_action(action);

	free(action_snapshot_session->snapshot_name);

	if (snapshot_name) {
		action_snapshot_session->snapshot_name = strdup(snapshot_name);
		if (!action_snapshot_session->snapshot_name) {
			status = LTTNG_ACTION_STATUS_ERROR;
			goto end;
		}
	} else {
		action_snapshot_session->snapshot_name = NULL;
	}

	status = LTTNG_ACTION_STATUS_OK;
end:
	return status;
}
