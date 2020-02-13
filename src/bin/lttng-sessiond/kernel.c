/*
 * Copyright (C) 2011 David Goulet <david.goulet@polymtl.ca>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#define _LGPL_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>

#include <common/common.h>
#include <common/hashtable/utils.h>
#include <common/trace-chunk.h>
#include <common/kernel-ctl/kernel-ctl.h>
#include <common/kernel-ctl/kernel-ioctl.h>
#include <common/sessiond-comm/sessiond-comm.h>

#include <lttng/userspace-probe.h>
#include <lttng/userspace-probe-internal.h>
#include <lttng/condition/event-rule.h>
#include <lttng/condition/event-rule-internal.h>
#include <lttng/event-rule/event-rule.h>
#include <lttng/event-rule/event-rule-internal.h>
#include <lttng/event-rule/event-rule-uprobe-internal.h>

#include "lttng-sessiond.h"
#include "lttng-syscall.h"
#include "consumer.h"
#include "kernel.h"
#include "kernel-consumer.h"
#include "kern-modules.h"
#include "utils.h"
#include "rotate.h"
#include "modprobe.h"
#include "notification-thread-commands.h"

/*
 * Key used to reference a channel between the sessiond and the consumer. This
 * is only read and updated with the session_list lock held.
 */
static uint64_t next_kernel_channel_key;

static const char *module_proc_lttng = "/proc/lttng";

static int kernel_tracer_fd = -1;
static int kernel_tracer_trigger_group_fd = -1;
static int kernel_tracer_trigger_group_notification_fd = -1;
static struct ltt_kernel_token_event_rule_list kernel_tracer_token_list;

/*
 * Add context on a kernel channel.
 *
 * Assumes the ownership of ctx.
 */
int kernel_add_channel_context(struct ltt_kernel_channel *chan,
		struct ltt_kernel_context *ctx)
{
	int ret;

	assert(chan);
	assert(ctx);

	DBG("Adding context to channel %s", chan->channel->name);
	ret = kernctl_add_context(chan->fd, &ctx->ctx);
	if (ret < 0) {
		switch (-ret) {
		case ENOSYS:
			/* Exists but not available for this kernel */
			ret = LTTNG_ERR_KERN_CONTEXT_UNAVAILABLE;
			goto error;
		case EEXIST:
			/* If EEXIST, we just ignore the error */
			ret = 0;
			goto end;
		default:
			PERROR("add context ioctl");
			ret = LTTNG_ERR_KERN_CONTEXT_FAIL;
			goto error;
		}
	}
	ret = 0;

end:
	cds_list_add_tail(&ctx->list, &chan->ctx_list);
	ctx->in_list = true;
	ctx = NULL;
error:
	if (ctx) {
		trace_kernel_destroy_context(ctx);
	}
	return ret;
}

/*
 * Create a new kernel session, register it to the kernel tracer and add it to
 * the session daemon session.
 */
int kernel_create_session(struct ltt_session *session)
{
	int ret;
	struct ltt_kernel_session *lks;

	assert(session);

	/* Allocate data structure */
	lks = trace_kernel_create_session();
	if (lks == NULL) {
		ret = -1;
		goto error;
	}

	/* Kernel tracer session creation */
	ret = kernctl_create_session(kernel_tracer_fd);
	if (ret < 0) {
		PERROR("ioctl kernel create session");
		goto error;
	}

	lks->fd = ret;
	/* Prevent fd duplication after execlp() */
	ret = fcntl(lks->fd, F_SETFD, FD_CLOEXEC);
	if (ret < 0) {
		PERROR("fcntl session fd");
	}

	lks->id = session->id;
	lks->consumer_fds_sent = 0;
	session->kernel_session = lks;

	DBG("Kernel session created (fd: %d)", lks->fd);

	/*
	 * This is necessary since the creation time is present in the session
	 * name when it is generated.
	 */
	if (session->has_auto_generated_name) {
		ret = kernctl_session_set_name(lks->fd, DEFAULT_SESSION_NAME);
	} else {
		ret = kernctl_session_set_name(lks->fd, session->name);
	}
	if (ret) {
		WARN("Could not set kernel session name for session %" PRIu64 " name: %s",
			session->id, session->name);
	}

	ret = kernctl_session_set_creation_time(lks->fd, session->creation_time);
	if (ret) {
		WARN("Could not set kernel session creation time for session %" PRIu64 " name: %s",
			session->id, session->name);
	}

	return 0;

error:
	if (lks) {
		trace_kernel_destroy_session(lks);
		trace_kernel_free_session(lks);
	}
	return ret;
}

/*
 * Create a kernel channel, register it to the kernel tracer and add it to the
 * kernel session.
 */
int kernel_create_channel(struct ltt_kernel_session *session,
		struct lttng_channel *chan)
{
	int ret;
	struct ltt_kernel_channel *lkc;

	assert(session);
	assert(chan);

	/* Allocate kernel channel */
	lkc = trace_kernel_create_channel(chan);
	if (lkc == NULL) {
		goto error;
	}

	DBG3("Kernel create channel %s with attr: %d, %" PRIu64 ", %" PRIu64 ", %u, %u, %d, %d",
			chan->name, lkc->channel->attr.overwrite,
			lkc->channel->attr.subbuf_size, lkc->channel->attr.num_subbuf,
			lkc->channel->attr.switch_timer_interval, lkc->channel->attr.read_timer_interval,
			lkc->channel->attr.live_timer_interval, lkc->channel->attr.output);

	/* Kernel tracer channel creation */
	ret = kernctl_create_channel(session->fd, &lkc->channel->attr);
	if (ret < 0) {
		PERROR("ioctl kernel create channel");
		goto error;
	}

	/* Setup the channel fd */
	lkc->fd = ret;
	/* Prevent fd duplication after execlp() */
	ret = fcntl(lkc->fd, F_SETFD, FD_CLOEXEC);
	if (ret < 0) {
		PERROR("fcntl session fd");
	}

	/* Add channel to session */
	cds_list_add(&lkc->list, &session->channel_list.head);
	session->channel_count++;
	lkc->session = session;
	lkc->key = ++next_kernel_channel_key;

	DBG("Kernel channel %s created (fd: %d, key: %" PRIu64 ")",
			lkc->channel->name, lkc->fd, lkc->key);

	return 0;

error:
	if (lkc) {
		free(lkc->channel);
		free(lkc);
	}
	return -1;
}

/*
 * Create a kernel channel, register it to the kernel tracer and add it to the
 * kernel session.
 */
static
int kernel_create_trigger_group(int *trigger_group_fd)
{
	int ret;
	int local_fd = -1;

	assert(trigger_group_fd);

	/* Kernel tracer channel creation */
	ret = kernctl_create_trigger_group(kernel_tracer_fd);
	if (ret < 0) {
		PERROR("ioctl kernel create trigger group");
		ret = -1;
		goto error;
	}

	/* Store locally */
	local_fd = ret;

	/* Prevent fd duplication after execlp() */
	ret = fcntl(local_fd, F_SETFD, FD_CLOEXEC);
	if (ret < 0) {
		PERROR("fcntl session fd");
	}

	DBG("Kernel trigger group created (fd: %d)",
			local_fd);
	ret = 0;

error:
	*trigger_group_fd = local_fd;
	return ret;
}

/*
 * Compute the offset of the instrumentation byte in the binary based on the
 * function probe location using the ELF lookup method.
 *
 * Returns 0 on success and set the offset out parameter to the offset of the
 * elf symbol
 * Returns -1 on error
 */
