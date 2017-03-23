/*
 * Copyright (C) 2017 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2 only, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _LGPL_SOURCE
#include <urcu.h>
#include <urcu/rculfhash.h>

#include "notification-thread.h"
#include "notification-thread-events.h"
#include "notification-thread-commands.h"
#include <common/error.h>
#include <common/futex.h>
#include <common/unix.h>
#include <common/dynamic-buffer.h>
#include <common/hashtable/utils.h>
#include <common/sessiond-comm/sessiond-comm.h>
#include <lttng/condition/condition.h>
#include <lttng/action/action.h>
#include <lttng/notification/notification-internal.h>
#include <lttng/condition/condition-internal.h>
#include <lttng/condition/buffer-usage-internal.h>
#include <lttng/notification/channel-internal.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <inttypes.h>

struct lttng_trigger_list_element {
	struct lttng_trigger *trigger;
	struct cds_list_head node;
};

struct lttng_channel_trigger_list {
	struct channel_key channel_key;
	struct cds_list_head list;
	struct cds_lfht_node channel_triggers_ht_node;
};

struct lttng_trigger_ht_element {
	struct lttng_trigger *trigger;
	struct cds_lfht_node node;
};

struct lttng_condition_list_element {
	struct lttng_condition *condition;
	struct cds_list_head node;
};

struct notification_client_list_element {
	struct notification_client *client;
	struct cds_list_head node;
};

struct notification_client_list {
	struct lttng_trigger *trigger;
	struct cds_list_head list;
	struct cds_lfht_node notification_trigger_ht_node;
};

struct notification_client {
	int socket;
	uid_t uid;
	gid_t gid;
	/*
	 * Conditions to which the client's notification channel is subscribed.
	 * List of struct lttng_condition_list_node. The condition member is
	 * owned by the client.
	 */
	struct cds_list_head condition_list;
	struct cds_lfht_node client_socket_ht_node;
};

struct channel_state_sample {
	struct channel_key key;
	struct cds_lfht_node channel_state_ht_node;
	uint64_t highest_usage;
	uint64_t lowest_usage;
};

static
int match_client(struct cds_lfht_node *node, const void *key)
{
	int socket = (int) key;
	struct notification_client *client;

	client = caa_container_of(node, struct notification_client,
			client_socket_ht_node);

	return !!(client->socket == socket);
}

static
int match_channel_trigger_list(struct cds_lfht_node *node, const void *key)
{
	struct channel_key *channel_key = (struct channel_key *) key;
	struct lttng_channel_trigger_list *trigger_list;

	trigger_list = caa_container_of(node, struct lttng_channel_trigger_list,
			channel_triggers_ht_node);

	return !!((channel_key->key == trigger_list->channel_key.key) &&
			(channel_key->domain == trigger_list->channel_key.domain));
}

static
int match_channel_state_sample(struct cds_lfht_node *node, const void *key)
{
	struct channel_key *channel_key = (struct channel_key *) key;
	struct channel_state_sample *sample;

	sample = caa_container_of(node, struct channel_state_sample,
			channel_state_ht_node);

	return !!((channel_key->key == sample->key.key) &&
			(channel_key->domain == sample->key.domain));
}

static
int match_channel_info(struct cds_lfht_node *node, const void *key)
{
	struct channel_key *channel_key = (struct channel_key *) key;
	struct channel_info *channel_info;

	channel_info = caa_container_of(node, struct channel_info,
			channels_ht_node);

	return !!((channel_key->key == channel_info->key.key) &&
			(channel_key->domain == channel_info->key.domain));
}

static
int match_condition(struct cds_lfht_node *node, const void *key)
{
	struct lttng_condition *condition_key = (struct lttng_condition *) key;
	struct lttng_trigger_ht_element *trigger;
	struct lttng_condition *condition;

	trigger = caa_container_of(node, struct lttng_trigger_ht_element,
			node);
	condition = lttng_trigger_get_condition(trigger->trigger);
	assert(condition);

	return !!lttng_condition_is_equal(condition_key, condition);
}

static
int match_client_list(struct cds_lfht_node *node, const void *key)
{
	struct lttng_trigger *trigger_key = (struct lttng_trigger *) key;
	struct notification_client_list *client_list;
	struct lttng_condition *condition;
	struct lttng_condition *condition_key = lttng_trigger_get_condition(
			trigger_key);

	assert(condition_key);

	client_list = caa_container_of(node, struct notification_client_list,
			notification_trigger_ht_node);
	condition = lttng_trigger_get_condition(client_list->trigger);

	return !!lttng_condition_is_equal(condition_key, condition);
}

static
int match_client_list_condition(struct cds_lfht_node *node, const void *key)
{
	struct lttng_condition *condition_key = (struct lttng_condition *) key;
	struct notification_client_list *client_list;
	struct lttng_condition *condition;

	assert(condition_key);

	client_list = caa_container_of(node, struct notification_client_list,
			notification_trigger_ht_node);
	condition = lttng_trigger_get_condition(client_list->trigger);

	return !!lttng_condition_is_equal(condition_key, condition);
}

static
unsigned long lttng_condition_buffer_usage_hash(
	struct lttng_condition *_condition)
{
	unsigned long hash = 0;
	struct lttng_condition_buffer_usage *condition;

	condition = container_of(_condition,
			struct lttng_condition_buffer_usage, parent);

	if (condition->session_name) {
		hash ^= hash_key_str(condition->session_name, lttng_ht_seed);
	}
	if (condition->channel_name) {
		hash ^= hash_key_str(condition->session_name, lttng_ht_seed);
	}
	if (condition->domain.set) {
		hash ^= hash_key_ulong(
				(void *) condition->domain.type,
				lttng_ht_seed);
	}
	if (condition->threshold_ratio.set) {
		uint64_t val;

		val = condition->threshold_ratio.value * (double) UINT32_MAX;
		hash ^= hash_key_u64(&val, lttng_ht_seed);
	} else if (condition->threshold_ratio.set) {
		uint64_t val;

		val = condition->threshold_bytes.value;
		hash ^= hash_key_u64(&val, lttng_ht_seed);
	}
	return hash;
}

/*
 * The lttng_condition hashing code is kept in this file (rather than
 * condition.c) since it makes use of GPLv2 code (hashtable utils), which we
 * don't want to link in liblttng-ctl.
 */
