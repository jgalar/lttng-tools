/*
 * Copyright (C) 2019 - Jonathan Rajotte-Julien <jonathan.rajotte-julien@efficios.com>
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

#include <lttng/event-rule/event-rule-internal.h>
#include <lttng/event-rule/event-rule-uprobe-internal.h>
#include <lttng/userspace-probe-internal.h>
#include <common/macros.h>
#include <common/error.h>
#include <common/runas.h>
#include <assert.h>

#define IS_UPROBE_EVENT_RULE(rule) ( \
	lttng_event_rule_get_type(rule) == LTTNG_EVENT_RULE_TYPE_UPROBE \
	)

static
void lttng_event_rule_uprobe_destroy(struct lttng_event_rule *rule)
{
	struct lttng_event_rule_uprobe *uprobe;

	uprobe = container_of(rule, struct lttng_event_rule_uprobe,
			parent);

	lttng_userspace_probe_location_destroy(uprobe->location);
	free(uprobe->name);
	free(uprobe);
}

static
bool lttng_event_rule_uprobe_validate(
		const struct lttng_event_rule *rule)
{
	bool valid = false;
	struct lttng_event_rule_uprobe *uprobe;

	if (!rule) {
		goto end;
	}

	uprobe = container_of(rule, struct lttng_event_rule_uprobe,
			parent);

	/* Required field */
	if (!uprobe->name) {
		ERR("Invalid uprobe event rule: a pattern must be set.");
		goto end;
	}

	if (!uprobe->location) {
		ERR("Invalid uprobe event rule: a location must be set.");
		goto end;
	}

	/* TODO should we validate the probe location? */

	valid = true;
end:
	return valid;
}

static
int lttng_event_rule_uprobe_serialize(
		const struct lttng_event_rule *rule,
		struct lttng_dynamic_buffer *buf,
		int *fd_to_send)
{
	int ret;
	size_t name_len, header_offset, size_before_probe;
	struct lttng_event_rule_uprobe *uprobe;
	struct lttng_event_rule_uprobe_comm uprobe_comm = { 0 };
	struct lttng_event_rule_uprobe_comm *header;
	int local_fd_to_send;

	if (!rule || !IS_UPROBE_EVENT_RULE(rule)) {
		ret = -1;
		goto end;
	}

	header_offset = buf->size;

	DBG("Serializing uprobe event rule");
	uprobe = container_of(
			rule, struct lttng_event_rule_uprobe, parent);

	name_len = strlen(uprobe->name) + 1;

	uprobe_comm.name_len = name_len;

	ret = lttng_dynamic_buffer_append(
			buf, &uprobe_comm, sizeof(uprobe_comm));
	if (ret) {
		goto end;
	}
	ret = lttng_dynamic_buffer_append(buf, uprobe->name,
			name_len);
	if (ret) {
		goto end;
	}

	size_before_probe = buf->size;

	/* This serialize return the size taken in the buffer */
	/* TODO: should all serialize standardise on this? */
	ret = lttng_userspace_probe_location_serialize(
			uprobe->location, buf, &local_fd_to_send);
	if (ret < 0) {
		goto end;
	}

	/* Update the header regarding the probe size */
	header = (struct lttng_event_rule_uprobe_comm *) ((char *) buf->data + header_offset);
	header->location_len = buf->size - size_before_probe;

	if (fd_to_send) {
		*fd_to_send = local_fd_to_send;
	}

	ret = 0;

end:
	return ret;
}

static
bool lttng_event_rule_uprobe_is_equal(const struct lttng_event_rule *_a,
		const struct lttng_event_rule *_b)
{
	bool is_equal = false;
	struct lttng_event_rule_uprobe *a, *b;

	a = container_of(_a, struct lttng_event_rule_uprobe, parent);
	b = container_of(_b, struct lttng_event_rule_uprobe, parent);

	/* uprobe is invalid if this is not true */
	assert(a->name);
	assert(b->name);
	if (strcmp(a->name, b->name)) {
		goto end;
	}

	assert(a->location);
	assert(b->location);
	is_equal = lttng_userspace_probe_location_is_equal(a->location, b->location);
end:
	return is_equal;
}

static
enum lttng_error_code lttng_event_rule_uprobe_populate(struct lttng_event_rule *rule, uid_t uid, gid_t gid)
{
	/* Nothing to do */
	return LTTNG_OK;
}

static
char *lttng_event_rule_uprobe_get_filter(struct lttng_event_rule *rule)
{
	/* Unsupported */
	return NULL;
}

static
struct lttng_filter_bytecode *lttng_event_rule_uprobe_get_filter_bytecode(struct lttng_event_rule *rule)
{
	/* Unsupported */
	return NULL;
}

static
struct lttng_event_exclusion *lttng_event_rule_uprobe_generate_exclusions(struct lttng_event_rule *rule)
{
	/* Unsupported */
	return NULL;
}

struct lttng_event_rule *lttng_event_rule_uprobe_create()
{
	struct lttng_event_rule_uprobe *rule;

	rule = zmalloc(sizeof(struct lttng_event_rule_uprobe));
	if (!rule) {
		return NULL;
	}

