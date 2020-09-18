/*
 * notification.c
 *
 * Tests suite for LTTng notification API
 *
 * Copyright (C) 2017 Jonathan Rajotte <jonathan.rajotte-julien@efficios.com>
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>

#include <lttng/action/action.h>
#include <lttng/action/notify.h>
#include <lttng/condition/buffer-usage.h>
#include <lttng/condition/condition.h>
#include <lttng/condition/evaluation.h>
#include <lttng/condition/event-rule.h>
#include <lttng/domain.h>
#include <lttng/endpoint.h>
#include <lttng/event-rule/kprobe.h>
#include <lttng/event-rule/syscall.h>
#include <lttng/event-rule/tracepoint.h>
#include <lttng/event-rule/uprobe.h>
#include <lttng/kernel-probe.h>
#include <lttng/lttng-error.h>
#include <lttng/lttng.h>
#include <lttng/notification/channel.h>
#include <lttng/notification/notification.h>
#include <lttng/condition/evaluation.h>
#include <lttng/trigger/trigger.h>
#include <lttng/userspace-probe.h>

#include <tap/tap.h>

int nb_args = 0;
int named_pipe_args_start = 0;
pid_t app_pid = 0;
const char *app_state_file = NULL;

static const char *get_notification_trigger_name(
		struct lttng_notification *notification)
{
	const char *name = NULL;
	enum lttng_evaluation_status status;
	const struct lttng_evaluation *evaluation;
	evaluation = lttng_notification_get_evaluation(notification);
	if (evaluation == NULL) {
		fail("lttng_notification_get_evaluation");
		goto end;
	}

	switch (lttng_evaluation_get_type(evaluation)) {
	case LTTNG_CONDITION_TYPE_EVENT_RULE_HIT:
	{
		status = lttng_evaluation_event_rule_get_trigger_name(
				evaluation, &name);
		if (status != LTTNG_EVALUATION_STATUS_OK) {
			fail("lttng_evaluation_event_rule_get_trigger_name");
			name = NULL;
			goto end;
		}
		break;
	}
	default:
		fail("Wrong notification evaluation type \n");
		goto end;
	}
end:
	return name;
}

static int validator_notification_trigger_name(
		struct lttng_notification *notification,
		const char *trigger_name)
{
	int ret;
	bool name_is_equal;
	const char *name;

	assert(notification);
	assert(trigger_name);

	name = get_notification_trigger_name(notification);
	if (name == NULL) {
		ret = 1;
		goto end;
	}

	name_is_equal = (strcmp(trigger_name, name) == 0);
	ok(name_is_equal, "Expected trigger name: %s got %s", trigger_name,
			name);

	ret = !name_is_equal;

end:
	return ret;
}

static
void wait_on_file(const char *path, bool file_exist)
{
	if (!path) {
		return;
	}
	for (;;) {
		int ret;
		struct stat buf;

		ret = stat(path, &buf);
		if (ret == -1 && errno == ENOENT) {
			if (file_exist) {
				/*
				 * The file does not exist. wait a bit and
				 * continue looping until it does.
				 */
				(void) poll(NULL, 0, 10);
				continue;
			}

			/*
			 * File does not exist and the exit condition we want.
			 * Break from the loop and return.
			 */
			break;
		}
		if (ret) {
			perror("stat");
			exit(EXIT_FAILURE);
		}
		/*
		 * stat() returned 0, so the file exists. break now only if
		 * that's the exit condition we want.
		 */
		if (file_exist) {
			break;
		}
	}
}

static
int write_pipe(const char *path, uint8_t data)
{
	int ret = 0;
	int fd = 0;

	fd = open(path, O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		perror("Could not open consumer control named pipe");
		goto end;
	}

	ret = write(fd, &data , sizeof(data));
	if (ret < 1) {
		perror("Named pipe write failed");
		if (close(fd)) {
			perror("Named pipe close failed");
		}
		ret = -1;
		goto end;
	}

	ret = close(fd);
	if (ret < 0) {
		perror("Name pipe closing failed");
		ret = -1;
		goto end;
	}
end:
	return ret;
}

static
int stop_consumer(const char **argv)
{
	int ret = 0, i;

	for (i = named_pipe_args_start; i < nb_args; i++) {
		ret = write_pipe(argv[i], 49);
	}
	return ret;
}

static
int resume_consumer(const char **argv)
{
	int ret = 0, i;

	for (i = named_pipe_args_start; i < nb_args; i++) {
		ret = write_pipe(argv[i], 0);
	}
	return ret;
}

static
int suspend_application(void)
{
	int ret;
	struct stat buf;

	if (!stat(app_state_file, &buf)) {
		fail("App is already in a suspended state.");
		ret = -1;
		goto error;
	}

	/*
	 * Send SIGUSR1 to application instructing it to bypass tracepoint.
	 */
	assert(app_pid > 1);

	ret = kill(app_pid, SIGUSR1);
	if (ret) {
		fail("SIGUSR1 failed. errno %d", errno);
		ret = -1;
		goto error;
	}

	wait_on_file(app_state_file, true);

error:
	return ret;

}

static
int resume_application()
{
	int ret;
	struct stat buf;

	ret = stat(app_state_file, &buf);
	if (ret == -1 && errno == ENOENT) {
		fail("State file does not exist");
		goto error;
	}
	if (ret) {
		perror("stat");
		goto error;
	}

	assert(app_pid > 1);

	ret = kill(app_pid, SIGUSR1);
	if (ret) {
		fail("SIGUSR1 failed. errno %d", errno);
		ret = -1;
		goto error;
	}

	wait_on_file(app_state_file, false);

error:
	return ret;

}


static
void test_triggers_buffer_usage_condition(const char *session_name,
		const char *channel_name,
		enum lttng_domain_type domain_type,
		enum lttng_condition_type condition_type)
{
	unsigned int test_vector_size = 5, i;
	enum lttng_condition_status condition_status;
	struct lttng_action *action;

	/* Set-up */
	action = lttng_action_notify_create();
	if (!action) {
		fail("Setup error on action creation");
		goto end;
	}

	/* Test lttng_register_trigger with null value */
	ok(lttng_register_trigger(NULL) == -LTTNG_ERR_INVALID, "Registering a NULL trigger fails as expected");

	/* Test: register a trigger */