static
unsigned long lttng_condition_hash(struct lttng_condition *condition)
{
	switch (condition->type) {
	case LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW:
	case LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH:
		return lttng_condition_buffer_usage_hash(condition);
	default:
		ERR("[notification-thread] Unexpected condition type caught");
		abort();
	}
}

static
void channel_info_destroy(struct channel_info *channel_info)
{
	if (!channel_info) {
		return;
	}

	if (channel_info->session_name) {
		free(channel_info->session_name);
	}
	if (channel_info->channel_name) {
		free(channel_info->channel_name);
	}
	free(channel_info);
}

static
struct channel_info *channel_info_copy(struct channel_info *channel_info)
{
	struct channel_info *copy = zmalloc(sizeof(*channel_info));

	assert(channel_info);
	assert(channel_info->session_name);
	assert(channel_info->channel_name);

	if (!copy) {
		goto end;
	}

	memcpy(copy, channel_info, sizeof(*channel_info));
	copy->session_name = NULL;
	copy->channel_name = NULL;

	copy->session_name = strdup(channel_info->session_name);
	if (!copy->session_name) {
		goto error;
	}
	copy->channel_name = strdup(channel_info->channel_name);
	if (!copy->channel_name) {
		goto error;
	}
	cds_lfht_node_init(&channel_info->channels_ht_node);
end:
	return copy;
error:
	channel_info_destroy(copy);
	return NULL;
}

static
int notification_thread_client_subscribe(struct notification_client *client,
		struct lttng_condition *condition,
		struct notification_thread_state *state,
		enum lttng_notification_channel_status *_status)
{
	int ret = 0;
	struct cds_lfht_iter iter;
	struct cds_lfht_node *node;
	struct notification_client_list *client_list;
	struct lttng_condition_list_element *condition_list_element = NULL;
	struct notification_client_list_element *client_list_element = NULL;
	enum lttng_notification_channel_status status =
			LTTNG_NOTIFICATION_CHANNEL_STATUS_OK;

	/*
	 * Ensure that the client has not already subscribed to this condition
	 * before.
	 */
	cds_list_for_each_entry(condition_list_element, &client->condition_list, node) {
		if (lttng_condition_is_equal(condition_list_element->condition,
				condition)) {
			status = LTTNG_NOTIFICATION_CHANNEL_STATUS_ALREADY_SUBSCRIBED;
			goto end;
		}
	}

	condition_list_element = zmalloc(sizeof(*condition_list_element));
	if (!condition_list_element) {
		ret = -1;
		goto error;
	}
	client_list_element = zmalloc(sizeof(*client_list_element));
	if (!client_list_element) {
		ret = -1;
		goto error;
	}

	rcu_read_lock();

	/*
	 * Add the newly-subscribed condition to the client's subscription list.
	 */
	CDS_INIT_LIST_HEAD(&condition_list_element->node);
	condition_list_element->condition = condition;
	cds_list_add(&condition_list_element->node, &client->condition_list);

	/*
	 * Add the client to the list of clients interested in a given trigger
	 * if a "notification" trigger with a corresponding condition was
	 * added prior.
	 */
	cds_lfht_lookup(state->notification_trigger_clients_ht,
			lttng_condition_hash(condition),
			match_client_list_condition,
			condition,
			&iter);
	node = cds_lfht_iter_get_node(&iter);
	if (!node) {
		goto end_unlock;
	}

	client_list = caa_container_of(node, struct notification_client_list,
			notification_trigger_ht_node);
	client_list_element->client = client;
	CDS_INIT_LIST_HEAD(&client_list_element->node);
	cds_list_add(&client_list->list, &client_list_element->node);
end_unlock:
	rcu_read_unlock();
end:
	if (_status) {
		*_status = status;
	}
	return ret;
error:
	free(condition_list_element);
	free(client_list_element);
	return ret;
}

static
int notification_thread_client_unsubscribe(
		struct notification_client *client,
		struct lttng_condition *condition,
		struct notification_thread_state *state,
		enum lttng_notification_channel_status *_status)
{
	struct cds_lfht_iter iter;
	struct cds_lfht_node *node;
	struct notification_client_list *client_list;
	struct lttng_condition_list_element *condition_list_element,
			*condition_tmp;
	struct notification_client_list_element *client_list_element,
			*client_tmp;
	bool condition_found = false;
	enum lttng_notification_channel_status status =
			LTTNG_NOTIFICATION_CHANNEL_STATUS_OK;

	/* Remove the condition from the client's condition list. */
	cds_list_for_each_entry_safe(condition_list_element, condition_tmp,
			&client->condition_list, node) {
		if (!lttng_condition_is_equal(condition_list_element->condition,
				condition)) {
			continue;
		}

		cds_list_del(&condition_list_element->node);
		/*
		 * The caller may be iterating on the client's conditions to
		 * tear down a client's connection. In this case, the condition
		 * will be destroyed at the end.
		 */
		if (condition != condition_list_element->condition) {
			lttng_condition_destroy(
					condition_list_element->condition);
		}
		free(condition_list_element);
		condition_found = true;
		break;
	}

	if (!condition_found) {
		status = LTTNG_NOTIFICATION_CHANNEL_STATUS_UNKNOWN_CONDITION;
		goto end;
	}

	/*
	 * Remove the client from the list of clients interested the trigger
	 * matching the condition.
	 */
	rcu_read_lock();
	cds_lfht_lookup(state->notification_trigger_clients_ht,
			lttng_condition_hash(condition),
			match_client_list_condition,
			condition,
			&iter);
	node = cds_lfht_iter_get_node(&iter);
	if (!node) {
		goto end_unlock;
	}

	client_list = caa_container_of(node, struct notification_client_list,
			notification_trigger_ht_node);
	cds_list_for_each_entry_safe(client_list_element, client_tmp,
			&client_list->list, node) {
		if (client_list_element->client->socket != client->socket) {
			continue;
		}
		cds_list_del(&client_list_element->node);
		free(client_list_element);
		break;
	}
end_unlock:
	rcu_read_unlock();
end:
	lttng_condition_destroy(condition);
	if (_status) {
		*_status = status;
	}
	return 0;
}