static
int extract_userspace_probe_offset_function_elf(
		const struct lttng_userspace_probe_location *probe_location,
		uid_t uid, gid_t gid, uint64_t *offset)
{
	int fd;
	int ret = 0;
	const char *symbol = NULL;
	const struct lttng_userspace_probe_location_lookup_method *lookup = NULL;
	enum lttng_userspace_probe_location_lookup_method_type lookup_method_type;

	assert(lttng_userspace_probe_location_get_type(probe_location) ==
			LTTNG_USERSPACE_PROBE_LOCATION_TYPE_FUNCTION);

	lookup = lttng_userspace_probe_location_get_lookup_method(
			probe_location);
	if (!lookup) {
		ret = -1;
		goto end;
	}

	lookup_method_type =
			lttng_userspace_probe_location_lookup_method_get_type(lookup);

	assert(lookup_method_type ==
			LTTNG_USERSPACE_PROBE_LOCATION_LOOKUP_METHOD_TYPE_FUNCTION_ELF);

	symbol = lttng_userspace_probe_location_function_get_function_name(
			probe_location);
	if (!symbol) {
		ret = -1;
		goto end;
	}

	fd = lttng_userspace_probe_location_function_get_binary_fd(probe_location);
	if (fd < 0) {
		ret = -1;
		goto end;
	}

	ret = run_as_extract_elf_symbol_offset(fd, symbol, uid, gid, offset);
	if (ret < 0) {
		DBG("userspace probe offset calculation failed for "
				"function %s", symbol);
		goto end;
	}

	DBG("userspace probe elf offset for %s is 0x%jd", symbol, (intmax_t)(*offset));
end:
	return ret;
}

/*
 * Compute the offsets of the instrumentation bytes in the binary based on the
 * tracepoint probe location using the SDT lookup method. This function
 * allocates the offsets buffer, the caller must free it.
 *
 * Returns 0 on success and set the offset out parameter to the offsets of the
 * SDT tracepoint.
 * Returns -1 on error.
 */
static
int extract_userspace_probe_offset_tracepoint_sdt(
		const struct lttng_userspace_probe_location *probe_location,
		uid_t uid, gid_t gid, uint64_t **offsets,
		uint32_t *offsets_count)
{
	enum lttng_userspace_probe_location_lookup_method_type lookup_method_type;
	const struct lttng_userspace_probe_location_lookup_method *lookup = NULL;
	const char *probe_name = NULL, *provider_name = NULL;
	int ret = 0;
	int fd, i;

	assert(lttng_userspace_probe_location_get_type(probe_location) ==
			LTTNG_USERSPACE_PROBE_LOCATION_TYPE_TRACEPOINT);

	lookup = lttng_userspace_probe_location_get_lookup_method(probe_location);
	if (!lookup) {
		ret = -1;
		goto end;
	}

	lookup_method_type =
			lttng_userspace_probe_location_lookup_method_get_type(lookup);

	assert(lookup_method_type ==
			LTTNG_USERSPACE_PROBE_LOCATION_LOOKUP_METHOD_TYPE_TRACEPOINT_SDT);


	probe_name = lttng_userspace_probe_location_tracepoint_get_probe_name(
			probe_location);
	if (!probe_name) {
		ret = -1;
		goto end;
	}

	provider_name = lttng_userspace_probe_location_tracepoint_get_provider_name(
			probe_location);
	if (!provider_name) {
		ret = -1;
		goto end;
	}

	fd = lttng_userspace_probe_location_tracepoint_get_binary_fd(probe_location);
	if (fd < 0) {
		ret = -1;
		goto end;
	}

	ret = run_as_extract_sdt_probe_offsets(fd, provider_name, probe_name,
			uid, gid, offsets, offsets_count);
	if (ret < 0) {
		DBG("userspace probe offset calculation failed for sdt "
				"probe %s:%s", provider_name, probe_name);
		goto end;
	}

	if (*offsets_count == 0) {
		DBG("no userspace probe offset found");
		goto end;
	}

	DBG("%u userspace probe SDT offsets found for %s:%s at:",
			*offsets_count, provider_name, probe_name);
	for (i = 0; i < *offsets_count; i++) {
		DBG("\t0x%jd", (intmax_t)((*offsets)[i]));
	}
end:
	return ret;
}

static
int userspace_probe_add_callsite(
		const struct lttng_userspace_probe_location *location,
		uid_t uid, gid_t gid, int fd)
{
	const struct lttng_userspace_probe_location_lookup_method *lookup_method = NULL;
	enum lttng_userspace_probe_location_lookup_method_type type;
	int ret;

	lookup_method = lttng_userspace_probe_location_get_lookup_method(location);
	if (!lookup_method) {
		ret = -1;
		goto end;
	}

	type = lttng_userspace_probe_location_lookup_method_get_type(lookup_method);
	switch (type) {
	case LTTNG_USERSPACE_PROBE_LOCATION_LOOKUP_METHOD_TYPE_FUNCTION_ELF:
	{
		struct lttng_kernel_event_callsite callsite;
		uint64_t offset;

		ret = extract_userspace_probe_offset_function_elf(location,
				uid, gid, &offset);
		if (ret) {
			ret = LTTNG_ERR_PROBE_LOCATION_INVAL;
			goto end;
		}

		callsite.u.uprobe.offset = offset;
		ret = kernctl_add_callsite(fd, &callsite);
		if (ret) {
			WARN("Adding callsite to ELF userspace probe failed.");
			ret = LTTNG_ERR_KERN_ENABLE_FAIL;
			goto end;
		}
		break;
	}
	case LTTNG_USERSPACE_PROBE_LOCATION_LOOKUP_METHOD_TYPE_TRACEPOINT_SDT:
	{
		int i;
		uint64_t *offsets = NULL;
		uint32_t offsets_count;
		struct lttng_kernel_event_callsite callsite;

		/*
		 * This call allocates the offsets buffer. This buffer must be freed
		 * by the caller
		 */
		ret = extract_userspace_probe_offset_tracepoint_sdt(location,
				uid, gid, &offsets, &offsets_count);
		if (ret) {
			ret = LTTNG_ERR_PROBE_LOCATION_INVAL;
			goto end;
		}
		for (i = 0; i < offsets_count; i++) {
			callsite.u.uprobe.offset = offsets[i];
			ret = kernctl_add_callsite(fd, &callsite);
			if (ret) {
				WARN("Adding callsite to SDT userspace probe "
					"failed.");
				ret = LTTNG_ERR_KERN_ENABLE_FAIL;
				free(offsets);
				goto end;
			}
		}
		free(offsets);
		break;
	}
	default:
		ret = LTTNG_ERR_PROBE_LOCATION_INVAL;
		goto end;
	}
end:
	return ret;
}

/*
 * Extract the offsets of the instrumentation point for the different lookup
 * methods.
 */
static
int userspace_probe_event_add_callsites(struct lttng_event *ev,
			struct ltt_kernel_session *session, int fd)
{
	const struct lttng_userspace_probe_location *location = NULL;
	int ret;

	assert(ev);
	assert(ev->type == LTTNG_EVENT_USERSPACE_PROBE);

	location = lttng_event_get_userspace_probe_location(ev);
	if (!location) {
		ret = -1;
		goto end;
	}

	ret = userspace_probe_add_callsite(location, session->uid, session->gid,
		fd);
	if (ret) {
		WARN("Adding callsite to userspace probe event \"%s\" "
			"failed.", ev->name);
	}

end:
	return ret;
}

/*
 * Extract the offsets of the instrumentation point for the different lookup
 * methods.
 */
static int userspace_probe_event_rule_add_callsites(
		const struct lttng_event_rule *rule,
		const struct lttng_credentials *creds,
		int fd)
{
	const struct lttng_userspace_probe_location *location = NULL;
	enum lttng_event_rule_status status;
	int ret;

	assert(rule);
	assert(creds);
	assert(lttng_event_rule_get_type(rule) == LTTNG_EVENT_RULE_TYPE_UPROBE);

	status = lttng_event_rule_uprobe_get_location(rule, &location);
	if (!location) {
		ret = -1;
		goto end;
	}

	ret = userspace_probe_add_callsite(location, creds->uid, creds->gid,
		fd);
	if (ret) {
		WARN("Adding callsite to userspace probe object %d"
			"failed.", fd);
	}

end:
	return ret;
}

/*
 * Create a kernel event, enable it to the kernel tracer and add it to the
 * channel event list of the kernel session.
 * We own filter_expression and filter.
 */
int kernel_create_event(struct lttng_event *ev,
		struct ltt_kernel_channel *channel,
		char *filter_expression,
		struct lttng_filter_bytecode *filter)
{
	int err, fd;
	enum lttng_error_code ret;
	struct ltt_kernel_event *event;

	assert(ev);
	assert(channel);

