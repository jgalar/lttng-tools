/*
 * Copyright (C) 2019 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
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
#include <common/trace-chunk.h>
#include <common/trace-chunk-registry.h>
#include <common/hashtable/utils.h>
#include <common/hashtable/hashtable.h>
#include <common/error.h>
#include <common/utils.h>
#include <common/optional.h>
#include <common/compat/directory-handle.h>
#include <common/credentials.h>
#include <common/defaults.h>

#include <urcu/ref.h>
#include <urcu/rculfhash.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>

/*
 * Two ISO 8601-compatible timestamps, separated by a hypen, followed an
 * index, i.e. <start-iso-8601>-<end-iso-8601>-<id-uint64_t>.
 */
#define CHUNK_NAME_MAX_LEN (2 * sizeof("YYYYmmddTHHMMSS+HHMM") + sizeof("18446744073709551615"))
#define DIR_CREATION_MODE (S_IRWXU | S_IRWXG)

struct chunk_credentials {
	bool use_current_user;
	struct lttng_credentials user;
};

struct lttng_trace_chunk {
	pthread_mutex_t lock;
	struct urcu_ref ref;
	/* An unset id means the chunk is anonymous. */
	LTTNG_OPTIONAL(uint64_t) id;
	LTTNG_OPTIONAL(time_t) timestamp_begin;
	LTTNG_OPTIONAL(time_t) timestamp_end;
	LTTNG_OPTIONAL(struct chunk_credentials) credentials;
	LTTNG_OPTIONAL(struct lttng_directory_handle) session_output_directory;
	LTTNG_OPTIONAL(struct lttng_directory_handle) chunk_directory;
	bool in_registry_element;
	char name[CHUNK_NAME_MAX_LEN];
};

/* A trace chunk is uniquely identified by its (session id, chunk id) tuple. */
struct lttng_trace_chunk_registry_element {
	uint64_t session_id;
	struct lttng_trace_chunk chunk;
	/* Weak and only set when added. */
	struct lttng_trace_chunk_registry *registry;
	struct cds_lfht_node trace_chunk_registry_ht_node;
	/* call_rcu delayed reclaim. */
	struct rcu_head rcu_node;
};

struct lttng_trace_chunk_registry {
	struct cds_lfht *ht;
};

static
bool lttng_trace_chunk_registry_element_equals(
		const struct lttng_trace_chunk_registry_element *a,
		const struct lttng_trace_chunk_registry_element *b)
{
	if (a->session_id != b->session_id) {
		goto not_equal;
	}
	if (a->chunk.id.is_set != b->chunk.id.is_set) {
		goto not_equal;
	}
	if (a->chunk.id.is_set && a->chunk.id.value != b->chunk.id.value) {
		goto not_equal;
	}
	return true;
not_equal:
	return false;
}

static
int lttng_trace_chunk_registry_element_match(struct cds_lfht_node *node,
		const void *key)
{
	const struct lttng_trace_chunk_registry_element *element_a, *element_b;

	element_a = (const struct lttng_trace_chunk_registry_element *) key;
	element_b = caa_container_of(node, typeof(*element_b),
			trace_chunk_registry_ht_node);
	return lttng_trace_chunk_registry_element_equals(element_a, element_b);
}

static
unsigned long lttng_trace_chunk_registry_element_hash(
		const struct lttng_trace_chunk_registry_element *element)
{
	unsigned long hash = hash_key_u64(&element->session_id,
			lttng_ht_seed);

	if (element->chunk.id.is_set) {
		hash ^= hash_key_u64(&element->chunk.id.value, lttng_ht_seed);
	}
	return hash;
}

static
int set_datetime_str_from_timestamp(time_t timestamp, char *str, size_t len)
{
	int ret = 0;
	struct tm *tm_result;
	struct tm tm_storage;
	size_t strf_ret;

        tm_result = localtime_r(&timestamp, &tm_storage);
	if (!tm_result) {
		ret = -1;
		PERROR("Failed to break down trace chunk timestamp to tm structure");
		goto end;
	}

	strf_ret = strftime(str, len, "%Y%m%dT%H%M%S%z",
			tm_result);
	if (strf_ret == 0) {
		ret = -1;
		ERR("Failed to format timestamp as local time");
		goto end;
	}
end:
	return ret;
}

