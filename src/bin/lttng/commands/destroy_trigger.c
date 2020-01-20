#include <stdio.h>

#include "../command.h"

#include "common/argpar/argpar.h"

#ifdef LTTNG_EMBED_HELP
static const char help_msg[] =
#include <lttng-destroy-trigger.1.h>
;
#endif

enum {
	OPT_HELP,
	OPT_LIST_OPTIONS,
};

static const
struct argpar_opt_descr destroy_trigger_options[] = {
	{ OPT_HELP, 'h', "help", false },
	{ OPT_LIST_OPTIONS, '\0', "list-options", false },
	ARGPAR_OPT_DESCR_SENTINEL,
};

int cmd_destroy_trigger(int argc, const char **argv)
{
	int ret;
	struct argpar_parse_ret argpar_parse_ret = { 0 };
	const char *id = NULL;
	int i;
	struct lttng_triggers *triggers = NULL;
	unsigned int triggers_count;
	enum lttng_trigger_status trigger_status;
	const struct lttng_trigger *trigger_to_destroy = NULL;

	argpar_parse_ret = argpar_parse(argc - 1, argv + 1,
		destroy_trigger_options, true);
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
					destroy_trigger_options);
				ret = 0;
				goto end;

			default:
				abort();
			}

		} else {
			struct argpar_item_non_opt *item_non_opt =
				(struct argpar_item_non_opt *) item;

			if (id) {
				fprintf(stderr, "Unexpected argument: %s\n", item_non_opt->arg);
				goto error;
			}

			id = item_non_opt->arg;
		}
	}

	if (!id) {
		fprintf(stderr, "Missing `id` argument.\n");
		goto error;
	}

	ret = lttng_list_triggers(&triggers);
	if (ret != 0) {
		fprintf(stderr, "Failed to get the list of triggers.\n");
		goto error;
	}

	trigger_status = lttng_triggers_get_count(triggers, &triggers_count);
	assert(trigger_status == LTTNG_TRIGGER_STATUS_OK);

	for (i = 0; i < triggers_count; i++) {
		const struct lttng_trigger *trigger;
		const char *trigger_name;

		trigger = lttng_triggers_get_at_index(triggers, i);
		trigger_status = lttng_trigger_get_name(trigger, &trigger_name);
		assert(trigger_status == LTTNG_TRIGGER_STATUS_OK);

		if (strcmp(trigger_name, id) == 0) {
			trigger_to_destroy = trigger;
			break;
		}
	}

	if (!trigger_to_destroy) {
		fprintf(stderr, "Couldn't find trigger with id `%s`.\n", id);
		goto error;
	}

	ret = lttng_unregister_trigger(trigger_to_destroy);
	if (ret != 0) {
		fprintf(stderr, "Failed to unregister trigger `%s`.\n", id);
		goto error;
	}

	printf("Destroyed trigger `%s`.\n", id);

	ret = 0;
	goto end;

error:
	ret = 1;

end:
	argpar_parse_ret_fini(&argpar_parse_ret);
	lttng_triggers_destroy(triggers);

	return ret;
}
