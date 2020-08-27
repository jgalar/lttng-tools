/*
 * event-internal.h
 *
 * Linux Trace Toolkit Control Library
 *
 * Copyright (C) 2017 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#ifndef LTTNG_EVENT_INTERNAL_H
#define LTTNG_EVENT_INTERNAL_H

#include <common/macros.h>
#include <lttng/event.h>

struct lttng_userspace_probe_location;

struct lttng_event_extended {
	/*
	 * exclusions and filter_expression are only set when the lttng_event
	 * was created/allocated by a list operation. These two elements must
	 * not be free'd as they are part of the same contiguous buffer that
	 * contains all events returned by the listing.
	 */
	char *filter_expression;
	struct {
		unsigned int count;
		/* Array of strings of fixed LTTNG_SYMBOL_NAME_LEN length. */
		char *strings;
	} exclusions;
	struct lttng_userspace_probe_location *probe_location;
};

LTTNG_HIDDEN
struct lttng_event *lttng_event_copy(const struct lttng_event *event);

// FIXME: the implementation of these should be moved to some common file,
// they should not be in the enable_events.c file.

LTTNG_HIDDEN
int loglevel_str_to_value(const char *inputstr);

LTTNG_HIDDEN
int loglevel_log4j_str_to_value(const char *inputstr);

LTTNG_HIDDEN
int loglevel_jul_str_to_value(const char *inputstr);

LTTNG_HIDDEN
int loglevel_python_str_to_value(const char *inputstr);

#endif /* LTTNG_EVENT_INTERNAL_H */