static
void notification_client_destroy(struct notification_client *client,
		struct notification_thread_state *state)
{
	struct lttng_condition_list_element *condition_list_element, *tmp;

	if (!client) {
		return;
	}

	/* Release all conditions to which the client was subscribed. */
	cds_list_for_each_entry_safe(condition_list_element, tmp,
			&client->condition_list, node) {
		(void) notification_thread_client_unsubscribe(client,
				condition_list_element->condition, state, NULL);
	}

	if (client->socket >= 0) {
		(void) lttcomm_close_unix_sock(client->socket);
	}
	free(client);
}

/*
 * Call with rcu_read_lock held (and hold for the lifetime of the returned
 * client pointer).
 */
static
struct notification_client *get_client_from_socket(int socket,
		struct notification_thread_state *state)
{
	struct cds_lfht_iter iter;
	struct cds_lfht_node *node;
	struct notification_client *client = NULL;

	cds_lfht_lookup(state->client_socket_ht,
			hash_key_ulong((void *) (unsigned long) socket, lttng_ht_seed),
			match_client,
			(void *) (unsigned long) socket,
			&iter);
	node = cds_lfht_iter_get_node(&iter);
	if (!node) {
		goto end;
	}

	client = caa_container_of(node, struct notification_client,
			client_socket_ht_node);
end:
	return client;
}

static
bool trigger_applies_to_channel(struct lttng_trigger *trigger,
		struct channel_info *info)
{
	enum lttng_condition_status status;
	struct lttng_condition *condition;
	const char *trigger_session_name = NULL;
	const char *trigger_channel_name = NULL;
	enum lttng_domain_type trigger_domain;

	condition = lttng_trigger_get_condition(trigger);
	if (!condition) {
		goto fail;
	}

	switch (lttng_condition_get_type(condition)) {
	case LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW:
	case LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH:
		break;
	default:
		goto fail;
	}

	status = lttng_condition_buffer_usage_get_domain_type(condition,
			&trigger_domain);
	assert(status == LTTNG_CONDITION_STATUS_OK);
	if (info->key.domain != trigger_domain) {
		goto fail;
	}

	status = lttng_condition_buffer_usage_get_session_name(
			condition, &trigger_session_name);
	assert((status == LTTNG_CONDITION_STATUS_OK) && trigger_session_name);

	status = lttng_condition_buffer_usage_get_channel_name(
			condition, &trigger_channel_name);
	assert((status == LTTNG_CONDITION_STATUS_OK) && trigger_channel_name);

	if (strcmp(info->session_name, trigger_session_name)) {
		goto fail;
	}
	if (strcmp(info->channel_name, trigger_channel_name)) {
		goto fail;
	}

	return true;
fail:
	return false;
}

static
bool trigger_applies_to_client(struct lttng_trigger *trigger,
		struct notification_client *client)
{
	bool applies = false;
	struct lttng_condition_list_element *condition_list_element;

	cds_list_for_each_entry(condition_list_element, &client->condition_list,
			node) {
		applies = lttng_condition_is_equal(
				condition_list_element->condition,
				lttng_trigger_get_condition(trigger));
		if (applies) {
			break;
		}
	}
	return applies;
}

static
unsigned long hash_channel_key(struct channel_key *key)
{
	return hash_key_u64(&key->key, lttng_ht_seed) ^ hash_key_ulong(
		(void *) (unsigned long) key->domain, lttng_ht_seed);
}

static
int handle_notification_thread_command_add_channel(
	struct notification_thread_state *state,
	struct channel_info *channel_info,
	enum lttng_error_code *cmd_result)
{
	struct cds_list_head trigger_list;
	struct channel_info *new_channel_info;
	struct channel_key *channel_key;
	struct lttng_channel_trigger_list *channel_trigger_list = NULL;
	struct lttng_trigger_ht_element *trigger_ht_element = NULL;
	int trigger_count = 0;
	struct cds_lfht_iter iter;

	DBG("[notification-thread] Adding channel %s from session %s, channel key = %" PRIu64 " in %s domain",
			channel_info->channel_name, channel_info->session_name,
			channel_info->key.key, channel_info->key.domain == LTTNG_DOMAIN_KERNEL ? "kernel" : "user space");

	CDS_INIT_LIST_HEAD(&trigger_list);

	new_channel_info = channel_info_copy(channel_info);
	if (!new_channel_info) {
		goto error;
	}

	channel_key = &new_channel_info->key;

	/* Build a list of all triggers applying to the new channel. */
	cds_lfht_for_each_entry(state->triggers_ht, &iter, trigger_ht_element,
			node) {
		struct lttng_trigger_list_element *new_element;

		if (!trigger_applies_to_channel(trigger_ht_element->trigger,
				channel_info)) {
			continue;
		}

		new_element = zmalloc(sizeof(*new_element));
		if (!new_element) {
			goto error;
		}
		CDS_INIT_LIST_HEAD(&new_element->node);
		new_element->trigger = trigger_ht_element->trigger;
		cds_list_add(&new_element->node, &trigger_list);
		trigger_count++;
	}

	DBG("[notification-thread] Found %i triggers that apply to newly added channel",
			trigger_count);
	channel_trigger_list = zmalloc(sizeof(*channel_trigger_list));
	if (!channel_trigger_list) {
		goto error;
	}
	channel_trigger_list->channel_key = *channel_key;
	CDS_INIT_LIST_HEAD(&channel_trigger_list->list);
	cds_lfht_node_init(&channel_trigger_list->channel_triggers_ht_node);
	cds_list_splice(&trigger_list, &channel_trigger_list->list);

	rcu_read_lock();
	/* Add channel to the channel_ht which owns the channel_infos. */
	cds_lfht_add(state->channels_ht,
			hash_channel_key(channel_key),
			&new_channel_info->channels_ht_node);
	/*
	 * Add the list of triggers associated with this channel to the
	 * channel_triggers_ht.
	 */
	cds_lfht_add(state->channel_triggers_ht,
			hash_channel_key(channel_key),
			&channel_trigger_list->channel_triggers_ht_node);
	rcu_read_unlock();
	*cmd_result = LTTNG_OK;
	return 0;
error:
	/* Empty trigger list */
	channel_info_destroy(new_channel_info);
	return 1;
}

