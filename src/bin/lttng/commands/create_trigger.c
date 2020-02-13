#include <stdio.h>

#include "../command.h"

#include "common/argpar/argpar.h"
#include "common/dynamic-array.h"
#include "common/string-utils/string-utils.h"
#include "common/utils.h"
#include <lttng/condition/event-rule.h>
#include <lttng/event-rule/event-rule-internal.h>
#include <lttng/event-rule/event-rule-tracepoint.h>

#ifdef LTTNG_EMBED_HELP
static const char help_msg[] =
#include <lttng-create-trigger.1.h>
;
#endif

enum {
	OPT_HELP,
	OPT_LIST_OPTIONS,

	OPT_CONDITION,
	OPT_ACTION,

	OPT_ALL,
	OPT_FILTER,
	OPT_EXCLUDE,

	OPT_USERSPACE,
	OPT_KERNEL,
	OPT_LOG4J,
	OPT_JUL,
	OPT_PYTHON,

	OPT_FUNCTION,
	OPT_PROBE,
	OPT_USERSPACE_PROBE,
	OPT_SYSCALL,
	OPT_TRACEPOINT,
};

static const struct argpar_opt_descr event_rule_opt_descrs[] = {
	{ OPT_ALL, 'a', "all", false },
	{ OPT_FILTER, 'f', "filter", true },
	{ OPT_EXCLUDE, 'x', "exclude", true },

	/* Domains */
	{ OPT_USERSPACE, 'u', "userspace", false },
	{ OPT_KERNEL, 'k', "kernel", false },
	{ OPT_LOG4J, 'l', "log4j", false },
	{ OPT_JUL, 'j', "jul", false },
	{ OPT_PYTHON, 'p', "python", false },

	/* Event rule types */
	{ OPT_FUNCTION, '\0', "function", true },
	{ OPT_PROBE, '\0', "probe", true },
	{ OPT_USERSPACE_PROBE, '\0', "userspace-probe", true },
	{ OPT_SYSCALL, '\0', "syscall" },
	{ OPT_TRACEPOINT, '\0', "tracepoint" },

	ARGPAR_OPT_DESCR_SENTINEL
};

static
bool assign_domain_type(enum lttng_domain_type *dest,
		enum lttng_domain_type src)
{
	bool ret;

	if (*dest == LTTNG_DOMAIN_NONE || *dest == src) {
		*dest = src;
		ret = true;
	} else {
		fprintf(stderr, "Error: Multiple domains specified.\n");
		ret = false;
	}

	return ret;
}

static
bool assign_event_rule_type(enum lttng_event_rule_type *dest,
		enum lttng_event_rule_type src)
{
	bool ret;

	if (*dest == LTTNG_EVENT_RULE_TYPE_UNKNOWN || *dest == src) {
		*dest = src;
		ret = true;
	} else {
		fprintf(stderr, "Error: Multiple event type not supported.\n");
		ret = false;
	}

	return ret;
}

static
bool assign_string(char **dest, const char *src, const char *opt_name)
{
	bool ret;

	if (*dest) {
		fprintf(stderr,
			"Duplicate %s given.\n", opt_name);
		goto error;
	}

	*dest = strdup(src);
	if (!*dest) {
		fprintf(stderr, "Failed to allocate %s string.\n", opt_name);
		goto error;
	}

	ret = true;
	goto end;

error:
	ret = false;

end:
	return ret;
}

/* This is defined in enable_events.c. */
LTTNG_HIDDEN
int create_exclusion_list_and_validate(const char *event_name,
		const char *exclusions_arg,
		char ***exclusion_list);

static
struct lttng_event_rule *parse_event_rule(int *argc, const char ***argv)
{
	struct lttng_event_rule *er = NULL;
	enum lttng_domain_type domain_type = LTTNG_DOMAIN_NONE;
	enum lttng_event_rule_type event_rule_type = LTTNG_EVENT_RULE_TYPE_UNKNOWN;
	struct argpar_state *state;
	struct argpar_item *item = NULL;
	char *error = NULL;
	int consumed_args = -1;

	/* Was the -a/--all flag provided? */
	bool all_events = false;

