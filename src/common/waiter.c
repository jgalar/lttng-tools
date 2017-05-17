/*
 * Copyright (C) 2012 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *               2017 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License, version 2.1 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * This code is originally adapted from userspace-rcu's urcu-wait.h
 */

#include "waiter.h"
#include <urcu/uatomic.h>
#include <urcu/futex.h>
#include <assert.h>
#include "error.h"
#include <poll.h>

/*
 * Number of busy-loop attempts before waiting on futex.
 */
#define WAIT_ATTEMPTS 1000

enum waiter_state {
	/* WAITER_WAITING is compared directly (futex compares it). */
	WAITER_WAITING =	0,
	/* non-zero are used as masks. */
	WAITER_WOKEN_UP =	(1 << 0),
	WAITER_RUNNING =	(1 << 1),
	WAITER_TEARDOWN =	(1 << 2),
};

struct wait_queue {
	struct cds_wfs_stack stack;
};

#define WAIT_QUEUE_HEAD_INIT()	\
	{ .stack.head = CDS_WFS_END, .stack.lock = PTHREAD_MUTEX_INITIALIZER }

struct waiters {
	struct cds_wfs_head *head;
};

LTTNG_HIDDEN
void lttng_waiter_init(struct lttng_waiter *waiter)
{
	cds_wfs_node_init(&waiter->wait_queue_node);
	uatomic_set(&waiter->state, WAITER_WAITING);
	cmm_smp_mb();
}

/*
 * User must init "waiter" before passing its memory to waker thread.
 */
LTTNG_HIDDEN
void lttng_waiter_wait(struct lttng_waiter *waiter)
{
	unsigned int i;

	DBG("Beginning of waiter wait period");
	/* Load and test condition before read state */
	cmm_smp_rmb();
	for (i = 0; i < WAIT_ATTEMPTS; i++) {
		if (uatomic_read(&waiter->state) != WAITER_WAITING) {
			goto skip_futex_wait;
		}
		caa_cpu_relax();
	}
	while (futex_noasync(&waiter->state, FUTEX_WAIT, WAITER_WAITING,
			NULL, NULL, 0)) {
		switch (errno) {
		case EWOULDBLOCK:
			/* Value already changed. */
			goto skip_futex_wait;
		case EINTR:
			/* Retry if interrupted by signal. */
			break;	/* Get out of switch. */
		default:
			/* Unexpected error. */
			PERROR("futex_noasync");
			abort();
		}
	}
skip_futex_wait:

	/* Tell waker thread than we are running. */
	uatomic_or(&waiter->state, WAITER_RUNNING);

	/*
	 * Wait until waker thread lets us know it's ok to tear down
	 * memory allocated for struct lttng_waiter.
	 */
	for (i = 0; i < WAIT_ATTEMPTS; i++) {
		if (uatomic_read(&waiter->state) & WAITER_TEARDOWN) {
			break;
		}
		caa_cpu_relax();
	}
	while (!(uatomic_read(&waiter->state) & WAITER_TEARDOWN)) {
		poll(NULL, 0, 10);
	}
	assert(uatomic_read(&waiter->state) & WAITER_TEARDOWN);
	DBG("End of waiter wait period");
}

/*
 * Note: lttng_waiter_wake needs waiter to stay allocated throughout its
 * execution. In this scheme, the waiter owns the node memory, and we only allow
 * it to free this memory when it sees the WAITER_TEARDOWN flag.
 */
LTTNG_HIDDEN
void lttng_waiter_wake_up(struct lttng_waiter *waiter)
{
	cmm_smp_mb();
	assert(uatomic_read(&waiter->state) == WAITER_WAITING);
	uatomic_set(&waiter->state, WAITER_WOKEN_UP);
	if (!(uatomic_read(&waiter->state) & WAITER_RUNNING)) {
		if (futex_noasync(&waiter->state, FUTEX_WAKE, 1,
				NULL, NULL, 0) < 0) {
			PERROR("futex_noasync");
			abort();
		}
	}
	/* Allow teardown of struct urcu_wait memory. */
	uatomic_or(&waiter->state, WAITER_TEARDOWN);
}

LTTNG_HIDDEN
void lttng_wait_queue_init(struct lttng_wait_queue *queue)
{
	cds_wfs_init(&queue->stack);
}

/*
 * Add lttng_waiter atomically to a wait queue.
 * A full memory barrier is issued before being added to the wait queue.
 */
LTTNG_HIDDEN
void lttng_wait_queue_add(struct lttng_wait_queue *queue,
		struct lttng_waiter *waiter)
{
	cds_wfs_push(&queue->stack, &waiter->wait_queue_node);
}

LTTNG_HIDDEN
void lttng_wait_queue_wake_all(struct lttng_wait_queue *queue)
{
	struct waiters waiters;
	struct cds_wfs_node *iter, *iter_n;

	waiters.head = __cds_wfs_pop_all(&queue->stack);
	DBG("Waking all waiters enqueued in wait queue");
	/* Wake all waiters in our stack head */
	cds_wfs_for_each_blocking_safe(waiters.head, iter, iter_n) {
		struct lttng_waiter *waiter = container_of(iter,
				struct lttng_waiter, wait_queue_node);

		/* Don't wake already running threads */
		if (waiter->state & WAITER_RUNNING) {
			continue;
		}
		lttng_waiter_wake_up(waiter);
	}
}

LTTNG_HIDDEN
void lttng_wait_queue_fini(struct lttng_wait_queue *queue)
{
	cds_wfs_destroy(&queue->stack);
}