static
int handle_notification_thread_command_remove_channel(
	struct notification_thread_state *state,
	uint64_t channel_key, enum lttng_domain_type domain,
	enum lttng_error_code *cmd_result)
{
	struct cds_lfht_node *node;
	struct cds_lfht_iter iter;
	struct lttng_channel_trigger_list *trigger_list;
	struct lttng_trigger_list_element *trigger_list_element, *tmp;
	struct channel_key key = { .key = channel_key, .domain = domain };
	struct channel_info *channel_info;

	DBG("[notification-thread] Removing channel key = %" PRIu64 " in %s domain",
			channel_key, domain == LTTNG_DOMAIN_KERNEL ? "kernel" : "user space");

	rcu_read_lock();

	cds_lfht_lookup(state->channel_triggers_ht,
			hash_channel_key(&key),
			match_channel_trigger_list,
			&key,
			&iter);
	node = cds_lfht_iter_get_node(&iter);
	/*
	 * There is a severe internal error if we are being asked to remove a
	 * channel that doesn't exist.
	 */
	assert(node);
	/* Free the list of triggers associated with this channel. */
	trigger_list = caa_container_of(node, struct lttng_channel_trigger_list,
			channel_triggers_ht_node);
	cds_list_for_each_entry_safe(trigger_list_element, tmp,
			&trigger_list->list, node) {
		cds_list_del(&trigger_list_element->node);
		free(trigger_list_element);
	}
	cds_lfht_del(state->channel_triggers_ht, node);
	free(trigger_list);

	/* Free sampled channel state. */
	cds_lfht_lookup(state->channel_state_ht,
			hash_channel_key(&key),
			match_channel_state_sample,
			&key,
			&iter);
	node = cds_lfht_iter_get_node(&iter);
	/*
	 * This is expected to be NULL if the channel is destroyed before we
	 * received a sample.
	 */
	if (node) {
		struct channel_state_sample *sample = caa_container_of(node,
				struct channel_state_sample,
				channel_state_ht_node);

		cds_lfht_del(state->channel_state_ht, node);
		free(sample);
	}

	/* Remove the channel from the channels_ht and free it. */
	cds_lfht_lookup(state->channels_ht,
			hash_channel_key(&key),
			match_channel_info,
			&key,
			&iter);
	node = cds_lfht_iter_get_node(&iter);
	assert(node);
	channel_info = caa_container_of(node, struct channel_info,
			channels_ht_node);
	cds_lfht_del(state->channels_ht, node);
	channel_info_destroy(channel_info);
	rcu_read_unlock();

	*cmd_result = LTTNG_OK;
	return 0;
}

/*
 * FIXME A client's credentials are not checked when registering a trigger, nor
 *       are they stored alongside with the trigger.
 *
 * The effects of this are benign:
 *     - The client will succeed in registering the trigger, as it is valid,
 *     - The trigger will, internally, be bound to the channel,
 *     - The notifications will not be sent since the client's credentials
 *       are checked against the channel at that moment.
 */
static
int handle_notification_thread_command_register_trigger(
	struct notification_thread_state *state,
	struct lttng_trigger *trigger,
	enum lttng_error_code *cmd_result)
{
	int ret = 0;
	struct lttng_condition *condition;
	struct notification_client *client;
	struct notification_client_list *client_list = NULL;
	struct lttng_trigger_ht_element *trigger_ht_element = NULL;
	struct notification_client_list_element *client_list_element, *tmp;
	struct cds_lfht_node *node;
	struct cds_lfht_iter iter;
	struct channel_info *channel;
	bool free_trigger = true;

	rcu_read_lock();

	condition = lttng_trigger_get_condition(trigger);
	trigger_ht_element = zmalloc(sizeof(*trigger_ht_element));
	if (!trigger_ht_element) {
		ret = -1;
		goto error;
	}

	/* Add trigger to the trigger_ht. */
	cds_lfht_node_init(&trigger_ht_element->node);
	trigger_ht_element->trigger = trigger;

	node = cds_lfht_add_unique(state->triggers_ht,
			lttng_condition_hash(condition),
			match_condition,
			condition,
			&trigger_ht_element->node);
	if (node != &trigger_ht_element->node) {
		/* Not a fatal error, simply report it to the client. */
		*cmd_result = LTTNG_ERR_TRIGGER_EXISTS;
		goto error_free_ht_element;
	}

	/*
	 * Ownership of the trigger and of its wrapper was transfered to
	 * the triggers_ht.
	 */
	trigger_ht_element = NULL;
	free_trigger = false;

	/*
	 * The rest only applies to triggers that have a "notify" action.
	 * It is not skipped as this is the only action type currently
	 * supported.
	 */
	client_list = zmalloc(sizeof(*client_list));
	if (!client_list) {
		ret = -1;
		goto error_free_ht_element;
	}
	cds_lfht_node_init(&client_list->notification_trigger_ht_node);
	CDS_INIT_LIST_HEAD(&client_list->list);
	client_list->trigger = trigger;

	/* Build a list of clients to which this new trigger applies. */
	cds_lfht_for_each_entry(state->client_socket_ht, &iter, client,
			client_socket_ht_node) {
		if (!trigger_applies_to_client(trigger, client)) {
			continue;
		}

		client_list_element = zmalloc(sizeof(*client_list_element));
		if (!client_list_element) {
			ret = -1;
			goto error_free_client_list;
		}
		CDS_INIT_LIST_HEAD(&client_list_element->node);
		client_list_element->client = client;
		cds_list_add(&client_list_element->node, &client_list->list);
	}

	cds_lfht_add(state->notification_trigger_clients_ht,
			lttng_condition_hash(condition),
			&client_list->notification_trigger_ht_node);
	/*
	 * Client list ownership transferred to the
	 * notification_trigger_clients_ht.
	 */
	client_list = NULL;

