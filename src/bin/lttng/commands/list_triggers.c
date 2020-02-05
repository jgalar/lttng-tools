#include <stdio.h>

#include "../command.h"

#include "common/argpar/argpar.h"
#include "common/mi-lttng.h"
#include "lttng/condition/condition-internal.h"
#include "lttng/condition/event-rule.h"
#include "lttng/domain-internal.h"
#include "lttng/event-rule/event-rule-internal.h"
#include "lttng/event-rule/event-rule-kprobe.h"
#include "lttng/event-rule/event-rule-kprobe-internal.h"
#include "lttng/event-rule/event-rule-syscall.h"
#include "lttng/event-rule/event-rule-tracepoint.h"
#include "lttng/event-rule/event-rule-uprobe.h"
#include "lttng/trigger/trigger-internal.h"

#ifdef LTTNG_EMBED_HELP
static const char help_msg[] =
#include <lttng-list-trigger.1.h>
;
#endif

enum {
	OPT_HELP,
	OPT_LIST_OPTIONS,
};

static const
struct argpar_opt_descr list_trigger_options[] = {
	{ OPT_HELP, 'h', "help", false },
	{ OPT_LIST_OPTIONS, '\0', "list-options", false },
	ARGPAR_OPT_DESCR_SENTINEL,
};

static
void print_event_rule_tracepoint(const struct lttng_event_rule *event_rule)
{
	enum lttng_event_rule_status event_rule_status;
	enum lttng_domain_type domain_type;
	const char *pattern;
	const char *filter;
	int loglevel;
	unsigned int exclusions_count;
	int i;

	event_rule_status = lttng_event_rule_tracepoint_get_pattern(
		event_rule, &pattern);
	assert(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK);

	event_rule_status = lttng_event_rule_tracepoint_get_domain_type(
		event_rule, &domain_type);
	assert(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK);

	printf("    rule: %s (type: tracepoint, domain: %s", pattern,
		lttng_domain_type_str(domain_type));

	event_rule_status = lttng_event_rule_tracepoint_get_filter(
		event_rule, &filter);
	if (event_rule_status == LTTNG_EVENT_RULE_STATUS_OK) {
		printf(", filter: %s", filter);
	} else {
		assert(event_rule_status == LTTNG_EVENT_RULE_STATUS_UNSET);
	}

	event_rule_status = lttng_event_rule_tracepoint_get_loglevel(
		event_rule, &loglevel);
	if (event_rule_status == LTTNG_EVENT_RULE_STATUS_OK) {
		enum lttng_loglevel_type loglevel_type;
		const char *loglevel_op;

		event_rule_status = lttng_event_rule_tracepoint_get_loglevel_type(
			event_rule, &loglevel_type);
		assert(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK);
		assert(loglevel_type == LTTNG_EVENT_LOGLEVEL_RANGE ||
			loglevel_type == LTTNG_EVENT_LOGLEVEL_SINGLE);

		loglevel_op = (loglevel_type == LTTNG_EVENT_LOGLEVEL_RANGE ? "<=" : "==");

		printf(", log level %s %s", loglevel_op,
			mi_lttng_loglevel_string(loglevel, domain_type));
	} else {
		assert(event_rule_status == LTTNG_EVENT_RULE_STATUS_UNSET);
	}

	event_rule_status = lttng_event_rule_tracepoint_get_exclusions_count(
		event_rule, &exclusions_count);
	assert(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK);
	if (exclusions_count > 0) {
		printf(", exclusions: ");
		for (i = 0; i < exclusions_count; i++) {
			const char *exclusion;

			event_rule_status = lttng_event_rule_tracepoint_get_exclusion_at_index(
				event_rule, i, &exclusion);
			assert(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK);

			printf("%s%s", i > 0 ? "," : "", exclusion);
		}
	}


	printf(")\n");
}

