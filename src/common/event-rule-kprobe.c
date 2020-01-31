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

#include <lttng/constant.h>
#include <lttng/event-rule/event-rule-internal.h>
#include <lttng/event-rule/event-rule-kprobe-internal.h>
#include <common/macros.h>
#include <common/error.h>
#include <common/runas.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>

#define IS_KPROBE_EVENT_RULE(rule) ( \
	lttng_event_rule_get_type(rule) == LTTNG_EVENT_RULE_TYPE_KPROBE \
	)

#if (LTTNG_SYMBOL_NAME_LEN == 256)
#define LTTNG_SYMBOL_NAME_LEN_SCANF_IS_A_BROKEN_API "255"
#endif

static
void lttng_event_rule_kprobe_destroy(struct lttng_event_rule *rule)
{
	struct lttng_event_rule_kprobe *kprobe;

	kprobe = container_of(rule, struct lttng_event_rule_kprobe,
			parent);

	free(kprobe->name);
	free(kprobe->probe.symbol_name);
	free(kprobe);
}

static
bool lttng_event_rule_kprobe_validate(
		const struct lttng_event_rule *rule)
{
	bool valid = false;
	struct lttng_event_rule_kprobe *kprobe;

	if (!rule) {
		goto end;
	}

	kprobe = container_of(rule, struct lttng_event_rule_kprobe, parent);

	/* Required field */
	if (!kprobe->name) {
		ERR("Invalid name event rule: a name must be set.");
		goto end;
	}
	if (kprobe->probe.set == LTTNG_DOMAIN_NONE) {
		ERR("Invalid kprobe event rule: a source must be set.");
		goto end;
	}

	valid = true;
end:
	return valid;
}

static
int lttng_event_rule_kprobe_serialize(
		const struct lttng_event_rule *rule,
		struct lttng_dynamic_buffer *buf,
		int *fd_to_send)
{
	int ret;
	size_t name_len, probe_symbol_name_len;
	struct lttng_event_rule_kprobe *kprobe;
	struct lttng_event_rule_kprobe_comm kprobe_comm;

	if (!rule || !IS_KPROBE_EVENT_RULE(rule)) {
		ret = -1;
		goto end;
	}

	DBG("Serializing kprobe event rule");
	kprobe = container_of(rule, struct lttng_event_rule_kprobe, parent);

	name_len = strlen(kprobe->name) + 1;

	if (kprobe->probe.symbol_name != NULL) {
		probe_symbol_name_len = strlen(kprobe->probe.symbol_name) + 1;
	} else {
		probe_symbol_name_len = 0;
	}

	kprobe_comm.name_len = name_len;
	kprobe_comm.probe_symbol_name_len = probe_symbol_name_len;
	kprobe_comm.probe_address = kprobe->probe.address;
	kprobe_comm.probe_offset = kprobe->probe.offset;

	ret = lttng_dynamic_buffer_append(
			buf, &kprobe_comm, sizeof(kprobe_comm));
	if (ret) {
		goto end;
	}
	ret = lttng_dynamic_buffer_append(buf, kprobe->name, name_len);
	if (ret) {
		goto end;
	}
	ret = lttng_dynamic_buffer_append(
			buf, kprobe->probe.symbol_name, probe_symbol_name_len);
	if (ret) {
		goto end;
	}

	if (fd_to_send) {
		/* Nothing to send */
		*fd_to_send = -1;
	}
end:
	return ret;
}

static
bool lttng_event_rule_kprobe_is_equal(const struct lttng_event_rule *_a,
		const struct lttng_event_rule *_b)
{
	bool is_equal = false;
	struct lttng_event_rule_kprobe *a, *b;

	a = container_of(_a, struct lttng_event_rule_kprobe, parent);
	b = container_of(_b, struct lttng_event_rule_kprobe, parent);

	/* Quick checks */
	if (!!a->name != !!b->name) {
		goto end;
	}

	if (!!a->probe.symbol_name != !!b->probe.symbol_name) {
		goto end;
	}

	/* Long check */
	/* kprobe is invalid if this is not true */
	/* TODO: validate that a kprobe MUST have a name */
	assert(a->name);
	assert(b->name);
	if (strcmp(a->name, b->name)) {
		goto end;
	}

	if (a->probe.symbol_name) {
		/* Both have symbol name due to previous checks */
		if (strcmp(a->probe.symbol_name, b->probe.symbol_name)) {
			goto end;
		}
	}

	if (a->probe.offset != b->probe.offset) {
		goto end;
	}

	if (a->probe.address != b->probe.address) {
		goto end;
	}

	is_equal = true;
end:
	return is_equal;
}

static
enum lttng_error_code lttng_event_rule_kprobe_populate(struct lttng_event_rule *rule, uid_t uid, gid_t gid)
{
	/* Nothing to do */
	return LTTNG_OK;
}

static
char *lttng_event_rule_kprobe_get_filter(struct lttng_event_rule *rule)
{
	/* Not supported */
	return NULL;
}