	/*
	 * Add the trigger to list of triggers bound to the channels currently
	 * known.
	 */
	cds_lfht_for_each_entry(state->channels_ht, &iter, channel,
			channels_ht_node) {
		struct lttng_trigger_list_element *trigger_list_element;
		struct lttng_channel_trigger_list *trigger_list;

		if (!trigger_applies_to_channel(trigger, channel)) {
			continue;
		}

		cds_lfht_lookup(state->channel_triggers_ht,
				hash_channel_key(&channel->key),
				match_channel_trigger_list,
				&channel->key,
				&iter);
		node = cds_lfht_iter_get_node(&iter);
		assert(node);
		/* Free the list of triggers associated with this channel. */
		trigger_list = caa_container_of(node,
				struct lttng_channel_trigger_list,
				channel_triggers_ht_node);
		
		trigger_list_element = zmalloc(sizeof(*trigger_list_element));
		if (!trigger_list_element) {
			ret = -1;
			goto error_free_client_list;
		}
		CDS_INIT_LIST_HEAD(&trigger_list_element->node);
		trigger_list_element->trigger = trigger;
		cds_list_add(&trigger_list_element->node, &trigger_list->list);
		/* A trigger can only apply to one channel. */
		break;
	}

	*cmd_result = LTTNG_OK;
error_free_client_list:
	if (client_list) {
		cds_list_for_each_entry_safe(client_list_element, tmp,
				&client_list->list, node) {
			free(client_list_element);
		}
		free(client_list);
	}
error_free_ht_element:
	free(trigger_ht_element);
error:
	if (free_trigger) {
		lttng_trigger_destroy(trigger);
	}
	rcu_read_unlock();
	return ret;
}

int handle_notification_thread_command_unregister_trigger(
		struct notification_thread_state *state,
		struct lttng_trigger *trigger,
		enum lttng_error_code *_cmd_reply)
{
	struct cds_lfht_iter iter;
	struct cds_lfht_node *node, *triggers_ht_node;
	struct lttng_channel_trigger_list *trigger_list;
	struct notification_client_list *client_list;
	struct notification_client_list_element *client_list_element, *tmp;
	struct lttng_trigger_ht_element *trigger_ht_element = NULL;
	struct lttng_condition *condition = lttng_trigger_get_condition(
			trigger);
	enum lttng_error_code cmd_reply;

	rcu_read_lock();

	cds_lfht_lookup(state->triggers_ht,
			lttng_condition_hash(condition),
			match_condition,
			condition,
			&iter);
	triggers_ht_node = cds_lfht_iter_get_node(&iter);
	if (!triggers_ht_node) {
		cmd_reply = LTTNG_ERR_TRIGGER_NOT_FOUND;
		goto end;
	} else {
		cmd_reply = LTTNG_OK;
	}

	/* Remove trigger from channel_triggers_ht. */
	cds_lfht_for_each_entry(state->channel_triggers_ht, &iter, trigger_list,
			channel_triggers_ht_node) {
		struct lttng_trigger_list_element *trigger_element, *tmp;

		cds_list_for_each_entry_safe(trigger_element, tmp,
				&trigger_list->list, node) {
			struct lttng_condition *current_condition =
					lttng_trigger_get_condition(
						trigger_element->trigger);

			assert(current_condition);
			if (!lttng_condition_is_equal(condition,
					current_condition)) {
				continue;
			}

			DBG("[notification-thread] Removed trigger from channel_triggers_ht");
			cds_list_del(&trigger_element->node);
		}
	}

	/*
	 * Remove and release the client list from
	 * notification_trigger_clients_ht.
	 */
	cds_lfht_lookup(state->notification_trigger_clients_ht,
			lttng_condition_hash(condition),
			match_client_list,
			trigger,
			&iter);
	node = cds_lfht_iter_get_node(&iter);
	assert(node);
	client_list = caa_container_of(node, struct notification_client_list,
			notification_trigger_ht_node);
	cds_list_for_each_entry_safe(client_list_element, tmp,
			&client_list->list, node) {
		free(client_list_element);
	}
	cds_lfht_del(state->notification_trigger_clients_ht, node);
	free(client_list);

	/* Remove trigger from triggers_ht. */
	trigger_ht_element = caa_container_of(triggers_ht_node,
			struct lttng_trigger_ht_element, node);
	cds_lfht_del(state->triggers_ht, triggers_ht_node);
	lttng_trigger_destroy(trigger_ht_element->trigger);
	free(trigger_ht_element);
end:
	rcu_read_unlock();
	if (_cmd_reply) {
		*_cmd_reply = cmd_reply;
	}
	return 0;
}

int handle_notification_thread_command(
		struct notification_thread_handle *handle,
		struct notification_thread_state *state)
{
	int ret;
	uint64_t counter;
	struct notification_thread_command *cmd;

	/* Read event_fd to put it back into a quiescent state. */
	ret = read(handle->cmd_queue.event_fd, &counter, sizeof(counter));
	if (ret == -1) {
		goto error;
	}

	pthread_mutex_lock(&handle->cmd_queue.lock);
	cmd = cds_list_first_entry(&handle->cmd_queue.list,
			struct notification_thread_command, cmd_list_node);
	switch (cmd->type) {
	case NOTIFICATION_COMMAND_TYPE_REGISTER_TRIGGER:
		DBG("[notification-thread] Received register trigger command");
		ret = handle_notification_thread_command_register_trigger(
				state, cmd->parameters.trigger,
				&cmd->reply_code);
		break;
	case NOTIFICATION_COMMAND_TYPE_UNREGISTER_TRIGGER:
		DBG("[notification-thread] Received unregister trigger command");
		ret = handle_notification_thread_command_unregister_trigger(
				state, cmd->parameters.trigger,
				&cmd->reply_code);
		break;
	case NOTIFICATION_COMMAND_TYPE_ADD_CHANNEL:
		DBG("[notification-thread] Received add channel command");
		ret = handle_notification_thread_command_add_channel(
				state, &cmd->parameters.add_channel,
				&cmd->reply_code);
		break;
	case NOTIFICATION_COMMAND_TYPE_REMOVE_CHANNEL:
		DBG("[notification-thread] Received remove channel command");
		ret = handle_notification_thread_command_remove_channel(
				state, cmd->parameters.remove_channel.key,
				cmd->parameters.remove_channel.domain,
				&cmd->reply_code);
		break;
	default:
		ERR("[notification-thread] Unknown internal command received");
		goto error_unlock;
	}

	if (ret) {
		goto error_unlock;
	}

