/*
 * base_client.c
 *
 * Base client application for testing of LTTng trigger API
 *
 * Copyright 2019 Jonathan Rajotte <jonathan.rajotte-julien@efficios.com>
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
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

#include <lttng/action/action.h>
#include <lttng/action/start-session.h>
#include <lttng/action/notify.h>
#include <lttng/condition/condition.h>
#include <lttng/condition/event-rule.h>
#include <lttng/event-rule/event-rule-tracepoint.h>
#include <lttng/domain.h>
#include <lttng/trigger/trigger.h>
#include <lttng/lttng-error.h>
#include <lttng/endpoint.h>
#include <lttng/notification/channel.h>
#include <lttng/notification/notification.h>

const char *session_name = NULL;
enum lttng_domain_type domain_type = LTTNG_DOMAIN_NONE;
const char *pattern = NULL;

int parse_arguments(char **argv) {
	const char *domain_type_string = NULL;

	session_name = argv[1];
	domain_type_string = argv[2];
	pattern = argv[3];

	/* Parse arguments */
	/* Domain type */
	if (!strcasecmp("LTTNG_DOMAIN_UST", domain_type_string)) {
		domain_type = LTTNG_DOMAIN_UST;
	}
	if (!strcasecmp("LTTNG_DOMAIN_KERNEL", domain_type_string)) {
		domain_type = LTTNG_DOMAIN_KERNEL;
	}
	if (!strcasecmp("LTTNG_DOMAIN_JUL", domain_type_string)) {
		domain_type = LTTNG_DOMAIN_JUL;
	}
	if (!strcasecmp("LTTNG_DOMAIN_PYTHON", domain_type_string)) {
		domain_type = LTTNG_DOMAIN_PYTHON;
	}
	if (!strcasecmp("LTTNG_DOMAIN_LOG4J", domain_type_string)) {
		domain_type = LTTNG_DOMAIN_LOG4J;
	}
	if (domain_type == LTTNG_DOMAIN_NONE) {
		printf("error: Unknown domain type\n");
		goto error;
	}

	return 0;
error:
	return 1;
}

int main(int argc, char **argv)
{
	int ret = 0;
	struct lttng_condition *condition = NULL;

	enum lttng_event_rule_status event_rule_status;
	struct lttng_event_rule *event_rule = NULL;

	enum lttng_action_status action_status;
	struct lttng_action *action = NULL;

	enum lttng_notification_channel_status nc_status;
	struct lttng_notification_channel *notification_channel = NULL;

	const char* exclusions[] = { "sample_component:message2"};

	struct lttng_trigger *trigger = NULL;

	if (argc < 4) {
		printf("error: Missing arguments for tests\n");
		ret = 1;
		goto end;
	}

	ret = parse_arguments(argv);
	if (ret) {
		printf("error: Could not parse arguments\n");
		goto end;
	}

	event_rule = lttng_event_rule_tracepoint_create(domain_type);
	if (!event_rule) {
		printf("error: Could not create condition object\n");
		ret = 1;
		goto end;
	}

	event_rule_status = lttng_event_rule_tracepoint_set_pattern(event_rule, pattern);
	if (event_rule_status != LTTNG_EVENT_RULE_STATUS_OK) {
		printf("error: Could not set pattern\n");
		ret = 1;
		goto end;
	}

	event_rule_status = lttng_event_rule_tracepoint_set_filter(event_rule, "message=='Hello World'");
	if (event_rule_status != LTTNG_EVENT_RULE_STATUS_OK) {
		printf("error: Could not set pattern\n");
		ret = 1;
		goto end;
	}

	event_rule_status = lttng_event_rule_tracepoint_set_exclusions(event_rule, 1, exclusions);
	if (event_rule_status != LTTNG_EVENT_RULE_STATUS_OK) {
		printf("error: Could not set exclusions\n");
		ret = 1;
		goto end;
	}

	condition = lttng_condition_event_rule_create(event_rule);
	if (!condition) {
		printf("error: Could not create condition\n");
		ret = 1;
		goto end;
	}
	/* Ownership was passed to condition */
	event_rule = NULL;

	action = lttng_action_notify_create();
	if (!action) {
		printf("error: Could not create action notify\n");
		ret = 1;
		goto end;
	}

	trigger = lttng_trigger_create(condition, action);
	if (!trigger) {
		printf("error: Could not create trigger\n");
		ret = 1;
		goto end;
	}

	ret = lttng_register_trigger(trigger);

	notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	if (!notification_channel) {
		printf("error: Could not create notification channel\n");
		ret = 1;
		goto end;
	}
	/*
	 * An equivalent trigger might already be registered if an other app
	 * registered an equivalent trigger.
	 */
	if (ret < 0 && ret != -LTTNG_ERR_TRIGGER_EXISTS) {
		printf("error: %s\n", lttng_strerror(ret));
		ret = 1;
		goto end;
	}

	nc_status = lttng_notification_channel_subscribe(notification_channel, condition);
	if (nc_status != LTTNG_NOTIFICATION_CHANNEL_STATUS_OK) {
		printf("error: Could not subscribe\n");
		ret = 1;
		goto end;
	}

	for (;;) {
		struct lttng_notification *notification;
		enum lttng_notification_channel_status status;
		const struct lttng_evaluation *notification_evaluation;
		const struct lttng_condition *notification_condition;
		const char *name;

		/* Receive the next notification. */
		status = lttng_notification_channel_get_next_notification(
				notification_channel,
				&notification);

		switch (status) {
		case LTTNG_NOTIFICATION_CHANNEL_STATUS_OK:
			break;
		case LTTNG_NOTIFICATION_CHANNEL_STATUS_NOTIFICATIONS_DROPPED:
			ret = 1;
			printf("error: No drop should be observed during this test app\n");
			goto end;
		case LTTNG_NOTIFICATION_CHANNEL_STATUS_CLOSED:
			/*
			 * The notification channel has been closed by the
			 * session daemon. This is typically caused by a session
			 * daemon shutting down (cleanly or because of a crash).
			 */
			printf("error: Notification channel was closed\n");
			ret = 1;
			goto end;
		default:
			/* Unhandled conditions / errors. */
			printf("error: Unknown notification channel status\n");
			ret = 1;
			goto end;
		}

		notification_condition = lttng_notification_get_condition(notification);
		notification_evaluation = lttng_notification_get_evaluation(notification);
		switch (lttng_evaluation_get_type(notification_evaluation)) {
		case LTTNG_CONDITION_TYPE_EVENT_RULE_HIT:
			lttng_evaluation_event_rule_get_trigger_name(notification_evaluation, &name);
			printf("Received nootification from trigger \"%s\"\n", name);
			break;
		default:
			printf("error: Wrong notification evaluation type \n");
			break;

		}

		lttng_notification_destroy(notification);
	}
end:
	if (trigger) {
		lttng_unregister_trigger(trigger);
	}
	lttng_event_rule_destroy(event_rule);
	lttng_trigger_destroy(trigger);
	lttng_condition_destroy(condition);
	lttng_action_destroy(action);
	printf("exit: %d\n", ret);
	return ret;
}
