/*
 * test_condition.c
 *
 * Unit tests for the condition API.
 *
 * Copyright (C) 2019 Jonathan Rajotte <jonathan.rajotte-julien@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <tap/tap.h>

#include <lttng/event.h>
#include <lttng/event-rule/event-rule-tracepoint.h>
#include <lttng/condition/condition-internal.h>
#include <lttng/condition/event-rule.h>
#include <lttng/domain.h>
#include <common/dynamic-buffer.h>
#include <common/buffer-view.h>

/* For error.h */
int lttng_opt_quiet = 1;
int lttng_opt_verbose;
int lttng_opt_mi;

#define NUM_TESTS 3

void test_condition_event_rule(void)
{
	int ret;
	struct lttng_event_rule *tracepoint = NULL;
	const struct lttng_event_rule *tracepoint_tmp = NULL;
	enum lttng_event_rule_status status;
	struct lttng_condition *condition = NULL;
	struct lttng_condition *condition_from_buffer = NULL;
	enum lttng_condition_status condition_status;
	const char *pattern="my_event_*";
	const char *filter="msg_id == 23 && size >= 2048";
	const char *exclusions[] = {"my_event_test1", "my_event_test2" ,"my_event_test3"};
	struct lttng_dynamic_buffer buffer;
	struct lttng_buffer_view view;

	lttng_dynamic_buffer_init(&buffer);

	tracepoint = lttng_event_rule_tracepoint_create(LTTNG_DOMAIN_UST);
	ok(tracepoint, "tracepoint UST_DOMAIN");

	status = lttng_event_rule_tracepoint_set_pattern(tracepoint, pattern);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "setting pattern");

	status = lttng_event_rule_tracepoint_set_filter(tracepoint, filter);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "setting filter");

	status = lttng_event_rule_tracepoint_set_loglevel_range(tracepoint, LTTNG_LOGLEVEL_WARNING);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "setting range loglevel");

	status = lttng_event_rule_tracepoint_set_exclusions(tracepoint, 3, exclusions);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "setting exclusions");

	condition = lttng_condition_event_rule_create(tracepoint);
	ok(condition, "created condition");

	condition_status = lttng_condition_event_rule_get_rule(condition, &tracepoint_tmp);
	ok(condition_status == LTTNG_CONDITION_STATUS_OK, "getting event rule");
	ok(tracepoint == tracepoint_tmp, "Ownership transfer is good");

	ret = lttng_condition_serialize(condition, &buffer);
	ok(ret == 0, "Condition serialized");

	view = lttng_buffer_view_from_dynamic_buffer(&buffer, 0, -1);
	(void) lttng_condition_create_from_buffer(&view, &condition_from_buffer);
	ok(condition_from_buffer, "condition from buffer is non null");

	ok(lttng_condition_is_equal(condition, condition_from_buffer), "serialized and from buffer are equal");

	lttng_dynamic_buffer_reset(&buffer);
	lttng_condition_destroy(condition);
	lttng_condition_destroy(condition_from_buffer);
}

int main(int argc, const char *argv[])
{
	plan_tests(NUM_TESTS);
	test_condition_event_rule();
	return exit_status();
}
