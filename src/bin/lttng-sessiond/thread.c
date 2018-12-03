/*
 * Copyright (C) 2018 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
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

#include "thread.h"
#include <urcu/list.h>
#include <urcu/ref.h>
#include <pthread.h>
#include <common/macros.h>
#include <common/error.h>
#include <common/defaults.h>

static struct thread_list {
	struct cds_list_head head;
	pthread_mutex_t lock;
} thread_list = {
	.head = CDS_LIST_HEAD_INIT(thread_list.head),
	.lock = PTHREAD_MUTEX_INITIALIZER,
};

struct lttng_thread {
	struct urcu_ref ref;
	/*
	 * Only protects 'has_returned' as it is the only user-mutable
	 * attribute.
	 */
	pthread_mutex_t lock;
	struct cds_list_head node;
	pthread_t thread;
	const char *name;
	/* Main thread function */
	lttng_thread_entry_point entry;
	/*
	 * Thread-specific shutdown method. Allows threads to implement their
	 * own shutdown mechanism as some of them use a structured message
	 * passed through a command queue and some rely on a dedicated "quit"
	 * pipe.
	 */
	lttng_thread_shutdown_cb shutdown;
	lttng_thread_cleanup_cb cleanup;
	/* Thread implementation-specific data. */
	void *data;
	bool has_returned;
};

static
void lttng_thread_destroy(struct lttng_thread *thread)
{
	if (thread->cleanup) {
		thread->cleanup(thread->data);
	}
	pthread_mutex_destroy(&thread->lock);
	free(thread);
}

static
void lttng_thread_release(struct urcu_ref *ref)
{
	lttng_thread_destroy(container_of(ref, struct lttng_thread, ref));
}

static
void *launch_thread(void *data)
{
	void *ret;
	struct lttng_thread *thread = (struct lttng_thread *) data;

	DBG("Launching \"%s\" thread", thread->name);
	ret = thread->entry(thread->data);
	DBG("Thread \"%s\" has returned", thread->name);

	pthread_mutex_lock(&thread->lock);
	thread->has_returned = true;
	pthread_mutex_unlock(&thread->lock);
	return ret;
}

struct lttng_thread *lttng_thread_create(const char *name,
		lttng_thread_entry_point entry,
		lttng_thread_shutdown_cb shutdown,
		lttng_thread_cleanup_cb cleanup,
		void *thread_data)
{
	int ret;
	struct lttng_thread *thread;

	thread = zmalloc(sizeof(*thread));
	if (!thread) {
		goto error;
	}

	urcu_ref_init(&thread->ref);
	pthread_mutex_init(&thread->lock, NULL);
	CDS_INIT_LIST_HEAD(&thread->node);
	thread->name = name;
	thread->entry = entry;
	thread->shutdown = shutdown;
	thread->cleanup = cleanup;
	thread->data = thread_data;

	pthread_mutex_lock(&thread_list.lock);
	ret = pthread_create(&thread->thread, default_pthread_attr(),
			launch_thread, thread);
	if (ret) {
		PERROR("Failed to create \"%s\" thread", thread->name);
		goto error;
	}
	/*
	 * Add the thread at the head of the list to shutdown threads in the
	 * opposite order of their creation. A reference is taken for the
	 * thread list which will be released on shutdown of the thread.
	 */
	(void) lttng_thread_get(thread);
	cds_list_add(&thread->node, &thread_list.head);
	pthread_mutex_unlock(&thread_list.lock);

	return thread;
error:
	lttng_thread_destroy(thread);
	thread = NULL;
	return thread;
}

bool lttng_thread_get(struct lttng_thread *thread)
{
	return urcu_ref_get_unless_zero(&thread->ref);
}

void lttng_thread_put(struct lttng_thread *thread)
{
	assert(thread->ref.refcount);
	urcu_ref_put(&thread->ref, lttng_thread_release);
}

const char *lttng_thread_get_name(const struct lttng_thread *thread)
{
	return thread->name;
}

static
bool _lttng_thread_shutdown(struct lttng_thread *thread)
{
	int ret;
	void *status;
	bool result = true;

	pthread_mutex_lock(&thread->lock);
	DBG("Shutting down \"%s\" thread", thread->name);
	if (thread->shutdown && !thread->has_returned) {
		result = thread->shutdown(thread->data);
		if (!result) {
			pthread_mutex_unlock(&thread->lock);
			result = false;
			goto end;
		}
	}
	pthread_mutex_unlock(&thread->lock);

	cds_list_del(&thread->node);
	ret = pthread_join(thread->thread, &status);
	if (ret) {
		PERROR("Failed to join \"%s\" thread", thread->name);
		result = false;
	}

end:
	return result;
}

bool lttng_thread_shutdown(struct lttng_thread *thread)
{
	bool result;

	pthread_mutex_lock(&thread_list.lock);
	result = _lttng_thread_shutdown(thread);
	cds_list_del(&thread->node);
	/* Release the list's reference to the thread. */
	lttng_thread_put(thread);
	pthread_mutex_unlock(&thread_list.lock);
	return result;
}

void lttng_thread_shutdown_all(void)
{
	struct lttng_thread *thread, *tmp;

	pthread_mutex_lock(&thread_list.lock);
	cds_list_for_each_entry_safe(thread, tmp, &thread_list.head, node) {
		bool result = _lttng_thread_shutdown(thread);

		if (!result) {
			ERR("Failed to shutdown thread \"%s\"", thread->name);
		}
	}
	pthread_mutex_unlock(&thread_list.lock);
}