	cds_list_del(&cmd->cmd_list_node);
	futex_nto1_wake(&cmd->reply_futex);
	pthread_mutex_unlock(&handle->cmd_queue.lock);
	return 0;
error_unlock:
	/* Wake-up and return a fatal error to the calling thread. */
	futex_nto1_wake(&cmd->reply_futex);
	pthread_mutex_unlock(&handle->cmd_queue.lock);
	cmd->reply_code = LTTNG_ERR_FATAL;
error:
	/* Indicate a fatal error to the caller. */
	return 1;
}

static
unsigned long hash_client_socket(int socket)
{
	return hash_key_ulong((void *) (unsigned long) socket, lttng_ht_seed);
}

int handle_notification_thread_client_connect(
		struct notification_thread_state *state)
{
	int ret;
	struct notification_client *client;

	DBG("[notification-thread] Handling new notification channel client connection");

	client = zmalloc(sizeof(*client));
	if (!client) {
		/* Fatal error. */
		ret = -1;
		goto error;
	}
	CDS_INIT_LIST_HEAD(&client->condition_list);

	ret = lttcomm_accept_unix_sock(state->notification_channel_socket);
	if (ret < 0) {
		ERR("[notification-thread] Failed to accept new notification channel client connection");
		ret = 0;
		goto error;
	}

	client->socket = ret;

	/* FIXME set client socket as non-blocking. */
	/*
	ret = set_socket_non_blocking(client->socket);
	if (ret) {
		ERR("[notification-thread] Failed to set new notification channel client connection socket as non-blocking");
		goto error;
	}
	*/

	/* FIXME handle creds. */
	ret = lttcomm_setsockopt_creds_unix_sock(client->socket);
	if (ret < 0) {
		ERR("[notification-thread] Failed to set socket options on new notification channel client socket");
		ret = 0;
		goto error;
	}

	/* FIXME perform handshake. */

	ret = lttng_poll_add(&state->events, client->socket,
			LPOLLIN | LPOLLERR |
			LPOLLHUP | LPOLLRDHUP);
	if (ret < 0) {
		ERR("[notification-thread] Failed to add notification channel client socket to poll set");
		ret = 0;
		goto error;
	}
	DBG("[notification-thread] Added new notification channel client socket (%i) to poll set",
			client->socket);

	/* Add to ht. */
	rcu_read_lock();
	cds_lfht_add(state->client_socket_ht,
			hash_client_socket(client->socket),
			&client->client_socket_ht_node);
	rcu_read_unlock();

	return ret;
error:
	notification_client_destroy(client, state);
	return ret;
}

int handle_notification_thread_client_disconnect(
		int client_socket,
		struct notification_thread_state *state)
{
	int ret = 0;
	struct notification_client *client;

	rcu_read_lock();
	DBG("[notification-thread] Closing client connection (socket fd = %i)",
			client_socket);
	client = get_client_from_socket(client_socket, state);
	if (!client) {
		/* Internal state corruption, fatal error. */
		ERR("[notification-thread] Unable to find client (socket fd = %i)",
				client_socket);
		ret = -1;
		goto end;
	}

	ret = lttng_poll_del(&state->events, client_socket);
	if (ret) {
		ERR("[notification-thread] Failed to remove client socket from poll set");
	}
        cds_lfht_del(state->client_socket_ht,
			&client->client_socket_ht_node);
	notification_client_destroy(client, state);
end:
	rcu_read_unlock();
	return ret;
}

int handle_notification_thread_client_disconnect_all(
		struct notification_thread_state *state)
{
	struct cds_lfht_iter iter;
	struct notification_client *client;
	bool error_encoutered = false;

	rcu_read_lock();
	DBG("[notification-thread] Closing all client connections");
	cds_lfht_for_each_entry(state->client_socket_ht, &iter, client,
		client_socket_ht_node) {
		int ret;

		ret = handle_notification_thread_client_disconnect(
				client->socket, state);
		if (ret) {
			error_encoutered = true;
		}
	}
	rcu_read_unlock();
	return error_encoutered ? 1 : 0;
}

int handle_notification_thread_trigger_unregister_all(
		struct notification_thread_state *state)
{
	bool error_occured = false;
	struct cds_lfht_iter iter;
	struct lttng_trigger_ht_element *trigger_ht_element;

	cds_lfht_for_each_entry(state->triggers_ht, &iter, trigger_ht_element,
			node) {
		int ret = handle_notification_thread_command_unregister_trigger(
				state, trigger_ht_element->trigger, NULL);
		if (ret) {
			error_occured = true;
		}
	}
	return error_occured ? -1 : 0;
}

static
int send_client_reply(int socket,
		enum lttng_notification_channel_status status)
{
	ssize_t ret;
	struct lttng_notification_channel_command_reply reply = {
		.status = (int8_t) status,
	};
	struct lttng_notification_channel_message msg = {
		.type = (int8_t) LTTNG_NOTIFICATION_CHANNEL_MESSAGE_TYPE_COMMAND_REPLY,
		.size = sizeof(reply),
	};
	char *buffer[sizeof(msg) + sizeof(reply)] = {};

	memcpy(buffer, &msg, sizeof(msg));
	memcpy(buffer + sizeof(msg), &reply, sizeof(reply));
	DBG("[notification-thread] Send command reply (%i)", (int) status);

	ret = lttcomm_send_unix_sock(socket, buffer,
			sizeof(msg) + sizeof(reply));
	if (ret < 0) {
		ERR("[notification-thread] Failed to send command reply");
		goto error;
	}
	return 0;
error:
	return -1;
}