	/* Tracepoint name (non-option argument) */
	const char *tracepoint_name = NULL;

	/* Filter */
	char *filter = NULL;

	/* Exclude */
	char *exclude = NULL;
	char **exclusion_list = NULL;

	state = argpar_state_create(*argc, *argv, event_rule_opt_descrs);
	if (!state) {
		fprintf(stderr, "Failed to allocate an argpar state.\n");
		goto error;
	}

	while (true) {
		enum argpar_state_parse_next_status status;

		ARGPAR_ITEM_DESTROY_AND_RESET(item);
		status = argpar_state_parse_next(state, &item, &error);
		if (status == ARGPAR_STATE_PARSE_NEXT_STATUS_ERROR) {
			fprintf(stderr, "Internal argpar error: %s\n", error);
			goto error;
		} else if (status == ARGPAR_STATE_PARSE_NEXT_STATUS_ERROR_UNKNOWN_OPT) {
			/* Just stop parsing here. */
			break;
		} else if (status == ARGPAR_STATE_PARSE_NEXT_STATUS_END) {
			break;
		}

		assert(status == ARGPAR_STATE_PARSE_NEXT_STATUS_OK);

		if (item->type == ARGPAR_ITEM_TYPE_OPT) {
			struct argpar_item_opt *item_opt =
				(struct argpar_item_opt *) item;

			switch (item_opt->descr->id) {
			/* Domains */
			case OPT_USERSPACE:
				if (!assign_domain_type(&domain_type, LTTNG_DOMAIN_UST)) {
					goto error;
				}
				break;

			case OPT_KERNEL:
				if (!assign_domain_type(&domain_type, LTTNG_DOMAIN_KERNEL)) {
					goto error;
				}
				break;

			case OPT_LOG4J:
				if (!assign_domain_type(&domain_type, LTTNG_DOMAIN_LOG4J)) {
					goto error;
				}
				break;

			case OPT_JUL:
				if (!assign_domain_type(&domain_type, LTTNG_DOMAIN_JUL)) {
					goto error;
				}
				break;

			case OPT_PYTHON:
				if (!assign_domain_type(&domain_type, LTTNG_DOMAIN_PYTHON)) {
					goto error;
				}
				break;

			/* Event rule types */
			case OPT_FUNCTION:
				if (!assign_event_rule_type(&event_rule_type,
						LTTNG_EVENT_RULE_TYPE_KRETPROBE)) {
					goto error;
				}
				break;

			case OPT_PROBE:
				if (!assign_event_rule_type(&event_rule_type,
						LTTNG_EVENT_RULE_TYPE_KPROBE)) {
					goto error;
				}
				break;

			case OPT_USERSPACE_PROBE:
				if (!assign_event_rule_type(&event_rule_type,
						LTTNG_EVENT_RULE_TYPE_UPROBE)) {
					goto error;
				}
				break;

			case OPT_SYSCALL:
				if (!assign_event_rule_type(&event_rule_type,
						LTTNG_EVENT_RULE_TYPE_SYSCALL)) {
					goto error;
				}
				break;

			case OPT_TRACEPOINT:
				if (!assign_event_rule_type(&event_rule_type,
						LTTNG_EVENT_RULE_TYPE_TRACEPOINT)) {
					goto error;
				}
				break;

			case OPT_ALL:
				all_events = true;
				break;

			case OPT_FILTER:
				if (!assign_string(&filter, item_opt->arg, "--filter/-f")) {
					goto error;
				}
				break;

			case OPT_EXCLUDE:
				if (!assign_string(&exclude, item_opt->arg, "--exclude/-x")) {
					goto error;
				}
				break;

			default:
				abort();
			}
		} else {
			struct argpar_item_non_opt *item_non_opt =
				(struct argpar_item_non_opt *) item;

			/*
			 * Don't accept two non-option arguments/tracepoint
			 * names.
			 */
			if (tracepoint_name) {
				fprintf(stderr,
					"Error: Unexpected argument: %s\n",
					item_non_opt->arg);
				goto error;
			}

			tracepoint_name = item_non_opt->arg;
		}
	}