static
int set_chunk_name(struct lttng_trace_chunk *chunk)
{
	int ret;
	char start_datetime[sizeof("YYYYmmddTHHMMSS+HHMM")] = {};
	char end_datetime_suffix[sizeof("-YYYYmmddTHHMMSS+HHMM")] = {};

	if (!chunk->id.is_set || chunk->id.value == 0 ||
			!chunk->timestamp_begin.is_set) {
		goto end;
	}

	ret = set_datetime_str_from_timestamp(
			chunk->timestamp_begin.value,
			start_datetime, sizeof(start_datetime));
	if (ret) {
		goto end;
	}
	if (chunk->timestamp_end.is_set) {
		*end_datetime_suffix = '-';
		ret = set_datetime_str_from_timestamp(
				chunk->timestamp_end.value,
				end_datetime_suffix + 1,
				sizeof(end_datetime_suffix));
		if (ret) {
			goto end;
		}
	}
	ret = snprintf(chunk->name, sizeof(chunk->name), "%s%s-%" PRIu64,
			start_datetime, end_datetime_suffix, chunk->id.value);
	if (ret >= sizeof(chunk->name) || ret == -1) {
		ret = -1;
		goto end;
	}
end:
	return ret;
}

static
void lttng_trace_chunk_init(struct lttng_trace_chunk *chunk)
{
	urcu_ref_init(&chunk->ref);
	pthread_mutex_init(&chunk->lock, NULL);	
}

static
void lttng_trace_chunk_fini(struct lttng_trace_chunk *chunk)
{
	if (chunk->session_output_directory.is_set) {
		lttng_directory_handle_fini(
				&chunk->session_output_directory.value);
	}
	if (chunk->chunk_directory.is_set) {
		lttng_directory_handle_fini(&chunk->chunk_directory.value);
	}
	pthread_mutex_destroy(&chunk->lock);
}

static
struct lttng_trace_chunk *lttng_trace_chunk_allocate(void)
{
	struct lttng_trace_chunk *chunk = NULL;

	chunk = zmalloc(sizeof(*chunk));
	if (!chunk) {
		ERR("Failed to allocate trace chunk");
		goto end;
	}
	lttng_trace_chunk_init(chunk);
end:
	return chunk;
}

LTTNG_HIDDEN
struct lttng_trace_chunk *lttng_trace_chunk_create_anonymous(void)
{
	DBG("Creating anonymous trace chunk");
	return lttng_trace_chunk_allocate();
}

LTTNG_HIDDEN
struct lttng_trace_chunk *lttng_trace_chunk_create(
		uint64_t chunk_id, time_t chunk_creation_time)
{
	int ret;
	struct lttng_trace_chunk *chunk;
	char chunk_creation_datetime[16] = "UNKNOWN";
	struct tm timeinfo_buf, *timeinfo;

	timeinfo = localtime_r(&chunk_creation_time, &timeinfo_buf);
	if (timeinfo) {
		/* Don't fail because of this; it is only used for logging. */
		strftime(chunk_creation_datetime,
				sizeof(chunk_creation_datetime),
				"%Y%m%d-%H%M%S", timeinfo);
	}
	DBG("Creating trace chunk: chunk_id = %" PRIu64 ", creation time = %s",
			chunk_id, chunk_creation_datetime);
	chunk = lttng_trace_chunk_allocate();
	if (!chunk) {
		goto end;
	}
	LTTNG_OPTIONAL_SET(&chunk->id, chunk_id);
	LTTNG_OPTIONAL_SET(&chunk->timestamp_begin, chunk_creation_time);
	ret = set_chunk_name(chunk);
	if (ret) {
		ERR("Failed to format chunk name");
		goto error;
	}
	DBG("Chunk name set to \"%s\"", chunk->name);
end:
	return chunk;
error:
	lttng_trace_chunk_put(chunk);
	return NULL;
}

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_get_id(
		const struct lttng_trace_chunk *chunk, uint64_t *id)
{
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;

	if (chunk->id.is_set) {
		*id = chunk->id.value;
	} else {
		status = LTTNG_TRACE_CHUNK_STATUS_NONE;
	}
	return status;
}

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_get_creation_timestamp(
		const struct lttng_trace_chunk *chunk, time_t *creation_ts)

