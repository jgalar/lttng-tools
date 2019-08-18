#ifndef _SESSION_H
#define _SESSION_H

/*
 * Copyright (C) 2013 - Julien Desfossez <jdesfossez@efficios.com>
 *                      David Goulet <dgoulet@efficios.com>
 *               2015 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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

#include <limits.h>
#include <inttypes.h>
#include <pthread.h>
#include <urcu/list.h>
#include <urcu/ref.h>

#include <lttng/constant.h>
#include <common/hashtable/hashtable.h>
#include <common/compat/uuid.h>
#include <common/trace-chunk.h>
#include <common/optional.h>

/*
 * Represents a session for the relay point of view
 */
struct relay_session {
	/*
	 * This session id is generated by the relay daemon to guarantee
	 * its uniqueness even when serving multiple session daemons.
	 * It is used to match a set of streams to their session.
	 */
	uint64_t id;
	/*
	 * ID of the session in the session daemon's domain.
	 * This information is only provided by 2.11+ peers.
	 */
	LTTNG_OPTIONAL(uint64_t) id_sessiond;
	/*
	 * Only provided by 2.11+ peers. However, the UUID is set to 'nil' in
	 * the other cases.
	 */
	lttng_uuid sessiond_uuid;
	LTTNG_OPTIONAL(time_t) creation_time;
	char session_name[LTTNG_NAME_MAX];
	char hostname[LTTNG_HOST_NAME_MAX];
	char base_path[LTTNG_PATH_MAX];
	uint32_t live_timer;

	/* Session in snapshot mode. */
	bool snapshot;

	/*
	 * Session has no back reference to its connection because it
	 * has a life-time that can be longer than the consumer connection's
	 * life-time; a reference can still be held by the viewer
	 * connection through the viewer streams.
	 */

	struct urcu_ref ref;

	pthread_mutex_t lock;

	/* major/minor version used for this session. */
	uint32_t major;
	uint32_t minor;

	bool viewer_attached;
	/* Tell if the session connection has been closed on the streaming side. */
	bool connection_closed;

	/*
	 * Tell if the session is currently living in a exiting relayd and
	 * should be cleaned forcefully without waiting for pending data or
	 * pending ctrl data.
	 */
	bool aborted;

	bool session_name_contains_creation_time;

	/* Contains ctf_trace object of that session indexed by path name. */
	struct lttng_ht *ctf_traces_ht;

	/*
	 * This contains streams that are received on that connection.
	 * It's used to store them until we get the streams sent
	 * command. When this is received, we remove those streams from
	 * the list and publish them.
	 *
	 * Updates are protected by the recv_list_lock.
	 * Traversals are protected by RCU.
	 * recv_list_lock also protects stream_count.
	 */
	struct cds_list_head recv_list;	/* RCU list. */
	uint32_t stream_count;
	pthread_mutex_t recv_list_lock;

	/*
	 * Flag checked and exchanged with uatomic_cmpxchg to tell the
	 * viewer-side if new streams got added since the last check.
	 */
	unsigned long new_streams;

	/*
	 * Node in the global session hash table.
	 */
	struct lttng_ht_node_u64 session_n;
	/*
	 * Member of the session list in struct relay_viewer_session.
	 * Updates are protected by the relay_viewer_session
	 * session_list_lock. Traversals are protected by RCU.
	 */
	struct cds_list_head viewer_session_node;
	struct lttng_trace_chunk *current_trace_chunk;
	struct lttng_trace_chunk *pending_closure_trace_chunk;
	struct rcu_head rcu_node;	/* For call_rcu teardown. */
};

struct relay_session *session_create(const char *session_name,
		const char *hostname, const char *base_path,
		uint32_t live_timer,
		bool snapshot,
		const lttng_uuid sessiond_uuid,
		const uint64_t *id_sessiond,
		const uint64_t *current_chunk_id,
		const time_t *creation_time,
		uint32_t major,
		uint32_t minor,
		bool session_name_contains_creation_timestamp);
struct relay_session *session_get_by_id(uint64_t id);
bool session_get(struct relay_session *session);
void session_put(struct relay_session *session);

int session_close(struct relay_session *session);
int session_abort(struct relay_session *session);

void print_sessions(void);

#endif /* _SESSION_H */
