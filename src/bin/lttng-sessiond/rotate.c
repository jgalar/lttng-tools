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

#define _LGPL_SOURCE
#include <lttng/trigger/trigger.h>
#include <common/error.h>
#include <common/config/session-config.h>
#include <common/defaults.h>
#include <common/utils.h>
#include <common/futex.h>
#include <common/align.h>
#include <common/time.h>
#include <common/hashtable/utils.h>
#include <common/kernel-ctl/kernel-ctl.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <inttypes.h>

#include "session.h"
#include "rotate.h"
#include "rotation-thread.h"
#include "lttng-sessiond.h"
#include "health-sessiond.h"
#include "cmd.h"

#include <urcu.h>
#include <urcu/list.h>
#include <urcu/rculfhash.h>

unsigned long hash_channel_key(struct rotation_channel_key *key)
{
	return hash_key_u64(&key->key, lttng_ht_seed) ^ hash_key_ulong(
		(void *) (unsigned long) key->domain, lttng_ht_seed);
}

int rotate_add_channel_pending(uint64_t key, enum lttng_domain_type domain,
		struct ltt_session *session)
{
	int ret;
	struct rotation_channel_info *new_info;
	struct rotation_channel_key channel_key = { .key = key,
		.domain = domain };

	new_info = zmalloc(sizeof(struct rotation_channel_info));
	if (!new_info) {
		goto error;
	}

	new_info->channel_key.key = key;
	new_info->channel_key.domain = domain;
	new_info->session = session;
	cds_lfht_node_init(&new_info->rotate_channels_ht_node);

	session->nr_chan_rotate_pending++;
	cds_lfht_add(channel_pending_rotate_ht,
			hash_channel_key(&channel_key),
			&new_info->rotate_channels_ht_node);

	ret = 0;
	goto end;

error:
	ret = -1;
end:
	return ret;
}

int session_rename_chunk(struct ltt_session *session, char *current_path,
		char *new_path, uint32_t create)
{
	int ret;
	struct consumer_socket *socket;
	struct consumer_output *output;
	struct lttng_ht_iter iter;
	uid_t uid;
	gid_t gid;

	/*
	 * Either one of the sessions is enough to find the consumer_output
	 * and uid/gid.
	 */
	if (session->kernel_session) {
		output = session->kernel_session->consumer;
		uid = session->kernel_session->uid;
		gid = session->kernel_session->gid;
	} else if (session->ust_session) {
		output = session->ust_session->consumer;
		uid = session->ust_session->uid;
		gid = session->ust_session->gid;
	} else {
		assert(0);
	}

	if (!output || !output->socks) {
		ERR("No consumer output found");
		ret = -1;
		goto end;
	}

	rcu_read_lock();
	/*
	 * We have to iterate to find a socket, but we only need to send the
	 * rename command to one consumer, so we break after the first one.
	 */
	cds_lfht_for_each_entry(output->socks->ht, &iter.iter, socket, node.node) {
		pthread_mutex_lock(socket->lock);
		ret = consumer_rotate_rename(socket, session->id, output,
				current_path, new_path, create, uid, gid);
		pthread_mutex_unlock(socket->lock);
		if (ret) {
			ERR("Consumer rename chunk");
			ret = -1;
			rcu_read_unlock();
			goto end;
		}
		break;
	}
	rcu_read_unlock();

	ret = 0;

end:
	return ret;
}

static
int rename_first_chunk(struct ltt_session *session,
		struct consumer_output *consumer, char *new_path)
{
	int ret;
	char *tmppath = NULL, *tmppath2 = NULL;

	tmppath = zmalloc(PATH_MAX * sizeof(char));
	if (!tmppath) {
		ret = -LTTNG_ERR_NOMEM;
		goto error;
	}
	tmppath2 = zmalloc(PATH_MAX * sizeof(char));
	if (!tmppath2) {
		ret = -LTTNG_ERR_NOMEM;
		goto error;
	}