	/* Need to provide exactly one of an event name and -a. */
	if (tracepoint_name && all_events) {
		fprintf(stderr, "Error: Can't provide a tracepoint name with -a/--all.\n");
		goto error;
	}

	if (!tracepoint_name) {
		if (!all_events) {
			fprintf(stderr, "Error: Need to provide either a tracepoint name or -a/--all.\n");
			goto error;
		}

		tracepoint_name = "*";
	}

	/*
	 * Update *argc and *argv so our caller can keep parsing what follows.
	 */
	consumed_args = argpar_state_get_ingested_orig_args(state);
	assert(consumed_args >= 0);
	*argc -= consumed_args;
	*argv += consumed_args;

	/* Need to specify a domain. */
	if (domain_type == LTTNG_DOMAIN_NONE) {
		fprintf(stderr, "Error: Please specify a domain (-k/-u/-j).\n");
		goto error;
	}

	if (event_rule_type == LTTNG_EVENT_RULE_TYPE_UNKNOWN) {
		event_rule_type = LTTNG_EVENT_RULE_TYPE_TRACEPOINT;
	}

	/* Validate event rule type against domain. */
	switch (event_rule_type) {
	case LTTNG_EVENT_RULE_TYPE_KPROBE:
	case LTTNG_EVENT_RULE_TYPE_KRETPROBE:
	case LTTNG_EVENT_RULE_TYPE_UPROBE:
	case LTTNG_EVENT_RULE_TYPE_SYSCALL:
		if (domain_type != LTTNG_DOMAIN_KERNEL) {
			fprintf(stderr, "Error: Event type not available for user-space tracing\n");
			goto error;
		}
		break;
	case LTTNG_EVENT_RULE_TYPE_TRACEPOINT:
		break;
	default:
		abort();
	}

	/*
	 * Adding a filter to a probe, function or userspace-probe would be
	 * denied by the kernel tracer as it's not supported at the moment. We
	 * do an early check here to warn the user.
	 */
	if (filter && domain_type == LTTNG_DOMAIN_KERNEL) {
		switch (event_rule_type) {
		case LTTNG_EVENT_RULE_TYPE_TRACEPOINT:
		case LTTNG_EVENT_RULE_TYPE_SYSCALL:
			break;
		default:
			ERR("Filter expressions are not supported for %s events",
					lttng_event_rule_type_str(event_rule_type));
			goto error;
		}
	}

	/* If --exclude/-x was passed, split it into an exclusion list. */
	if (exclude) {
		if (domain_type != LTTNG_DOMAIN_UST) {
			fprintf(stderr, "Event name exclusions are not yet implemented for %s events",
						get_domain_str(domain_type));
			goto error;
		}


		if (create_exclusion_list_and_validate(tracepoint_name, exclude,
				&exclusion_list) != 0) {
			fprintf(stderr, "Failed to create exclusion list.\n");
			goto error;
		}
	}

	if (event_rule_type == LTTNG_EVENT_RULE_TYPE_TRACEPOINT) {
		enum lttng_event_rule_status event_rule_status;

		er = lttng_event_rule_tracepoint_create(domain_type);
		if (!er) {
			goto error;
		}

		/* Set pattern. */
		event_rule_status =
			lttng_event_rule_tracepoint_set_pattern(er, tracepoint_name);
		if (event_rule_status != LTTNG_EVENT_RULE_STATUS_OK) {
			fprintf(stderr, "Failed to set tracepoint pattern.\n");
			goto error;
		}

		/* Set filter. */
		if (filter) {
			event_rule_status =
				lttng_event_rule_tracepoint_set_filter(
					er, filter);
			if (event_rule_status != LTTNG_EVENT_RULE_STATUS_OK) {
				fprintf(stderr, "Failed to set tracepoint filter expression.\n");
				goto error;
			}
		}

		/* Set exclusion list. */
		if (exclusion_list) {
			int n;

			/* Count number of items in exclusion list. */
			for (n = 0; exclusion_list[n]; n++);

			event_rule_status =
				lttng_event_rule_tracepoint_set_exclusions(er,
					n, (const char **) exclusion_list);
			if (event_rule_status != LTTNG_EVENT_RULE_STATUS_OK) {
				fprintf(stderr, "Failed to set tracepoint exclusion list.\n");
				goto error;
			}
		}
	} else {
		fprintf(stderr, "parse_event_rule: I only support tracepoints at the moment.\n");
		goto error;
	}

