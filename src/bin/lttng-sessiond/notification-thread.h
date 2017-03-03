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

#ifndef NOTIFICATION_THREAD_H
#define NOTIFICATION_THREAD_H

#include <urcu/wfcqueue.h>
#include <lttng/trigger/trigger.h>
#include <pthread.h>

enum notification_command_type {
	NOTIFICATION_COMMAND_TYPE_NEW_TRIGGER,
};

struct notification_command {
	/* Futex on which to wait for command reply. */
	int32_t futex;
	enum lttng_error_code result;
	enum notification_command_type type;
};

struct notification_new_trigger_command {
	struct notification_command parent;
	/* Set to NULL if ownership was transfered. */
	struct lttng_trigger *trigger;
};

/* Data passed to the thread on initialization. */
struct notification_thread_data {
	/*
	 * Queue of struct notification command.
	 * event_fd must be WRITE(2) to signal that a new command
	 * has been enqueued.
	 */
	struct notification_cmd_queue {
		int event_fd;
		struct cds_list_head list;
		pthread_mutex_t lock;
	} cmd_queue;
};

struct notification_command *notification_new_trigger_command_create(
		struct lttng_trigger *trigger);
struct notification_command *notification_new_trigger_command_destroy(
		struct lttng_trigger *trigger);

void *thread_notification(void *data);

struct notification_thread_data *notification_init_data(void);
void notification_destroy_data(struct notification_thread_data *data);

#endif /* NOTIFICATION_THREAD_H */