static
struct lttng_filter_bytecode *lttng_event_rule_kprobe_get_filter_bytecode(struct lttng_event_rule *rule)
{
	/* Not supported */
	return NULL;
}

struct lttng_event_rule *lttng_event_rule_kprobe_create()
{
	struct lttng_event_rule_kprobe *rule;

	rule = zmalloc(sizeof(struct lttng_event_rule_kprobe));
	if (!rule) {
		return NULL;
	}

	lttng_event_rule_init(&rule->parent, LTTNG_EVENT_RULE_TYPE_KPROBE);
	rule->parent.validate = lttng_event_rule_kprobe_validate;
	rule->parent.serialize = lttng_event_rule_kprobe_serialize;
	rule->parent.equal = lttng_event_rule_kprobe_is_equal;
	rule->parent.destroy = lttng_event_rule_kprobe_destroy;
	rule->parent.populate = lttng_event_rule_kprobe_populate;
	rule->parent.get_filter = lttng_event_rule_kprobe_get_filter;
	rule->parent.get_filter_bytecode = lttng_event_rule_kprobe_get_filter_bytecode;
	return &rule->parent;
}

LTTNG_HIDDEN
ssize_t lttng_event_rule_kprobe_create_from_buffer(
		const struct lttng_buffer_view *view,
		struct lttng_event_rule **_event_rule)
{
	ssize_t ret, offset = 0;
	enum lttng_event_rule_status status;
	const struct lttng_event_rule_kprobe_comm *kprobe_comm;
	const char *name;
	const char *probe_symbol_name = NULL;
	struct lttng_buffer_view current_view;
	struct lttng_event_rule *rule = NULL;
	struct lttng_event_rule_kprobe *kprobe = NULL;

	if (!_event_rule) {
		ret = -1;
		goto end;
	}

	if (view->size < sizeof(*kprobe_comm)) {
		ERR("Failed to initialize from malformed event rule kprobe: buffer too short to contain header");
		ret = -1;
		goto end;
	}

	current_view = lttng_buffer_view_from_view(
			view, offset, sizeof(*kprobe_comm));
	kprobe_comm = (typeof(kprobe_comm)) current_view.data;
	if (!kprobe_comm) {
		ret = -1;
		goto end;
	}

	rule = lttng_event_rule_kprobe_create();
	if (!rule) {
		ERR("Failed to create event rule kprobe");
		ret = -1;
		goto end;
	}

	kprobe = container_of(rule, struct lttng_event_rule_kprobe, parent);

	/* Skip to payload */
	offset += current_view.size;
	/* Map the name */
	current_view = lttng_buffer_view_from_view(
			view, offset, kprobe_comm->name_len);
	name = current_view.data;
	if (!name) {
		ret = -1;
		goto end;
	}

	if (kprobe_comm->name_len == 1 ||
			name[kprobe_comm->name_len - 1] != '\0' ||
			strlen(name) != kprobe_comm->name_len - 1) {
		/*
		 * Check that the name is not NULL, is NULL-terminated, and
		 * does not contain a NULL before the last byte.
		 */
		ret = -1;
		goto end;
	}

	/* Skip after the name */
	offset += kprobe_comm->name_len;
	if (!kprobe_comm->probe_symbol_name_len) {
		goto skip_probe_symbol_name;
	}

	/* Map the probe_symbol_name */
	current_view = lttng_buffer_view_from_view(
			view, offset, kprobe_comm->probe_symbol_name_len);
	probe_symbol_name = current_view.data;
	if (!probe_symbol_name) {
		ret = -1;
		goto end;
	}

	if (kprobe_comm->probe_symbol_name_len == 1 ||
			probe_symbol_name[kprobe_comm->probe_symbol_name_len -
					  1] != '\0' ||
			strlen(probe_symbol_name) !=
					kprobe_comm->probe_symbol_name_len -
							1) {
		/*
		 * Check that the filter expression is not NULL, is
		 * NULL-terminated, and does not contain a NULL before the last
		 * byte.
		 */
		ret = -1;
		goto end;
	}

	/* Skip after the pattern */
	offset += kprobe_comm->probe_symbol_name_len;

skip_probe_symbol_name:

	status = lttng_event_rule_kprobe_set_name(rule, name);
	if (status != LTTNG_EVENT_RULE_STATUS_OK) {
		ERR("Failed to set event rule kprobe name");
		ret = -1;
		goto end;
	}
	kprobe->probe.offset = kprobe_comm->probe_offset;
	kprobe->probe.address = kprobe_comm->probe_address;
	if (probe_symbol_name) {
		kprobe->probe.symbol_name = strdup(probe_symbol_name);
		if (!kprobe->probe.symbol_name) {
			ERR("Failed to set event rule kprobe probe symbol name");
			ret = -1;
			goto end;
		}
	}

	*_event_rule = rule;
	rule = NULL;
	ret = offset;
end:
	lttng_event_rule_destroy(rule);
	return ret;
}

