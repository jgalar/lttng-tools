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

#ifndef NOTIFICATION_THREAD_COMMANDS_H
#define NOTIFICATION_THREAD_COMMANDS_H

#include <lttng/domain.h>
#include <lttng/lttng-error.h>
#include <urcu/rculfhash.h>
#include "notification-thread.h"
#include "notification-thread-internal.h"
#include "notification-thread-events.h"
#include "consumer.h"
#include <common/waiter.h>

struct notification_thread_data;
struct lttng_trigger;

enum notification_thread_command_type {
	NOTIFICATION_COMMAND_TYPE_REGISTER_TRIGGER,
	NOTIFICATION_COMMAND_TYPE_UNREGISTER_TRIGGER,
	NOTIFICATION_COMMAND_TYPE_CREATE_SESSION,
	NOTIFICATION_COMMAND_TYPE_DESTROY_SESSION,
	NOTIFICATION_COMMAND_TYPE_ADD_CHANNEL,
	NOTIFICATION_COMMAND_TYPE_REMOVE_CHANNEL,
	NOTIFICATION_COMMAND_TYPE_BEGIN_ROTATION,
	NOTIFICATION_COMMAND_TYPE_END_ROTATION,
	NOTIFICATION_COMMAND_TYPE_QUIT,
};

struct notification_thread_command {
	struct cds_list_head cmd_list_node;

	enum notification_thread_command_type type;
	union {
		/* Register/Unregister trigger. */
		struct lttng_trigger *trigger;
		/* Create session. */
		struct {
			struct {
				const char *name;
				uid_t uid;
				gid_t gid;
			} session;
		} create_session;
		/* Destroy session. */
		struct {
			const char *session_name;
		} destroy_session;
		/* Add channel. */
		struct {
			const char *session_name;
			struct {
				const char *name;
				enum lttng_domain_type domain;
				uint64_t key;
				uint64_t capacity;
			} channel;
		} add_channel;
		/* Remove channel. */
		struct {
			uint64_t key;
			enum lttng_domain_type domain;
		} remove_channel;
		/* Mark session rotation beginning. */
		struct {
			struct {
				const char *name;
				uid_t uid;
				gid_t gid;
			} session;
			uint64_t id;
		} begin_rotation;
		/* Mark session rotation end. */
		struct {
			const char *session_name;
			uint64_t id;
			enum consumer_dst_type destination_type;
			/*
			 * path is relative to the relayd output
			 * if destination_type is CONSUMER_DST_NET.
			 * path is absolute if destination_type is
			 * CONSUMER_DST_LOCAL.
			 */
			const char *path;
		} end_rotation;
	} parameters;

	/* lttng_waiter on which to wait for command reply (optional). */
	struct lttng_waiter reply_waiter;
	enum lttng_error_code reply_code;
};

enum lttng_error_code notification_thread_command_register_trigger(
		struct notification_thread_handle *handle,
		struct lttng_trigger *trigger);

enum lttng_error_code notification_thread_command_unregister_trigger(
		struct notification_thread_handle *handle,
		struct lttng_trigger *trigger);

enum lttng_error_code notification_thread_command_create_session(
		struct notification_thread_handle *handle,
		const char *session_name, uid_t uid, gid_t gid);

enum lttng_error_code notification_thread_command_destroy_session(
		struct notification_thread_handle *handle,
		const char *session_name);

enum lttng_error_code notification_thread_command_add_channel(
		struct notification_thread_handle *handle,
		const char *session_name, const char *channel_name,
		uint64_t key, enum lttng_domain_type domain, uint64_t capacity);

enum lttng_error_code notification_thread_command_remove_channel(
		struct notification_thread_handle *handle,
		uint64_t key, enum lttng_domain_type domain);

enum lttng_error_code notification_thread_command_begin_session_rotation(
		struct notification_thread_handle *handle,
		const char *session_name, uid_t uid, gid_t gid, uint64_t id);

enum lttng_error_code notification_thread_command_end_session_rotation(
		struct notification_thread_handle *handle,
		const char *session_name, uint64_t id,
		enum consumer_dst_type destination_type,
		const char *path);

void notification_thread_command_quit(
		struct notification_thread_handle *handle);

#endif /* NOTIFICATION_THREAD_COMMANDS_H */