	goto end;
error:
	lttng_event_rule_destroy(er);
	er = NULL;

end:
	argpar_item_destroy(item);
	free(error);
	argpar_state_destroy(state);
	free(filter);
	free(exclude);
	strutils_free_null_terminated_array_of_strings(exclusion_list);
	return er;
}

static
struct lttng_condition *handle_condition_event(int *argc, const char ***argv)
{
	struct lttng_event_rule *er;
	struct lttng_condition *c;

	er = parse_event_rule(argc, argv);
	if (!er) {
		c = NULL;
		goto end;
	}

	c = lttng_condition_event_rule_create(er);
	if (!c) {
		goto end;
	}

end:
	return c;
}

static
struct lttng_condition *handle_condition_session_consumed_size(int *argc, const char ***argv)
{
	struct lttng_condition *cond = NULL;
	struct argpar_state *state = NULL;
	struct argpar_item *item = NULL;
	const char *threshold_arg = NULL;
	const char *session_name_arg = NULL;
	uint64_t threshold;
	char *error = NULL;
	enum lttng_condition_status condition_status;

	state = argpar_state_create(*argc, *argv, event_rule_opt_descrs);
	if (!state) {
		fprintf(stderr, "Failed to allocate an argpar state.\n");
		goto error;
	}

	while (true) {
		enum argpar_state_parse_next_status status;

		ARGPAR_ITEM_DESTROY_AND_RESET(item);
		status = argpar_state_parse_next(state, &item, &error);
		if (status == ARGPAR_STATE_PARSE_NEXT_STATUS_ERROR) {
			fprintf(stderr, "Internal argpar error: %s\n", error);
			goto error;
		} else if (status == ARGPAR_STATE_PARSE_NEXT_STATUS_ERROR_UNKNOWN_OPT) {
			/* Just stop parsing here. */
			break;
		} else if (status == ARGPAR_STATE_PARSE_NEXT_STATUS_END) {
			break;
		}

		assert(status == ARGPAR_STATE_PARSE_NEXT_STATUS_OK);

		if (item->type == ARGPAR_ITEM_TYPE_OPT) {
			struct argpar_item_opt *item_opt =
				(struct argpar_item_opt *) item;

			switch (item_opt->descr->id) {
			default:
				abort();
			}
		} else {
			struct argpar_item_non_opt *item_non_opt;

			assert(item->type == ARGPAR_ITEM_TYPE_NON_OPT);

			item_non_opt = (struct argpar_item_non_opt *) item;

			switch (item_non_opt->non_opt_index) {
			case 0:
				session_name_arg = item_non_opt->arg;
				break;
			case 1:
				threshold_arg = item_non_opt->arg;
				break;
			default:
				fprintf(stderr, "Unexpected argument `%s`.\n",
					item_non_opt->arg);
				goto error;
			}
		}
	}

	*argc -= argpar_state_get_ingested_orig_args(state);
	*argv += argpar_state_get_ingested_orig_args(state);

	if (!session_name_arg) {
		fprintf(stderr, "Missing session name argument.\n");
		goto error;
	}

	if (!threshold_arg) {
		fprintf(stderr, "Missing threshold argument.\n");
		goto error;
	}

	if (utils_parse_size_suffix(threshold_arg, &threshold) != 0) {
		fprintf(stderr, "Failed to parse `%s` as a size.\n", threshold_arg);
		goto error;
	}

	cond = lttng_condition_session_consumed_size_create();
	if (!cond) {
		fprintf(stderr, "Failed to allocate a session consumed size condition.\n");
		goto error;
	}

	condition_status = lttng_condition_session_consumed_size_set_session_name(
		cond, session_name_arg);
	if (condition_status != LTTNG_CONDITION_STATUS_OK) {
		fprintf(stderr, "Failed to set session consumed size condition session name.\n");
		goto error;
	}


	condition_status = lttng_condition_session_consumed_size_set_threshold(
		cond, threshold);
	if (condition_status != LTTNG_CONDITION_STATUS_OK) {
		fprintf(stderr, "Failed to set session consumed size condition threshold.\n");
		goto error;
	}

