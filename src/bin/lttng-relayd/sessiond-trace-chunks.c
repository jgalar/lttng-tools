/*
 * Copyright (C) 2019 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "sessiond-trace-chunks.h"
#include <urcu.h>
#include <urcu/rculfhash.h>
#include <urcu/ref.h>
#include <common/macros.h>
#include <common/hashtable/hashtable.h>
#include <common/hashtable/utils.h>
#include <common/trace-chunk-registry.h>
#include <common/defaults.h>
#include <common/error.h>

/*
 * Lifetime of trace chunks within the relay daemon.
 *
 * Trace chunks are shared accross connections initiated from a given
 * session daemon. When a session is created by a consumer daemon, the
 * UUID of its associated session daemon is transmitted (in the case of
 * 2.11+ consumer daemons).
 *
 * The sessiond_trace_chunk_registry_new_session() and
 * sessiond_trace_chunk_registry_session_closed() methods create and
 * manage the reference count of lttng_trace_chunk_registry objects
 * associated to the various sessiond instances served by the relay daemon.
 *
 * When all sessions associated with a given sessiond instance are
 * destroyed, its registry is destroyed.
 *
 * lttng_trace_chunk objects are uniquely identified by the
 * (sessiond_uuid, sessiond_session_id, chunk_id) tuple. If a trace chunk
 * matching that tuple already exists, a new reference to the trace chunk
 * is acquired and it is returned to the caller. Otherwise, a new trace
 * chunk is created. This is how trace chunks are de-duplicated across
 * multiple consumer daemons managed by the same session daemon.
 *
 * Note that trace chunks are always added to their matching
 * lttng_trace_chunk_registry. They are automatically removed from the
 * trace chunk registry when their reference count reaches zero.
 */

/*
 * It is assumed that the sessiond_trace_chunk_registry is created and
 * destroyed by the same thread.
 */
struct sessiond_trace_chunk_registry {
	/* Maps an lttng_uuid to an lttng_trace_chunk_registry. */
	struct cds_lfht *ht;
};

struct trace_chunk_registry_ht_key {
	lttng_uuid sessiond_uuid;
};

struct trace_chunk_registry_ht_element {
	struct trace_chunk_registry_ht_key key;
	struct urcu_ref ref;
	/* Node into the sessiond_trace_chunk_registry's hash table. */
	struct cds_lfht_node ht_node;
	/* Used for defered call_rcu reclaim. */
	struct rcu_head rcu_node;
	struct lttng_trace_chunk_registry *trace_chunk_registry;
	struct sessiond_trace_chunk_registry *sessiond_trace_chunk_registry;
};

static
unsigned long trace_chunk_registry_ht_key_hash(
		const struct trace_chunk_registry_ht_key *key)
{
	uint64_t uuid_h1 = ((uint64_t *) key->sessiond_uuid)[0];
	uint64_t uuid_h2 = ((uint64_t *) key->sessiond_uuid)[1];

	return hash_key_u64(&uuid_h1, lttng_ht_seed) ^
			hash_key_u64(&uuid_h2, lttng_ht_seed);
}

/* cds_lfht match function */
static
int trace_chunk_registry_ht_key_match(struct cds_lfht_node *node,
		const void *_key)
{
	const struct trace_chunk_registry_ht_key *key =
			(struct trace_chunk_registry_ht_key *) _key;
	struct trace_chunk_registry_ht_element *registry;

	registry = container_of(node, typeof(*registry), ht_node);
	return lttng_uuid_is_equal(key->sessiond_uuid,
			registry->key.sessiond_uuid);
}

static
void trace_chunk_registry_ht_element_free(struct rcu_head *node)
{
	struct trace_chunk_registry_ht_element *element =
			container_of(node, typeof(*element), rcu_node);

	lttng_trace_chunk_registry_destroy(element->trace_chunk_registry);
	free(element);
}

static
void trace_chunk_registry_ht_element_release(struct urcu_ref *ref)
{
	struct trace_chunk_registry_ht_element *element =
			container_of(ref, typeof(*element), ref);
	char uuid_str[UUID_STR_LEN];

	lttng_uuid_to_str(element->key.sessiond_uuid, uuid_str);

	DBG("Destroying trace chunk registry associated to sessiond {%s}",
			uuid_str);
	if (element->sessiond_trace_chunk_registry) {
		/* Unpublish. */
		rcu_read_lock();
		cds_lfht_del(element->sessiond_trace_chunk_registry->ht,
				&element->ht_node);
		rcu_read_unlock();
		element->sessiond_trace_chunk_registry = NULL;
	}

	/* Defered reclaim of the object */
	call_rcu(&element->rcu_node, trace_chunk_registry_ht_element_free);
}