int handle_notification_thread_client(struct notification_thread_state *state,
		int socket)
{
	int ret = 0;
	size_t received = 0;
	struct notification_client *client;
	struct lttng_notification_channel_message msg;
	struct lttng_dynamic_buffer buffer;
	struct lttng_condition *condition = NULL;
	enum lttng_notification_channel_status status =
			LTTNG_NOTIFICATION_CHANNEL_STATUS_OK;

	lttng_dynamic_buffer_init(&buffer);

	client = get_client_from_socket(socket, state);
	if (!client) {
		/* Internal error, abort. */
		ret = 1;
		goto error_no_reply;
	}

	/* Receive message header. */
	do {
		ssize_t recv_ret;

		recv_ret = lttcomm_recv_unix_sock(socket,
				((char *) &msg) + received,
				sizeof(msg) - received);
		if (recv_ret <= 0) {
			ERR("[notification-thread] Failed to receive channel command from client (received %zu bytes)", received);
			/*
			 * Protocol error, disconnect the client but don't
			 * signal an error.
			 */
			goto error_disconnect_client;
		}
		received += recv_ret;
	} while (received < sizeof(msg));

	ret = lttng_dynamic_buffer_set_size(&buffer, msg.size);
	if (ret) {
		goto error_disconnect_client;
	}

	/* Receive message body. */
	received = 0;
	do {
		ssize_t recv_ret;

		recv_ret = lttcomm_recv_unix_sock(socket,
				buffer.data + received,
				msg.size - received);
		if (recv_ret <= 0) {
			ERR("[notification-thread] Failed to receive condition from client");
			goto error_disconnect_client;
		}
		received += recv_ret;
	} while (received < msg.size);

	ret = lttng_condition_create_from_buffer(buffer.data, &condition);
	if (ret < 0 || ret < msg.size) {
		ERR("[notification-thread] Malformed condition received from client");
		goto error_disconnect_client;
	}

	DBG("[notification-thread] Successfully received condition from notification channel client");

	switch ((enum lttng_notification_channel_message_type) msg.type) {
	case LTTNG_NOTIFICATION_CHANNEL_MESSAGE_TYPE_SUBSCRIBE:
		ret = notification_thread_client_subscribe(client, condition,
				state, &status);
		break;
	case LTTNG_NOTIFICATION_CHANNEL_MESSAGE_TYPE_UNSUBSCRIBE:
		ret = notification_thread_client_unsubscribe(client, condition,
				state, &status);
		break;
	default:
		ERR("[notification-thread] Unknown command type received from notification channel client");
		goto error_disconnect_client;
	}

	if (send_client_reply(socket, status)) {
		ERR("[notification-thread] Failed to send reply to notification channel client");
		goto error_disconnect_client;
	}

	lttng_dynamic_buffer_reset(&buffer);
	ret = 0;
	return ret;

error_disconnect_client:
	ret = handle_notification_thread_client_disconnect(socket, state);
error_no_reply:
	lttng_dynamic_buffer_reset(&buffer);
	lttng_condition_destroy(condition);
	return ret;
}

static
bool evaluate_buffer_usage_condition(struct lttng_condition *condition,
		struct channel_state_sample *sample, uint64_t buffer_capacity)
{
	bool result = false;
	uint64_t threshold;
	enum lttng_condition_type condition_type;
	struct lttng_condition_buffer_usage *use_condition = container_of(
			condition, struct lttng_condition_buffer_usage,
			parent);

	if (!sample) {
		goto end;
	}

	if (use_condition->threshold_bytes.set) {
		threshold = use_condition->threshold_bytes.value;
	} else {
		/* Threshold was expressed as a ratio. */
		threshold = (uint64_t) (use_condition->threshold_ratio.value *
				(double) buffer_capacity);
	}

	condition_type = lttng_condition_get_type(condition);
	if (condition_type == LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW) {
		/*
		 * The low condition should only be triggered once _all_ of the
		 * streams in a channel have gone below the "low" threshold.
		 */
		if (sample->highest_usage <= threshold) {
			result = true;
		}
	} else {
		/*
		 * For high buffer usage scenarios, we want to trigger whenever
		 * _any_ of the streams has reached the "high" threshold.
		 */
		if (sample->highest_usage >= threshold) {
			result = true;
		}
	}
end:
	return result;
}

static
int evaluate_condition(struct lttng_condition *condition,
		struct lttng_evaluation **evaluation,
		struct notification_thread_state *state,
		struct channel_state_sample *previous_sample,
		struct channel_state_sample *latest_sample,
		uint64_t buffer_capacity)
{
	int ret = 0;
	enum lttng_condition_type condition_type;
	bool previous_sample_result;
	bool latest_sample_result;

	condition_type = lttng_condition_get_type(condition);
	/* No other condition type supported for the moment. */
	assert(condition_type == LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW ||
			condition_type == LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH);

	previous_sample_result = evaluate_buffer_usage_condition(condition,
			previous_sample, buffer_capacity);
	latest_sample_result = evaluate_buffer_usage_condition(condition,
			latest_sample, buffer_capacity);

	if (!latest_sample_result ||
			(previous_sample_result == latest_sample_result)) {
		/*
		 * Only trigger on a condition evaluation transition.
		 * NOTE: This edge-triggered logic may not be appropriate for
		 * future condition types.
		 */
		goto end;
	}

	if (evaluation && latest_sample_result) {
		*evaluation = lttng_evaluation_buffer_usage_create(
				condition_type,
				latest_sample->highest_usage,
				buffer_capacity);
		if (!*evaluation) {
			ret = -1;
			goto end;
		}
	}
end:
	return ret;
}

static
int send_evaluation_to_clients(struct lttng_trigger *trigger,
		struct lttng_evaluation *evaluation,
		struct notification_client_list* client_list)
{
	int ret = 0;
	struct lttng_dynamic_buffer msg_buffer;
	struct notification_client_list_element *client_list_element, *tmp;
	struct lttng_notification *notification;
	struct lttng_condition *condition;
	ssize_t expected_notification_size, notification_size;
	struct lttng_notification_channel_message msg;

	lttng_dynamic_buffer_init(&msg_buffer);

	condition = lttng_trigger_get_condition(trigger);
	assert(condition);

	notification = lttng_notification_create(condition, evaluation);
	if (!notification) {
		ret = -1;
		goto end;
	}

	expected_notification_size = lttng_notification_serialize(notification,
			NULL);
	if (expected_notification_size < 0) {
		ERR("[notification-thread] Failed to get size of serialized notification");
		ret = -1;
		goto end;
	}

	msg.type = (int8_t) LTTNG_NOTIFICATION_CHANNEL_MESSAGE_TYPE_NOTIFICATION;
	msg.size = (uint32_t) expected_notification_size;
	ret = lttng_dynamic_buffer_append(&msg_buffer, &msg, sizeof(msg));
	if (ret) {
		goto end;
	}

	ret = lttng_dynamic_buffer_set_size(&msg_buffer,
			msg_buffer.size + expected_notification_size);
	if (ret) {
		goto end;
	}