	for (i = 0; i < pow(2,test_vector_size); i++) {
		int loop_ret = 0;
		char *test_tuple_string = NULL;
		unsigned int mask_position = 0;
		bool session_name_set = false;
		bool channel_name_set = false;
		bool threshold_ratio_set = false;
		bool threshold_byte_set = false;
		bool domain_type_set = false;

		struct lttng_trigger *trigger = NULL;
		struct lttng_condition *condition = NULL;

		/* Create base condition */
		switch (condition_type) {
		case LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW:
			condition = lttng_condition_buffer_usage_low_create();
			break;
		case LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH:
			condition = lttng_condition_buffer_usage_high_create();
			break;
		default:
			loop_ret = 1;
			goto loop_end;
		}

		if (!condition) {
			loop_ret = 1;
			goto loop_end;

		}

		/* Prepare the condition for trigger registration test */

		/* Set session name */
		if ((1 << mask_position) & i) {
			condition_status = lttng_condition_buffer_usage_set_session_name(
					condition, session_name);
			if (condition_status != LTTNG_CONDITION_STATUS_OK) {
				loop_ret = 1;
				goto loop_end;
			}
			session_name_set = true;
		}
		mask_position++;

		/* Set channel name */
		if ((1 << mask_position) & i) {
			condition_status = lttng_condition_buffer_usage_set_channel_name(
					condition, channel_name);
			if (condition_status != LTTNG_CONDITION_STATUS_OK) {
				loop_ret = 1;
				goto loop_end;
			}
			channel_name_set = true;
		}
		mask_position++;

		/* Set threshold ratio */
		if ((1 << mask_position) & i) {
			condition_status = lttng_condition_buffer_usage_set_threshold_ratio(
					condition, 0.0);
			if (condition_status != LTTNG_CONDITION_STATUS_OK) {
				loop_ret = 1;
				goto loop_end;
			}
			threshold_ratio_set = true;
		}
		mask_position++;

		/* Set threshold byte */
		if ((1 << mask_position) & i) {
			condition_status = lttng_condition_buffer_usage_set_threshold(
					condition, 0);
			if (condition_status != LTTNG_CONDITION_STATUS_OK) {
				loop_ret = 1;
				goto loop_end;
			}
			threshold_byte_set = true;
		}
		mask_position++;

		/* Set domain type */
		if ((1 << mask_position) & i) {
			condition_status = lttng_condition_buffer_usage_set_domain_type(
					condition, LTTNG_DOMAIN_UST);
			if (condition_status != LTTNG_CONDITION_STATUS_OK) {
				loop_ret = 1;
				goto loop_end;
			}
			domain_type_set = true;
		}

		/* Safety check */
		if (mask_position != test_vector_size -1) {
			assert("Logic error for test vector generation");
		}

		loop_ret = asprintf(&test_tuple_string, "session name %s, channel name %s, threshold ratio %s, threshold byte %s, domain type %s",
				session_name_set ? "set" : "unset",
				channel_name_set ? "set" : "unset",
				threshold_ratio_set ? "set" : "unset",
				threshold_byte_set ? "set" : "unset",
				domain_type_set? "set" : "unset");
		if (!test_tuple_string || loop_ret < 0) {
			loop_ret = 1;
			goto loop_end;
		}

		/* Create trigger */
		trigger = lttng_trigger_create(condition, action);
		if (!trigger) {
			loop_ret = 1;
			goto loop_end;
		}

		loop_ret = lttng_register_trigger(trigger);

loop_end:
		if (loop_ret == 1) {
			fail("Setup error occurred for tuple: %s", test_tuple_string);
			goto loop_cleanup;
		}

		/* This combination happens three times */
		if (session_name_set && channel_name_set
				&& (threshold_ratio_set || threshold_byte_set)
				&& domain_type_set) {
			ok(loop_ret == 0, "Trigger is registered: %s", test_tuple_string);

			/*
			 * Test that a trigger cannot be registered
			 * multiple time.
			 */
			loop_ret = lttng_register_trigger(trigger);
			ok(loop_ret == -LTTNG_ERR_TRIGGER_EXISTS, "Re-register trigger fails as expected: %s", test_tuple_string);

			/* Test that a trigger can be unregistered */
			loop_ret = lttng_unregister_trigger(trigger);
			ok(loop_ret == 0, "Unregister trigger: %s", test_tuple_string);

			/*
			 * Test that unregistration of a non-previously
			 * registered trigger fail.
			 */
			loop_ret = lttng_unregister_trigger(trigger);
			ok(loop_ret == -LTTNG_ERR_TRIGGER_NOT_FOUND, "Unregister of a non-registered trigger fails as expected: %s", test_tuple_string);
		} else {
			ok(loop_ret == -LTTNG_ERR_INVALID_TRIGGER, "Trigger is invalid as expected and cannot be registered: %s", test_tuple_string);
		}

loop_cleanup:
		free(test_tuple_string);
		lttng_trigger_destroy(trigger);
		lttng_condition_destroy(condition);
	}

end:
	lttng_action_destroy(action);
}

static
void wait_data_pending(const char *session_name)
{
	int ret;

	do {
		ret = lttng_data_pending(session_name);
		assert(ret >= 0);
	} while (ret != 0);
}

static
int setup_buffer_usage_condition(struct lttng_condition *condition,
		const char *condition_name,
		const char *session_name,
		const char *channel_name,
		const enum lttng_domain_type domain_type)
{
	enum lttng_condition_status condition_status;
	int ret = 0;

	condition_status = lttng_condition_buffer_usage_set_session_name(
			condition, session_name);
	if (condition_status != LTTNG_CONDITION_STATUS_OK) {
		fail("Error setting session name on %s creation", condition_name);
		ret = -1;
		goto end;
	}

	condition_status = lttng_condition_buffer_usage_set_channel_name(
			condition, channel_name);
	if (condition_status != LTTNG_CONDITION_STATUS_OK) {
		fail("Error setting channel name on %s creation", condition_name);
		ret = -1;
		goto end;
	}

	condition_status = lttng_condition_buffer_usage_set_domain_type(
			condition, domain_type);
	if (condition_status != LTTNG_CONDITION_STATUS_OK) {
		fail("Error setting domain type on %s creation", condition_name);
		ret = -1;
		goto end;
	}

end:
	return ret;
}