	lttng_event_rule_init(&rule->parent, LTTNG_EVENT_RULE_TYPE_UPROBE);
	rule->parent.validate = lttng_event_rule_uprobe_validate;
	rule->parent.serialize = lttng_event_rule_uprobe_serialize;
	rule->parent.equal = lttng_event_rule_uprobe_is_equal;
	rule->parent.destroy = lttng_event_rule_uprobe_destroy;
	rule->parent.populate = lttng_event_rule_uprobe_populate;
	rule->parent.get_filter = lttng_event_rule_uprobe_get_filter;
	rule->parent.get_filter_bytecode = lttng_event_rule_uprobe_get_filter_bytecode;
	rule->parent.generate_exclusions = lttng_event_rule_uprobe_generate_exclusions;
	return &rule->parent;
}

LTTNG_HIDDEN
ssize_t lttng_event_rule_uprobe_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_event_rule **_event_rule)
{
	ssize_t ret, offset = 0;
	const struct lttng_event_rule_uprobe_comm *uprobe_comm;
	const char *name;
	struct lttng_buffer_view current_view;
	struct lttng_event_rule *rule = NULL;
	struct lttng_userspace_probe_location *location;
	struct lttng_event_rule_uprobe *uprobe;

	if (!_event_rule) {
		ret = -1;
		goto end;
	}

	if (view->size < sizeof(*uprobe_comm)) {
		ERR("Failed to initialize from malformed event rule uprobe: buffer too short to contain header");
		ret = -1;
		goto end;
	}

	current_view = lttng_buffer_view_from_view(view, offset, sizeof(*uprobe_comm));
	uprobe_comm = (typeof(uprobe_comm)) current_view.data;

	if(!uprobe_comm) {
		ret = -1;
		goto end;
	}

	rule = lttng_event_rule_uprobe_create();
	if (!rule) {
		ERR("Failed to create event rule uprobe");
		ret = -1;
		goto end;
	}

	/* Skip to payload */
	offset += current_view.size;

	/* Map the name */
	current_view = lttng_buffer_view_from_view(view, offset, uprobe_comm->name_len);
	name = current_view.data;
	if (!name) {
		ret = -1;
		goto end;
	}

	if (uprobe_comm->name_len == 1 ||
			name[uprobe_comm->name_len - 1] != '\0' ||
			strlen(name) != uprobe_comm->name_len - 1) {
		/*
		 * Check that the name is not NULL, is NULL-terminated, and
		 * does not contain a NULL before the last byte.
		 */
		ret = -1;
		goto end;
	}

	/* Skip after the name */
	offset += uprobe_comm->name_len;

	/* Map the location */
	current_view = lttng_buffer_view_from_view(view, offset, uprobe_comm->location_len);
	ret = lttng_userspace_probe_location_create_from_buffer(&current_view, &location);
	if (ret < 0) {
		ret = -1;
		goto end;
	}

	assert(ret == uprobe_comm->location_len);

	/* Skip after the location */
	offset += uprobe_comm->location_len;

	uprobe = container_of(rule, struct lttng_event_rule_uprobe, parent);
	uprobe->location = location;

	*_event_rule = rule;
	rule = NULL;
	ret = offset;
end:
	lttng_event_rule_destroy(rule);
	return ret;
}

enum lttng_event_rule_status lttng_event_rule_uprobe_set_location(
		struct lttng_event_rule *rule,
		const struct lttng_userspace_probe_location *location)
{
	struct lttng_userspace_probe_location *location_copy = NULL;
	struct lttng_event_rule_uprobe *uprobe;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_UPROBE_EVENT_RULE(rule) || !location) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	uprobe = container_of(rule, struct lttng_event_rule_uprobe,
			parent);
	location_copy = lttng_userspace_probe_location_copy(location);
	if (!location_copy) {
		status = LTTNG_EVENT_RULE_STATUS_ERROR;
		goto end;
	}

	if (uprobe->location) {
		lttng_userspace_probe_location_destroy(uprobe->location);
	}

	uprobe->location = location_copy;
	location_copy = NULL;
end:
	lttng_userspace_probe_location_destroy(location_copy);
	return status;
}

LTTNG_HIDDEN
struct lttng_userspace_probe_location *
lttng_event_rule_uprobe_get_location_no_const(
		const struct lttng_event_rule *rule)
{
	struct lttng_event_rule_uprobe *uprobe;
	assert(rule);
	uprobe = container_of(rule, struct lttng_event_rule_uprobe,
			parent);

	return uprobe->location;
}

enum lttng_event_rule_status lttng_event_rule_uprobe_set_name(
		struct lttng_event_rule *rule, const char *name)
{
	char *name_copy = NULL;
	struct lttng_event_rule_uprobe *uprobe;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_UPROBE_EVENT_RULE(rule) || !name ||
			strlen(name) == 0) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	uprobe = container_of(rule, struct lttng_event_rule_uprobe,
			parent);
	name_copy = strdup(name);
	if (!name_copy) {
		status = LTTNG_EVENT_RULE_STATUS_ERROR;
		goto end;
	}

	if (uprobe->name) {
		free(uprobe->name);
	}

	uprobe->name = name_copy;
	name_copy = NULL;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_uprobe_get_name(
		const struct lttng_event_rule *rule, const char **name)
{
	struct lttng_event_rule_uprobe *uprobe;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_UPROBE_EVENT_RULE(rule) || !name) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	uprobe = container_of(rule, struct lttng_event_rule_uprobe,
			parent);
	if (!uprobe->name) {
		status = LTTNG_EVENT_RULE_STATUS_UNSET;
		goto end;
	}

	*name = uprobe->name;
end:
	return status;
}
