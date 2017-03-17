/*
 * Copyright (C) 2017 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License, version 2.1 only,
 * as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <common/dynamic-buffer.h>
#include <common/macros.h>
#include <common/utils.h>
#include <assert.h>

static
size_t round_to_power_of_2(size_t val)
{
	int order;
	size_t rounded;

	order = utils_get_count_order_u64(val);
	assert(order >= 0);
	rounded = (1ULL << order);
	assert(rounded >= val);

	return rounded;
}

int lttng_dynamic_buffer_append(struct lttng_dynamic_buffer *buffer,
		const void *buf, size_t len)
{
	int ret = 0;

	assert(buffer);
	assert(buf);

	if (len == 0) {
		goto end;
	}

	if (!buffer->data) {
		size_t new_capacity = round_to_power_of_2(len);

		buffer->data = zmalloc(new_capacity);
		if (!buffer->data) {
			ret = -1;
			goto end;
		}
		buffer->capacity = new_capacity;
	} else if (buffer->capacity - buffer->size < len) {
		void *new_buf;
		size_t new_capacity = round_to_power_of_2(
				buffer->capacity + len);

		new_buf = realloc(buffer->data, new_capacity);
		if (new_buf) {
			memset(new_buf + buffer->capacity, 0,
					new_capacity - buffer->capacity);
		} else {
			/* Realloc failed, try to acquire a new block. */
			new_buf = zmalloc(new_capacity);
			if (!new_buf) {
				ret = -1;
				goto end;
			}
			memcpy(new_buf, buffer->data, buffer->size);
			free(buffer->data);
			buffer->data = new_buf;
		}
		buffer->capacity = new_capacity;
	}

	memcpy(buffer->data + buffer->size, buf, len);
	buffer->size += len;
end:
	return ret;
}
