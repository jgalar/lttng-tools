/*
 * Copyright (C) 2016 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef LTTNG_TIME_H
#define LTTNG_TIME_H

#include <time.h>
#include <stdbool.h>

#define MSEC_PER_SEC	1000ULL
#define NSEC_PER_SEC	1000000000ULL
#define NSEC_PER_MSEC	1000000ULL
#define NSEC_PER_USEC	1000ULL

bool locale_supports_utf8(void);

#define NSEC_UNIT	"ns"
#define USEC_UNIT	(locale_supports_utf8() ? "µs" : "us")
#define MSEC_UNIT	"ms"
#define SEC_UNIT	"s"
#define MINUTE_UNIT	"m"
#define HOUR_UNIT	"h"

/*
 * timespec_to_ms: Convert timespec to milliseconds.
 *
 * Returns 0 on success, else -1 on error. errno is set to EOVERFLOW if
 * input would overflow the output in milliseconds.
 */
int timespec_to_ms(struct timespec ts, unsigned long *ms);

/*
 * timespec_abs_diff: Absolute difference between timespec.
 */
struct timespec timespec_abs_diff(struct timespec ts_a, struct timespec ts_b);

#endif /* LTTNG_TIME_H */
