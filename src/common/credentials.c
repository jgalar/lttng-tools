/*
 * Copyright (C) 2020 - Jonathan Rajotte-Julien <jonathan.rajotte-julien@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <assert.h>
#include <stdbool.h>
#include "credentials.h"

bool lttng_credentials_is_equal(const struct lttng_credentials *a,
		const struct lttng_credentials *b)
{
	assert(a);
	assert(b);
	if ((a->uid != b->uid) || (a->gid != b->gid)) {
		return false;
	}
	return true;
};