	goto end;

error:
	lttng_condition_destroy(cond);
	cond = NULL;

end:
	argpar_state_destroy(state);
	argpar_item_destroy(item);
	free(error);
	return cond;
}

static
struct lttng_condition *handle_condition_buffer_usage_high(int *argc, const char ***argv)
{
	struct lttng_condition *cond = NULL;
	struct argpar_state *state = NULL;
	struct argpar_item *item = NULL;
	const char *threshold_arg = NULL;
	const char *session_name_arg = NULL;
	uint64_t threshold;
	char *error = NULL;
	enum lttng_condition_status condition_status;

	state = argpar_state_create(*argc, *argv, event_rule_opt_descrs);
	if (!state) {
		fprintf(stderr, "Failed to allocate an argpar state.\n");
		goto error;
	}

	while (true) {
		enum argpar_state_parse_next_status status;

		ARGPAR_ITEM_DESTROY_AND_RESET(item);
		status = argpar_state_parse_next(state, &item, &error);
		if (status == ARGPAR_STATE_PARSE_NEXT_STATUS_ERROR) {
			fprintf(stderr, "Internal argpar error: %s\n", error);
			goto error;
		} else if (status == ARGPAR_STATE_PARSE_NEXT_STATUS_ERROR_UNKNOWN_OPT) {
			/* Just stop parsing here. */
			break;
		} else if (status == ARGPAR_STATE_PARSE_NEXT_STATUS_END) {
			break;
		}

		assert(status == ARGPAR_STATE_PARSE_NEXT_STATUS_OK);

		if (item->type == ARGPAR_ITEM_TYPE_OPT) {
			struct argpar_item_opt *item_opt =
				(struct argpar_item_opt *) item;

			switch (item_opt->descr->id) {
			default:
				abort();
			}
		} else {
			struct argpar_item_non_opt *item_non_opt;

			assert(item->type == ARGPAR_ITEM_TYPE_NON_OPT);

			item_non_opt = (struct argpar_item_non_opt *) item;

			switch (item_non_opt->non_opt_index) {
			case 0:
				session_name_arg = item_non_opt->arg;
				break;
			case 1:
				threshold_arg = item_non_opt->arg;
				break;
			default:
				fprintf(stderr, "Unexpected argument `%s`.\n",
					item_non_opt->arg);
				goto error;
			}
		}
	}

	*argc -= argpar_state_get_ingested_orig_args(state);
	*argv += argpar_state_get_ingested_orig_args(state);

	if (!session_name_arg) {
		fprintf(stderr, "Missing session name argument.\n");
		goto error;
	}

	if (!threshold_arg) {
		fprintf(stderr, "Missing threshold argument.\n");
		goto error;
	}

	if (utils_parse_size_suffix(threshold_arg, &threshold) != 0) {
		fprintf(stderr, "Failed to parse `%s` as a size.\n", threshold_arg);
		goto error;
	}

	cond = lttng_condition_session_consumed_size_create();
	if (!cond) {
		fprintf(stderr, "Failed to allocate a session consumed size condition.\n");
		goto error;
	}

	condition_status = lttng_condition_session_consumed_size_set_session_name(
		cond, session_name_arg);
	if (condition_status != LTTNG_CONDITION_STATUS_OK) {
		fprintf(stderr, "Failed to set session consumed size condition session name.\n");
		goto error;
	}

	condition_status = lttng_condition_session_consumed_size_set_threshold(
		cond, threshold);
	if (condition_status != LTTNG_CONDITION_STATUS_OK) {
		fprintf(stderr, "Failed to set session consumed size condition threshold.\n");
		goto error;
	}

	goto end;

error:
	lttng_condition_destroy(cond);
	cond = NULL;

end:
	argpar_state_destroy(state);
	argpar_item_destroy(item);
	free(error);
	return cond;
}

static
struct lttng_condition *handle_condition_buffer_usage_low(int *argc, const char ***argv)
{
	return NULL;
}

static
struct lttng_condition *handle_condition_session_rotation_ongoing(int *argc, const char ***argv)
{
	return NULL;
}

