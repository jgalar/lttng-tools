/*
 * Copyright (C) 2019 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License, version 2.1 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <common/dynamic-array.h>

LTTNG_HIDDEN
void lttng_dynamic_array_init(struct lttng_dynamic_array *array,
		size_t element_size)
{
	lttng_dynamic_buffer_init(&array->buffer);
	array->element_size = element_size;
}

LTTNG_HIDDEN
int lttng_dynamic_array_add_element(struct lttng_dynamic_array *array,
		const void *element)
{
	int ret;

	if (!array || !element) {
		ret = -1;
		goto end;
	}

	ret = lttng_dynamic_buffer_append(&array->buffer, element,
			array->element_size);
	if (ret) {
		goto end;
	}
	array->size++;
end:
	return ret;
}

LTTNG_HIDDEN
void lttng_dynamic_array_reset(struct lttng_dynamic_array *array)
{
	lttng_dynamic_buffer_reset(&array->buffer);
	array->size = 0;
}