{
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;

	if (chunk->timestamp_begin.is_set) {
		*creation_ts = chunk->timestamp_begin.value;
	} else {
		status = LTTNG_TRACE_CHUNK_STATUS_NONE;
	}
	return status;
}

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_get_credentials(
		const struct lttng_trace_chunk *chunk,
		struct lttng_credentials *credentials)
{
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;

	if (chunk->credentials.is_set) {
		if (chunk->credentials.value.use_current_user) {
			credentials->uid = geteuid();
			credentials->gid = getegid();
		} else {
			*credentials = chunk->credentials.value.user;
		}
	} else {
		status = LTTNG_TRACE_CHUNK_STATUS_NONE;
	}
	return status;
}

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_set_credentials(
		struct lttng_trace_chunk *chunk,
		const struct lttng_credentials *user_credentials)
{
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;
	const struct chunk_credentials credentials = {
		.user = *user_credentials,
		.use_current_user = false,
	};

	if (chunk->credentials.is_set) {
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	LTTNG_OPTIONAL_SET(&chunk->credentials, credentials);
end:
	return status;
}

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_set_credentials_current_user(
		struct lttng_trace_chunk *chunk)
{
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;
	const struct chunk_credentials credentials = {
		.use_current_user = true,
	};

	if (chunk->credentials.is_set) {
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	LTTNG_OPTIONAL_SET(&chunk->credentials, credentials);
end:
	return status;
}

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_set_session_output_directory(
		struct lttng_trace_chunk *chunk,
		const char *session_output_directory_path,
		bool create_directory)
{
	int ret;
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;
	struct lttng_directory_handle session_output_directory_handle = {};
	struct lttng_directory_handle chunk_directory_handle = {};
	char chunk_directory_path_buf[LTTNG_PATH_MAX];
	const char *chunk_directory_path;

	DBG("Setting trace chunk session output directory to \"%s\"",
			session_output_directory_path);
	if (*session_output_directory_path != '/') {
		ERR("Session output directory path \"%s\" is not absolute",
				session_output_directory_path);
		status = LTTNG_TRACE_CHUNK_STATUS_INVALID_ARGUMENT;
		goto end;
	}
	if (chunk->session_output_directory.is_set) {
		ERR("Trace chunk session output directory path is already set");
		status=  LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	if (!*chunk->name) {
		/*
		 * Chunk is unnamed, its directory's path is that of the
		 * session.
		 */
		chunk_directory_path = session_output_directory_path;
	} else {
		ret = snprintf(chunk_directory_path_buf, sizeof(chunk_directory_path_buf),
				"%s/%s", session_output_directory_path,
				chunk->name);
		if (ret >= sizeof(chunk_directory_path_buf) || ret == -1) {
			ERR("Failed to format trace chunk directory path");
			status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
			goto end;
		}
		chunk_directory_path = chunk_directory_path_buf;
	}
	if (create_directory) {
		int uid, gid;

		if (!chunk->credentials.is_set) {
			/*
			 * Fatal error, credentials must be set before a
			 * directory is created.
			 */
			ERR("Credentials of trace chunk are unset: refusing to create session output directory");
			status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
			goto end;
		}
		if (chunk->credentials.value.use_current_user) {
			uid = -1;
			gid = -1;
		} else {
			uid = chunk->credentials.value.user.uid;
			gid = chunk->credentials.value.user.gid;
		}

		DBG("Creating trace chunk and session output directories \"%s\"",
				chunk_directory_path);
		ret = utils_mkdir_recursive(chunk_directory_path,
				DIR_CREATION_MODE, uid, gid);
		if (ret) {
			PERROR("Failed to create chunk and session output directories \"%s\"",
					chunk_directory_path);
			status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
			goto end;
		}
	}

	ret = lttng_directory_handle_init(&session_output_directory_handle,
			session_output_directory_path);
	if (ret) {
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	LTTNG_OPTIONAL_SET(&chunk->session_output_directory,
			session_output_directory_handle);
	ret = lttng_directory_handle_init(&chunk_directory_handle,
			chunk_directory_path);
	if (ret) {
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	LTTNG_OPTIONAL_SET(&chunk->chunk_directory, chunk_directory_handle);
end:
	return status;
}

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_create_subdirectory(
		const struct lttng_trace_chunk *chunk,
		const char *path)
{
	int ret;
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;

	DBG("Creating trace chunk subdirectory \"%s\"", path);
	if (!chunk->credentials.is_set) {
		/*
		 * Fatal error, credentials must be set before a
		 * directory is created.
		 */
		ERR("Credentials of trace chunk are unset: refusing to create subdirectory");
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	if (!chunk->chunk_directory.is_set) {
		ERR("Attempted to create trace chunk subdirectory \"%s\" before setting the session output directory",
				path);
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	if (*path == '/') {
		ERR("Refusing to create absolute trace chunk directory \"%s\"",
				path);
		status = LTTNG_TRACE_CHUNK_STATUS_INVALID_ARGUMENT;
		goto end;
	}
	if (chunk->credentials.value.use_current_user) {
		ret = lttng_directory_handle_create_subdirectory_recursive(
				&chunk->chunk_directory.value, path,
				DIR_CREATION_MODE);
	} else {
		ret = lttng_directory_handle_create_subdirectory_recursive_as_user(
				&chunk->chunk_directory.value, path,
				DIR_CREATION_MODE,
				&chunk->credentials.value.user);
	}
	if (ret) {
		PERROR("Failed to create trace chunk subdirectory \"%s\"",
				path);
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
end:
	return status;
}

LTTNG_HIDDEN
bool lttng_trace_chunk_get(struct lttng_trace_chunk *chunk)
{
	return urcu_ref_get_unless_zero(&chunk->ref);
}

static
void free_lttng_trace_chunk_registry_element(struct rcu_head *node)
{
	struct lttng_trace_chunk_registry_element *element =
			container_of(node, typeof(*element), rcu_node);

	lttng_trace_chunk_fini(&element->chunk);
	free(element);
}

static
void lttng_trace_chunk_release(struct urcu_ref *ref)
{
	struct lttng_trace_chunk *chunk = container_of(ref, typeof(*chunk),
			ref);

	if (chunk->in_registry_element) {
		struct lttng_trace_chunk_registry_element *element;

		element = container_of(chunk, typeof(*element), chunk);
		if (element->registry) {
			rcu_read_lock();
			cds_lfht_del(element->registry->ht,
					&element->trace_chunk_registry_ht_node);
			rcu_read_unlock();
		} else {
			/* Never published, can be free'd immediately. */
			free_lttng_trace_chunk_registry_element(
					&element->rcu_node);
		}
	} else {
		/* Not RCU-protected, free immediately. */
		lttng_trace_chunk_fini(chunk);
		free(chunk);
	}
}

LTTNG_HIDDEN
void lttng_trace_chunk_put(struct lttng_trace_chunk *chunk)
{
	if (!chunk) {
		return;
	}
	assert(chunk->ref.refcount);
	urcu_ref_put(&chunk->ref, lttng_trace_chunk_release);
}

LTTNG_HIDDEN
struct lttng_trace_chunk_registry *lttng_trace_chunk_registry_create(void)
{
	struct lttng_trace_chunk_registry *registry;

	registry = zmalloc(sizeof(*registry));
	if (!registry) {
		goto end;
	}

	registry->ht = cds_lfht_new(DEFAULT_HT_SIZE, 1, 0,
			CDS_LFHT_AUTO_RESIZE | CDS_LFHT_ACCOUNTING, NULL);
	if (!registry->ht) {
		goto error;
	}
end:
	return registry;
error:
	lttng_trace_chunk_registry_destroy(registry);
	goto end;
}

LTTNG_HIDDEN
void lttng_trace_chunk_registry_destroy(
		struct lttng_trace_chunk_registry *registry)
{
	if (!registry) {
		return;
	}
	if (registry->ht) {
		int ret = cds_lfht_destroy(registry->ht, NULL);
		assert(!ret);
	}
	free(registry);
}

static
struct lttng_trace_chunk_registry_element *
lttng_trace_chunk_registry_element_create_from_chunk(
		const struct lttng_trace_chunk *chunk, uint64_t session_id)
{
	int ret;
	struct lttng_trace_chunk_registry_element *element =
			zmalloc(sizeof(*element));

	if (!element) {
		goto end;
	}
	cds_lfht_node_init(&element->trace_chunk_registry_ht_node);
	element->session_id = session_id;

	element->chunk = *chunk;
	lttng_trace_chunk_init(&element->chunk);
	element->chunk.in_registry_element = true;
	if (chunk->session_output_directory.is_set) {
		ret = lttng_directory_handle_copy(
				&chunk->session_output_directory.value,
				&element->chunk.session_output_directory.value);
		if (ret) {
			ERR("Failed to copy trace chunk session output directory handle");
			goto error;
		}
	}
	if (chunk->chunk_directory.is_set) {
		ret = lttng_directory_handle_copy(
				&chunk->chunk_directory.value,
				&element->chunk.chunk_directory.value);
		if (ret) {
			ERR("Failed to copy trace chunk directory handle");
			goto error;
		}
	}
end:
	return element;
error:
	lttng_trace_chunk_put(&element->chunk);
	return NULL;
}

LTTNG_HIDDEN
struct lttng_trace_chunk *
lttng_trace_chunk_registry_publish_chunk(
		struct lttng_trace_chunk_registry *registry,
		uint64_t session_id, const struct lttng_trace_chunk *chunk)
{
	struct lttng_trace_chunk_registry_element *element;
	unsigned long element_hash;

	element = lttng_trace_chunk_registry_element_create_from_chunk(chunk,
			session_id);
	if (!element) {
		goto end;
	}
	element_hash = lttng_trace_chunk_registry_element_hash(element);

	rcu_read_lock();
	while (1) {
		struct cds_lfht_node *published_node;
		struct lttng_trace_chunk *published_chunk;
		struct lttng_trace_chunk_registry_element *published_element;

		published_node = cds_lfht_add_unique(registry->ht,
			        element_hash,
				lttng_trace_chunk_registry_element_match,
			        element,
				&element->trace_chunk_registry_ht_node);
		if (published_node == &element->trace_chunk_registry_ht_node) {
			/* Successfully published. */
		        element->registry = registry;
			break;
		}

		/*
		 * An equivalent trace chunk was published before this trace
		 * chunk. Attempt to acquire a reference to the one that was
		 * already published and release the reference to the copy we
		 * created if successful.
		 */
		published_element = container_of(published_node,
				typeof(*published_element),
				trace_chunk_registry_ht_node);
		published_chunk = &published_element->chunk;
		if (lttng_trace_chunk_get(published_chunk)) {
			lttng_trace_chunk_put(&element->chunk);
			element = published_element;
			break;
		}
		/*
		 * A reference to the previously published trace chunk could not
		 * be acquired. Hence, retry to publish our copy of the trace 
		 * chunk.
		 */
	}
	rcu_read_unlock();
end:
	return element ? &element->chunk : NULL;
}

static
struct lttng_trace_chunk *_lttng_trace_chunk_registry_find_chunk(
		const struct lttng_trace_chunk_registry *registry,
		uint64_t session_id, uint64_t *chunk_id)
{
	const struct lttng_trace_chunk_registry_element target_element = {
		.chunk.id.is_set = !!chunk_id,
		.chunk.id.value = chunk_id ? *chunk_id : 0,
		.session_id = session_id,
	};
	const unsigned long element_hash =
			lttng_trace_chunk_registry_element_hash(
				&target_element);
	struct cds_lfht_node *published_node;
	struct lttng_trace_chunk_registry_element *published_element;
	struct lttng_trace_chunk *published_chunk = NULL;
	struct cds_lfht_iter iter;

	rcu_read_lock();
	cds_lfht_lookup(registry->ht,
			element_hash,
			lttng_trace_chunk_registry_element_match,
			&target_element,
			&iter);
	published_node = cds_lfht_iter_get_node(&iter);
	if (!published_node) {
		goto end;
	}

	published_element = container_of(published_node,
			typeof(*published_element),
			trace_chunk_registry_ht_node);
	if (lttng_trace_chunk_get(&published_element->chunk)) {
		published_chunk = &published_element->chunk;
	}
end:
	rcu_read_unlock();
	return published_chunk;
}

LTTNG_HIDDEN
struct lttng_trace_chunk *
lttng_trace_chunk_registry_find_chunk(
		const struct lttng_trace_chunk_registry *registry,
		uint64_t session_id, uint64_t chunk_id)
{
        return _lttng_trace_chunk_registry_find_chunk(registry,
			session_id, &chunk_id);
}

LTTNG_HIDDEN
struct lttng_trace_chunk *
lttng_trace_chunk_registry_find_anonymous_chunk(
		const struct lttng_trace_chunk_registry *registry,
		uint64_t session_id)
{
        return _lttng_trace_chunk_registry_find_chunk(registry,
			session_id, NULL);
}