static
struct lttng_condition *handle_condition_session_rotation_completed(int *argc, const char ***argv)
{
	return NULL;
}

struct condition_descr {
	const char *name;
	struct lttng_condition *(*handler) (int *argc, const char ***argv);
};

static const
struct condition_descr condition_descrs[] = {
	{ "on-event", handle_condition_event },
	{ "on-session-consumed-size", handle_condition_session_consumed_size },
	{ "on-buffer-usage-high", handle_condition_buffer_usage_high },
	{ "on-buffer-usage-low", handle_condition_buffer_usage_low },
	{ "on-session-rotation-ongoing", handle_condition_session_rotation_ongoing },
	{ "on-session-rotation-completed", handle_condition_session_rotation_completed },
};

static
struct lttng_condition *parse_condition(int *argc, const char ***argv)
{
	int i;
	struct lttng_condition *cond;
	const char *condition_name;
	const struct condition_descr *descr = NULL;

	if (*argc == 0) {
		fprintf(stderr, "Missing condition name.\n");
		goto error;
	}

	condition_name = (*argv)[0];

	(*argc)--;
	(*argv)++;

	for (i = 0; i < ARRAY_SIZE(condition_descrs); i++) {
		if (strcmp(condition_name, condition_descrs[i].name) == 0) {
			descr = &condition_descrs[i];
			break;
		}
	}

	if (!descr) {
		fprintf(stderr, "Unknown condition name: %s\n", condition_name);
		goto error;
	}

	cond = descr->handler(argc, argv);
	if (!cond) {
		/* The handler has already printed an error message. */
		goto error;
	}

	goto end;
error:
	cond = NULL;
end:
	return cond;
}


static
struct lttng_action *handle_action_notify(int *argc, const char ***argv)
{
	return lttng_action_notify_create();
}

struct action_descr {
	const char *name;
	struct lttng_action *(*handler) (int *argc, const char ***argv);
};

static const
struct action_descr action_descrs[] = {
	{ "notify", handle_action_notify },
};

static
struct lttng_action *parse_action(int *argc, const char ***argv)
{
	int i;
	struct lttng_action *action;
	const char *action_name;
	const struct action_descr *descr = NULL;

	if (*argc == 0) {
		fprintf(stderr, "Missing action name.\n");
		goto error;
	}

	action_name = (*argv)[0];

	(*argc)--;
	(*argv)++;

	for (i = 0; i < ARRAY_SIZE(action_descrs); i++) {
		if (strcmp(action_name, action_descrs[i].name) == 0) {
			descr = &action_descrs[i];
			break;
		}
	}

	if (!descr) {
		fprintf(stderr, "Unknown action name: %s\n", action_name);
		goto error;
	}

	action = descr->handler(argc, argv);
	if (!action) {
		/* The handler has already printed an error message. */
		goto error;
	}

	goto end;
error:
	action = NULL;
end:
	return action;
}

static const
struct argpar_opt_descr create_trigger_options[] = {
	{ OPT_HELP, 'h', "help", false },
	{ OPT_LIST_OPTIONS, '\0', "list-options", false },
	{ OPT_CONDITION, '\0', "condition", false },
	{ OPT_ACTION, '\0', "action", false },
	ARGPAR_OPT_DESCR_SENTINEL,
};

static
void lttng_actions_destructor(void *p)
{
	struct lttng_action *action = p;

	lttng_action_destroy(action);
}