	/* Current domain path: <session>/kernel */
	snprintf(tmppath, PATH_MAX, "%s/%s",
			consumer->dst.session_root_path, consumer->subdir);
	/* New domain path: <session>/<start-date>-<end-date>-<rotate-count>/kernel */
	snprintf(tmppath2, PATH_MAX, "%s/%s",
			new_path, consumer->subdir);
	/*
	 * Move the per-domain folder inside the first rotation
	 * folder.
	 */
	ret = session_rename_chunk(session, tmppath, tmppath2, 1);
	if (ret < 0) {
		PERROR("Rename first trace directory");
		ret = -LTTNG_ERR_ROTATE_NO_DATA;
		goto error;
	}

	ret = 0;

error:
	free(tmppath);
	free(tmppath2);

	return ret;
}

int rename_complete_chunk(struct ltt_session *session, time_t ts)
{
	struct tm *timeinfo;
	char datetime[16];
	char *new_path = NULL;
	int ret;

	/*
	 * TODO 2: on first rotate, the current_rotate_path is the session root
	 * path, so move the kernel/ and ust/ folders inside the
	 * "session->last_chunk_start_ts-now()"
	 */

	timeinfo = localtime(&ts);
	strftime(datetime, sizeof(datetime), "%Y%m%d-%H%M%S", timeinfo);

	new_path = zmalloc(PATH_MAX * sizeof(char));
	if (!new_path) {
		ERR("Alloc new_path");
		ret = -1;
		goto end;
	}

	fprintf(stderr, "COUNT: %d\n", session->rotate_count);

	if (session->rotate_count == 1) {
		char start_time[16];

		timeinfo = localtime(&session->last_chunk_start_ts);
		strftime(start_time, sizeof(start_time), "%Y%m%d-%H%M%S", timeinfo);

		/*
		 * On the first rotation, the current_rotate_path is the
		 * session_root_path, so we need to create the chunk folder
		 * and move the domain-specific folders inside it.
		 */
		snprintf(new_path, PATH_MAX, "%s/%s-%s-%" PRIu64,
				session->rotation_chunk.current_rotate_path,
				start_time,
				datetime, session->rotate_count);

		if (session->kernel_session) {
			fprintf(stderr, "rename %s/kernel to %s\n",
					session->rotation_chunk.current_rotate_path,
					new_path);
			ret = rename_first_chunk(session,
					session->kernel_session->consumer,
					new_path);
			if (ret) {
				ERR("Rename kernel session");
				ret = -1;
				goto end;
			}
		}
		if (session->ust_session) {
			fprintf(stderr, "rename %s/kernel to %s\n",
					session->rotation_chunk.current_rotate_path,
					new_path);
			ret = rename_first_chunk(session,
					session->ust_session->consumer,
					new_path);
			if (ret) {
				ERR("Rename ust session");
				ret = -1;
				goto end;
			}
		}
	} else {
		/*
		 * After the first rotation, all the trace data is already in
		 * its own chunk folder, we just need to append the suffix.
		 */
		snprintf(new_path, PATH_MAX, "%s%s-%" PRIu64,
				session->rotation_chunk.current_rotate_path,
				datetime, session->rotate_count);

		fprintf(stderr, "rename %s to %s\n",
				session->rotation_chunk.current_rotate_path,
				new_path);

		ret = session_rename_chunk(session,
				session->rotation_chunk.current_rotate_path,
				new_path, 0);
		if (ret) {
			ERR("Session rename");
			ret = -1;
			goto end;
		}
	}

	/*
	 * Store the path where the readable chunk is. This path is valid
	 * and can be queried by the client with rotate_pending until the next
	 * rotation is started.
	 */
	snprintf(session->rotation_chunk.current_rotate_path, PATH_MAX,
			"%s", new_path);
	session->rotate_pending = 0;

end:
	free(new_path);
	return ret;
}