	/* We pass ownership of filter_expression and filter */
	ret = trace_kernel_create_event(ev, filter_expression,
			filter, &event);
	if (ret != LTTNG_OK) {
		goto error;
	}

	fd = kernctl_create_event(channel->fd, event->event);
	if (fd < 0) {
		switch (-fd) {
		case EEXIST:
			ret = LTTNG_ERR_KERN_EVENT_EXIST;
			break;
		case ENOSYS:
			WARN("Event type not implemented");
			ret = LTTNG_ERR_KERN_EVENT_ENOSYS;
			break;
		case ENOENT:
			WARN("Event %s not found!", ev->name);
			ret = LTTNG_ERR_KERN_ENABLE_FAIL;
			break;
		default:
			ret = LTTNG_ERR_KERN_ENABLE_FAIL;
			PERROR("create event ioctl");
		}
		goto free_event;
	}

	event->type = ev->type;
	event->fd = fd;
	/* Prevent fd duplication after execlp() */
	err = fcntl(event->fd, F_SETFD, FD_CLOEXEC);
	if (err < 0) {
		PERROR("fcntl session fd");
	}

	if (filter) {
		err = kernctl_filter(event->fd, filter);
		if (err < 0) {
			switch (-err) {
			case ENOMEM:
				ret = LTTNG_ERR_FILTER_NOMEM;
				break;
			default:
				ret = LTTNG_ERR_FILTER_INVAL;
				break;
			}
			goto filter_error;
		}
	}

	if (ev->type == LTTNG_EVENT_USERSPACE_PROBE) {
		ret = userspace_probe_event_add_callsites(ev, channel->session,
			event->fd);
		if (ret) {
			goto add_callsite_error;
		}
	}

	err = kernctl_enable(event->fd);
	if (err < 0) {
		switch (-err) {
		case EEXIST:
			ret = LTTNG_ERR_KERN_EVENT_EXIST;
			break;
		default:
			PERROR("enable kernel event");
			ret = LTTNG_ERR_KERN_ENABLE_FAIL;
			break;
		}
		goto enable_error;
	}

	/* Add event to event list */
	cds_list_add(&event->list, &channel->events_list.head);
	channel->event_count++;

	DBG("Event %s created (fd: %d)", ev->name, event->fd);

	return 0;

add_callsite_error:
enable_error:
filter_error:
	{
		int closeret;

		closeret = close(event->fd);
		if (closeret) {
			PERROR("close event fd");
		}
	}
free_event:
	free(event);
error:
	return ret;
}

/*
 * Disable a kernel channel.
 */
int kernel_disable_channel(struct ltt_kernel_channel *chan)
{
	int ret;

	assert(chan);

	ret = kernctl_disable(chan->fd);
	if (ret < 0) {
		PERROR("disable chan ioctl");
		goto error;
	}

	chan->enabled = 0;
	DBG("Kernel channel %s disabled (fd: %d, key: %" PRIu64 ")",
			chan->channel->name, chan->fd, chan->key);

	return 0;

error:
	return ret;
}

/*
 * Enable a kernel channel.
 */
int kernel_enable_channel(struct ltt_kernel_channel *chan)
{
	int ret;

	assert(chan);

	ret = kernctl_enable(chan->fd);
	if (ret < 0 && ret != -EEXIST) {
		PERROR("Enable kernel chan");
		goto error;
	}

	chan->enabled = 1;
	DBG("Kernel channel %s enabled (fd: %d, key: %" PRIu64 ")",
			chan->channel->name, chan->fd, chan->key);

	return 0;

error:
	return ret;
}

/*
 * Enable a kernel event.
 */
int kernel_enable_event(struct ltt_kernel_event *event)
{
	int ret;

	assert(event);

	ret = kernctl_enable(event->fd);
	if (ret < 0) {
		switch (-ret) {
		case EEXIST:
			ret = LTTNG_ERR_KERN_EVENT_EXIST;
			break;
		default:
			PERROR("enable kernel event");
			break;
		}
		goto error;
	}

	event->enabled = 1;
	DBG("Kernel event %s enabled (fd: %d)", event->event->name, event->fd);

	return 0;

error:
	return ret;
}

/*
 * Disable a kernel event.
 */
int kernel_disable_event(struct ltt_kernel_event *event)
{
	int ret;

	assert(event);

	ret = kernctl_disable(event->fd);
	if (ret < 0) {
		switch (-ret) {
		case EEXIST:
			ret = LTTNG_ERR_KERN_EVENT_EXIST;
			break;
		default:
			PERROR("disable kernel event");
			break;
		}
		goto error;
	}

	event->enabled = 0;
	DBG("Kernel event %s disabled (fd: %d)", event->event->name, event->fd);

	return 0;

error:
	return ret;
}

/*
 * Disable a kernel trigger.
 */
int kernel_disable_token_event_rule(struct ltt_kernel_token_event_rule *event)
{
	int ret;

	assert(event);

	ret = kernctl_disable(event->fd);
	if (ret < 0) {
		switch (-ret) {
		case EEXIST:
			ret = LTTNG_ERR_KERN_EVENT_EXIST;
			break;
		default:
			PERROR("disable kernel event");
			break;
		}
		goto error;
	}

	event->enabled = 0;
	DBG("Kernel trigger token %" PRIu64" disabled (fd: %d)", event->token, event->fd);

	return 0;

error:
	return ret;
}
static struct lttng_tracker_list *get_id_tracker_list(
		struct ltt_kernel_session *session,
		enum lttng_tracker_type tracker_type)
{
	switch (tracker_type) {
	case LTTNG_TRACKER_PID:
		return session->tracker_list_pid;
	case LTTNG_TRACKER_VPID:
		return session->tracker_list_vpid;
	case LTTNG_TRACKER_UID:
		return session->tracker_list_uid;
	case LTTNG_TRACKER_VUID:
		return session->tracker_list_vuid;
	case LTTNG_TRACKER_GID:
		return session->tracker_list_gid;
	case LTTNG_TRACKER_VGID:
		return session->tracker_list_vgid;
	default:
		return NULL;
	}
}