static
void print_event_rule_kprobe(const struct lttng_event_rule *event_rule)
{
	enum lttng_event_rule_status event_rule_status;
	const char *name, *symbol_name;
	uint64_t offset;

	event_rule_status = lttng_event_rule_kprobe_get_name(event_rule, &name);
	if (event_rule_status != LTTNG_EVENT_RULE_STATUS_OK) {
		fprintf(stderr, "Failed to get kprobe event rule's name.\n");
		goto end;
	}

	assert(lttng_event_rule_get_type(event_rule) == LTTNG_EVENT_RULE_TYPE_KPROBE);

	printf("    rule: %s (type: probe, location: ", name);

	// FIXME: When the location has been specified by address, this field
	// contains the address as a string.  The only downside is that we are
	// missing a `0x` prefix.
	symbol_name = lttng_event_rule_kprobe_get_symbol_name(event_rule);
	printf("%s", symbol_name);

	offset = lttng_event_rule_kprobe_get_offset(event_rule);
	if (offset > 0) {
		printf("+0x%" PRIx64, offset);
	}

	printf(")\n");

end:
	return;
}

static
void print_event_rule_uprobe(const struct lttng_event_rule *event_rule)
{
	enum lttng_event_rule_status event_rule_status;
	const char *name;
	const struct lttng_userspace_probe_location *location;
	enum lttng_userspace_probe_location_type userspace_probe_location_type;

	assert(lttng_event_rule_get_type(event_rule) == LTTNG_EVENT_RULE_TYPE_UPROBE);

	event_rule_status = lttng_event_rule_uprobe_get_name(event_rule, &name);
	if (event_rule_status != LTTNG_EVENT_RULE_STATUS_OK) {
		fprintf(stderr, "Failed to get uprobe event rule's name.\n");
		goto end;
	}

	event_rule_status = lttng_event_rule_uprobe_get_location(event_rule, &location);
	if (event_rule_status != LTTNG_EVENT_RULE_STATUS_OK) {
		fprintf(stderr, "Failed to get uprobe event rule's location.\n");
		goto end;
	}

	printf("    rule: %s (type: userspace probe, location: ", name);

	userspace_probe_location_type =
		lttng_userspace_probe_location_get_type(location);

	switch (userspace_probe_location_type) {
	case LTTNG_USERSPACE_PROBE_LOCATION_TYPE_FUNCTION:
	{
		const char *binary_path, *function_name;

		binary_path = lttng_userspace_probe_location_function_get_binary_path(location);
		function_name = lttng_userspace_probe_location_function_get_function_name(location);

		printf("%s:%s", binary_path, function_name);
		break;
	}

	case LTTNG_USERSPACE_PROBE_LOCATION_TYPE_TRACEPOINT:
		printf("SDT not implemented yet");
		break;

	default:
		abort();
	}

	printf(")\n");

end:
	return;
}

static
void print_event_rule_syscall(const struct lttng_event_rule *event_rule)
{
	const char *pattern, *filter;
	enum lttng_event_rule_status event_rule_status;

	assert(lttng_event_rule_get_type(event_rule) == LTTNG_EVENT_RULE_TYPE_SYSCALL);

	event_rule_status = lttng_event_rule_syscall_get_pattern(event_rule, &pattern);
	assert(event_rule_status == LTTNG_EVENT_RULE_STATUS_OK);

	printf("  - rule: %s (type: syscall", pattern);

	event_rule_status = lttng_event_rule_syscall_get_filter(
		event_rule, &filter);
	if (event_rule_status == LTTNG_EVENT_RULE_STATUS_OK) {
		printf(", filter: %s", filter);
	} else {
		assert(event_rule_status == LTTNG_EVENT_RULE_STATUS_UNSET);
	}

	printf(")\n");
}

static
void print_event_rule(const struct lttng_event_rule *event_rule)
{
	enum lttng_event_rule_type event_rule_type =
		lttng_event_rule_get_type(event_rule);

	switch (event_rule_type) {
	case LTTNG_EVENT_RULE_TYPE_TRACEPOINT:
		print_event_rule_tracepoint(event_rule);
		break;

	case LTTNG_EVENT_RULE_TYPE_KPROBE:
		print_event_rule_kprobe(event_rule);
		break;

	case LTTNG_EVENT_RULE_TYPE_UPROBE:
		print_event_rule_uprobe(event_rule);
		break;

	case LTTNG_EVENT_RULE_TYPE_SYSCALL:
		print_event_rule_syscall(event_rule);
		break;

	default:
		abort();
	}
}

