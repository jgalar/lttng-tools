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

#ifndef ROTATE_H
#define ROTATE_H

#include "rotation-thread.h"

extern struct cds_lfht *channel_pending_rotate_ht;

unsigned long hash_channel_key(struct rotation_channel_key *key);

int rotate_add_channel_pending(uint64_t key, enum lttng_domain_type domain,
		struct ltt_session *session);

int session_rename_chunk(struct ltt_session *session, char *current_path,
		char *new_path);

int rename_complete_chunk(struct ltt_session *session, time_t ts);

#endif /* ROTATE_H */