int cmd_create_trigger(int argc, const char **argv)
{
	int ret;
	int my_argc = argc - 1;
	const char **my_argv = argv + 1;
	struct lttng_condition *condition = NULL;
	struct lttng_dynamic_pointer_array actions;
	struct argpar_state *argpar_state = NULL;
	struct argpar_item *argpar_item = NULL;
	struct lttng_action *action_group = NULL;
	struct lttng_action *action = NULL;
	struct lttng_trigger *trigger = NULL;
	char *error = NULL;
	int i;

	lttng_dynamic_pointer_array_init(&actions, lttng_actions_destructor);

	while (true) {
		enum argpar_state_parse_next_status status;
		struct argpar_item_opt *item_opt;
		int ingested_args;

		argpar_state_destroy(argpar_state);
		argpar_state = argpar_state_create(my_argc, my_argv,
			create_trigger_options);
		if (!argpar_state) {
			fprintf(stderr, "Failed to create argpar state.\n");
			goto error;
		}

		status = argpar_state_parse_next(argpar_state, &argpar_item, &error);

		if (status == ARGPAR_STATE_PARSE_NEXT_STATUS_ERROR) {
			fprintf(stderr, "Internal argpar error: %s\n", error);
			goto error;
		} else if (status == ARGPAR_STATE_PARSE_NEXT_STATUS_ERROR_UNKNOWN_OPT) {
			fprintf(stderr, "%s\n", error);
			goto error;
		} else if (status == ARGPAR_STATE_PARSE_NEXT_STATUS_END) {
			break;
		}

		assert(status == ARGPAR_STATE_PARSE_NEXT_STATUS_OK);

		if (argpar_item->type == ARGPAR_ITEM_TYPE_NON_OPT) {
			struct argpar_item_non_opt *item_non_opt =
				(struct argpar_item_non_opt *) argpar_item;

			fprintf(stderr, "Unexpected argument `%s`.\n",
				item_non_opt->arg);
			goto error;
		}

		item_opt = (struct argpar_item_opt *) argpar_item;

		ingested_args = argpar_state_get_ingested_orig_args(
			argpar_state);

		my_argc -= ingested_args;
		my_argv += ingested_args;

		switch (item_opt->descr->id) {
		case OPT_HELP:
			SHOW_HELP();
			ret = 0;
			goto end;

		case OPT_LIST_OPTIONS:
			list_cmd_options_argpar(stdout, create_trigger_options);
			ret = 0;
			goto end;

		case OPT_CONDITION:
		{
			if (condition) {
				fprintf(stderr, "A --condition was already given.\n");
				goto error;
			}

			condition = parse_condition(&my_argc, &my_argv);
			if (!condition) {
				/*
				 * An error message was already printed by
				 * parse_condition.
				 */
				goto error;
			}

			break;
		}
		case OPT_ACTION:
		{
			action = parse_action(&my_argc, &my_argv);
			if (!action) {
				/*
				 * An error message was already printed by
				 * parse_condition.
				 */
				goto error;
			}

			ret = lttng_dynamic_pointer_array_add_pointer(
				&actions, action);
			if (ret) {
				fprintf(stderr, "Failed to add pointer to pointer array.\n");
				goto error;
			}

			/* Ownership of the action was transferred to the group. */
			action = NULL;

			break;
		}
		default:
			abort();
		}
	}

	if (!condition) {
		fprintf(stderr, "Missing --condition.\n");
		goto error;
	}

	if (lttng_dynamic_pointer_array_get_count(&actions) == 0) {
		fprintf(stderr, "Need at least one --action.\n");
		goto error;
	}

	action_group = lttng_action_group_create();
	if (!action_group) {
		goto error;
	}

	for (i = 0; i < lttng_dynamic_pointer_array_get_count(&actions); i++) {
		enum lttng_action_status status;

		action = lttng_dynamic_pointer_array_steal_pointer(&actions, i);

		status = lttng_action_group_add_action(
			action_group, action);
		if (status != LTTNG_ACTION_STATUS_OK) {
			goto error;
		}

		/* Ownership of the action was transferred to the group. */
		action = NULL;
	}


	trigger = lttng_trigger_create(condition, action_group);
	if (!trigger) {
		goto error;
	}
	/*
	 * Ownership of the condition and action group was transferred to the
	 * trigger.
	 */
	condition = NULL;
	action_group = NULL;

	ret = lttng_register_trigger(trigger);
	if (ret) {
		fprintf(stderr, "Failed to register trigger.\n");
		goto error;
	}

	printf("Trigger registered successfully.\n");

	goto end;

error:
	ret = 1;

end:
	argpar_state_destroy(argpar_state);
	lttng_dynamic_pointer_array_reset(&actions);
	lttng_condition_destroy(condition);
	lttng_action_destroy(action_group);
	lttng_trigger_destroy(trigger);
	// TODO: check what else to free

	return ret;
}
