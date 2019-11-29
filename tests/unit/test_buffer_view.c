/*
 * Copyright (C) - EfficiOS, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by as
 * published by the Free Software Foundation; only version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <common/buffer-view.h>
#include <tap/tap.h>

static const int TEST_COUNT = 5;

/* For error.h */
int lttng_opt_quiet = 1;
int lttng_opt_verbose;
int lttng_opt_mi;

static void test_validate_string(void)
{
	const char buf[] = {'A', 'l', 'l', 'o', '\0'};
	struct lttng_buffer_view view = lttng_buffer_view_init(buf, 0, 5);
	struct lttng_buffer_view view_minus_one =
			lttng_buffer_view_init(buf, 0, 4);

	ok1(!lttng_buffer_view_validate_string(&view, buf, 4));
	ok1(lttng_buffer_view_validate_string(&view, buf, 5));
	ok1(!lttng_buffer_view_validate_string(&view, buf, 6));

	ok1(!lttng_buffer_view_validate_string(&view_minus_one, buf, 4));
	ok1(!lttng_buffer_view_validate_string(&view_minus_one, buf, 5));
}

int main(void)
{
	plan_tests(TEST_COUNT);

	test_validate_string();

	return exit_status();
}