static
void print_condition_event_rule_hit(const struct lttng_condition *condition)
{
	const struct lttng_event_rule *event_rule;
	enum lttng_condition_status condition_status;

	condition_status =
		lttng_condition_event_rule_get_rule(condition, &event_rule);
	assert(condition_status == LTTNG_CONDITION_STATUS_OK);

	print_event_rule(event_rule);
}

static
void print_one_action(const struct lttng_action *action)
{
	enum lttng_action_type action_type;
	enum lttng_action_status action_status;
	const char *value;

	action_type = lttng_action_get_type(action);
	assert(action_type != LTTNG_ACTION_TYPE_GROUP);

	switch (action_type) {
	case LTTNG_ACTION_TYPE_NOTIFY:
		printf("notify\n");
		break;

	case LTTNG_ACTION_TYPE_START_SESSION:
		action_status = lttng_action_start_session_get_session_name(
			action, &value);
		assert(action_status == LTTNG_ACTION_STATUS_OK);
		printf("start session `%s`\n", value);
		break;

	case LTTNG_ACTION_TYPE_STOP_SESSION:
		action_status = lttng_action_stop_session_get_session_name(
			action, &value);
		assert(action_status == LTTNG_ACTION_STATUS_OK);
		printf("stop session `%s`\n", value);
		break;

	case LTTNG_ACTION_TYPE_ROTATE_SESSION:
		action_status = lttng_action_rotate_session_get_session_name(
			action, &value);
		assert(action_status == LTTNG_ACTION_STATUS_OK);
		printf("rotate session `%s`\n", value);
		break;

	case LTTNG_ACTION_TYPE_SNAPSHOT_SESSION:
	{
		const struct lttng_snapshot_output *output;

		action_status = lttng_action_snapshot_session_get_session_name(
			action, &value);
		assert(action_status == LTTNG_ACTION_STATUS_OK);
		printf("snapshot session `%s`", value);

		action_status = lttng_action_snapshot_session_get_output_const(
			action, &output);
		if (action_status == LTTNG_ACTION_STATUS_OK) {
			const char *name;
			uint64_t max_size;
			const char *ctrl_url, *data_url;
			bool starts_with_file, starts_with_net, starts_with_net6;

			ctrl_url = lttng_snapshot_output_get_ctrl_url(output);
			assert(ctrl_url && strlen(ctrl_url) > 0);

			data_url = lttng_snapshot_output_get_data_url(output);
			assert(data_url);

			starts_with_file = strncmp(ctrl_url, "file://", strlen("file://")) == 0;
			starts_with_net = strncmp(ctrl_url, "net://", strlen("net://")) == 0;
			starts_with_net6 = strncmp(ctrl_url, "net6://", strlen("net6://")) == 0;

			if (ctrl_url[0] == '/' || starts_with_file) {
				if (starts_with_file) {
					ctrl_url += strlen("file://");
				}

				printf(", path: %s", ctrl_url);
			} else if (starts_with_net || starts_with_net6) {
				printf(", url: %s", ctrl_url);
			} else {
				assert(strlen(data_url) > 0);

				printf(", control url: %s, data url: %s", ctrl_url, data_url);
			}

			name = lttng_snapshot_output_get_name(output);
			assert(name);
			if (strlen(name) > 0) {
				printf(", name: %s", name);
			}

			max_size = lttng_snapshot_output_get_maxsize(output);
			if (max_size != -1ULL) {
				printf(", max size: %" PRIu64, max_size);
			}
		}

		printf("\n");
		break;
	}

	default:
		abort();
	}
}

