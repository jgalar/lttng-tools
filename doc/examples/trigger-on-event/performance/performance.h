/*
 * Copyright (C) 2020 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: MIT
 *
 */

#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER performance

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./performance.h"

#if !defined(_TRACEPOINT_PERFORMANCE_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define _TRACEPOINT_PERFORMANCE_H

#include <lttng/tracepoint.h>

TRACEPOINT_EVENT(performance, hit,
	TP_ARGS(int, source,
		int, iteration),
	TP_FIELDS(
		ctf_integer(uint64_t, source, source)
		ctf_integer(uint64_t, iteration, iteration)
	)
)

TRACEPOINT_EVENT(performance, receive,
	TP_ARGS(int, source,
		int, iteration),
	TP_FIELDS(
		ctf_integer(uint64_t, source, source)
		ctf_integer(uint64_t, iteration, iteration)
	)
)

#endif /* _TRACEPOINT_PERFORMANCE_H */

#include <lttng/tracepoint-event.h>