static
bool trace_chunk_registry_ht_element_get(
		struct trace_chunk_registry_ht_element *element)
{
	return urcu_ref_get_unless_zero(&element->ref);
}

static
void trace_chunk_registry_ht_element_put(
		struct trace_chunk_registry_ht_element *element)
{
	urcu_ref_put(&element->ref, trace_chunk_registry_ht_element_release);
}

/* Acquires a reference to the returned element on behalf of the caller. */
static
struct trace_chunk_registry_ht_element *trace_chunk_registry_ht_element_find(
		struct sessiond_trace_chunk_registry *sessiond_registry,
		const struct trace_chunk_registry_ht_key *key)
{
	struct trace_chunk_registry_ht_element *element = NULL;
	struct cds_lfht_node *node;
	struct cds_lfht_iter iter;

	rcu_read_lock();
	cds_lfht_lookup(sessiond_registry->ht,
			trace_chunk_registry_ht_key_hash(key),
			trace_chunk_registry_ht_key_match,
			key,
			&iter);
	node = cds_lfht_iter_get_node(&iter);
	if (node) {
		element = container_of(node, typeof(*element), ht_node);
		/*
		 * Only consider the look-up as successful if a reference
		 * could be acquired.
		 */
		if (!trace_chunk_registry_ht_element_get(element)) {
			element = NULL;
		}
	}
	rcu_read_unlock();
	return element;
}

static
int trace_chunk_registry_ht_element_create(
		struct sessiond_trace_chunk_registry *sessiond_registry,
		const struct trace_chunk_registry_ht_key *key)
{
	int ret = 0;
	struct trace_chunk_registry_ht_element *new_element;
	struct lttng_trace_chunk_registry *trace_chunk_registry;
	char uuid_str[UUID_STR_LEN];

	lttng_uuid_to_str(key->sessiond_uuid, uuid_str);

	trace_chunk_registry = lttng_trace_chunk_registry_create();
	if (!trace_chunk_registry) {
		ret = -1;
		goto end;
	}

	new_element = zmalloc(sizeof(*new_element));
	if (!new_element) {
		ret = -1;
		goto end;
	}

	memcpy(&new_element->key, key, sizeof(new_element->key));
	urcu_ref_init(&new_element->ref);
	cds_lfht_node_init(&new_element->ht_node);
	new_element->trace_chunk_registry = trace_chunk_registry;

	/* Attempt to publish the new element. */
	rcu_read_lock();
	while (1) {
		struct cds_lfht_node *published_node;
		struct trace_chunk_registry_ht_element *published_element;

		published_node = cds_lfht_add_unique(sessiond_registry->ht,
				trace_chunk_registry_ht_key_hash(&new_element->key),
				trace_chunk_registry_ht_key_match,
				&new_element->key,
				&new_element->ht_node);
		if (published_node == &new_element->ht_node) {
			/* New element published successfully. */
			DBG("Created trace chunk registry for sessiond {%s}",
					uuid_str);
			new_element->sessiond_trace_chunk_registry =
					sessiond_registry;
			break;
		}

		/*
		 * An equivalent element was published during the creation of
		 * this element. Attempt to acquire a reference to the one that
		 * was already published and release the reference to the copy
		 * we created if successful.
		 */
		published_element = container_of(published_node,
				typeof(*published_element), ht_node);
		if (trace_chunk_registry_ht_element_get(published_element)) {
			DBG("Acquired reference to trace chunk registry of sessiond {%s}",
					uuid_str);
			trace_chunk_registry_ht_element_put(new_element);
			new_element = NULL;
			break;
		}
		/*
		 * A reference to the previously published element could not
		 * be acquired. Hence, retry to publish our copy of the
		 * element.
		 */
	}
	rcu_read_unlock();
end:
	if (ret < 0) {
		ERR("Failed to create trace chunk registry for session daemon {%s}",
				uuid_str);
	}
	return ret;
}

struct sessiond_trace_chunk_registry *sessiond_trace_chunk_registry_create(void)
{
	struct sessiond_trace_chunk_registry *sessiond_registry =
			zmalloc(sizeof(*sessiond_registry));

	if (!sessiond_registry) {
		goto end;
	}

	sessiond_registry->ht = cds_lfht_new(DEFAULT_HT_SIZE,
			1, 0, CDS_LFHT_AUTO_RESIZE | CDS_LFHT_ACCOUNTING, NULL);
	if (!sessiond_registry->ht) {
		goto error;
	}

end:
	return sessiond_registry;
error:
	sessiond_trace_chunk_registry_destroy(sessiond_registry);
	return NULL;
}

