/*
 * test_event_rule.c
 *
 * Unit tests for the notification API.
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
#include <lttng/event-rule/event-rule-tracepoint-internal.h>
#include <lttng/domain.h>
#include <common/dynamic-buffer.h>
#include <common/buffer-view.h>

/* For error.h */
int lttng_opt_quiet = 1;
int lttng_opt_verbose;
int lttng_opt_mi;

#define NUM_TESTS 32

void test_event_rule_tracepoint_ust(void)
{
	int ret;
	unsigned int count;
	struct lttng_event_rule *tracepoint = NULL;
	struct lttng_event_rule *tracepoint_from_buffer = NULL;
	enum lttng_event_rule_status status;
	enum lttng_domain_type domain_type;
	enum lttng_loglevel_type loglevel_type;
	const char *pattern="my_event_*";
	const char *filter="msg_id == 23 && size >= 2048";
	const char *tmp;
	const char *exclusions[] = {"my_event_test1", "my_event_test2" ,"my_event_test3"};
	struct lttng_dynamic_buffer buffer;
	struct lttng_buffer_view view;

	lttng_dynamic_buffer_init(&buffer);

	tracepoint = lttng_event_rule_tracepoint_create(LTTNG_DOMAIN_UST);
	ok(tracepoint, "tracepoint UST_DOMAIN");

	status = lttng_event_rule_tracepoint_get_domain_type(tracepoint, &domain_type);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "get tracepoint domain");
	ok(domain_type == LTTNG_DOMAIN_UST, "domain is UST");

	status = lttng_event_rule_tracepoint_set_pattern(tracepoint, pattern);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "setting pattern");
	status = lttng_event_rule_tracepoint_get_pattern(tracepoint, &tmp);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "getting pattern");
	ok(!strncmp(pattern, tmp, strlen(pattern)), "pattern is equal");

	status = lttng_event_rule_tracepoint_set_filter(tracepoint, filter);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "setting filter");
	status = lttng_event_rule_tracepoint_get_filter(tracepoint, &tmp);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "getting filter");
	ok(!strncmp(filter, tmp, strlen(filter)), "filter is equal");

	status = lttng_event_rule_tracepoint_set_loglevel_all(tracepoint);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "setting all loglevel");
	status = lttng_event_rule_tracepoint_get_loglevel_type(tracepoint, &loglevel_type);
	ok(loglevel_type == LTTNG_EVENT_LOGLEVEL_ALL, "getting loglevel type all");
	status = lttng_event_rule_tracepoint_get_loglevel(tracepoint, &ret);
	ok(status == LTTNG_EVENT_RULE_STATUS_UNSET, "get unset loglevel value");

	status = lttng_event_rule_tracepoint_set_loglevel(tracepoint, LTTNG_LOGLEVEL_INFO);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "setting single loglevel");
	status = lttng_event_rule_tracepoint_get_loglevel_type(tracepoint, &loglevel_type);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "get loglevel type");
	ok(loglevel_type == LTTNG_EVENT_LOGLEVEL_SINGLE, "getting loglevel type single");
	status = lttng_event_rule_tracepoint_get_loglevel(tracepoint, &ret);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "get loglevel value");
	ok(ret == LTTNG_LOGLEVEL_INFO, "loglevel value is equal");

	status = lttng_event_rule_tracepoint_set_loglevel_range(tracepoint, LTTNG_LOGLEVEL_WARNING);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "setting range loglevel");
	status = lttng_event_rule_tracepoint_get_loglevel_type(tracepoint, &loglevel_type);
	ok(loglevel_type == LTTNG_EVENT_LOGLEVEL_RANGE, "getting loglevel type range");
	status = lttng_event_rule_tracepoint_get_loglevel(tracepoint, &ret);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "get loglevel value");
	ok(ret == LTTNG_LOGLEVEL_WARNING, "loglevel valuei is equal");

	status = lttng_event_rule_tracepoint_set_exclusions(tracepoint, 3, exclusions);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "setting exclusions");

	status = lttng_event_rule_tracepoint_get_exclusions_count(tracepoint, &count);
	ok(status == LTTNG_EVENT_RULE_STATUS_OK, "getting exclusion count");
	ok(count == 3, "count is %d/3", count);

	for (int i = 0; i < count; i++) {
		status = lttng_event_rule_tracepoint_get_exclusion_at_index(tracepoint, i, &tmp);
		ok(status == LTTNG_EVENT_RULE_STATUS_OK, "getting exclusion at index %d", i);
		ok(!strncmp(exclusions[i], tmp, strlen(exclusions[i])), "%s == %s", tmp, exclusions[i]);
	}

	lttng_event_rule_serialize(tracepoint, &buffer);
	view = lttng_buffer_view_from_dynamic_buffer(&buffer, 0, -1);
	lttng_event_rule_create_from_buffer(&view, &tracepoint_from_buffer);

	ok(lttng_event_rule_is_equal(tracepoint, tracepoint_from_buffer), "serialized and from buffer are equal");




	lttng_dynamic_buffer_reset(&buffer);
	lttng_event_rule_destroy(tracepoint);
	lttng_event_rule_destroy(tracepoint_from_buffer);
}

void test_event_rule_tracepoint(void)
{
	struct lttng_event_rule *tracepoint = NULL;

	diag("Testing lttng_event_rule_tracepoint");
	tracepoint = lttng_event_rule_tracepoint_create(LTTNG_DOMAIN_NONE);
	ok(!tracepoint, "Domain type restriction on create");

	test_event_rule_tracepoint_ust();
}

int main(int argc, const char *argv[])
{
	plan_tests(NUM_TESTS);
	test_event_rule_tracepoint();
	return exit_status();
}