enum lttng_event_rule_status lttng_event_rule_kprobe_set_source(
		struct lttng_event_rule *rule, const char *source)
{
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;
	int match;
	char s_hex[19];
	char name[LTTNG_SYMBOL_NAME_LEN];
	struct lttng_event_rule_kprobe *kprobe;

	/* TODO: support multiple call for this, we must free the symbol name if
	 * that happens !!!
	 */

	if (!source || !IS_KPROBE_EVENT_RULE(rule) || !rule) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	kprobe = container_of(rule, struct lttng_event_rule_kprobe, parent);

	/* Check for symbol+offset */
	match = sscanf(source, "%18s[^'+']+%18s", name, s_hex);
	if (match == 2) {
		/* TODO double validate termination handling of this */
		kprobe->probe.symbol_name =
				strndup(name, LTTNG_SYMBOL_NAME_LEN);
		if (!kprobe->probe.symbol_name) {
			status = LTTNG_EVENT_RULE_STATUS_ERROR;
			goto end;
		}
		if (*s_hex == '\0') {
			status = LTTNG_EVENT_RULE_STATUS_INVALID;
			goto end;
		}
		kprobe->probe.offset = strtoul(s_hex, NULL, 0);
		kprobe->probe.address = 0;
		kprobe->probe.set = true;
		goto end;
	}

	/* Check for symbol */
	if (isalpha(name[0]) || name[0] == '_') {
		match = sscanf(source,
				"%" LTTNG_SYMBOL_NAME_LEN_SCANF_IS_A_BROKEN_API
				"s",
				name);
		if (match == 1) {
			/* TODO double validate termination handling of this */
			kprobe->probe.symbol_name =
					strndup(name, LTTNG_SYMBOL_NAME_LEN);
			if (!kprobe->probe.symbol_name) {
				status = LTTNG_EVENT_RULE_STATUS_ERROR;
				goto end;
			}
			kprobe->probe.offset = 0;
			kprobe->probe.address = 0;
			kprobe->probe.set = true;
			goto end;
		}
	}

	/* Check for address */
	match = sscanf(source, "%18s", s_hex);
	if (match > 0) {
		if (*s_hex == '\0') {
			status = LTTNG_EVENT_RULE_STATUS_INVALID;
			goto end;
		}
		kprobe->probe.address = strtoul(s_hex, NULL, 0);
		kprobe->probe.offset = 0;
		kprobe->probe.symbol_name = NULL;
		kprobe->probe.set = true;
		goto end;
	}

	/* No match */
	status = LTTNG_EVENT_RULE_STATUS_INVALID;

end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_kprobe_set_name(
		struct lttng_event_rule *rule, const char *name)
{
	char *name_copy = NULL;
	struct lttng_event_rule_kprobe *kprobe;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_KPROBE_EVENT_RULE(rule) || !name ||
			strlen(name) == 0) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	kprobe = container_of(rule, struct lttng_event_rule_kprobe, parent);
	name_copy = strdup(name);
	if (!name_copy) {
		status = LTTNG_EVENT_RULE_STATUS_ERROR;
		goto end;
	}

	if (kprobe->name) {
		free(kprobe->name);
	}

	kprobe->name = name_copy;
	name_copy = NULL;
end:
	return status;
}

enum lttng_event_rule_status lttng_event_rule_kprobe_get_name(
		const struct lttng_event_rule *rule, const char **name)
{
	struct lttng_event_rule_kprobe *kprobe;
	enum lttng_event_rule_status status = LTTNG_EVENT_RULE_STATUS_OK;

	if (!rule || !IS_KPROBE_EVENT_RULE(rule) || !name) {
		status = LTTNG_EVENT_RULE_STATUS_INVALID;
		goto end;
	}

	kprobe = container_of(rule, struct lttng_event_rule_kprobe, parent);
	if (!kprobe->name) {
		status = LTTNG_EVENT_RULE_STATUS_UNSET;
		goto end;
	}

	*name = kprobe->name;
end:
	return status;
}

LTTNG_HIDDEN
uint64_t lttng_event_rule_kprobe_get_address(
		const struct lttng_event_rule *rule)
{
	struct lttng_event_rule_kprobe *kprobe;

	assert(rule && IS_KPROBE_EVENT_RULE(rule));

	kprobe = container_of(rule, struct lttng_event_rule_kprobe, parent);

	return kprobe->probe.address;
}

LTTNG_HIDDEN
uint64_t lttng_event_rule_kprobe_get_offset(
		const struct lttng_event_rule *rule)
{
	struct lttng_event_rule_kprobe *kprobe;

	assert(rule && IS_KPROBE_EVENT_RULE(rule));

	kprobe = container_of(rule, struct lttng_event_rule_kprobe, parent);
	return kprobe->probe.offset;
}

LTTNG_HIDDEN
const char *lttng_event_rule_kprobe_get_symbol_name(
		const struct lttng_event_rule *rule)
{
	struct lttng_event_rule_kprobe *kprobe;

	assert(rule && IS_KPROBE_EVENT_RULE(rule));

	kprobe = container_of(rule, struct lttng_event_rule_kprobe, parent);
	return kprobe->probe.symbol_name;
}
