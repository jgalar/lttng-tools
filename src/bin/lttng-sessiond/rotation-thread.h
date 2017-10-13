/*
 * Copyright (C) 2017 - Julien Desfossez <jdesfossez@efficios.com>
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

#ifndef ROTATION_THREAD_H
#define ROTATION_THREAD_H

#include <urcu/list.h>
#include <urcu.h>
#include <urcu/rculfhash.h>
#include <lttng/domain.h>
#include <common/pipe.h>
#include <common/compat/poll.h>
#include <common/hashtable/hashtable.h>
#include <pthread.h>
#include "session.h"

enum rotation_thread_commands {
	ROTATION_THREAD_SUBSCRIBE_SESSION_USAGE = 0,
};

struct rotation_thread_msg {
	enum rotation_thread_commands cmd;
	union {
		/* ROTATION_THREAD_SUBSCRIBE_SESSION_USAGE */
		struct {
			struct ltt_session *session;
			uint64_t size;
		} LTTNG_PACKED subscribe_session_usage;
	};
};

struct rotation_channel_key {
	uint64_t key;
	enum lttng_domain_type domain;
};

struct rotation_channel_info {
	uint64_t session_id;
	struct rotation_channel_key channel_key;
	struct cds_lfht_node rotate_channels_ht_node;
};

struct rotation_thread_handle {
	/*
	 * Read side of pipes used to communicate with the rotation thread.
	 */
	/* Notification from the consumers */
	int ust32_consumer;
	int ust64_consumer;
	int kernel_consumer;
	/* quit pipe */
	int thread_quit_pipe;
	/* Timer thread wakeup pipe */
	int rotate_timer_pipe;
	/* thread_manage_clients command pipe */
	int client_rotate_pipe;
};

struct rotation_thread_state {
	struct lttng_poll_event events;
};

/* rotation_thread_data takes ownership of the channel rotate pipes. */
struct rotation_thread_handle *rotation_thread_handle_create(
		struct lttng_pipe *ust32_channel_rotate_pipe,
		struct lttng_pipe *ust64_channel_rotate_pipe,
		struct lttng_pipe *kernel_channel_rotate_pipe,
		struct lttng_pipe *client_rotate_pipe,
		int thread_quit_pipe, int rotate_timer_pipe);

void rotation_thread_handle_destroy(
		struct rotation_thread_handle *handle);

void rotation_thread_quit(struct rotation_thread_handle *handle);

int rotate_add_channel_pending(uint64_t key, enum lttng_domain_type domain,
		struct ltt_session *session);

void *thread_rotation(void *data);

#endif /* ROTATION_THREAD_H */
