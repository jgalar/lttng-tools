/*
 * Copyright (C) 2017 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef NOTIFICATION_THREAD_H
#define NOTIFICATION_THREAD_H

#include "action-executor.h"
#include "thread.h"
#include <common/compat/poll.h>
#include <common/hashtable/hashtable.h>
#include <common/pipe.h>
#include <lttng/trigger/trigger.h>
#include <pthread.h>
#include <semaphore.h>
#include <urcu.h>
#include <urcu/list.h>
#include <urcu/rculfhash.h>

typedef uint64_t notification_client_id;

struct notification_event_trigger_source_element {
	int fd;
	struct cds_list_head node;
};

struct notification_thread_handle {
	/*
	 * Queue of struct notification command.
	 * event_pipe must be WRITE(2) to signal that a new command
	 * has been enqueued.
	 */
	struct {
		struct lttng_pipe *event_pipe;
		struct cds_list_head list;
		pthread_mutex_t lock;
	} cmd_queue;
	/*
	 * Read side of pipes used to receive channel status info collected
	 * by the various consumer daemons.
	 */
	struct {
		int ust32_consumer;
		int ust64_consumer;
		int kernel_consumer;
	} channel_monitoring_pipes;
	/*
	 * Read side of pipes used to reveice event trigger generetated by
	 * registered applications
	 */
	struct {
		/* List of notification_event_trigger_source_element */
		struct cds_list_head list;
		pthread_mutex_t lock;
	} event_trigger_sources;
	/* Used to wait for the launch of the notification thread. */
	sem_t ready;
};

