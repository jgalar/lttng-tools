#include <stdio.h>

#include "../command.h"

#include "common/argpar/argpar.h"

enum {
	OPT_HELP,
};

static
struct lttng_condition *handle_condition_session_consumed_size(void)
{
	return NULL;
}

static
struct lttng_condition *handle_condition_buffer_usage_high(void)
{
	return NULL;
}

static
struct lttng_condition *handle_condition_buffer_usage_low(void)
{
	return NULL;
}

static
struct lttng_condition *handle_condition_session_rotation_ongoing(void)
{
	return NULL;
}

static
struct lttng_condition *handle_condition_session_rotation_completed(void)
{
	return NULL;
}

struct condition_descr {
	const char *name;
	struct lttng_condition *(*handler) (void);
};

static const
struct condition_descr condition_descrs[] = {
	{ "on-session-consumed-size", handle_condition_session_consumed_size },
	{ "on-buffer-usage-high", handle_condition_buffer_usage_high },
	{ "on-buffer-usage-low", handle_condition_buffer_usage_low },
	{ "on-session-rotation-ongoing", handle_condition_session_rotation_ongoing },
	{ "on-session-rotation-completed", handle_condition_session_rotation_completed },
};

static
struct lttng_condition *create_condition(const char *condition_name,
		int *argc, const char ***argv)
{
	int i;
	struct lttng_condition *cond;

	for (i = 0; i < ARRAY_SIZE(condition_descrs); i++) {
		const struct condition_descr *descr = &condition_descrs[i];
		if (strcmp(condition_name, descr->name) == 0) {
			cond = descr->handler();
			goto end;
		}
	}

	fprintf(stderr, "Unknown condition name: %s\n", condition_name);
	cond = NULL;

end:
	return cond;
}


static
struct lttng_action *handle_action_notify(void)
{
	return NULL;
}

struct action_descr {
	const char *name;
	struct lttng_action *(*handler) (void);
};

static const
struct action_descr action_descrs[] = {
	{ "notify", handle_action_notify },
};

static
struct lttng_action *create_action(const char *action_name, int *argc, const char ***argv)
{
	int i;
	struct lttng_action *action;

	for (i = 0; i < ARRAY_SIZE(action_descrs); i++) {
		const struct action_descr *descr = &action_descrs[i];
		if (strcmp(action_name, descr->name) == 0) {
			action = descr->handler();
			goto end;
		}
	}

	fprintf(stderr, "Unknown action name: %s\n", action_name);
	action = NULL;

end:
	return action;
}

static const
struct argpar_opt_descr options[] = {
	{ OPT_HELP, 'h', "help", false },
	ARGPAR_OPT_DESCR_SENTINEL,
};

int cmd_create_trigger(int argc, const char **argv)
{
	struct argpar_parse_ret parse_ret = { 0 };
	unsigned int i;
	int ret;
	int my_argc = argc - 1;
	const char **my_argv = argv + 1;
	const char *condition_name;
	int num_actions = 0;
	struct lttng_condition *condition;
	struct lttng_action *action;

	parse_ret = argpar_parse(my_argc, my_argv, options, false);
	if (!parse_ret.items) {
		fprintf(stderr, "%s", parse_ret.error);
		ret = 1;
		goto end;
	}

	/*
	 * Only process the top-level options, stop when reach the end or see a
	 * non-option.
	 */
	for (i = 0; i < parse_ret.items->n_items; i++) {
		struct argpar_item *item = parse_ret.items->items[i];


		if (item->type == ARGPAR_ITEM_TYPE_NON_OPT) {
			struct argpar_item_non_opt *item_non_opt =
				(struct argpar_item_non_opt *) item;

			my_argc -= item_non_opt->orig_index;
			my_argv += item_non_opt->orig_index;

			break;
		} else {
			struct argpar_item_opt *item_opt =
				(struct argpar_item_opt *) item;

			switch (item_opt->descr->id) {
			case OPT_HELP:
				printf("Help!\n");
				ret = 0;
				goto end;
			default:
				/*
				 * argpar isn't supposed to return something not in the
				 * option description list.
				 */
				abort();
			}
		}
	}

	if (my_argc == 0) {
		fprintf(stderr, "Yo madame I need a condition name.\n");
		ret = 1;
		goto end;
	}

	condition_name = my_argv[0];
	my_argc--;
	my_argv++;

	condition = create_condition(condition_name, &my_argc, &my_argv);
	if (!condition) {
		goto end;
	}

	while (my_argc > 0) {
		const char *action_name = my_argv[0];
		my_argc--;
		my_argv++;
		action = create_action(action_name, &my_argc, &my_argv);
		if (!action) {
			goto end;
		}

		num_actions++;
	}

	if (num_actions == 0) {
		fprintf(stderr, "Yo madame I need at least one action.\n");
		ret = 1;
		goto end;
	}

end:
	argpar_parse_ret_fini(&parse_ret);

	return ret;
}
