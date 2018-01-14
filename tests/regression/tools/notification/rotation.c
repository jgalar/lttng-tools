/*
 * rotation.c
 *
 * Tests suite for LTTng notification API (rotation notifications)
 *
 * Copyright (C) 2017 Jérémie Galarneau <jeremie.galarneau@efficios.com>
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

#include <stdio.h>
#include <assert.h>
#include <tap/tap.h>
#include <lttng/rotate.h>
#include <lttng/notification/channel.h>
#include <lttng/endpoint.h>

#define TEST_COUNT 12

struct session {
	const char *name;
	struct lttng_rotate_session_attr *attr;
};

int setup_rotation_trigger(struct session *session,
		struct lttng_notification_channel *notification_channel)
{
	int ret;

	
end:
	return ret;
}

int main(int argc, const char *argv[])
{
	int ret = 0;
	struct session session;
	struct lttng_notification_channel *notification_channel = NULL;

	if (argc != 3) {
		puts("Usage: rotation SESSION_NAME SESSION_OUTPUT_PATH");
		ret = 1;
		goto error;
	}

	session.name = argv[1];

	ret = plan_tests(TEST_COUNT);
	if (ret) {
		goto error;
	}

	session.attr = lttng_rotate_session_attr_create();
	ok(session.attr, "Create rotate session attr");
	if (!session.attr) {
		ret = -1;
		goto error;
	}

	ret = lttng_rotate_session_attr_set_session_name(session.attr,
			session.name);
	ok(ret == 0, "Set rotate session name attribute to %s", session.name);

	notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	if (!notification_channel) {
		diag("Failed to create notification channel");
		ret = -1;
		goto error;
	}

	ret = setup_rotation_trigger(&session, notification_channel);
	if (ret) {
		goto error;
	}
error:
	lttng_rotate_session_attr_destroy(session.attr);
	lttng_notification_channel_destroy(notification_channel);
	return ret;
}

