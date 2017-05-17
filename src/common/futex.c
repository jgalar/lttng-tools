/*
 * Copyright (C)  2011 - David Goulet <david.goulet@polymtl.ca>
 *                       Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _LGPL_SOURCE
#include <limits.h>
#include <unistd.h>
#include <urcu.h>
#include <urcu/futex.h>

#include <common/common.h>

#include "futex.h"

/*
 * Update futex according to active or not. This scheme is used to wake every
 * libust waiting on the shared memory map futex hence the INT_MAX used in the
 * futex() call. If active, we set the value and wake everyone else we indicate
 * that we are gone (cleanup() case).
 */
LTTNG_HIDDEN
void futex_wait_update(int32_t *futex, int active)
{
	if (active) {
		uatomic_set(futex, 1);
		if (futex_async(futex, FUTEX_WAKE,
				INT_MAX, NULL, NULL, 0) < 0) {
			PERROR("futex_async");
			abort();
		}
	} else {
		uatomic_set(futex, 0);
	}

	DBG("Futex wait update active %d", active);
}