void sessiond_trace_chunk_registry_destroy(
		struct sessiond_trace_chunk_registry *sessiond_registry)
{
	int ret = cds_lfht_destroy(sessiond_registry->ht, NULL);

	assert(!ret);
	free(sessiond_registry);
}

int sessiond_trace_chunk_registry_session_created(
		struct sessiond_trace_chunk_registry *sessiond_registry,
		const lttng_uuid sessiond_uuid)
{
	int ret = 0;
	struct trace_chunk_registry_ht_key key;
	struct trace_chunk_registry_ht_element *element;

	lttng_uuid_copy(key.sessiond_uuid, sessiond_uuid);

	element = trace_chunk_registry_ht_element_find(sessiond_registry, &key);
	if (element) {
		char uuid_str[UUID_STR_LEN];

		lttng_uuid_to_str(sessiond_uuid, uuid_str);
		DBG("Acquired reference to trace chunk registry of sessiond {%s}",
				uuid_str);
		goto end;
	} else {
		ret = trace_chunk_registry_ht_element_create(
				sessiond_registry, &key);
	}
end:
	return ret;
}

int sessiond_trace_chunk_registry_session_destroyed(
		struct sessiond_trace_chunk_registry *sessiond_registry,
		const lttng_uuid sessiond_uuid)
{
	int ret = 0;
	struct trace_chunk_registry_ht_key key;
	struct trace_chunk_registry_ht_element *element;
	char uuid_str[UUID_STR_LEN];

	lttng_uuid_to_str(sessiond_uuid, uuid_str);
	lttng_uuid_copy(key.sessiond_uuid, sessiond_uuid);

	rcu_read_lock();
	element = trace_chunk_registry_ht_element_find(sessiond_registry, &key);
	if (element) {
		DBG("Releasing reference to trace chunk registry of sessiond {%s}",
				uuid_str);
		/*
		 * Release the reference held by the session and the reference
		 * acquired through the "find" operation.
		 */
		trace_chunk_registry_ht_element_put(element);
		trace_chunk_registry_ht_element_put(element);
	} else {
		ERR("Failed to find trace chunk registry of sessiond {%s}",
				uuid_str);
		ret = -1;
	}
	rcu_read_unlock();
	return ret;
}

struct lttng_trace_chunk *
sessiond_trace_chunk_registry_get_anonymous_chunk(
		struct sessiond_trace_chunk_registry *sessiond_registry,
		const lttng_uuid sessiond_uuid,
		uint64_t session_id,
		const char *base_path)
{
	struct lttng_trace_chunk *chunk = NULL;
	struct trace_chunk_registry_ht_element *element;
	struct trace_chunk_registry_ht_key key;
	char uuid_str[UUID_STR_LEN];

	lttng_uuid_to_str(sessiond_uuid, uuid_str);

	lttng_uuid_copy(key.sessiond_uuid, sessiond_uuid);
	element = trace_chunk_registry_ht_element_find(sessiond_registry, &key);
	if (!element) {
		ERR("Failed to find trace chunk registry of sessiond {%s}",
				uuid_str);
		goto end;
	}

	chunk = lttng_trace_chunk_registry_find_or_create_anonymous_chunk(
			element->trace_chunk_registry,
			session_id,
			base_path,
			-1, -1,
			true);
	trace_chunk_registry_ht_element_put(element);
end:
	return chunk;
}

struct lttng_trace_chunk *
sessiond_trace_chunk_registry_get_chunk(
		struct sessiond_trace_chunk_registry *sessiond_registry,
		const lttng_uuid sessiond_uuid,
		uint64_t session_id, uint64_t chunk_id,
		uint64_t timestamp_begin,
		const char *base_path,
		const char *host_path_element,
		const char *session_path_element)
{
	struct lttng_trace_chunk *chunk = NULL;
	struct trace_chunk_registry_ht_element *element;
	struct trace_chunk_registry_ht_key key;
	char uuid_str[UUID_STR_LEN];

	lttng_uuid_to_str(sessiond_uuid, uuid_str);

	lttng_uuid_copy(key.sessiond_uuid, sessiond_uuid);
	element = trace_chunk_registry_ht_element_find(sessiond_registry, &key);
	if (!element) {
		ERR("Failed to find trace chunk registry of sessiond {%s}",
				uuid_str);
		goto end;
	}

	chunk = lttng_trace_chunk_registry_find_or_create_chunk(
			element->trace_chunk_registry,
			session_id, chunk_id,
			timestamp_begin,
			LTTNG_TRACE_CHUNK_PATH_FORMAT_OUTPUT_HOST_SESSION_CHUNK,
			base_path,
			host_path_element,
			session_path_element,
			-1, -1,
			true);
	trace_chunk_registry_ht_element_put(element);
end:
	return chunk;
}
