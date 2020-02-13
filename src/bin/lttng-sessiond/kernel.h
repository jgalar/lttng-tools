/*
 * Copyright (C) 2011 David Goulet <david.goulet@polymtl.ca>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef _LTT_KERNEL_CTL_H
#define _LTT_KERNEL_CTL_H

#include "session.h"
#include "snapshot.h"
#include "trace-kernel.h"

/*
 * Default size for the event list when kernel_list_events is called. This size
 * value is based on the initial LTTng 2.0 version set of tracepoints.
 *
 * This is NOT an upper bound because if the real event list size is bigger,
 * dynamic reallocation is performed.
 */
#define KERNEL_EVENT_INIT_LIST_SIZE 64
#define KERNEL_TRACKER_IDS_INIT_LIST_SIZE 64

int kernel_add_channel_context(struct ltt_kernel_channel *chan,
		struct ltt_kernel_context *ctx);
int kernel_create_session(struct ltt_session *session);
int kernel_create_channel(struct ltt_kernel_session *session,
		struct lttng_channel *chan);
int kernel_create_event(struct lttng_event *ev, struct ltt_kernel_channel *channel,
		char *filter_expression, struct lttng_filter_bytecode *filter);
int kernel_disable_channel(struct ltt_kernel_channel *chan);
int kernel_disable_event(struct ltt_kernel_event *event);
int kernel_enable_event(struct ltt_kernel_event *event);
int kernel_enable_channel(struct ltt_kernel_channel *chan);
int kernel_track_id(enum lttng_tracker_type tracker_type,
		struct ltt_kernel_session *session,
		const struct lttng_tracker_id *id);
int kernel_untrack_id(enum lttng_tracker_type tracker_type,
		struct ltt_kernel_session *session,
		const struct lttng_tracker_id *id);
int kernel_open_metadata(struct ltt_kernel_session *session);
int kernel_open_metadata_stream(struct ltt_kernel_session *session);
int kernel_open_channel_stream(struct ltt_kernel_channel *channel);
int kernel_flush_buffer(struct ltt_kernel_channel *channel);
int kernel_metadata_flush_buffer(int fd);
int kernel_start_session(struct ltt_kernel_session *session);
int kernel_stop_session(struct ltt_kernel_session *session);
ssize_t kernel_list_events(struct lttng_event **event_list);
void kernel_wait_quiescent(void);
int kernel_validate_version(struct lttng_kernel_tracer_version *kernel_tracer_version,
		struct lttng_kernel_tracer_abi_version *kernel_tracer_abi_version);
void kernel_destroy_session(struct ltt_kernel_session *ksess);
void kernel_free_session(struct ltt_kernel_session *ksess);
void kernel_destroy_channel(struct ltt_kernel_channel *kchan);
enum lttng_error_code kernel_snapshot_record(
		struct ltt_kernel_session *ksess,
		const struct consumer_output *output, int wait,
		uint64_t nb_packets_per_stream);
int kernel_syscall_mask(int chan_fd, char **syscall_mask, uint32_t *nr_bits);
enum lttng_error_code kernel_rotate_session(struct ltt_session *session);
enum lttng_error_code kernel_clear_session(struct ltt_session *session);

int init_kernel_workarounds(void);
int kernel_list_tracker_ids(enum lttng_tracker_type tracker_type,
		struct ltt_kernel_session *session,
		struct lttng_tracker_ids **ids);
int kernel_supports_ring_buffer_snapshot_sample_positions(void);
int kernel_supports_ring_buffer_packet_sequence_number(void);
int init_kernel_tracer(void);
void cleanup_kernel_tracer(void);
bool kernel_tracer_is_initialized(void);

enum lttng_error_code kernel_create_channel_subdirectories(
		const struct ltt_kernel_session *ksess);

enum lttng_error_code kernel_create_trigger_group_notification_fd(
		int *trigger_group_notification_fd);
enum lttng_error_code kernel_destroy_trigger_group_notification_fd(
		int trigger_group_notification_fd);
enum lttng_error_code kernel_update_tokens(void);
int kernel_get_notification_fd(void);

#endif /* _LTT_KERNEL_CTL_H */