/**
 * This thread maintains an internal state associating clients and triggers.
 *
 * In order to speed-up and simplify queries, hash tables providing the
 * following associations are maintained:
 *
 *   - client_socket_ht: associate a client's socket (fd) to its
 *             "struct notification_client".
 *             This hash table owns the "struct notification_client" which must
 *             thus be disposed-of on removal from the hash table.
 *
 *   - client_id_ht: associate a client's id to its "struct notification_client"
 *             This hash table holds a _weak_ reference to the
 *             "struct notification_client".
 *
 *   - channel_triggers_ht:
 *             associates a channel key to a list of
 *             struct lttng_trigger_list_nodes. The triggers in this list are
 *             those that have conditions that apply to a particular channel.
 *             A channel entry is only created when a channel is added; the
 *             list of triggers applying to such a channel is built at that
 *             moment.
 *             This hash table owns the list, but not the triggers themselves.
 *
 *   - session_triggers_ht:
 *             associates a session name to a list of
 *             struct lttng_trigger_list_nodes. The triggers in this list are
 *             those that have conditions that apply to a particular session.
 *             A session entry is only created when a session is created; the
 *             list of triggers applying to this new session is built at that
 *             moment. This happens at the time of creation of a session_info.
 *             Likewise, the list is destroyed at the time of the session_info's
 *             destruction.
 *
 *   - channel_state_ht:
 *             associates a pair (channel key, channel domain) to its last
 *             sampled state received from the consumer daemon
 *             (struct channel_state).
 *             This previous sample is kept to implement edge-triggered
 *             conditions as we need to detect the state transitions.
 *             This hash table owns the channel state.
 *
 *   - notification_trigger_clients_ht:
 *             associates notification-emitting triggers to clients
 *             (struct notification_client_list) subscribed to those
 *             conditions.
 *             The condition's hash and match functions are used directly since
 *             all triggers in this hash table have the "notify" action.
 *             This hash table holds no ownership.
 *
 *   - channels_ht:
 *             associates a channel_key to a struct channel_info. The hash table
 *             holds the ownership of the struct channel_info.
 *
 *   - sessions_ht:
 *             associates a session_name (hash) to a struct session_info. The
 *             hash table holds no ownership of the struct session_info;
 *             the session_info structure is owned by the session's various
 *             channels through their struct channel_info (ref-counting is used).
 *
 *   - triggers_ht:
 *             associates a trigger to a struct lttng_trigger_ht_element.
 *             The hash table holds the ownership of the
 *             lttng_trigger_ht_elements along with the triggers themselves.
 *   - triggers_by_name_ht:
 *             associates a trigger name to a struct lttng_trigger_ht_element.
 *             The hash table does not hold any ownership and is used strictly
 *             for lookup on registration.
 *
 * The thread reacts to the following internal events:
 *   1) creation of a tracing channel,
 *   2) destruction of a tracing channel,
 *   3) registration of a trigger,
 *   4) unregistration of a trigger,
 *   5) reception of a channel monitor sample from the consumer daemon.
 *   6) Session rotation ongoing
 *   7) Session rotation completed
 *
 * Events specific to notification-emitting triggers:
 *   8) connection of a notification client,
 *   9) disconnection of a notification client,
 *   10) subscription of a client to a conditions' notifications,
 *   11) unsubscription of a client from a conditions' notifications,
 *
 *
 * 1) Creation of a tracing channel
 *    - notification_trigger_clients_ht is traversed to identify
 *      triggers which apply to this new channel,
 *      - triggers identified are added to the channel_triggers_ht.
 *    - add channel to channels_ht
 *    - if it is the first channel of a session, a session_info is created and
 *      added to the sessions_ht. A list of the triggers associated with that
 *      session is built, and it is added to session_triggers_ht.
 *
 * 2) Destruction of a tracing channel
 *    - remove entry from channel_triggers_ht, releasing the list wrapper and
 *      elements,
 *    - remove entry from the channel_state_ht.
 *    - remove channel from channels_ht
 *    - if it was the last known channel of a session, the session_info
 *      structure is torndown, which in return destroys the list of triggers
 *      applying to that session.
 *
 * 3) Registration of a trigger
 *    - if the trigger's action is of type "notify",
 *      - traverse the list of conditions of every client to build a list of
 *        clients which have to be notified when this trigger's condition is met,
 *        - add list of clients (even if it is empty) to the
 *          notification_trigger_clients_ht,
 *    - add trigger to channel_triggers_ht (if applicable),
 *    - add trigger to session_triggers_ht (if applicable),
 *    - add trigger to triggers_by_name_ht
 *    - add trigger to triggers_ht
 *    - evaluate the trigger's condition right away to react if that condition
 *      is true from the beginning.
 *
 * 4) Unregistration of a trigger
 *    - if the trigger's action is of type "notify",
 *      - remove the trigger from the notification_trigger_clients_ht,
 *    - remove trigger from channel_triggers_ht (if applicable),
 *    - remove trigger from session_triggers_ht (if applicable),
 *    - remove trigger to triggers_by_name_ht
 *    - remove trigger from triggers_ht
 *
 * 5) Reception of a channel monitor sample from the consumer daemon
 *    - evaluate the conditions associated with the triggers found in
 *      the channel_triggers_ht,
 *      - if a condition evaluates to "true" and the condition is of type
 *        "notify", query the notification_trigger_clients_ht and send
 *        a notification to the clients.
 *
 * 6) Session rotation ongoing
 *
 * 7) Session rotation completed
 *
 * 8) Connection of a client
 *    - add client socket to the client_socket_ht,
 *    - add client socket to the client_id_ht.
 *
 * 9) Disconnection of a client
 *    - remove client socket from the client_id_ht,
 *    - remove client socket from the client_socket_ht,
 *    - traverse all conditions to which the client is subscribed and remove
 *      the client from the notification_trigger_clients_ht.
 *
 * 10) Subscription of a client to a condition's notifications
 *    - Add the condition to the client's list of subscribed conditions,
 *    - Look-up notification_trigger_clients_ht and add the client to
 *      list of clients.
 *    - Evaluate the condition for the client that subscribed if the trigger
 *      was already registered.
 *
 * 11) Unsubscription of a client to a condition's notifications
 *    - Remove the condition from the client's list of subscribed conditions,
 *    - Look-up notification_trigger_clients_ht and remove the client
 *      from the list of clients.
 */
struct notification_thread_state {
	int notification_channel_socket;
	struct lttng_poll_event events;
	struct cds_lfht *client_socket_ht;
	struct cds_lfht *client_id_ht;
	struct cds_lfht *channel_triggers_ht;
	struct cds_lfht *session_triggers_ht;
	struct cds_lfht *channel_state_ht;
	struct cds_lfht *notification_trigger_clients_ht;
	struct cds_lfht *channels_ht;
	struct cds_lfht *sessions_ht;
	struct cds_lfht *triggers_ht;
	struct cds_lfht *triggers_by_name_ht;
	struct cds_lfht *trigger_tokens_ht;
	struct {
		uint64_t token_generator;
		uint64_t name_offset;
	} trigger_id;
	notification_client_id next_notification_client_id;
	struct action_executor *executor;
};

/* notification_thread_data takes ownership of the channel monitor pipes. */
struct notification_thread_handle *notification_thread_handle_create(
		struct lttng_pipe *ust32_channel_monitor_pipe,
		struct lttng_pipe *ust64_channel_monitor_pipe,
		struct lttng_pipe *kernel_channel_monitor_pipe);
void notification_thread_handle_destroy(
		struct notification_thread_handle *handle);
struct lttng_thread *launch_notification_thread(
		struct notification_thread_handle *handle);

#endif /* NOTIFICATION_THREAD_H */