static
void print_one_trigger(const struct lttng_trigger *trigger)
{
	const struct lttng_condition *condition;
	enum lttng_condition_type condition_type;
	const struct lttng_action *action;
	enum lttng_action_type action_type;
	enum lttng_trigger_status trigger_status;
	const char *name;

	trigger_status = lttng_trigger_get_name(trigger, &name);
	assert(trigger_status == LTTNG_TRIGGER_STATUS_OK);
	printf("- id: %s\n", name);

	condition = lttng_trigger_get_const_condition(trigger);
	condition_type = lttng_condition_get_type(condition);
	printf("  condition: %s\n",
		lttng_condition_type_str(condition_type));

	switch (condition_type) {
	case LTTNG_CONDITION_TYPE_EVENT_RULE_HIT:
		print_condition_event_rule_hit(condition);
		break;

	default:
		printf("  (condition type not handled in %s)\n", __func__);
		break;
	}

	action = lttng_trigger_get_const_action(trigger);
	action_type = lttng_action_get_type(action);
	if (action_type == LTTNG_ACTION_TYPE_GROUP) {
		enum lttng_action_status action_status;
		unsigned int count, i;

		printf("  actions:\n");

		action_status = lttng_action_group_get_count(action, &count);
		assert(action_status == LTTNG_ACTION_STATUS_OK);

		for (i = 0; i < count; i++) {
			const struct lttng_action *subaction =
				lttng_action_group_get_at_index_const(action, i);

			printf("    ");
			print_one_action(subaction);
		}
	} else {
		printf(" action:");
		print_one_action(action);
	}
}

static
int compare_triggers_by_name(const void *a, const void *b)
{
	const struct lttng_trigger *trigger_a = *((const struct lttng_trigger **) a);
	const struct lttng_trigger *trigger_b = *((const struct lttng_trigger **) b);
	const char *name_a, *name_b;
	enum lttng_trigger_status trigger_status;

	trigger_status = lttng_trigger_get_name(trigger_a, &name_a);
	assert(trigger_status == LTTNG_TRIGGER_STATUS_OK);

	trigger_status = lttng_trigger_get_name(trigger_b, &name_b);
	assert(trigger_status == LTTNG_TRIGGER_STATUS_OK);

	return strcmp(name_a, name_b);
}

int cmd_list_triggers(int argc, const char **argv)
{
	int ret;
	struct argpar_parse_ret argpar_parse_ret = { 0 };
	struct lttng_triggers *triggers = NULL;
	int i;
	const struct lttng_trigger **sorted_triggers = NULL;
	enum lttng_trigger_status trigger_status;
	unsigned int num_triggers;

	argpar_parse_ret = argpar_parse(argc - 1, argv + 1,
		list_trigger_options, true);
	if (!argpar_parse_ret.items) {
		fprintf(stderr, "Error: %s\n", argpar_parse_ret.error);
		goto error;
	}

	for (i = 0; i < argpar_parse_ret.items->n_items; i++) {
		struct argpar_item *item = argpar_parse_ret.items->items[i];

		if (item->type == ARGPAR_ITEM_TYPE_OPT) {
			struct argpar_item_opt *item_opt =
				(struct argpar_item_opt *) item;

			switch (item_opt->descr->id) {
			case OPT_HELP:
				SHOW_HELP();
				ret = 0;
				goto end;

			case OPT_LIST_OPTIONS:
				list_cmd_options_argpar(stdout,
					list_trigger_options);
				ret = 0;
				goto end;

			default:
				abort();
			}

		} else {
			struct argpar_item_non_opt *item_non_opt =
				(struct argpar_item_non_opt *) item;

			fprintf(stderr, "Unexpected argument: %s\n", item_non_opt->arg);
		}
	}

	ret = lttng_list_triggers(&triggers);
	if (ret != 0) {
		fprintf(stderr, "Error listing triggers.\n");
		goto error;
	}

	trigger_status = lttng_triggers_get_count(triggers, &num_triggers);
	if (trigger_status != LTTNG_TRIGGER_STATUS_OK) {
		fprintf(stderr, "Failed to get trigger count.\n");
		goto error;
	}

	sorted_triggers = calloc(num_triggers, sizeof(struct lttng_trigger *));
	if (!sorted_triggers) {
		fprintf(stderr, "Failed to allocate array of struct lttng_trigger *.\n");
		goto error;
	}

	for (i = 0; i < num_triggers; i++) {
		sorted_triggers[i] = lttng_triggers_get_at_index(triggers, i);
	}

	qsort(sorted_triggers, num_triggers, sizeof(struct lttng_trigger *),
		compare_triggers_by_name);

	for (i = 0; i < num_triggers; i++) {
		print_one_trigger(sorted_triggers[i]);
	}

	goto end;

error:
	ret = 1;

end:
	argpar_parse_ret_fini(&argpar_parse_ret);
	lttng_triggers_destroy(triggers);

	return ret;
}