static
void test_invalid_channel_subscription(
		const enum lttng_domain_type domain_type)
{
	enum lttng_condition_status condition_status;
	enum lttng_notification_channel_status nc_status;
	struct lttng_condition *dummy_condition = NULL;
	struct lttng_condition *dummy_invalid_condition = NULL;
	struct lttng_notification_channel *notification_channel = NULL;
	int ret = 0;

	notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	ok(notification_channel, "Notification channel object creation");
	if (!notification_channel) {
		goto end;
	}

	/*
	 * Create a dummy, empty (thus invalid) condition to test error paths.
	 */
	dummy_invalid_condition = lttng_condition_buffer_usage_low_create();
	if (!dummy_invalid_condition) {
		fail("Setup error on condition creation");
		goto end;
	}

	/*
	 * Test subscription and unsubscription of an invalid condition to/from
	 * a channel.
	 */
	nc_status = lttng_notification_channel_subscribe(
			notification_channel, dummy_invalid_condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID,
			"Subscribing to an invalid condition");

	nc_status = lttng_notification_channel_unsubscribe(
			notification_channel, dummy_invalid_condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID,
			"Unsubscribing from an invalid condition");

	/* Create a valid dummy condition with a ratio of 0.5 */
	dummy_condition = lttng_condition_buffer_usage_low_create();
	if (!dummy_condition) {
		fail("Setup error on dummy_condition creation");
		goto end;
	}

	condition_status = lttng_condition_buffer_usage_set_threshold_ratio(
			dummy_condition, 0.5);
	if (condition_status != LTTNG_CONDITION_STATUS_OK) {
		fail("Setup error on condition creation");
		goto end;
	}

	ret = setup_buffer_usage_condition(dummy_condition, "dummy_condition",
			"dummy_session", "dummy_channel", domain_type);
	if (ret) {
		fail("Setup error on dummy condition creation");
		goto end;
	}

	/*
	 * Test subscription and unsubscription to/from a channel with invalid
	 * parameters.
	 */
	nc_status = lttng_notification_channel_subscribe(NULL, NULL);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID,
			"Notification channel subscription is invalid: NULL, NULL");

	nc_status = lttng_notification_channel_subscribe(
			notification_channel, NULL);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID,
			"Notification channel subscription is invalid: NON-NULL, NULL");

	nc_status = lttng_notification_channel_subscribe(NULL, dummy_condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_INVALID,
			"Notification channel subscription is invalid: NULL, NON-NULL");

	nc_status = lttng_notification_channel_unsubscribe(
			notification_channel, dummy_condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_UNKNOWN_CONDITION,
			"Unsubscribing from a valid unknown condition");

end:
	lttng_notification_channel_destroy(notification_channel);
	lttng_condition_destroy(dummy_invalid_condition);
	lttng_condition_destroy(dummy_condition);
	return;
}

enum buffer_usage_type {
	BUFFER_USAGE_TYPE_LOW,
	BUFFER_USAGE_TYPE_HIGH,
};

static int register_buffer_usage_notify_trigger(const char *session_name,
		const char *channel_name,
		const enum lttng_domain_type domain_type,
		enum buffer_usage_type buffer_usage_type,
		double ratio,
		struct lttng_condition **condition,
		struct lttng_action **action,
		struct lttng_trigger **trigger)
{
	enum lttng_condition_status condition_status;
	struct lttng_action *tmp_action = NULL;
	struct lttng_condition *tmp_condition = NULL;
	struct lttng_trigger *tmp_trigger = NULL;
	int ret = 0;

	/* Set-up */
	tmp_action = lttng_action_notify_create();
	if (!action) {
		fail("Setup error on action creation");
		ret = -1;
		goto error;
	}

	if (buffer_usage_type == BUFFER_USAGE_TYPE_LOW) {
		tmp_condition = lttng_condition_buffer_usage_low_create();
	} else {
		tmp_condition = lttng_condition_buffer_usage_high_create();
	}

	if (!tmp_condition) {
		fail("Setup error on condition creation");
		ret = -1;
		goto error;
	}

	/* Set the buffer usage threashold */
	condition_status = lttng_condition_buffer_usage_set_threshold_ratio(
			tmp_condition, ratio);
	if (condition_status != LTTNG_CONDITION_STATUS_OK) {
		fail("Setup error on condition creation");
		ret = -1;
		goto error;
	}

	ret = setup_buffer_usage_condition(tmp_condition, "condition_name",
			session_name, channel_name, domain_type);
	if (ret) {
		fail("Setup error on condition creation");
		ret = -1;
		goto error;
	}

	/* Register the triggers for condition */
	tmp_trigger = lttng_trigger_create(tmp_condition, tmp_action);
	if (!tmp_trigger) {
		fail("Setup error on trigger creation");
		ret = -1;
		goto error;
	}

	ret = lttng_register_trigger(tmp_trigger);
	if (ret) {
		fail("Setup error on trigger registration");
		ret = -1;
		goto error;
	}

	*condition = tmp_condition;
	*trigger = tmp_trigger;
	*action = tmp_action;
	goto end;

error:
	lttng_action_destroy(tmp_action);
	lttng_condition_destroy(tmp_condition);
	lttng_trigger_destroy(tmp_trigger);

end:
	return ret;
}