int kernel_track_id(enum lttng_tracker_type tracker_type,
		struct ltt_kernel_session *session,
		const struct lttng_tracker_id *id)
{
	int ret, value;
	struct lttng_tracker_list *tracker_list;
	struct lttng_tracker_ids *saved_ids;

	ret = lttng_tracker_id_lookup_string(tracker_type, id, &value);
	if (ret != LTTNG_OK) {
		return ret;
	}

	tracker_list = get_id_tracker_list(session, tracker_type);
	if (!tracker_list) {
		return LTTNG_ERR_INVALID;
	}

	/* Save list for restore on error. */
	ret = lttng_tracker_id_get_list(tracker_list, &saved_ids);
	if (ret != LTTNG_OK) {
		return LTTNG_ERR_INVALID;
	}

	/* Add to list. */
	ret = lttng_tracker_list_add(tracker_list, id);
	if (ret != LTTNG_OK) {
		goto end;
	}

	switch (tracker_type) {
	case LTTNG_TRACKER_PID:
		DBG("Kernel track PID %d for session id %" PRIu64 ".", value,
				session->id);
		ret = kernctl_track_pid(session->fd, value);
		if (!ret) {
			ret = LTTNG_OK;
			goto end;
		}
		break;
	case LTTNG_TRACKER_VPID:
		DBG("Kernel track VPID %d for session id %" PRIu64 ".", value,
				session->id);
		ret = kernctl_track_id(session->fd, LTTNG_TRACKER_VPID, value);
		if (!ret) {
			ret = LTTNG_OK;
			goto end;
		}
		break;
	case LTTNG_TRACKER_UID:
		DBG("Kernel track UID %d for session id %" PRIu64 ".", value,
				session->id);
		ret = kernctl_track_id(session->fd, LTTNG_TRACKER_UID, value);
		if (!ret) {
			ret = LTTNG_OK;
			goto end;
		}
		break;
	case LTTNG_TRACKER_GID:
		DBG("Kernel track GID %d for session id %" PRIu64 ".", value,
				session->id);
		ret = kernctl_track_id(session->fd, LTTNG_TRACKER_GID, value);
		if (!ret) {
			ret = LTTNG_OK;
			goto end;
		}
		break;
	case LTTNG_TRACKER_VUID:
		DBG("Kernel track VUID %d for session id %" PRIu64 ".", value,
				session->id);
		ret = kernctl_track_id(session->fd, LTTNG_TRACKER_VUID, value);
		if (!ret) {
			ret = LTTNG_OK;
			goto end;
		}
		break;
	case LTTNG_TRACKER_VGID:
		DBG("Kernel track VGID %d for session id %" PRIu64 ".", value,
				session->id);
		ret = kernctl_track_id(session->fd, LTTNG_TRACKER_VGID, value);
		if (!ret) {
			ret = LTTNG_OK;
			goto end;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	/* Error handling. */
	switch (-ret) {
	case EINVAL:
		ret = LTTNG_ERR_INVALID;
		break;
	case ENOMEM:
		ret = LTTNG_ERR_NOMEM;
		break;
	case EEXIST:
		ret = LTTNG_ERR_ID_TRACKED;
		break;
	default:
		ret = LTTNG_ERR_UNK;
		break;
	}

	if (lttng_tracker_id_set_list(tracker_list, saved_ids) != LTTNG_OK) {
		ERR("Error on tracker add error handling.\n");
	}
end:
	lttng_tracker_ids_destroy(saved_ids);
	return ret;
}

int kernel_untrack_id(enum lttng_tracker_type tracker_type,
		struct ltt_kernel_session *session,
		const struct lttng_tracker_id *id)
{
	int ret, value;
	struct lttng_tracker_list *tracker_list;
	struct lttng_tracker_ids *saved_ids;

	ret = lttng_tracker_id_lookup_string(tracker_type, id, &value);
	if (ret != LTTNG_OK) {
		return ret;
	}

	tracker_list = get_id_tracker_list(session, tracker_type);
	if (!tracker_list) {
		return LTTNG_ERR_INVALID;
	}
	/* Save list for restore on error. */
	ret = lttng_tracker_id_get_list(tracker_list, &saved_ids);
	if (ret != LTTNG_OK) {
		return LTTNG_ERR_INVALID;
	}
	/* Remove from list. */
	ret = lttng_tracker_list_remove(tracker_list, id);
	if (ret != LTTNG_OK) {
		goto end;
	}

	switch (tracker_type) {
	case LTTNG_TRACKER_PID:
		DBG("Kernel untrack PID %d for session id %" PRIu64 ".", value,
				session->id);
		ret = kernctl_untrack_pid(session->fd, value);
		if (!ret) {
			ret = LTTNG_OK;
			goto end;
		}
		break;
	case LTTNG_TRACKER_VPID:
		DBG("Kernel untrack VPID %d for session id %" PRIu64 ".", value,
				session->id);
		ret = kernctl_untrack_id(
				session->fd, LTTNG_TRACKER_VPID, value);
		if (!ret) {
			ret = LTTNG_OK;
			goto end;
		}
		break;
	case LTTNG_TRACKER_UID:
		DBG("Kernel untrack UID %d for session id %" PRIu64 ".", value,
				session->id);
		ret = kernctl_untrack_id(session->fd, LTTNG_TRACKER_UID, value);
		if (!ret) {
			ret = LTTNG_OK;
			goto end;
		}
		break;
	case LTTNG_TRACKER_GID:
		DBG("Kernel untrack GID %d for session id %" PRIu64 ".", value,
				session->id);
		ret = kernctl_untrack_id(session->fd, LTTNG_TRACKER_GID, value);
		if (!ret) {
			ret = LTTNG_OK;
			goto end;
		}
		break;
	case LTTNG_TRACKER_VUID:
		DBG("Kernel untrack VUID %d for session id %" PRIu64 ".", value,
				session->id);
		ret = kernctl_untrack_id(
				session->fd, LTTNG_TRACKER_VUID, value);
		if (!ret) {
			ret = LTTNG_OK;
			goto end;
		}
		break;
	case LTTNG_TRACKER_VGID:
		DBG("Kernel untrack VGID %d for session id %" PRIu64 ".", value,
				session->id);
		ret = kernctl_untrack_id(
				session->fd, LTTNG_TRACKER_VGID, value);
		if (!ret) {
			ret = LTTNG_OK;
			goto end;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	/* Error handling. */
	switch (-ret) {
	case EINVAL:
		ret = LTTNG_ERR_INVALID;
		break;
	case ENOMEM:
		ret = LTTNG_ERR_NOMEM;
		break;
	case EEXIST:
		ret = LTTNG_ERR_ID_TRACKED;
		break;
	default:
		ret = LTTNG_ERR_UNK;
		break;
	}

	if (lttng_tracker_id_set_list(tracker_list, saved_ids) != LTTNG_OK) {
		ERR("Error on tracker remove error handling.\n");
	}
end:
	lttng_tracker_ids_destroy(saved_ids);
	return ret;
}

/*
 * Called with session lock held.
 */
int kernel_list_tracker_ids(enum lttng_tracker_type tracker_type,
		struct ltt_kernel_session *session,
		struct lttng_tracker_ids **_ids)
{
	int ret = 0;
	struct lttng_tracker_list *tracker_list;

	tracker_list = get_id_tracker_list(session, tracker_type);
	if (!tracker_list) {
		ret = -LTTNG_ERR_INVALID;
		goto end;
	}

	ret = lttng_tracker_id_get_list(tracker_list, _ids);
	if (ret != LTTNG_OK) {
		ret = -LTTNG_ERR_INVALID;
		goto end;
	}

end:
	return ret;
}

/*
 * Create kernel metadata, open from the kernel tracer and add it to the
 * kernel session.
 */
int kernel_open_metadata(struct ltt_kernel_session *session)
{
	int ret;
	struct ltt_kernel_metadata *lkm = NULL;

	assert(session);

	/* Allocate kernel metadata */
	lkm = trace_kernel_create_metadata();
	if (lkm == NULL) {
		goto error;
	}

	/* Kernel tracer metadata creation */
	ret = kernctl_open_metadata(session->fd, &lkm->conf->attr);
	if (ret < 0) {
		goto error_open;
	}

	lkm->fd = ret;
	lkm->key = ++next_kernel_channel_key;
	/* Prevent fd duplication after execlp() */
	ret = fcntl(lkm->fd, F_SETFD, FD_CLOEXEC);
	if (ret < 0) {
		PERROR("fcntl session fd");
	}

	session->metadata = lkm;

	DBG("Kernel metadata opened (fd: %d)", lkm->fd);

	return 0;

error_open:
	trace_kernel_destroy_metadata(lkm);
error:
	return -1;
}

/*
 * Start tracing session.
 */
int kernel_start_session(struct ltt_kernel_session *session)
{
	int ret;

	assert(session);

	ret = kernctl_start_session(session->fd);
	if (ret < 0) {
		PERROR("ioctl start session");
		goto error;
	}

	DBG("Kernel session started");

	return 0;

error:
	return ret;
}

/*
 * Make a kernel wait to make sure in-flight probe have completed.
 */
void kernel_wait_quiescent(void)
{
	int ret;
	int fd = kernel_tracer_fd;

	DBG("Kernel quiescent wait on %d", fd);

	ret = kernctl_wait_quiescent(fd);
	if (ret < 0) {
		PERROR("wait quiescent ioctl");
		ERR("Kernel quiescent wait failed");
	}
}

/*
 *  Force flush buffer of metadata.
 */
int kernel_metadata_flush_buffer(int fd)
{
	int ret;

	DBG("Kernel flushing metadata buffer on fd %d", fd);

	ret = kernctl_buffer_flush(fd);
	if (ret < 0) {
		ERR("Fail to flush metadata buffers %d (ret: %d)", fd, ret);
	}

	return 0;
}

/*
 * Force flush buffer for channel.
 */
int kernel_flush_buffer(struct ltt_kernel_channel *channel)
{
	int ret;
	struct ltt_kernel_stream *stream;

	assert(channel);

	DBG("Flush buffer for channel %s", channel->channel->name);

	cds_list_for_each_entry(stream, &channel->stream_list.head, list) {
		DBG("Flushing channel stream %d", stream->fd);
		ret = kernctl_buffer_flush(stream->fd);
		if (ret < 0) {
			PERROR("ioctl");
			ERR("Fail to flush buffer for stream %d (ret: %d)",
					stream->fd, ret);
		}
	}

	return 0;
}

/*
 * Stop tracing session.
 */
int kernel_stop_session(struct ltt_kernel_session *session)
{
	int ret;

	assert(session);

	ret = kernctl_stop_session(session->fd);
	if (ret < 0) {
		goto error;
	}

	DBG("Kernel session stopped");

	return 0;

error:
	return ret;
}

/*
 * Open stream of channel, register it to the kernel tracer and add it
 * to the stream list of the channel.
 *
 * Note: given that the streams may appear in random order wrt CPU
 * number (e.g. cpu hotplug), the index value of the stream number in
 * the stream name is not necessarily linked to the CPU number.
 *
 * Return the number of created stream. Else, a negative value.
 */
int kernel_open_channel_stream(struct ltt_kernel_channel *channel)
{
	int ret;
	struct ltt_kernel_stream *lks;

	assert(channel);

	while ((ret = kernctl_create_stream(channel->fd)) >= 0) {
		lks = trace_kernel_create_stream(channel->channel->name,
				channel->stream_count);
		if (lks == NULL) {
			ret = close(ret);
			if (ret) {
				PERROR("close");
			}
			goto error;
		}

		lks->fd = ret;
		/* Prevent fd duplication after execlp() */
		ret = fcntl(lks->fd, F_SETFD, FD_CLOEXEC);
		if (ret < 0) {
			PERROR("fcntl session fd");
		}

		lks->tracefile_size = channel->channel->attr.tracefile_size;
		lks->tracefile_count = channel->channel->attr.tracefile_count;

		/* Add stream to channel stream list */
		cds_list_add(&lks->list, &channel->stream_list.head);
		channel->stream_count++;

		DBG("Kernel stream %s created (fd: %d, state: %d)", lks->name, lks->fd,
				lks->state);
	}

	return channel->stream_count;

error:
	return -1;
}

/*
 * Open the metadata stream and set it to the kernel session.
 */
int kernel_open_metadata_stream(struct ltt_kernel_session *session)
{
	int ret;

	assert(session);

	ret = kernctl_create_stream(session->metadata->fd);
	if (ret < 0) {
		PERROR("kernel create metadata stream");
		goto error;
	}

	DBG("Kernel metadata stream created (fd: %d)", ret);
	session->metadata_stream_fd = ret;
	/* Prevent fd duplication after execlp() */
	ret = fcntl(session->metadata_stream_fd, F_SETFD, FD_CLOEXEC);
	if (ret < 0) {
		PERROR("fcntl session fd");
	}

	return 0;

error:
	return -1;
}

/*
 * Get the event list from the kernel tracer and return the number of elements.
 */
ssize_t kernel_list_events(struct lttng_event **events)
{
	int fd, ret;
	char *event;
	size_t nbmem, count = 0;
	FILE *fp;
	struct lttng_event *elist;

	assert(events);

	fd = kernctl_tracepoint_list(kernel_tracer_fd);
	if (fd < 0) {
		PERROR("kernel tracepoint list");
		goto error;
	}

	fp = fdopen(fd, "r");
	if (fp == NULL) {
		PERROR("kernel tracepoint list fdopen");
		goto error_fp;
	}

	/*
	 * Init memory size counter
	 * See kernel-ctl.h for explanation of this value
	 */
	nbmem = KERNEL_EVENT_INIT_LIST_SIZE;
	elist = zmalloc(sizeof(struct lttng_event) * nbmem);
	if (elist == NULL) {
		PERROR("alloc list events");
		count = -ENOMEM;
		goto end;
	}

	while (fscanf(fp, "event { name = %m[^;]; };\n", &event) == 1) {
		if (count >= nbmem) {
			struct lttng_event *new_elist;
			size_t new_nbmem;

			new_nbmem = nbmem << 1;
			DBG("Reallocating event list from %zu to %zu bytes",
					nbmem, new_nbmem);
			new_elist = realloc(elist, new_nbmem * sizeof(struct lttng_event));
			if (new_elist == NULL) {
				PERROR("realloc list events");
				free(event);
				free(elist);
				count = -ENOMEM;
				goto end;
			}
			/* Zero the new memory */
			memset(new_elist + nbmem, 0,
				(new_nbmem - nbmem) * sizeof(struct lttng_event));
			nbmem = new_nbmem;
			elist = new_elist;
		}
		strncpy(elist[count].name, event, LTTNG_SYMBOL_NAME_LEN);
		elist[count].name[LTTNG_SYMBOL_NAME_LEN - 1] = '\0';
		elist[count].enabled = -1;
		count++;
		free(event);
	}

	*events = elist;
	DBG("Kernel list events done (%zu events)", count);
end:
	ret = fclose(fp);	/* closes both fp and fd */
	if (ret) {
		PERROR("fclose");
	}
	return count;

error_fp:
	ret = close(fd);
	if (ret) {
		PERROR("close");
	}
error:
	return -1;
}

/*
 * Get kernel version and validate it.
 */
int kernel_validate_version(struct lttng_kernel_tracer_version *version,
		struct lttng_kernel_tracer_abi_version *abi_version)
{
	int ret;

	ret = kernctl_tracer_version(kernel_tracer_fd, version);
	if (ret < 0) {
		ERR("Failed to retrieve the lttng-modules version");
		goto error;
	}

	/* Validate version */
	if (version->major != VERSION_MAJOR) {
		ERR("Kernel tracer major version (%d) is not compatible with lttng-tools major version (%d)",
			version->major, VERSION_MAJOR);
		goto error_version;
	}
	ret = kernctl_tracer_abi_version(kernel_tracer_fd, abi_version);
	if (ret < 0) {
		ERR("Failed to retrieve lttng-modules ABI version");
		goto error;
	}
	if (abi_version->major != LTTNG_MODULES_ABI_MAJOR_VERSION) {
		ERR("Kernel tracer ABI version (%d.%d) does not match the expected ABI major version (%d.*)",
			abi_version->major, abi_version->minor,
			LTTNG_MODULES_ABI_MAJOR_VERSION);
		goto error;
	}
	DBG2("Kernel tracer version validated (%d.%d, ABI %d.%d)",
			version->major, version->minor,
			abi_version->major, abi_version->minor);
	return 0;

error_version:
	ret = -1;

error:
	ERR("Kernel tracer version check failed; kernel tracing will not be available");
	return ret;
}

/*
 * Kernel work-arounds called at the start of sessiond main().
 */
int init_kernel_workarounds(void)
{
	int ret;
	FILE *fp;

	/*
	 * boot_id needs to be read once before being used concurrently
	 * to deal with a Linux kernel race. A fix is proposed for
	 * upstream, but the work-around is needed for older kernels.
	 */
	fp = fopen("/proc/sys/kernel/random/boot_id", "r");
	if (!fp) {
		goto end_boot_id;
	}
	while (!feof(fp)) {
		char buf[37] = "";

		ret = fread(buf, 1, sizeof(buf), fp);
		if (ret < 0) {
			/* Ignore error, we don't really care */
		}
	}
	ret = fclose(fp);
	if (ret) {
		PERROR("fclose");
	}
end_boot_id:
	return 0;
}

/*
 * Teardown of a kernel session, keeping data required by destroy notifiers.
 */
void kernel_destroy_session(struct ltt_kernel_session *ksess)
{
	struct lttng_trace_chunk *trace_chunk;

	if (ksess == NULL) {
		DBG3("No kernel session when tearing down session");
		return;
	}

	DBG("Tearing down kernel session");
	trace_chunk = ksess->current_trace_chunk;

	/*
	 * Destroy channels on the consumer if at least one FD has been sent and we
	 * are in no output mode because the streams are in *no* monitor mode so we
	 * have to send a command to clean them up or else they leaked.
	 */
	if (!ksess->output_traces && ksess->consumer_fds_sent) {
		int ret;
		struct consumer_socket *socket;
		struct lttng_ht_iter iter;

		/* For each consumer socket. */
		rcu_read_lock();
		cds_lfht_for_each_entry(ksess->consumer->socks->ht, &iter.iter,
				socket, node.node) {
			struct ltt_kernel_channel *chan;

			/* For each channel, ask the consumer to destroy it. */
			cds_list_for_each_entry(chan, &ksess->channel_list.head, list) {
				ret = kernel_consumer_destroy_channel(socket, chan);
				if (ret < 0) {
					/* Consumer is probably dead. Use next socket. */
					continue;
				}
			}
		}
		rcu_read_unlock();
	}

	/* Close any relayd session */
	consumer_output_send_destroy_relayd(ksess->consumer);

	trace_kernel_destroy_session(ksess);
	lttng_trace_chunk_put(trace_chunk);
}

/* Teardown of data required by destroy notifiers. */
void kernel_free_session(struct ltt_kernel_session *ksess)
{
	if (ksess == NULL) {
		return;
	}
	trace_kernel_free_session(ksess);
}

/*
 * Destroy a kernel channel object. It does not do anything on the tracer side.
 */
void kernel_destroy_channel(struct ltt_kernel_channel *kchan)
{
	struct ltt_kernel_session *ksess = NULL;

	assert(kchan);
	assert(kchan->channel);

	DBG3("Kernel destroy channel %s", kchan->channel->name);

	/* Update channel count of associated session. */
	if (kchan->session) {
		/* Keep pointer reference so we can update it after the destroy. */
		ksess = kchan->session;
	}

	trace_kernel_destroy_channel(kchan);

	/*
	 * At this point the kernel channel is not visible anymore. This is safe
	 * since in order to work on a visible kernel session, the tracing session
	 * lock (ltt_session.lock) MUST be acquired.
	 */
	if (ksess) {
		ksess->channel_count--;
	}
}

/*
 * Take a snapshot for a given kernel session.
 *
 * Return LTTNG_OK on success or else return a LTTNG_ERR code.
 */
enum lttng_error_code kernel_snapshot_record(
		struct ltt_kernel_session *ksess,
		const struct consumer_output *output, int wait,
		uint64_t nb_packets_per_stream)
{
	int err, ret, saved_metadata_fd;
	enum lttng_error_code status = LTTNG_OK;
	struct consumer_socket *socket;
	struct lttng_ht_iter iter;
	struct ltt_kernel_metadata *saved_metadata;
	char *trace_path = NULL;
	size_t consumer_path_offset = 0;

	assert(ksess);
	assert(ksess->consumer);
	assert(output);

	DBG("Kernel snapshot record started");

	/* Save current metadata since the following calls will change it. */
	saved_metadata = ksess->metadata;
	saved_metadata_fd = ksess->metadata_stream_fd;

	rcu_read_lock();

	ret = kernel_open_metadata(ksess);
	if (ret < 0) {
		status = LTTNG_ERR_KERN_META_FAIL;
		goto error;
	}

	ret = kernel_open_metadata_stream(ksess);
	if (ret < 0) {
		status = LTTNG_ERR_KERN_META_FAIL;
		goto error_open_stream;
	}

	trace_path = setup_channel_trace_path(ksess->consumer,
			DEFAULT_KERNEL_TRACE_DIR, &consumer_path_offset);
	if (!trace_path) {
		status = LTTNG_ERR_INVALID;
		goto error;
	}
	/* Send metadata to consumer and snapshot everything. */
	cds_lfht_for_each_entry(output->socks->ht, &iter.iter,
			socket, node.node) {
		struct ltt_kernel_channel *chan;

		pthread_mutex_lock(socket->lock);
		/* This stream must not be monitored by the consumer. */
		ret = kernel_consumer_add_metadata(socket, ksess, 0);
		pthread_mutex_unlock(socket->lock);
		if (ret < 0) {
			status = LTTNG_ERR_KERN_META_FAIL;
			goto error_consumer;
		}

		/* For each channel, ask the consumer to snapshot it. */
		cds_list_for_each_entry(chan, &ksess->channel_list.head, list) {
			status = consumer_snapshot_channel(socket, chan->key, output, 0,
					ksess->uid, ksess->gid,
					&trace_path[consumer_path_offset], wait,
					nb_packets_per_stream);
			if (status != LTTNG_OK) {
				(void) kernel_consumer_destroy_metadata(socket,
						ksess->metadata);
				goto error_consumer;
			}
		}

		/* Snapshot metadata, */
		status = consumer_snapshot_channel(socket, ksess->metadata->key, output,
				1, ksess->uid, ksess->gid, &trace_path[consumer_path_offset],
				wait, 0);
		if (status != LTTNG_OK) {
			goto error_consumer;
		}

		/*
		 * The metadata snapshot is done, ask the consumer to destroy it since
		 * it's not monitored on the consumer side.
		 */
		(void) kernel_consumer_destroy_metadata(socket, ksess->metadata);
	}

error_consumer:
	/* Close newly opened metadata stream. It's now on the consumer side. */
	err = close(ksess->metadata_stream_fd);
	if (err < 0) {
		PERROR("close snapshot kernel");
	}

error_open_stream:
	trace_kernel_destroy_metadata(ksess->metadata);
error:
	/* Restore metadata state.*/
	ksess->metadata = saved_metadata;
	ksess->metadata_stream_fd = saved_metadata_fd;
	rcu_read_unlock();
	free(trace_path);
	return status;
}

/*
 * Get the syscall mask array from the kernel tracer.
 *
 * Return 0 on success else a negative value. In both case, syscall_mask should
 * be freed.
 */
int kernel_syscall_mask(int chan_fd, char **syscall_mask, uint32_t *nr_bits)
{
	assert(syscall_mask);
	assert(nr_bits);

	return kernctl_syscall_mask(chan_fd, syscall_mask, nr_bits);
}

/*
 * Check for the support of the RING_BUFFER_SNAPSHOT_SAMPLE_POSITIONS via abi
 * version number.
 *
 * Return 1 on success, 0 when feature is not supported, negative value in case
 * of errors.
 */
int kernel_supports_ring_buffer_snapshot_sample_positions(void)
{
	int ret = 0; // Not supported by default
	struct lttng_kernel_tracer_abi_version abi;

	ret = kernctl_tracer_abi_version(kernel_tracer_fd, &abi);
	if (ret < 0) {
		ERR("Failed to retrieve lttng-modules ABI version");
		goto error;
	}

	/*
	 * RING_BUFFER_SNAPSHOT_SAMPLE_POSITIONS was introduced in 2.3
	 */
	if (abi.major >= 2 && abi.minor >= 3) {
		/* Supported */
		ret = 1;
	} else {
		/* Not supported */
		ret = 0;
	}
error:
	return ret;
}

/*
 * Check for the support of the packet sequence number via abi version number.
 *
 * Return 1 on success, 0 when feature is not supported, negative value in case
 * of errors.
 */
int kernel_supports_ring_buffer_packet_sequence_number(void)
{
	int ret = 0; // Not supported by default
	struct lttng_kernel_tracer_abi_version abi;

	ret = kernctl_tracer_abi_version(kernel_tracer_fd, &abi);
	if (ret < 0) {
		ERR("Failed to retrieve lttng-modules ABI version");
		goto error;
	}

	/*
	 * Packet sequence number was introduced in LTTng 2.8,
	 * lttng-modules ABI 2.1.
	 */
	if (abi.major >= 2 && abi.minor >= 1) {
		/* Supported */
		ret = 1;
	} else {
		/* Not supported */
		ret = 0;
	}
error:
	return ret;
}

/*
 * Rotate a kernel session.
 *
 * Return LTTNG_OK on success or else an LTTng error code.
 */
enum lttng_error_code kernel_rotate_session(struct ltt_session *session)
{
	int ret;
	enum lttng_error_code status = LTTNG_OK;
	struct consumer_socket *socket;
	struct lttng_ht_iter iter;
	struct ltt_kernel_session *ksess = session->kernel_session;

	assert(ksess);
	assert(ksess->consumer);

	DBG("Rotate kernel session %s started (session %" PRIu64 ")",
			session->name, session->id);

	rcu_read_lock();

	/*
	 * Note that this loop will end after one iteration given that there is
	 * only one kernel consumer.
	 */
	cds_lfht_for_each_entry(ksess->consumer->socks->ht, &iter.iter,
			socket, node.node) {
		struct ltt_kernel_channel *chan;

                /* For each channel, ask the consumer to rotate it. */
		cds_list_for_each_entry(chan, &ksess->channel_list.head, list) {
			DBG("Rotate kernel channel %" PRIu64 ", session %s",
					chan->key, session->name);
			ret = consumer_rotate_channel(socket, chan->key,
					ksess->uid, ksess->gid, ksess->consumer,
					/* is_metadata_channel */ false);
			if (ret < 0) {
				status = LTTNG_ERR_KERN_CONSUMER_FAIL;
				goto error;
			}
		}

		/*
		 * Rotate the metadata channel.
		 */
		ret = consumer_rotate_channel(socket, ksess->metadata->key,
				ksess->uid, ksess->gid, ksess->consumer,
				/* is_metadata_channel */ true);
		if (ret < 0) {
			status = LTTNG_ERR_KERN_CONSUMER_FAIL;
			goto error;
		}
	}

error:
	rcu_read_unlock();
	return status;
}

enum lttng_error_code kernel_create_channel_subdirectories(
		const struct ltt_kernel_session *ksess)
{
	enum lttng_error_code ret = LTTNG_OK;
	enum lttng_trace_chunk_status chunk_status;

	rcu_read_lock();
	assert(ksess->current_trace_chunk);

	/*
	 * Create the index subdirectory which will take care
	 * of implicitly creating the channel's path.
	 */
	chunk_status = lttng_trace_chunk_create_subdirectory(
			ksess->current_trace_chunk,
			DEFAULT_KERNEL_TRACE_DIR "/" DEFAULT_INDEX_DIR);
	if (chunk_status != LTTNG_TRACE_CHUNK_STATUS_OK) {
		ret = LTTNG_ERR_CREATE_DIR_FAIL;
		goto error;
	}
error:
	rcu_read_unlock();
	return ret;
}

/*
 * Setup necessary data for kernel tracer action.
 */
LTTNG_HIDDEN
int init_kernel_tracer(void)
{
	int ret;
	bool is_root = !getuid();

	/* Modprobe lttng kernel modules */
	ret = modprobe_lttng_control();
	if (ret < 0) {
		goto error;
	}

	/* Open debugfs lttng */
	kernel_tracer_fd = open(module_proc_lttng, O_RDWR);
	if (kernel_tracer_fd < 0) {
		DBG("Failed to open %s", module_proc_lttng);
		goto error_open;
	}

	/* Validate kernel version */
	ret = kernel_validate_version(&kernel_tracer_version,
			&kernel_tracer_abi_version);
	if (ret < 0) {
		goto error_version;
	}

	ret = modprobe_lttng_data();
	if (ret < 0) {
		goto error_modules;
	}

	ret = kernel_supports_ring_buffer_snapshot_sample_positions();
	if (ret < 0) {
		goto error_modules;
	}
	if (ret < 1) {
		WARN("Kernel tracer does not support buffer monitoring. "
			"The monitoring timer of channels in the kernel domain "
			"will be set to 0 (disabled).");
	}

	ret = kernel_create_trigger_group(&kernel_tracer_trigger_group_fd);
	if (ret < 0) {
		/* TODO: error handling if it is not supported etc. */
		WARN("Failed trigger group creation");
		kernel_tracer_trigger_group_fd = -1;
		/* This is not fatal */
	} else {
		ret = kernel_create_trigger_group_notification_fd(&kernel_tracer_trigger_group_notification_fd);
		if (ret < 0) {
			goto error_modules;
		}
	}

	CDS_INIT_LIST_HEAD(&kernel_tracer_token_list.head);

	DBG("Kernel tracer fd %d", kernel_tracer_fd);
	DBG("Kernel tracer trigger group fd %d", kernel_tracer_trigger_group_fd);
	DBG("Kernel tracer trigger group notificationi fd %d", kernel_tracer_trigger_group_notification_fd);

	ret = syscall_init_table(kernel_tracer_fd);
	if (ret < 0) {
		ERR("Unable to populate syscall table. Syscall tracing won't "
			"work for this session daemon.");
	}

	return 0;

error_version:
	modprobe_remove_lttng_control();
	ret = close(kernel_tracer_fd);
	if (ret) {
		PERROR("close");
	}
	kernel_tracer_fd = -1;
	return LTTNG_ERR_KERN_VERSION;

error_modules:
	ret = close(kernel_tracer_fd);
	if (ret) {
		PERROR("close");
	}

error_open:
	modprobe_remove_lttng_control();

error:
	WARN("No kernel tracer available");
	kernel_tracer_fd = -1;
	if (!is_root) {
		return LTTNG_ERR_NEED_ROOT_SESSIOND;
	} else {
		return LTTNG_ERR_KERN_NA;
	}
}

LTTNG_HIDDEN
void cleanup_kernel_tracer(void)
{
	int ret;

	struct ltt_kernel_token_event_rule *rule, *rtmp;
        cds_list_for_each_entry_safe(rule, rtmp, &kernel_tracer_token_list.head, list) {
		kernel_disable_token_event_rule(rule);
		trace_kernel_destroy_token_event_rule(rule);
	}

	DBG2("Closing kernel trigger group notification fd");
	if (kernel_tracer_trigger_group_notification_fd >= 0) {
		ret = close(kernel_tracer_trigger_group_notification_fd);
		if (ret) {
			PERROR("close");
		}
		kernel_tracer_trigger_group_notification_fd = -1;
	}

	/* TODO: do we iterate over the list to remove all token? */
	DBG2("Closing kernel trigger group fd");
	if (kernel_tracer_trigger_group_fd >= 0) {
		ret = close(kernel_tracer_trigger_group_fd);
		if (ret) {
			PERROR("close");
		}
		kernel_tracer_trigger_group_fd = -1;
	}

	DBG2("Closing kernel fd");
	if (kernel_tracer_fd >= 0) {
		ret = close(kernel_tracer_fd);
		if (ret) {
			PERROR("close");
		}
		kernel_tracer_fd = -1;
	}

	
	DBG("Unloading kernel modules");
	modprobe_remove_lttng_all();
	free(syscall_table);
}

LTTNG_HIDDEN
bool kernel_tracer_is_initialized(void)
{
	return kernel_tracer_fd >= 0;
}

/*
 *  Clear a kernel session.
 *
 * Return LTTNG_OK on success or else an LTTng error code.
 */
enum lttng_error_code kernel_clear_session(struct ltt_session *session)
{
	int ret;
	enum lttng_error_code status = LTTNG_OK;
	struct consumer_socket *socket;
	struct lttng_ht_iter iter;
	struct ltt_kernel_session *ksess = session->kernel_session;

	assert(ksess);
	assert(ksess->consumer);

	DBG("Clear kernel session %s (session %" PRIu64 ")",
			session->name, session->id);

	rcu_read_lock();

	if (ksess->active) {
		ERR("Expecting inactive session %s (%" PRIu64 ")", session->name, session->id);
		status = LTTNG_ERR_FATAL;
		goto end;
	}

	/*
	 * Note that this loop will end after one iteration given that there is
	 * only one kernel consumer.
	 */
	cds_lfht_for_each_entry(ksess->consumer->socks->ht, &iter.iter,
			socket, node.node) {
		struct ltt_kernel_channel *chan;

		/* For each channel, ask the consumer to clear it. */
		cds_list_for_each_entry(chan, &ksess->channel_list.head, list) {
			DBG("Clear kernel channel %" PRIu64 ", session %s",
					chan->key, session->name);
			ret = consumer_clear_channel(socket, chan->key);
			if (ret < 0) {
				goto error;
			}
		}

		if (!ksess->metadata) {
			/*
			 * Nothing to do for the metadata.
			 * This is a snapshot session.
			 * The metadata is genererated on the fly.
			 */
			continue;
		}

		/*
		 * Clear the metadata channel.
		 * Metadata channel is not cleared per se but we still need to
		 * perform a rotation operation on it behind the scene.
		 */
		ret = consumer_clear_channel(socket, ksess->metadata->key);
		if (ret < 0) {
			goto error;
		}
	}

	goto end;
error:
	switch (-ret) {
	case LTTCOMM_CONSUMERD_RELAYD_CLEAR_DISALLOWED:
	      status = LTTNG_ERR_CLEAR_RELAY_DISALLOWED;
	      break;
	default:
	      status = LTTNG_ERR_CLEAR_FAIL_CONSUMER;
	      break;
	}
end:
	rcu_read_unlock();
	return status;
}

enum lttng_error_code kernel_create_trigger_group_notification_fd(
		int *trigger_group_notification_fd)
{
	enum lttng_error_code ret = LTTNG_OK;
	int local_fd = -1;

	assert(trigger_group_notification_fd);

	ret = kernctl_create_trigger_group_notification_fd(kernel_tracer_trigger_group_fd);
	if (ret < 0) {
		PERROR("ioctl kernel create trigger group");
		ret = -1;
		goto error;
	}

	/* Store locally */
	local_fd = ret;

	/* Prevent fd duplication after execlp() */
	ret = fcntl(local_fd, F_SETFD, FD_CLOEXEC);
	if (ret < 0) {
		PERROR("fcntl session fd");
	}

	DBG("Kernel trigger group notification created (fd: %d)",
			local_fd);
	ret = 0;

error:
	*trigger_group_notification_fd = local_fd;
	return ret;
}

enum lttng_error_code kernel_destroy_trigger_group_notification_fd(
		int trigger_group_notification_fd)
{
	enum lttng_error_code ret = LTTNG_OK;
	DBG("Closing trigger group notification fd %d", trigger_group_notification_fd);
	if (trigger_group_notification_fd >= 0) {
		ret = close(trigger_group_notification_fd);
		if (ret) {
			PERROR("close");
		}
	}
	return ret;
}

/* TODO: find a better way, this is copied from notification-thread-events.c to
 * allows the lookup of the "passed" hast table from the notification thread.
 * This is ugly as fuck since the passed hash table is a cdf instead of a
 * lttng_ht
 */
static
int match_trigger_token(struct cds_lfht_node *node, const void *key)
{
	const uint64_t *_key = key;
	struct notification_trigger_tokens_ht_element *element;

	element = caa_container_of(node, struct notification_trigger_tokens_ht_element,
			node);
	return *_key == element->token ;
}

static int kernel_create_token_event_rule(struct lttng_event_rule *rule,
		const struct lttng_credentials *creds, uint64_t token)
{
	int err, fd;
	enum lttng_error_code ret;
	struct ltt_kernel_token_event_rule *event;
	struct lttng_kernel_trigger trigger;

	assert(rule);

	ret = trace_kernel_create_token_event_rule(rule, token, &event);
	if (ret != LTTNG_OK) {
		goto error;
	}
	
	trace_kernel_init_trigger_from_event_rule(event->event_rule, &trigger);
	trigger.id = event->token;

	fd = kernctl_create_trigger(kernel_tracer_trigger_group_fd, &trigger);
	if (fd < 0) {
		switch (-fd) {
		case EEXIST:
			ret = LTTNG_ERR_KERN_EVENT_EXIST;
			break;
		case ENOSYS:
			WARN("Trigger type not implemented");
			ret = LTTNG_ERR_KERN_EVENT_ENOSYS;
			break;
		case ENOENT:
			WARN("Event %s not found!", trigger.name);
			ret = LTTNG_ERR_KERN_ENABLE_FAIL;
			break;
		default:
			ret = LTTNG_ERR_KERN_ENABLE_FAIL;
			PERROR("create trigger ioctl");
		}
		goto free_event;
	}

	event->fd = fd;
	/* Prevent fd duplication after execlp() */
	err = fcntl(event->fd, F_SETFD, FD_CLOEXEC);
	if (err < 0) {
		PERROR("fcntl session fd");
	}

	if (event->filter) {
		err = kernctl_filter(event->fd, event->filter);
		if (err < 0) {
			switch (-err) {
			case ENOMEM:
				ret = LTTNG_ERR_FILTER_NOMEM;
				break;
			default:
				ret = LTTNG_ERR_FILTER_INVAL;
				break;
			}
			goto filter_error;
		}
	}

	if (lttng_event_rule_get_type(event->event_rule) ==
			LTTNG_EVENT_RULE_TYPE_UPROBE) {
		ret = userspace_probe_event_rule_add_callsites(
				rule, creds, event->fd);
		if (ret) {
			goto add_callsite_error;
		}
	}

	err = kernctl_enable(event->fd);
	if (err < 0) {
		switch (-err) {
		case EEXIST:
			ret = LTTNG_ERR_KERN_EVENT_EXIST;
			break;
		default:
			PERROR("enable kernel trigger");
			ret = LTTNG_ERR_KERN_ENABLE_FAIL;
			break;
		}
		goto enable_error;
	}

	/* Add event to event list */
	cds_list_add(&event->list, &kernel_tracer_token_list.head);

	DBG("Trigger %s created (fd: %d)", trigger.name, event->fd);

	return 0;

add_callsite_error:
enable_error:
filter_error:
	{
		int closeret;

		closeret = close(event->fd);
		if (closeret) {
			PERROR("close event fd");
		}
	}
free_event:
	free(event);
error:
	return ret;
}

enum lttng_error_code kernel_update_tokens(void)
{
	enum lttng_error_code ret = LTTNG_OK;
	struct cds_lfht *trigger_tokens_ht = NULL;
	struct cds_lfht_iter iter;
	struct notification_trigger_tokens_ht_element *trigger_token_element;
	struct ltt_kernel_token_event_rule *token_event_rule_element;
	
	/* TODO error handling */

	/* Get list of token trigger from the notification thread here */
	rcu_read_lock();
	pthread_mutex_lock(&notification_trigger_tokens_ht_lock);
	ret = notification_thread_command_get_tokens(notification_thread_handle, &trigger_tokens_ht);
	if (ret != LTTNG_OK) {
		ret = -1;
		goto end;
	}

	assert(trigger_tokens_ht);

	cds_lfht_for_each_entry (trigger_tokens_ht, &iter,
			trigger_token_element, node) {
		struct lttng_condition *condition;
		struct lttng_event_rule *event_rule;
		struct lttng_trigger *trigger;
		const struct lttng_credentials *creds;
		uint64_t token;
		struct ltt_kernel_token_event_rule *k_token;

		/* TODO: error checking and type checking */
		token = trigger_token_element->token;
		trigger = trigger_token_element->trigger;
		condition = lttng_trigger_get_condition(trigger);
		(void) lttng_condition_event_rule_get_rule_no_const(condition, &event_rule);

		if (lttng_event_rule_get_domain_type(event_rule) != LTTNG_DOMAIN_KERNEL) {
			/* Skip ust related trigger */
			continue;
		}

		creds = lttng_trigger_get_credentials(trigger);
		/* Iterate over all known token trigger */
		k_token = trace_kernel_find_trigger_by_token(&kernel_tracer_token_list, token);
		if (!k_token) {
			ret = kernel_create_token_event_rule(event_rule, creds, token);
			if (ret < 0) {
				goto end;
			}
		}
	}

	/* Remove all unknown trigger from the app
	 * TODO find a way better way then this, do it on the unregister command
	 * and be specific on the token to remove instead of going over all
	 * trigger known to the app. This is sub optimal.
	 */
	cds_list_for_each_entry (token_event_rule_element, &kernel_tracer_token_list.head,
			list) {
		struct cds_lfht_node *node;
		struct cds_lfht_iter lookup_iter;
		uint64_t token;

		token = token_event_rule_element->token;

		/* Check if the app event trigger still exists on the
		 * notification side.
		 */
		cds_lfht_lookup(trigger_tokens_ht, hash_key_u64(&token, lttng_ht_seed),
				match_trigger_token, &token, &lookup_iter);
		node = cds_lfht_iter_get_node(&lookup_iter);
		if (node != NULL) {
			/* Still valid, continue */
			continue;
		}

		kernel_disable_token_event_rule(token_event_rule_element);
		trace_kernel_destroy_token_event_rule(token_event_rule_element);
	}
end:
	rcu_read_unlock();
	pthread_mutex_unlock(&notification_trigger_tokens_ht_lock);
	return ret;

}

int kernel_get_notification_fd(void)
{
	return kernel_tracer_trigger_group_notification_fd;
}
