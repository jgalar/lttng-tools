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
#include <common/string-utils/format.h>
#include <common/trace-chunk.h>
#include <common/trace-chunk-registry.h>
#include <common/hashtable/utils.h>
#include <common/hashtable/hashtable.h>
#include <common/error.h>
#include <common/utils.h>
#include <common/time.h>
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
#define GENERATED_CHUNK_NAME_LEN (2 * sizeof("YYYYmmddTHHMMSS+HHMM") + MAX_INT_DEC_LEN(uint64_t))
#define DIR_CREATION_MODE (S_IRWXU | S_IRWXG)

enum trace_chunk_mode {
	/* Value at creation. */
	TRACE_CHUNK_MODE_UNSET = 0,
	TRACE_CHUNK_MODE_USER,
	TRACE_CHUNK_MODE_OWNER,
};

struct chunk_credentials {
	bool use_current_user;
	struct lttng_credentials user;
};

struct lttng_trace_chunk {
	pthread_mutex_t lock;
	struct urcu_ref ref;
	enum trace_chunk_mode mode;
	/* Is contained within an lttng_trace_chunk_registry_element? */
	bool in_registry_element;
	bool name_overriden;
	char *name;
	/* An unset id means the chunk is anonymous. */
	LTTNG_OPTIONAL(uint64_t) id;
	LTTNG_OPTIONAL(time_t) timestamp_begin;
	LTTNG_OPTIONAL(time_t) timestamp_end;
	LTTNG_OPTIONAL(struct chunk_credentials) credentials;
	LTTNG_OPTIONAL(struct lttng_directory_handle) session_output_directory;
	LTTNG_OPTIONAL(struct lttng_directory_handle) chunk_directory;
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
int set_chunk_name(struct lttng_trace_chunk *chunk)
{
	int ret = 0;
	char start_datetime[sizeof("YYYYmmddTHHMMSS+HHMM")] = {};
	char end_datetime_suffix[sizeof("-YYYYmmddTHHMMSS+HHMM")] = {};

	if (!chunk->id.is_set || chunk->id.value == 0 ||
			chunk->name_overriden) {
		/* Anonymous chunks and chunk "0" have no name. */
		goto end;
	}
	if (!chunk->timestamp_begin.is_set) {
		ret = -1;
		goto end;
	}

	free(chunk->name);
	chunk->name = zmalloc(GENERATED_CHUNK_NAME_LEN);
	if (!chunk->name) {
		ERR("Failed to allocate trace chunk name storage");
		ret = -1;
		goto end;
	}

	ret = time_to_iso8601_str(
			chunk->timestamp_begin.value,
			start_datetime, sizeof(start_datetime));
	if (ret) {
		ERR("Failed to format trace chunk start date time");
		goto end;
	}
	if (chunk->timestamp_end.is_set) {
		*end_datetime_suffix = '-';
		ret = time_to_iso8601_str(
				chunk->timestamp_end.value,
				end_datetime_suffix + 1,
				sizeof(end_datetime_suffix));
		if (ret) {
			ERR("Failed to format trace chunk end date time");
			goto end;
		}
	}
	ret = snprintf(chunk->name, GENERATED_CHUNK_NAME_LEN, "%s%s-%" PRIu64,
			start_datetime, end_datetime_suffix, chunk->id.value);
	if (ret >= sizeof(chunk->name) || ret == -1) {
		ERR("Failed to format trace chunk name");
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
	free(chunk->name);
	chunk->name = NULL;
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
		goto error;
	}
	DBG("Chunk name set to \"%s\"", chunk->name ? : "(none)");
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
enum lttng_trace_chunk_status lttng_trace_chunk_get_name(
		const struct lttng_trace_chunk *chunk, const char **name,
		bool *name_overriden)
{
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;

	*name_overriden = chunk->name_overriden;
	if (!chunk->name) {
		status = LTTNG_TRACE_CHUNK_STATUS_NONE;
		goto end;
	}
	*name = chunk->name;
end:
	return status;
}

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_override_name(
		struct lttng_trace_chunk *chunk, const char *name)

{
	char *new_name;
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;

	if (!name || *name || strnlen(name, LTTNG_NAME_MAX) == LTTNG_NAME_MAX) {
		ERR("Attempted to set an invalid name on a trace chunk: name = %s",
				name ? : "NULL");
		status = LTTNG_TRACE_CHUNK_STATUS_INVALID_ARGUMENT;
		goto end;
	}
	if (!chunk->id.is_set) {
		ERR("Attempted to set an override name on an anonymous trace chunk: name = %s",
				name);
		status = LTTNG_TRACE_CHUNK_STATUS_INVALID_OPERATION;
		goto end;
	}
	new_name = strdup(name);
	if (!new_name) {
		ERR("Failed to allocate new trace chunk name");
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	free(chunk->name);
	chunk->name = new_name;
	chunk->name_overriden = true;
end:
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
enum lttng_trace_chunk_status lttng_trace_chunk_set_as_owner(
		struct lttng_trace_chunk *chunk,
		struct lttng_directory_handle *session_output_directory)
{
	int ret;
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;
	struct lttng_directory_handle chunk_directory_handle;

	if (chunk->mode != TRACE_CHUNK_MODE_UNSET) {
		status = LTTNG_TRACE_CHUNK_STATUS_INVALID_OPERATION;
		goto end;
	}
	if (!chunk->credentials.is_set) {
		/*
		 * Fatal error, credentials must be set before a
		 * directory is created.
		 */
		ERR("Credentials of trace chunk are unset: refusing to set session output directory");
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}

	if (chunk->name) {
		/*
		 * A nameless chunk does not need its own output directory.
		 * The session's output directory will be used.
		 */
		ret = lttng_directory_handle_create_subdirectory_as_user(
				session_output_directory,
				chunk->name,
				DIR_CREATION_MODE,
				!chunk->credentials.value.use_current_user ?
					&chunk->credentials.value.user : NULL);
		if (ret) {
			PERROR("Failed to create chunk output directory \"%s\"",
				chunk->name);
			status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
			goto end;
		}
	}
	ret = lttng_directory_handle_init_from_handle(&chunk_directory_handle,
			chunk->name,
			session_output_directory);
	if (ret) {
		/* The function already logs on all error paths. */
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	LTTNG_OPTIONAL_SET(&chunk->session_output_directory,
			lttng_directory_handle_move(session_output_directory));
	LTTNG_OPTIONAL_SET(&chunk->chunk_directory,
			lttng_directory_handle_move(&chunk_directory_handle));
	chunk->mode = TRACE_CHUNK_MODE_OWNER;
end:
	return status;
}

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_set_as_user(
		struct lttng_trace_chunk *chunk,
		struct lttng_directory_handle *chunk_directory)
{
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;

	if (chunk->mode != TRACE_CHUNK_MODE_UNSET) {
		status = LTTNG_TRACE_CHUNK_STATUS_INVALID_OPERATION;
		goto end;
	}
	if (!chunk->credentials.is_set) {
		ERR("Credentials of trace chunk are unset: refusing to set chunk output directory");
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	LTTNG_OPTIONAL_SET(&chunk->chunk_directory,
			lttng_directory_handle_move(chunk_directory));
	chunk->mode = TRACE_CHUNK_MODE_USER;
end:
	return status;
}

LTTNG_HIDDEN
enum lttng_trace_chunk_status lttng_trace_chunk_get_chunk_directory_handle(
		const struct lttng_trace_chunk *chunk,
		const struct lttng_directory_handle **handle)
{
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;

	if (!chunk->chunk_directory.is_set) {
		status = LTTNG_TRACE_CHUNK_STATUS_NONE;
		goto end;
	}

	*handle = &chunk->chunk_directory.value;
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
		ERR("Credentials of trace chunk are unset: refusing to create subdirectory \"%s\"",
				path);
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	if (!chunk->chunk_directory.is_set) {
		ERR("Attempted to create trace chunk subdirectory \"%s\" before setting the chunk output directory",
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
	ret = lttng_directory_handle_create_subdirectory_recursive_as_user(
			&chunk->chunk_directory.value, path,
			DIR_CREATION_MODE,
			chunk->credentials.value.use_current_user ?
					NULL : &chunk->credentials.value.user);
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
enum lttng_trace_chunk_status lttng_trace_chunk_open_file(
		struct lttng_trace_chunk *chunk, const char *file_path,
		int flags, mode_t mode, int *out_fd)
{
	int ret;
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;

	DBG("Opening trace chunk file \"%s\"", file_path);
	if (!chunk->credentials.is_set) {
		/*
		 * Fatal error, credentials must be set before a
		 * file is created.
		 */
		ERR("Credentials of trace chunk are unset: refusing to open file \"%s\"",
				file_path);
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	if (!chunk->chunk_directory.is_set) {
		ERR("Attempted to open trace chunk file \"%s\" before setting the chunk output directory",
				file_path);
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	ret = lttng_directory_handle_open_file_as_user(
			&chunk->chunk_directory.value, file_path, flags, mode,
			chunk->credentials.value.use_current_user ?
					NULL : &chunk->credentials.value.user);
	if (ret < 0) {
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	*out_fd = ret;
end:	
	return status;
}

LTTNG_HIDDEN
int lttng_trace_chunk_unlink_file(struct lttng_trace_chunk *chunk,
		const char *file_path)
{
	int ret;
	enum lttng_trace_chunk_status status = LTTNG_TRACE_CHUNK_STATUS_OK;

	DBG("Unlinking trace chunk file \"%s\"", file_path);
	if (!chunk->credentials.is_set) {
		/*
		 * Fatal error, credentials must be set before a
		 * directory is created.
		 */
		ERR("Credentials of trace chunk are unset: refusing to unlink file \"%s\"",
				file_path);
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	if (!chunk->chunk_directory.is_set) {
		ERR("Attempted to unlink trace chunk file \"%s\" before setting the chunk output directory",
				file_path);
		status = LTTNG_TRACE_CHUNK_STATUS_ERROR;
		goto end;
	}
	ret = lttng_directory_handle_unlink_file_as_user(
			&chunk->chunk_directory.value, file_path,
			chunk->credentials.value.use_current_user ?
					NULL : &chunk->credentials.value.user);
	if (ret < 0) {
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
			call_rcu(&element->rcu_node,
					free_lttng_trace_chunk_registry_element);
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
		struct lttng_trace_chunk *chunk, uint64_t session_id)
{
	struct lttng_trace_chunk_registry_element *element =
			zmalloc(sizeof(*element));

	if (!element) {
		goto end;
	}
	cds_lfht_node_init(&element->trace_chunk_registry_ht_node);
	element->session_id = session_id;

	element->chunk = *chunk;
	lttng_trace_chunk_init(&element->chunk);
	if (chunk->session_output_directory.is_set) {
		element->chunk.session_output_directory.value =
				lttng_directory_handle_move(
					&chunk->session_output_directory.value);
	}
	if (chunk->chunk_directory.is_set) {
		element->chunk.chunk_directory.value =
				lttng_directory_handle_move(
					&chunk->chunk_directory.value);
	}
	/*
	 * The original chunk becomes invalid; the name attribute is transferred
	 * to the new chunk instance.
	 */
	chunk->name = NULL;
	element->chunk.in_registry_element = true;
end:
	return element;
}

LTTNG_HIDDEN
struct lttng_trace_chunk *
lttng_trace_chunk_registry_publish_chunk(
		struct lttng_trace_chunk_registry *registry,
		uint64_t session_id, struct lttng_trace_chunk *chunk)
{
	struct lttng_trace_chunk_registry_element *element;
	unsigned long element_hash;

	element = lttng_trace_chunk_registry_element_create_from_chunk(chunk,
			session_id);
	if (!element) {
		goto end;
	}
	/*
	 * chunk is now invalid, the only valid operation is a 'put' from the
	 * caller.
	 */
	chunk = NULL;
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
			/* Successfully published the new element. */
		        element->registry = registry;
			/* Acquire a reference for the caller. */
			if (lttng_trace_chunk_get(&element->chunk)) {
				break;
			} else {
				/*
				 * Another thread concurrently unpublished the
				 * trace chunk. This is currently unexpected.
				 *
				 * Re-attempt to publish.
				 */
				ERR("Attemp to publish a trace chunk to the chunk registry raced with a trace chunk deletion");
				continue;
			}
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

/*
 * Note that the caller must be registered as an RCU thread.
 * However, it does not need to hold the RCU read lock. The RCU read lock is
 * acquired to perform the look-up in the registry's hash table and held until
 * after a reference to the "found" trace chunk is acquired.
 *
 * IOW, holding a reference guarantees the existence of the object for the
 * caller.
 */
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