static void test_subscription_twice(const char *session_name,
		const char *channel_name,
		const enum lttng_domain_type domain_type)
{
	int ret = 0;
	enum lttng_notification_channel_status nc_status;

	struct lttng_action *action = NULL;
	struct lttng_notification_channel *notification_channel = NULL;
	struct lttng_trigger *trigger = NULL;

	struct lttng_condition *condition = NULL;

	ret = register_buffer_usage_notify_trigger(session_name, channel_name,
			domain_type, BUFFER_USAGE_TYPE_LOW, 0.99, &condition,
			&action, &trigger);
	if (ret) {
		fail("Setup error on trigger registration");
		goto end;
	}

	/* Begin testing. */
	notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	ok(notification_channel, "Notification channel object creation");
	if (!notification_channel) {
		goto end;
	}

	/* Subscribe a valid condition. */
	nc_status = lttng_notification_channel_subscribe(
			notification_channel, condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe to condition");

	/* Subscribing again should fail. */
	nc_status = lttng_notification_channel_subscribe(
			notification_channel, condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_ALREADY_SUBSCRIBED,
			"Subscribe to a condition for which subscription was already done");

end:
	lttng_unregister_trigger(trigger);
	lttng_trigger_destroy(trigger);
	lttng_notification_channel_destroy(notification_channel);
	lttng_action_destroy(action);
	lttng_condition_destroy(condition);
}

static void test_buffer_usage_notification_channel(const char *session_name,
		const char *channel_name,
		const enum lttng_domain_type domain_type,
		const char **argv)
{
	int ret = 0;
	enum lttng_notification_channel_status nc_status;

	struct lttng_action *low_action = NULL;
	struct lttng_action *high_action = NULL;
	struct lttng_notification *notification = NULL;
	struct lttng_notification_channel *notification_channel = NULL;
	struct lttng_trigger *low_trigger = NULL;
	struct lttng_trigger *high_trigger = NULL;

	struct lttng_condition *low_condition = NULL;
	struct lttng_condition *high_condition = NULL;

	double low_ratio = 0.0;
	/* This is not 99 since we can end up in scenario where an event is
	 * bigger than 1% of the buffer and hence the buffer ratio will never
	 * trigger since the event will always be discarder by the tracer.
	 */

	double high_ratio = 0.90;

	ret = register_buffer_usage_notify_trigger(session_name, channel_name,
			domain_type, BUFFER_USAGE_TYPE_LOW, low_ratio,
			&low_condition, &low_action, &low_trigger);
	if (ret) {
		fail("Setup error on low trigger registration");
		goto end;
	}

	ret = register_buffer_usage_notify_trigger(session_name, channel_name,
			domain_type, BUFFER_USAGE_TYPE_HIGH, high_ratio,
			&high_condition, &high_action, &high_trigger);
	if (ret) {
		fail("Setup error on high trigger registration");
		goto end;
	}

	/* Begin testing */
	notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	ok(notification_channel, "Notification channel object creation");
	if (!notification_channel) {
		goto end;
	}

	/* Subscribe a valid low condition */
	nc_status = lttng_notification_channel_subscribe(
			notification_channel, low_condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe to low condition");

	/* Subscribe a valid high condition */
	nc_status = lttng_notification_channel_subscribe(
			notification_channel, high_condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe to high condition");

	resume_application();

	/* Wait for notification to happen */
	stop_consumer(argv);
	lttng_start_tracing(session_name);

	/* Wait for high notification */
	do {
		nc_status = lttng_notification_channel_get_next_notification(
				notification_channel, &notification);
	} while (nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_INTERRUPTED);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK && notification &&
					lttng_condition_get_type(lttng_notification_get_condition(
							notification)) ==
							LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH,
			"High notification received after intermediary communication");
	lttng_notification_destroy(notification);
	notification = NULL;

	suspend_application();
	lttng_stop_tracing_no_wait(session_name);
	resume_consumer(argv);
	wait_data_pending(session_name);

	/*
	 * Test that communication still work even if there is notification
	 * waiting for consumption.
	 */

	nc_status = lttng_notification_channel_unsubscribe(
			notification_channel, low_condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Unsubscribe with pending notification");

	nc_status = lttng_notification_channel_subscribe(
			notification_channel, low_condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe with pending notification");

	do {
		nc_status = lttng_notification_channel_get_next_notification(
				notification_channel, &notification);
	} while (nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_INTERRUPTED);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK && notification &&
					lttng_condition_get_type(lttng_notification_get_condition(
							notification)) ==
							LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW,
			"Low notification received after intermediary communication");
	lttng_notification_destroy(notification);
	notification = NULL;

	/* Stop consumer to force a high notification */
	stop_consumer(argv);
	resume_application();
	lttng_start_tracing(session_name);

	do {
		nc_status = lttng_notification_channel_get_next_notification(
				notification_channel, &notification);
	} while (nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_INTERRUPTED);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK && notification &&
					lttng_condition_get_type(lttng_notification_get_condition(
							notification)) ==
							LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH,
			"High notification received after intermediary communication");
	lttng_notification_destroy(notification);
	notification = NULL;

	suspend_application();
	lttng_stop_tracing_no_wait(session_name);
	resume_consumer(argv);
	wait_data_pending(session_name);

	do {
		nc_status = lttng_notification_channel_get_next_notification(
				notification_channel, &notification);
	} while (nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_INTERRUPTED);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK && notification &&
					lttng_condition_get_type(lttng_notification_get_condition(
							notification)) ==
							LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW,
			"Low notification received after re-subscription");
	lttng_notification_destroy(notification);
	notification = NULL;

	stop_consumer(argv);
	resume_application();
	/* Stop consumer to force a high notification */
	lttng_start_tracing(session_name);

	do {
		nc_status = lttng_notification_channel_get_next_notification(
				notification_channel, &notification);
	} while (nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_INTERRUPTED);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK && notification &&
					lttng_condition_get_type(lttng_notification_get_condition(
							notification)) ==
							LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH,
			"High notification");
	lttng_notification_destroy(notification);
	notification = NULL;

	suspend_application();

	/* Resume consumer to allow event consumption */
	lttng_stop_tracing_no_wait(session_name);
	resume_consumer(argv);
	wait_data_pending(session_name);

	nc_status = lttng_notification_channel_unsubscribe(
			notification_channel, low_condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Unsubscribe low condition with pending notification");

	nc_status = lttng_notification_channel_unsubscribe(
			notification_channel, high_condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Unsubscribe high condition with pending notification");

end:
	lttng_notification_channel_destroy(notification_channel);
	lttng_trigger_destroy(low_trigger);
	lttng_trigger_destroy(high_trigger);
	lttng_action_destroy(low_action);
	lttng_action_destroy(high_action);
	lttng_condition_destroy(low_condition);
	lttng_condition_destroy(high_condition);
}

static void create_tracepoint_event_rule_trigger(const char *event_pattern,
		const char *trigger_name,
		const char *filter,
		unsigned int exclusion_count,
		const char **exclusions,
		enum lttng_domain_type domain_type,
		struct lttng_condition **condition,
		struct lttng_action **action,
		struct lttng_trigger **trigger)
{
	enum lttng_event_rule_status event_rule_status;
	enum lttng_trigger_status trigger_status;

	struct lttng_action *tmp_action = NULL;
	struct lttng_event_rule *event_rule = NULL;
	struct lttng_condition *tmp_condition = NULL;
	struct lttng_trigger *tmp_trigger = NULL;
	int ret;

	assert(event_pattern);
	assert(trigger_name);
	assert(condition);
	assert(trigger);

	event_rule = lttng_event_rule_tracepoint_create(domain_type);
	ok(event_rule, "Tracepoint event rule object creation");

	event_rule_status = lttng_event_rule_tracepoint_set_pattern(
			event_rule, event_pattern);
	ok(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK,
			"Setting tracepoint event rule pattern: %s",
			event_pattern);

	if (filter) {
		event_rule_status = lttng_event_rule_tracepoint_set_filter(
				event_rule, filter);
		ok(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK,
				"Setting tracepoint event rule filter: %s",
				filter);
	}

	if (exclusions) {
		int i;
		bool success = true;
		assert(domain_type == LTTNG_DOMAIN_UST);
		assert(exclusion_count > 0);

		for (i = 0; i < exclusion_count; i++) {
			event_rule_status =
					lttng_event_rule_tracepoint_add_exclusion(
							event_rule,
							exclusions[i]);
			if (event_rule_status != LTTNG_EVENT_RULE_STATUS_OK) {
				fail("Setting tracepoint event rule exclusion \"%s\".",
						exclusions[i]);
				success = false;
			}
		}
		ok(success, "Setting tracepoint event rule exclusions");
	}

	tmp_condition = lttng_condition_event_rule_create(event_rule);
	ok(tmp_condition, "Condition event rule object creation");
	/* Ownership was passed to condition */
	event_rule = NULL;

	tmp_action = lttng_action_notify_create();
	ok(tmp_action, "Action event rule object creation");

	tmp_trigger = lttng_trigger_create(tmp_condition, tmp_action);
	ok(tmp_trigger, "Trigger object creation %s", trigger_name);

	trigger_status = lttng_trigger_set_name(tmp_trigger, trigger_name);
	ok(trigger_status == LTTNG_TRIGGER_STATUS_OK,
			"Setting name to trigger %s", trigger_name);

	ret = lttng_register_trigger(tmp_trigger);
	ok(ret == 0, "Trigger registration %s", trigger_name);

	*condition = tmp_condition;
	*action = tmp_action;
	*trigger = tmp_trigger;

	return;
}

static struct lttng_notification *get_next_notification(
		struct lttng_notification_channel *notification_channel)
{
	struct lttng_notification *local_notification = NULL;
	enum lttng_notification_channel_status status;

	/* Receive the next notification. */
	status = lttng_notification_channel_get_next_notification(
			notification_channel, &local_notification);

	switch (status) {
	case LTTNG_NOTIFICATION_CHANNEL_STATUS_OK:
		break;
	case LTTNG_NOTIFICATION_CHANNEL_STATUS_NOTIFICATIONS_DROPPED:
		fail("Notifications have been dropped");
		local_notification = NULL;
		break;
	default:
		/* Unhandled conditions / errors. */
		fail("error: Unknown notification channel status\n");
		local_notification = NULL;
		break;
	}

	return local_notification;
}

static void test_tracepoint_event_rule_notification(
		enum lttng_domain_type domain_type)
{
	int i;
	int ret;
	enum lttng_notification_channel_status nc_status;

	struct lttng_action *action = NULL;
	struct lttng_condition *condition = NULL;
	struct lttng_notification_channel *notification_channel = NULL;
	struct lttng_trigger *trigger = NULL;
	const char *trigger_name = "my_precious";
	const char *pattern;

	if (domain_type == LTTNG_DOMAIN_UST) {
		pattern = "tp:tptest";
	} else {
		pattern = "lttng_test_filter_event";
	}

	create_tracepoint_event_rule_trigger(pattern, trigger_name, NULL, 0,
			NULL, domain_type, &condition, &action, &trigger);

	notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	ok(notification_channel, "Notification channel object creation");

	nc_status = lttng_notification_channel_subscribe(
			notification_channel, condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe to tracepoint event rule condition");

	resume_application();

	/* Get 3 notifications */
	for (i = 0; i < 3; i++) {
		struct lttng_notification *notification = get_next_notification(
				notification_channel);
		ok(notification, "Received notification");

		/* Error */
		if (notification == NULL) {
			goto end;
		}

		ret = validator_notification_trigger_name(notification, trigger_name);
		lttng_notification_destroy(notification);
		if (ret) {
			goto end;
		}
	}

end:
	suspend_application();
	lttng_notification_channel_destroy(notification_channel);
	lttng_unregister_trigger(trigger);
	lttng_trigger_destroy(trigger);
	lttng_action_destroy(action);
	lttng_condition_destroy(condition);
	return;
}

static void test_tracepoint_event_rule_notification_filter(
		enum lttng_domain_type domain_type)
{
	int i;
	enum lttng_notification_channel_status nc_status;

	struct lttng_condition *ctrl_condition = NULL, *condition = NULL;
	struct lttng_action *ctrl_action = NULL, *action = NULL;
	struct lttng_notification_channel *notification_channel = NULL;
	struct lttng_trigger *ctrl_trigger = NULL, *trigger = NULL;
	const char *ctrl_trigger_name = "control_trigger";
	const char *trigger_name = "trigger";
	const char *pattern;
	int ctrl_count = 0, count = 0;

	if (domain_type == LTTNG_DOMAIN_UST) {
		pattern = "tp:tptest";
	} else {
		pattern = "lttng_test_filter_event";
	}

	notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	ok(notification_channel, "Notification channel object creation");

	create_tracepoint_event_rule_trigger(pattern, ctrl_trigger_name, NULL,
			0, NULL, domain_type, &ctrl_condition,
			&ctrl_action, &ctrl_trigger);

	nc_status = lttng_notification_channel_subscribe(
			notification_channel, ctrl_condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe to tracepoint event rule condition");

	/*
	 * Attach a filter expression to get notification only if the
	 * `intfield` is even.
	 */
	create_tracepoint_event_rule_trigger(pattern, trigger_name,
			"(intfield & 1) == 0", 0, NULL, domain_type,
			&condition, &action, &trigger);

	nc_status = lttng_notification_channel_subscribe(
			notification_channel, condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe to tracepoint event rule condition");

	/*
	 * We registered 2 notifications triggers, one with a filter and one
	 * without (control). The one with a filter will only fired when the
	 * `intfield` is a multiple of 2. We should get two times as many
	 * control notifications as filter notifications.
	 */
	resume_application();

	/*
	 * Get 3 notifications. We should get 1 for the regular trigger (with
	 * the filter) and 2 from the control trigger. This works whatever
	 * the order we receive the notifications.
	 */
	for (i = 0; i < 3; i++) {
		const char *name;
		struct lttng_notification *notification = get_next_notification(
				notification_channel);
		ok(notification, "Received notification");

		/* Error */
		if (notification == NULL) {
			goto end;
		}

		name = get_notification_trigger_name(notification);
		if (name == NULL) {
			lttng_notification_destroy(notification);
			goto end;
		}

		if (strcmp(ctrl_trigger_name, name) == 0) {
			ctrl_count++;
		} else if (strcmp(trigger_name, name) == 0) {
			count++;
		}
		lttng_notification_destroy(notification);
	}
	ok(ctrl_count / 2 == count,
			"Get twice as many control notif as of regular notif");

end:
	suspend_application();
	lttng_unregister_trigger(trigger);
	lttng_unregister_trigger(ctrl_trigger);
	lttng_notification_channel_destroy(notification_channel);
	lttng_trigger_destroy(trigger);
	lttng_trigger_destroy(ctrl_trigger);
	lttng_condition_destroy(condition);
	lttng_condition_destroy(ctrl_condition);
	lttng_action_destroy(action);
	lttng_action_destroy(ctrl_action);
	return;
}

static void test_tracepoint_event_rule_notification_exclusion(
		enum lttng_domain_type domain_type)
{
	enum lttng_notification_channel_status nc_status;
	struct lttng_condition *ctrl_condition = NULL, *condition = NULL;
	struct lttng_action *ctrl_action = NULL, *action = NULL;
	struct lttng_notification_channel *notification_channel = NULL;
	struct lttng_trigger *ctrl_trigger = NULL, *trigger = NULL;
	const char *ctrl_trigger_name = "control_exclusion_trigger";
	const char *trigger_name = "exclusion_trigger";
	const char *pattern = "tp:tptest*";
	const char *exclusions[4] = {
			"tp:tptest2", "tp:tptest3", "tp:tptest4", "tp:tptest5"};
	int ctrl_count = 0, count = 0;
	int i;

	notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	ok(notification_channel, "Notification channel object creation");

	create_tracepoint_event_rule_trigger(pattern, ctrl_trigger_name, NULL,
			0, NULL, domain_type, &ctrl_condition, &ctrl_action,
			&ctrl_trigger);

	nc_status = lttng_notification_channel_subscribe(
			notification_channel, ctrl_condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe to tracepoint event rule condition");

	create_tracepoint_event_rule_trigger(pattern, trigger_name, NULL, 4,
			exclusions, domain_type, &condition, &action, &trigger);

	nc_status = lttng_notification_channel_subscribe(
			notification_channel, condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe to tracepoint event rule condition");

	/*
	 * We registered 2 notifications triggers, one with an exclusion and
	 * one without (control).
	 * - The trigger with an exclusion will fire once every iteration.
	 * - The trigger without an exclusion will fire 5 times every
	 *   iteration.
	 *
	 *   We should get 5 times as many notifications from the control
	 *   trigger.
	 */
	resume_application();

	/*
	 * Get 6 notifications. We should get 1 for the regular trigger (with
	 * the exclusion) and 5 from the control trigger. This works whatever
	 * the order we receive the notifications.
	 */
	for (i = 0; i < 6; i++) {
		const char *name;
		struct lttng_notification *notification = get_next_notification(
				notification_channel);
		ok(notification, "Received notification");

		/* Error */
		if (notification == NULL) {
			goto end;
		}

		name = get_notification_trigger_name(notification);
		if (name == NULL) {
			lttng_notification_destroy(notification);
			goto end;
		}

		if (strcmp(ctrl_trigger_name, name) == 0) {
			ctrl_count++;
		} else if (strcmp(trigger_name, name) == 0) {
			count++;
		}
		lttng_notification_destroy(notification);
	}
	ok(ctrl_count / 5 == count,
			"Got 5 times as many control notif as of regular notif");

end:
	suspend_application();
	lttng_unregister_trigger(trigger);
	lttng_unregister_trigger(ctrl_trigger);
	lttng_notification_channel_destroy(notification_channel);
	lttng_trigger_destroy(trigger);
	lttng_trigger_destroy(ctrl_trigger);
	lttng_condition_destroy(condition);
	lttng_condition_destroy(ctrl_condition);
	lttng_action_destroy(action);
	lttng_action_destroy(ctrl_action);
	return;
}

static void test_kprobe_event_rule_notification(
		enum lttng_domain_type domain_type)
{
	enum lttng_notification_channel_status nc_status;
	enum lttng_event_rule_status event_rule_status;
	enum lttng_trigger_status trigger_status;

	struct lttng_notification_channel *notification_channel = NULL;
	struct lttng_condition *condition = NULL;
	struct lttng_kernel_probe_location *location = NULL;
	struct lttng_event_rule *event_rule = NULL;
	struct lttng_action *action = NULL;
	struct lttng_trigger *trigger = NULL;
	const char *trigger_name = "kprobe_trigger";
	const char *symbol_name = "_do_fork";
	int i, ret;

	action = lttng_action_notify_create();
	if (!action) {
		fail("Setup error on action creation");
		goto end;
	}

	location = lttng_kernel_probe_location_symbol_create(symbol_name, 0);
	if (!location) {
		fail("Could not create kernel probe location.");
		goto end;
	}

	notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	ok(notification_channel, "Notification channel object creation");

	event_rule = lttng_event_rule_kprobe_create();
	ok(event_rule, "kprobe event rule object creation");

	event_rule_status = lttng_event_rule_kprobe_set_location(
			event_rule, location);
	ok(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK,
			"Setting kprobe event rule location: %s", symbol_name);

	event_rule_status = lttng_event_rule_kprobe_set_name(
			event_rule, trigger_name);
	ok(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK,
			"Setting kprobe event rule name: %s", trigger_name);

	condition = lttng_condition_event_rule_create(event_rule);
	ok(condition, "Condition event rule object creation");

	/* Register the triggers for condition */
	trigger = lttng_trigger_create(condition, action);
	if (!trigger) {
		fail("Setup error on trigger creation");
		goto end;
	}

	trigger_status = lttng_trigger_set_name(trigger, trigger_name);
	ok(trigger_status == LTTNG_TRIGGER_STATUS_OK,
			"Setting name to trigger %s", trigger_name);

	ret = lttng_register_trigger(trigger);
	if (ret) {
		fail("Setup error on trigger registration");
		goto end;
	}

	nc_status = lttng_notification_channel_subscribe(
			notification_channel, condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe to tracepoint event rule condition");

	resume_application();

	for (i = 0; i < 3; i++) {
		struct lttng_notification *notification = get_next_notification(
				notification_channel);
		ok(notification, "Received notification");

		/* Error */
		if (notification == NULL) {
			goto end;
		}

		ret = validator_notification_trigger_name(notification, trigger_name);
		lttng_notification_destroy(notification);
		if (ret) {
			goto end;
		}
	}

end:
	suspend_application();
	lttng_notification_channel_destroy(notification_channel);
	lttng_unregister_trigger(trigger);
	lttng_trigger_destroy(trigger);
	lttng_action_destroy(action);
	lttng_condition_destroy(condition);
	lttng_kernel_probe_location_destroy(location);
	return;
}

static void test_uprobe_event_rule_notification(
		enum lttng_domain_type domain_type,
		const char *testapp_path,
		const char *test_symbol_name)
{
	enum lttng_notification_channel_status nc_status;
	enum lttng_event_rule_status event_rule_status;
	enum lttng_trigger_status trigger_status;

	struct lttng_notification_channel *notification_channel = NULL;
	struct lttng_userspace_probe_location *probe_location = NULL;
	struct lttng_userspace_probe_location_lookup_method *lookup_method =
			NULL;
	struct lttng_condition *condition = NULL;
	struct lttng_event_rule *event_rule = NULL;
	struct lttng_action *action = NULL;
	struct lttng_trigger *trigger = NULL;
	const char *trigger_name = "uprobe_trigger";
	int i, ret;

	action = lttng_action_notify_create();
	if (!action) {
		fail("Setup error on action creation");
		goto end;
	}

	lookup_method = lttng_userspace_probe_location_lookup_method_function_elf_create();
	if (!lookup_method) {
		fail("Setup error on userspace probe lookup method creation");
		goto end;
	}

	probe_location = lttng_userspace_probe_location_function_create(
			testapp_path, test_symbol_name, lookup_method);
	if (!probe_location) {
		fail("Setup error on userspace probe location creation");
		goto end;
	}

	notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	ok(notification_channel, "Notification channel object creation");

	event_rule = lttng_event_rule_uprobe_create();
	ok(event_rule, "kprobe event rule object creation");

	event_rule_status = lttng_event_rule_uprobe_set_location(
			event_rule, probe_location);
	ok(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK,
			"Setting uprobe event rule location");

	event_rule_status = lttng_event_rule_uprobe_set_name(
			event_rule, trigger_name);
	ok(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK,
			"Setting uprobe event rule name: %s", trigger_name);

	condition = lttng_condition_event_rule_create(event_rule);
	ok(condition, "Condition event rule object creation");

	/* Register the triggers for condition */
	trigger = lttng_trigger_create(condition, action);
	if (!trigger) {
		fail("Setup error on trigger creation");
		goto end;
	}

	trigger_status = lttng_trigger_set_name(trigger, trigger_name);
	ok(trigger_status == LTTNG_TRIGGER_STATUS_OK,
			"Setting name to trigger %s", trigger_name);

	ret = lttng_register_trigger(trigger);
	if (ret) {
		fail("Setup error on trigger registration");
		goto end;
	}

	nc_status = lttng_notification_channel_subscribe(
			notification_channel, condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe to tracepoint event rule condition");

	resume_application();

	for (i = 0; i < 3; i++) {
		struct lttng_notification *notification = get_next_notification(
				notification_channel);
		ok(notification, "Received notification");

		/* Error */
		if (notification == NULL) {
			goto end;
		}

		ret = validator_notification_trigger_name(notification, trigger_name);
		lttng_notification_destroy(notification);
		if (ret) {
			goto end;
		}
	}
end:
	suspend_application();
	lttng_notification_channel_destroy(notification_channel);
	lttng_unregister_trigger(trigger);
	lttng_trigger_destroy(trigger);
	lttng_action_destroy(action);
	lttng_condition_destroy(condition);
	return;
}

static void test_syscall_event_rule_notification(
		enum lttng_domain_type domain_type)
{
	enum lttng_notification_channel_status nc_status;
	enum lttng_event_rule_status event_rule_status;
	enum lttng_trigger_status trigger_status;

	struct lttng_notification_channel *notification_channel = NULL;
	struct lttng_condition *condition = NULL;
	struct lttng_event_rule *event_rule = NULL;
	struct lttng_action *action = NULL;
	struct lttng_trigger *trigger = NULL;
	const char *trigger_name = "syscall_trigger";
	const char *syscall_name = "openat";
	int i, ret;

	action = lttng_action_notify_create();
	if (!action) {
		fail("Setup error on action creation");
		goto end;
	}

	notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	ok(notification_channel, "Notification channel object creation");

	event_rule = lttng_event_rule_syscall_create();
	ok(event_rule, "syscall event rule object creation");

	event_rule_status = lttng_event_rule_syscall_set_pattern(
			event_rule, syscall_name);
	ok(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK,
			"Setting syscall event rule pattern: %s", syscall_name);

	condition = lttng_condition_event_rule_create(event_rule);
	ok(condition, "Condition event rule object creation");

	/* Register the triggers for condition */
	trigger = lttng_trigger_create(condition, action);
	if (!trigger) {
		fail("Setup error on trigger creation");
		goto end;
	}

	trigger_status = lttng_trigger_set_name(trigger, trigger_name);
	ok(trigger_status == LTTNG_TRIGGER_STATUS_OK,
			"Setting name to trigger %s", trigger_name);

	ret = lttng_register_trigger(trigger);
	if (ret) {
		fail("Setup error on trigger registration");
		goto end;
	}

	nc_status = lttng_notification_channel_subscribe(
			notification_channel, condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe to tracepoint event rule condition");

	resume_application();

	for (i = 0; i < 3; i++) {
		struct lttng_notification *notification = get_next_notification(
				notification_channel);
		ok(notification, "Received notification");

		/* Error */
		if (notification == NULL) {
			goto end;
		}

		ret = validator_notification_trigger_name(notification, trigger_name);
		lttng_notification_destroy(notification);
		if (ret) {
			goto end;
		}
	}
end:
	suspend_application();
	lttng_notification_channel_destroy(notification_channel);
	lttng_unregister_trigger(trigger);
	lttng_trigger_destroy(trigger);
	lttng_action_destroy(action);
	lttng_condition_destroy(condition);
	return;
}

static void test_syscall_event_rule_notification_filter(
		enum lttng_domain_type domain_type)
{
	enum lttng_notification_channel_status nc_status;
	enum lttng_event_rule_status event_rule_status;
	enum lttng_trigger_status trigger_status;

	struct lttng_notification_channel *notification_channel = NULL;
	struct lttng_condition *condition = NULL;
	struct lttng_event_rule *event_rule = NULL;
	struct lttng_action *action = NULL;
	struct lttng_trigger *trigger = NULL;
	const char *trigger_name = "syscall_trigger";
	const char *syscall_name = "openat";
	int i, ret;

	action = lttng_action_notify_create();
	if (!action) {
		fail("Setup error on action creation");
		goto end;
	}

	notification_channel = lttng_notification_channel_create(
			lttng_session_daemon_notification_endpoint);
	ok(notification_channel, "Notification channel object creation");

	event_rule = lttng_event_rule_syscall_create();
	ok(event_rule, "syscall event rule object creation");

	event_rule_status = lttng_event_rule_syscall_set_pattern(
			event_rule, syscall_name);
	ok(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK,
			"Setting syscall event rule pattern: %s", syscall_name);

	event_rule_status = lttng_event_rule_syscall_set_filter(
			event_rule, "filename==\"/proc/cpuinfo\"");
	ok(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK,
			"Setting syscall event rule pattern: %s", syscall_name);

	condition = lttng_condition_event_rule_create(event_rule);
	ok(condition, "Condition event rule object creation");

	/* Register the triggers for condition */
	trigger = lttng_trigger_create(condition, action);
	if (!trigger) {
		fail("Setup error on trigger creation");
		goto end;
	}

	trigger_status = lttng_trigger_set_name(trigger, trigger_name);
	ok(trigger_status == LTTNG_TRIGGER_STATUS_OK,
			"Setting name to trigger %s", trigger_name);

	ret = lttng_register_trigger(trigger);
	if (ret) {
		fail("Setup error on trigger registration");
		goto end;
	}

	nc_status = lttng_notification_channel_subscribe(
			notification_channel, condition);
	ok(nc_status == LTTNG_NOTIFICATION_CHANNEL_STATUS_OK,
			"Subscribe to tracepoint event rule condition");

	resume_application();

	for (i = 0; i < 3; i++) {
		struct lttng_notification *notification = get_next_notification(
				notification_channel);
		ok(notification, "Received notification");

		/* Error */
		if (notification == NULL) {
			goto end;
		}

		ret = validator_notification_trigger_name(notification, trigger_name);
		lttng_notification_destroy(notification);
		if (ret) {
			goto end;
		}
	}

end:
	suspend_application();
	lttng_unregister_trigger(trigger);
	lttng_notification_channel_destroy(notification_channel);
	lttng_trigger_destroy(trigger);
	lttng_condition_destroy(condition);
	return;
}

int main(int argc, const char *argv[])
{
	int test_scenario;
	const char *domain_type_string = NULL;
	enum lttng_domain_type domain_type = LTTNG_DOMAIN_NONE;

	if (argc < 5) {
		fail("Missing test scenario, domain type, pid, or application state file argument(s)");
		goto error;
	}

	test_scenario = atoi(argv[1]);
	domain_type_string = argv[2];
	app_pid = (pid_t) atoi(argv[3]);
	app_state_file = argv[4];

	if (!strcmp("LTTNG_DOMAIN_UST", domain_type_string)) {
		domain_type = LTTNG_DOMAIN_UST;
	}
	if (!strcmp("LTTNG_DOMAIN_KERNEL", domain_type_string)) {
		domain_type = LTTNG_DOMAIN_KERNEL;
	}
	if (domain_type == LTTNG_DOMAIN_NONE) {
		fail("Unknown domain type");
		goto error;
	}

	/*
	 * Test cases are responsible for resuming the app when needed
	 * and making sure it's suspended when returning.
	 */
	suspend_application();

	switch (test_scenario) {
	case 1:
	{
		plan_tests(44);

		/* Test cases that need gen-ust-event testapp. */
		diag("Test basic notification error paths for domain %s",
				domain_type_string);
		test_invalid_channel_subscription(domain_type);

		diag("Test tracepoint event rule notifications for domain %s",
				domain_type_string);
		test_tracepoint_event_rule_notification(domain_type);

		diag("Test tracepoint event rule notifications with filter for domain %s",
				domain_type_string);
		test_tracepoint_event_rule_notification_filter(domain_type);
		break;
	}
	case 2:
	{
		const char *session_name, *channel_name;
		/* Test cases that need a tracing session enabled. */
		plan_tests(99);

		/*
		 * Argument 7 and upward are named pipe location for consumerd
		 * control.
		 */
		named_pipe_args_start = 7;

		if (argc < 8) {
			fail("Missing parameter for tests to run %d", argc);
			goto error;
		}

		nb_args = argc;

		session_name = argv[5];
		channel_name = argv[6];

		test_subscription_twice(session_name, channel_name,
				domain_type);

		diag("Test trigger for domain %s with buffer_usage_low condition",
				domain_type_string);
		test_triggers_buffer_usage_condition(session_name, channel_name,
				domain_type,
				LTTNG_CONDITION_TYPE_BUFFER_USAGE_LOW);

		diag("Test trigger for domain %s with buffer_usage_high condition",
				domain_type_string);
		test_triggers_buffer_usage_condition(session_name, channel_name,
				domain_type,
				LTTNG_CONDITION_TYPE_BUFFER_USAGE_HIGH);

		diag("Test buffer usage notification channel api for domain %s",
				domain_type_string);
		test_buffer_usage_notification_channel(session_name, channel_name,
				domain_type, argv);
		break;
	}
	case 3:
	{
		/*
		 * Test cases that need a test app with more than one event
		 * type.
		 */
		plan_tests(25);

		/*
		 * At the moment, the only test case of this scenario is
		 * exclusion which is only supported by UST
		 */
		assert(domain_type == LTTNG_DOMAIN_UST);
		diag("Test tracepoint event rule notifications with exclusion for domain %s",
				domain_type_string);
		test_tracepoint_event_rule_notification_exclusion(domain_type);

		break;
	}
	case 4:
	{
		plan_tests(13);
		/* Test cases that need the kernel tracer. */
		assert(domain_type == LTTNG_DOMAIN_KERNEL);

		diag("Test kprobe event rule notifications for domain %s",
				domain_type_string);

		test_kprobe_event_rule_notification(domain_type);

		break;
	}
	case 5:
	{
		plan_tests(25);
		/* Test cases that need the kernel tracer. */
		assert(domain_type == LTTNG_DOMAIN_KERNEL);

		diag("Test syscall event rule notifications for domain %s",
				domain_type_string);

		test_syscall_event_rule_notification(domain_type);

		diag("Test syscall filtering event rule notifications for domain %s",
				domain_type_string);

		test_syscall_event_rule_notification_filter(domain_type);

		break;
	}
	case 6:
	{
		const char *testapp_path, *test_symbol_name;

		plan_tests(13);

		if (argc < 7) {
			fail("Missing parameter for tests to run %d", argc);
			goto error;
		}

		testapp_path = argv[5];
		test_symbol_name = argv[6];
		/* Test cases that need the kernel tracer. */
		assert(domain_type == LTTNG_DOMAIN_KERNEL);

		diag("Test userspace-probe event rule notifications for domain %s",
				domain_type_string);

		test_uprobe_event_rule_notification(
				domain_type, testapp_path, test_symbol_name);

		break;
	}
	default:
		abort();
	}

error:
	return exit_status();
}