	notification_size = lttng_notification_serialize(notification,
			msg_buffer.data + sizeof(msg));
	if (notification_size != expected_notification_size) {
		ERR("[notification-thread] Failed to serialize notification");
		ret = -1;
		goto end;
	}

	cds_list_for_each_entry_safe(client_list_element, tmp,
			&client_list->list, node) {
		ret = lttcomm_send_unix_sock(
				client_list_element->client->socket,
				msg_buffer.data, msg_buffer.size);
		if (ret < 0) {
			ERR("[notification-thread] Failed to send notification to client");
		}
	}
	ret = 0;
end:
	lttng_notification_destroy(notification);
	lttng_dynamic_buffer_reset(&msg_buffer);
	return ret;
}

int handle_notification_thread_channel_sample(
		struct notification_thread_state *state, int pipe,
		enum lttng_domain_type domain)
{
	int ret = 0;
	struct lttcomm_consumer_channel_monitor_msg sample_msg;
	struct channel_state_sample previous_sample, latest_sample;
	struct channel_info *channel_info;
	struct cds_lfht_node *node;
	struct cds_lfht_iter iter;
	struct lttng_channel_trigger_list *trigger_list;
	struct lttng_trigger_list_element *trigger_list_element;
	bool previous_sample_available = false;

	/*
	 * The monitoring pipe only holds messages smaller than PIPE_BUF,
	 * ensuring that read/write of sampling messages are atomic.
	 */
	do {
		ret = read(pipe, &sample_msg, sizeof(sample_msg));
	} while (ret == -1 && errno == EINTR);
	if (ret != sizeof(sample_msg)) {
		ERR("[notification-thread] Failed to read from monitoring pipe (fd = %i)",
				pipe);
		ret = -1;
		goto end;
	}

	ret = 0;
	latest_sample.key.key = sample_msg.key;
	latest_sample.key.domain = domain;
	latest_sample.highest_usage = sample_msg.highest;
	latest_sample.lowest_usage = sample_msg.lowest;

	rcu_read_lock();

	/* Retrieve the channel's informations */
	cds_lfht_lookup(state->channels_ht,
			hash_channel_key(&latest_sample.key),
			match_channel_info,
			&latest_sample.key,
			&iter);
	node = cds_lfht_iter_get_node(&iter);
	if (!node) {
		/*
		 * Not an error since the consumer can push a sample to the pipe
		 * and the rest of the session daemon could notify us of the
		 * channel's destruction before we get a chance to process that
		 * sample.
		 */
		DBG("[notification-thread] Received a sample for an unknown channel from consumerd, key = %" PRIu64 " in %s domain",
				latest_sample.key.key,
				domain == LTTNG_DOMAIN_KERNEL ? "kernel" :
					"user space");
		goto end_unlock;
	}
	channel_info = caa_container_of(node, struct channel_info,
			channels_ht_node);

	/* Retrieve the channel's last sample, if it exists, and update it. */
	cds_lfht_lookup(state->channel_state_ht,
			hash_channel_key(&latest_sample.key),
			match_channel_state_sample,
			&latest_sample.key,
			&iter);
	node = cds_lfht_iter_get_node(&iter);
	if (node) {
		struct channel_state_sample *stored_sample;

		/* Update the sample stored. */
		stored_sample = caa_container_of(node,
				struct channel_state_sample,
				channel_state_ht_node);
		memcpy(&previous_sample, stored_sample,
				sizeof(previous_sample));
		stored_sample->highest_usage = latest_sample.highest_usage;
		stored_sample->lowest_usage = latest_sample.lowest_usage;
		previous_sample_available = true;
	} else {
		/*
		 * This is the channel's first sample, allocate space for and
		 * store the new sample.
		 */
		struct channel_state_sample *stored_sample;

		stored_sample = zmalloc(sizeof(*stored_sample));
		if (!stored_sample) {
			ret = -1;
			goto end_unlock;
		}

		memcpy(stored_sample, &latest_sample, sizeof(*stored_sample));
		cds_lfht_node_init(&stored_sample->channel_state_ht_node);
		cds_lfht_add(state->channel_state_ht,
				hash_channel_key(&stored_sample->key),
				&stored_sample->channel_state_ht_node);
	}

	/* Find triggers associated with this channel. */
	cds_lfht_lookup(state->channel_triggers_ht,
			hash_channel_key(&latest_sample.key),
			match_channel_trigger_list,
			&latest_sample.key,
			&iter);
	node = cds_lfht_iter_get_node(&iter);
	if (!node) {
		goto end_unlock;
	}

	trigger_list = caa_container_of(node, struct lttng_channel_trigger_list,
			channel_triggers_ht_node);
	cds_list_for_each_entry(trigger_list_element, &trigger_list->list,
		        node) {
		struct lttng_condition *condition;
		struct lttng_action *action;
		struct lttng_trigger *trigger;
		struct notification_client_list *client_list;
		struct lttng_evaluation *evaluation = NULL;

		trigger = trigger_list_element->trigger;
		condition = lttng_trigger_get_condition(trigger);
		assert(condition);
		action = lttng_trigger_get_action(trigger);

		/* Notify actions are the only type currently supported. */
		assert(lttng_action_get_type(action) ==
				LTTNG_ACTION_TYPE_NOTIFY);

		/*
		 * Check if any client is subscribed to the result of this
		 * evaluation.
		 */
		cds_lfht_lookup(state->notification_trigger_clients_ht,
				lttng_condition_hash(condition),
				match_client_list,
				trigger,
				&iter);
		node = cds_lfht_iter_get_node(&iter);
		assert(node);

		client_list = caa_container_of(node,
				struct notification_client_list,
				notification_trigger_ht_node);
		if (cds_list_empty(&client_list->list)) {
			/*
			 * No clients interested in the evaluation's result,
			 * skip it.
			 */
			continue;
		}

		ret = evaluate_condition(condition, &evaluation, state,
				previous_sample_available ? &previous_sample : NULL,
				&latest_sample, channel_info->capacity);
		if (ret) {
			goto end_unlock;
		}

		if (!evaluation) {
			continue;
		}

		/* Dispatch evaluation result to all clients. */
		ret = send_evaluation_to_clients(trigger_list_element->trigger,
				evaluation, client_list);
		if (ret) {
			goto end_unlock;
		}
	}
end_unlock:
	rcu_read_unlock();
end:
	return ret;
}
