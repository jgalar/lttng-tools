/*
 * Copyright (C) 2011 David Goulet <david.goulet@polymtl.ca>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#define _LGPL_SOURCE
#include <assert.h>
#include <popt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <ctype.h>

#include <common/sessiond-comm/sessiond-comm.h>
#include <common/compat/string.h>
#include <common/compat/getenv.h>
#include <common/string-utils/string-utils.h>
#include <common/utils.h>

#include <lttng/constant.h>
/* Mi dependancy */
#include <common/mi-lttng.h>

#include <lttng/event-internal.h>

#include "../command.h"

#if (LTTNG_SYMBOL_NAME_LEN == 256)
#define LTTNG_SYMBOL_NAME_LEN_SCANF_IS_A_BROKEN_API	"255"
#endif

static char *opt_event_list;
static int opt_event_type;
static const char *opt_loglevel;
static int opt_loglevel_type;
static int opt_kernel;
static char *opt_session_name;
static int opt_userspace;
static int opt_jul;
static int opt_log4j;
static int opt_python;
static int opt_enable_all;
static char *opt_probe;
static char *opt_userspace_probe;
static char *opt_function;
static char *opt_channel_name;
static char *opt_filter;
static char *opt_exclude;

#ifdef LTTNG_EMBED_HELP
static const char help_msg[] =
#include <lttng-enable-event.1.h>
;
#endif

enum {
	OPT_HELP = 1,
	OPT_TRACEPOINT,
	OPT_PROBE,
	OPT_USERSPACE_PROBE,
	OPT_FUNCTION,
	OPT_SYSCALL,
	OPT_USERSPACE,
	OPT_LOGLEVEL,
	OPT_LOGLEVEL_ONLY,
	OPT_LIST_OPTIONS,
	OPT_FILTER,
	OPT_EXCLUDE,
};

static struct lttng_handle *handle;
static struct mi_writer *writer;

static struct poptOption long_options[] = {
	/* longName, shortName, argInfo, argPtr, value, descrip, argDesc */
	{"help",           'h', POPT_ARG_NONE, 0, OPT_HELP, 0, 0},
	{"session",        's', POPT_ARG_STRING, &opt_session_name, 0, 0, 0},
	{"all",            'a', POPT_ARG_VAL, &opt_enable_all, 1, 0, 0},
	{"channel",        'c', POPT_ARG_STRING, &opt_channel_name, 0, 0, 0},
	{"kernel",         'k', POPT_ARG_VAL, &opt_kernel, 1, 0, 0},
	{"userspace",      'u', POPT_ARG_NONE, 0, OPT_USERSPACE, 0, 0},
	{"jul",            'j', POPT_ARG_VAL, &opt_jul, 1, 0, 0},
	{"log4j",          'l', POPT_ARG_VAL, &opt_log4j, 1, 0, 0},
	{"python",         'p', POPT_ARG_VAL, &opt_python, 1, 0, 0},
	{"tracepoint",     0,   POPT_ARG_NONE, 0, OPT_TRACEPOINT, 0, 0},
	{"probe",          0,   POPT_ARG_STRING, &opt_probe, OPT_PROBE, 0, 0},
	{"userspace-probe",0,   POPT_ARG_STRING, &opt_userspace_probe, OPT_USERSPACE_PROBE, 0, 0},
	{"function",       0,   POPT_ARG_STRING, &opt_function, OPT_FUNCTION, 0, 0},
	{"syscall",        0,   POPT_ARG_NONE, 0, OPT_SYSCALL, 0, 0},
	{"loglevel",       0,     POPT_ARG_STRING, 0, OPT_LOGLEVEL, 0, 0},
	{"loglevel-only",  0,     POPT_ARG_STRING, 0, OPT_LOGLEVEL_ONLY, 0, 0},
	{"list-options", 0, POPT_ARG_NONE, NULL, OPT_LIST_OPTIONS, NULL, NULL},
	{"filter",         'f', POPT_ARG_STRING, &opt_filter, OPT_FILTER, 0, 0},
	{"exclude",        'x', POPT_ARG_STRING, &opt_exclude, OPT_EXCLUDE, 0, 0},
	{0, 0, 0, 0, 0, 0, 0}
};

/*
 * Parse probe options.
 */
static int parse_probe_opts(struct lttng_event *ev, char *opt)
{
	int ret = CMD_SUCCESS;
	int match;
	char s_hex[19];
#define S_HEX_LEN_SCANF_IS_A_BROKEN_API "18"	/* 18 is (19 - 1) (\0 is extra) */
	char name[LTTNG_SYMBOL_NAME_LEN];

	if (opt == NULL) {
		ret = CMD_ERROR;
		goto end;
	}

	/* Check for symbol+offset */
	match = sscanf(opt, "%" LTTNG_SYMBOL_NAME_LEN_SCANF_IS_A_BROKEN_API
			"[^'+']+%" S_HEX_LEN_SCANF_IS_A_BROKEN_API "s", name, s_hex);
	if (match == 2) {
		strncpy(ev->attr.probe.symbol_name, name, LTTNG_SYMBOL_NAME_LEN);
		ev->attr.probe.symbol_name[LTTNG_SYMBOL_NAME_LEN - 1] = '\0';
		DBG("probe symbol %s", ev->attr.probe.symbol_name);
		if (*s_hex == '\0') {
			ERR("Invalid probe offset %s", s_hex);
			ret = CMD_ERROR;
			goto end;
		}
		ev->attr.probe.offset = strtoul(s_hex, NULL, 0);
		DBG("probe offset %" PRIu64, ev->attr.probe.offset);
		ev->attr.probe.addr = 0;
		goto end;
	}

	/* Check for symbol */
	if (isalpha(name[0]) || name[0] == '_') {
		match = sscanf(opt, "%" LTTNG_SYMBOL_NAME_LEN_SCANF_IS_A_BROKEN_API "s",
			name);
		if (match == 1) {
			strncpy(ev->attr.probe.symbol_name, name, LTTNG_SYMBOL_NAME_LEN);
			ev->attr.probe.symbol_name[LTTNG_SYMBOL_NAME_LEN - 1] = '\0';
			DBG("probe symbol %s", ev->attr.probe.symbol_name);
			ev->attr.probe.offset = 0;
			DBG("probe offset %" PRIu64, ev->attr.probe.offset);
			ev->attr.probe.addr = 0;
			goto end;
		}
	}

	/* Check for address */
	match = sscanf(opt, "%" S_HEX_LEN_SCANF_IS_A_BROKEN_API "s", s_hex);
	if (match > 0) {
		/*
		 * Return an error if the first character of the tentative
		 * address is NULL or not a digit. It can be "0" if the address
		 * is in hexadecimal and can be 1 to 9 if it's in decimal.
		 */
		if (*s_hex == '\0' || !isdigit(*s_hex)) {
			ERR("Invalid probe description %s", s_hex);
			ret = CMD_ERROR;
			goto end;
		}
		ev->attr.probe.addr = strtoul(s_hex, NULL, 0);
		DBG("probe addr %" PRIu64, ev->attr.probe.addr);
		ev->attr.probe.offset = 0;
		memset(ev->attr.probe.symbol_name, 0, LTTNG_SYMBOL_NAME_LEN);
		goto end;
	}

	/* No match */
	ret = CMD_ERROR;

end:
	return ret;
}

/*
 * Walk the directories in the PATH environment variable to find the target
 * binary passed as parameter.
 *
 * On success, the full path of the binary is copied in binary_full_path out
 * parameter. This buffer is allocated by the caller and must be at least
 * LTTNG_PATH_MAX bytes long.
 * On failure, returns -1;
 */
static int walk_command_search_path(const char *binary, char *binary_full_path)
{
	char *tentative_binary_path = NULL;
	char *command_search_path = NULL;
	char *curr_search_dir_end = NULL;
	char *curr_search_dir = NULL;
	struct stat stat_output;
	int ret = 0;

	command_search_path = lttng_secure_getenv("PATH");
	if (!command_search_path) {
		ret = -1;
		goto end;
	}

	/*
	 * Duplicate the $PATH string as the char pointer returned by getenv() should
	 * not be modified.
	 */
	command_search_path = strdup(command_search_path);
	if (!command_search_path) {
		ret = -1;
		goto end;
	}

	/*
	 * This char array is used to concatenate path to binary to look for
	 * the binary.
	 */
	tentative_binary_path = zmalloc(LTTNG_PATH_MAX * sizeof(char));
	if (!tentative_binary_path) {
		ret = -1;
		goto alloc_error;
	}

	curr_search_dir = command_search_path;
	do {
		/*
		 * Split on ':'. The return value of this call points to the
		 * matching character.
		 */
		curr_search_dir_end = strchr(curr_search_dir, ':');
		if (curr_search_dir_end != NULL) {
			/*
			 * Add a NULL byte to the end of the first token so it
			 * can be used as a string.
			 */
			curr_search_dir_end[0] = '\0';
		}

		/* Empty the tentative path */
		memset(tentative_binary_path, 0, LTTNG_PATH_MAX * sizeof(char));

		/*
		 * Build the tentative path to the binary using the current
		 * search directory and the name of the binary.
		 */
		ret = snprintf(tentative_binary_path, LTTNG_PATH_MAX, "%s/%s",
				curr_search_dir, binary);
		if (ret < 0) {
			goto free_binary_path;
		}
		if (ret < LTTNG_PATH_MAX) {
			 /*
			  * Use STAT(2) to see if the file exists.
			 */
			ret = stat(tentative_binary_path, &stat_output);
			if (ret == 0) {
				/*
				 * Verify that it is a regular file or a
				 * symlink and not a special file (e.g.
				 * device).
				 */
				if (S_ISREG(stat_output.st_mode)
						|| S_ISLNK(stat_output.st_mode)) {
					/*
					 * Found a match, set the out parameter
					 * and return success.
					 */
					ret = lttng_strncpy(binary_full_path,
							tentative_binary_path,
							LTTNG_PATH_MAX);
					if (ret == -1) {
						ERR("Source path does not fit "
							"in destination buffer.");
					}
					goto free_binary_path;
				}
			}
		}
		/* Go to the next entry in the $PATH variable. */
		curr_search_dir = curr_search_dir_end + 1;
	} while (curr_search_dir_end != NULL);

free_binary_path:
	free(tentative_binary_path);
alloc_error:
	free(command_search_path);
end:
	return ret;
}

/*
 * Check if the symbol field passed by the user is in fact an address or an
 * offset from a symbol. Those two instrumentation types are not supported yet.
 * It's expected to be a common mistake because of the existing --probe option
 * that does support these formats.
 *
 * Here are examples of these unsupported formats for the --userspace-probe
 * option:
 * elf:/path/to/binary:0x400430
 * elf:/path/to/binary:4194364
 * elf:/path/to/binary:my_symbol+0x323
 * elf:/path/to/binary:my_symbol+43
 */
static int warn_userspace_probe_syntax(const char *symbol)
{
	int ret;

	/* Check if the symbol field is an hex address. */
	ret = sscanf(symbol, "0x%*x");
	if (ret > 0) {
		/* If there is a match, print a warning and return an error. */
		ERR("Userspace probe on address not supported yet.");
		ret = CMD_UNSUPPORTED;
		goto error;
	}

	/* Check if the symbol field is an decimal address. */
	ret = sscanf(symbol, "%*u");
	if (ret > 0) {
		/* If there is a match, print a warning and return an error. */
		ERR("Userspace probe on address not supported yet.");
		ret = CMD_UNSUPPORTED;
		goto error;
	}

	/* Check if the symbol field is symbol+hex_offset. */
	ret = sscanf(symbol, "%*[^+]+0x%*x");
	if (ret > 0) {
		/* If there is a match, print a warning and return an error. */
		ERR("Userspace probe on symbol+offset not supported yet.");
		ret = CMD_UNSUPPORTED;
		goto error;
	}

	/* Check if the symbol field is symbol+decimal_offset. */
	ret = sscanf(symbol, "%*[^+]+%*u");
	if (ret > 0) {
		/* If there is a match, print a warning and return an error. */
		ERR("Userspace probe on symbol+offset not supported yet.");
		ret = CMD_UNSUPPORTED;
		goto error;
	}

	ret = 0;

error:
	return ret;
}

/*
 * Parse userspace probe options
 * Set the userspace probe fields in the lttng_event struct and set the
 * target_path to the path to the binary.
 */
static int parse_userspace_probe_opts(struct lttng_event *ev, char *opt)
{
	int ret = CMD_SUCCESS;
	int num_token;
	char **tokens;
	char *target_path = NULL;
	char *unescaped_target_path = NULL;
	char *real_target_path = NULL;
	char *symbol_name = NULL, *probe_name = NULL, *provider_name = NULL;
	struct lttng_userspace_probe_location *probe_location = NULL;
	struct lttng_userspace_probe_location_lookup_method *lookup_method =
			NULL;

	if (opt == NULL) {
		ret = CMD_ERROR;
		goto end;
	}

	switch (ev->type) {
	case LTTNG_EVENT_USERSPACE_PROBE:
		break;
	default:
		assert(0);
	}

	/*
	 * userspace probe fields are separated by ':'.
	 */
	tokens = strutils_split(opt, ':', 1);
	num_token = strutils_array_of_strings_len(tokens);

	/*
	 * Early sanity check that the number of parameter is between 2 and 4
	 * inclusively.
	 * elf:PATH:SYMBOL
	 * std:PATH:PROVIDER_NAME:PROBE_NAME
	 * PATH:SYMBOL (same behavior as ELF)
	 */
	if (num_token < 2 || num_token > 4) {
		ret = CMD_ERROR;
		goto end_string;
	}

	/*
	 * Looking up the first parameter will tell the technique to use to
	 * interpret the userspace probe/function description.
	 */
	switch (num_token) {
	case 2:
		/* When the probe type is omitted we assume ELF for now. */
	case 3:
		if (num_token == 3 && strcmp(tokens[0], "elf") == 0) {
			target_path = tokens[1];
			symbol_name = tokens[2];
		} else if (num_token == 2) {
			target_path = tokens[0];
			symbol_name = tokens[1];
		} else {
			ret = CMD_ERROR;
			goto end_string;
		}
		lookup_method =
			lttng_userspace_probe_location_lookup_method_function_elf_create();
		if (!lookup_method) {
			WARN("Failed to create ELF lookup method");
			ret = CMD_ERROR;
			goto end_string;
		}
		break;
	case 4:
		if (strcmp(tokens[0], "sdt") == 0) {
			target_path = tokens[1];
			provider_name = tokens[2];
			probe_name = tokens[3];
		} else {
			ret = CMD_ERROR;
			goto end_string;
		}
		lookup_method =
			lttng_userspace_probe_location_lookup_method_tracepoint_sdt_create();
		if (!lookup_method) {
			WARN("Failed to create SDT lookup method");
			ret = CMD_ERROR;
			goto end_string;
		}
		break;
	default:
		ret = CMD_ERROR;
		goto end_string;
	}

	/* strutils_unescape_string allocates a new char *. */
	unescaped_target_path = strutils_unescape_string(target_path, 0);
	if (!unescaped_target_path) {
		ret = CMD_ERROR;
		goto end_destroy_lookup_method;
	}

	/*
	 * If there is not forward slash in the path. Walk the $PATH else
	 * expand.
	 */
	if (strchr(unescaped_target_path, '/') == NULL) {
		/* Walk the $PATH variable to find the targeted binary. */
		real_target_path = zmalloc(LTTNG_PATH_MAX * sizeof(char));
		if (!real_target_path) {
			PERROR("Error allocating path buffer");
			ret = CMD_ERROR;
			goto end_destroy_lookup_method;
		}
		ret = walk_command_search_path(unescaped_target_path, real_target_path);
		if (ret) {
			ERR("Binary not found.");
			ret = CMD_ERROR;
			goto end_destroy_lookup_method;
		}
	} else {
		/*
		 * Expand references to `/./` and `/../`. This function does not check
		 * if the file exists. This call returns an allocated buffer on
		 * success.
		 */
		real_target_path = utils_expand_path_keep_symlink(unescaped_target_path);
		if (!real_target_path) {
			ERR("Error expanding the path to binary.");
			ret = CMD_ERROR;
			goto end_destroy_lookup_method;
		}

		/*
		 * Check if the file exists using access(2), If it does not,
		 * return an error.
		 */
		ret = access(real_target_path, F_OK);
		if (ret) {
			ERR("Cannot find binary at path: %s.", real_target_path);
			ret = CMD_ERROR;
			goto end_destroy_lookup_method;
		}
	}

	switch (lttng_userspace_probe_location_lookup_method_get_type(lookup_method)) {
	case LTTNG_USERSPACE_PROBE_LOCATION_LOOKUP_METHOD_TYPE_FUNCTION_ELF:
		/*
		 * Check for common mistakes in userspace probe description syntax.
		 */
		ret = warn_userspace_probe_syntax(symbol_name);
		if (ret) {
			goto end_destroy_lookup_method;
		}

		probe_location = lttng_userspace_probe_location_function_create(
				real_target_path, symbol_name, lookup_method);
		if (!probe_location) {
			WARN("Failed to create function probe location");
			ret = CMD_ERROR;
			goto end_destroy_lookup_method;
		}

		/* Ownership transferred to probe_location. */
		lookup_method = NULL;

		ret = lttng_event_set_userspace_probe_location(ev, probe_location);
		if (ret) {
			WARN("Failed to set probe location on event");
			ret = CMD_ERROR;
			goto end_destroy_location;
		}
		break;
	case LTTNG_USERSPACE_PROBE_LOCATION_LOOKUP_METHOD_TYPE_TRACEPOINT_SDT:
		probe_location = lttng_userspace_probe_location_tracepoint_create(
				real_target_path, provider_name, probe_name, lookup_method);
		if (!probe_location) {
			WARN("Failed to create function probe location");
			ret = CMD_ERROR;
			goto end_destroy_lookup_method;
		}

		/* Ownership transferred to probe_location. */
		lookup_method = NULL;

		ret = lttng_event_set_userspace_probe_location(ev, probe_location);
		if (ret) {
			WARN("Failed to set probe location on event");
			ret = CMD_ERROR;
			goto end_destroy_location;
		}
		break;
	default:
		ret = CMD_ERROR;
		goto end_destroy_lookup_method;
	}

	/* Successful parsing, now clean up everything and return. */
	goto end_string;

end_destroy_location:
	lttng_userspace_probe_location_destroy(probe_location);
end_destroy_lookup_method:
	lttng_userspace_probe_location_lookup_method_destroy(lookup_method);
end_string:
	strutils_free_null_terminated_array_of_strings(tokens);
	/*
	 * Freeing both char * here makes the error handling simplier. free()
	 * performs not action if the pointer is NULL.
	 */
	free(real_target_path);
	free(unescaped_target_path);
end:
	return ret;
}

/*
 * Maps LOG4j loglevel from string to value
 */
LTTNG_HIDDEN
int loglevel_log4j_str_to_value(const char *inputstr)
{
	int i = 0;
	char str[LTTNG_SYMBOL_NAME_LEN];

	if (!inputstr || strlen(inputstr) == 0) {
		return -1;
	}

	/*
	 * Loop up to LTTNG_SYMBOL_NAME_LEN minus one because the NULL bytes is
	 * added at the end of the loop so a the upper bound we avoid the overflow.
	 */
	while (i < (LTTNG_SYMBOL_NAME_LEN - 1) && inputstr[i] != '\0') {
		str[i] = toupper(inputstr[i]);
		i++;
	}
	str[i] = '\0';

	if (!strcmp(str, "LOG4J_OFF") || !strcmp(str, "OFF")) {
		return LTTNG_LOGLEVEL_LOG4J_OFF;
	} else if (!strcmp(str, "LOG4J_FATAL") || !strcmp(str, "FATAL")) {
		return LTTNG_LOGLEVEL_LOG4J_FATAL;
	} else if (!strcmp(str, "LOG4J_ERROR") || !strcmp(str, "ERROR")) {
		return LTTNG_LOGLEVEL_LOG4J_ERROR;
	} else if (!strcmp(str, "LOG4J_WARN") || !strcmp(str, "WARN")) {
		return LTTNG_LOGLEVEL_LOG4J_WARN;
	} else if (!strcmp(str, "LOG4J_INFO") || !strcmp(str, "INFO")) {
		return LTTNG_LOGLEVEL_LOG4J_INFO;
	} else if (!strcmp(str, "LOG4J_DEBUG") || !strcmp(str, "DEBUG")) {
		return LTTNG_LOGLEVEL_LOG4J_DEBUG;
	} else if (!strcmp(str, "LOG4J_TRACE") || !strcmp(str, "TRACE")) {
		return LTTNG_LOGLEVEL_LOG4J_TRACE;
	} else if (!strcmp(str, "LOG4J_ALL") || !strcmp(str, "ALL")) {
		return LTTNG_LOGLEVEL_LOG4J_ALL;
	} else {
		return -1;
	}
}

/*
 * Maps JUL loglevel from string to value
 */
LTTNG_HIDDEN
int loglevel_jul_str_to_value(const char *inputstr)
{
	int i = 0;
	char str[LTTNG_SYMBOL_NAME_LEN];

	if (!inputstr || strlen(inputstr) == 0) {
		return -1;
	}

	/*
	 * Loop up to LTTNG_SYMBOL_NAME_LEN minus one because the NULL bytes is
	 * added at the end of the loop so a the upper bound we avoid the overflow.
	 */
	while (i < (LTTNG_SYMBOL_NAME_LEN - 1) && inputstr[i] != '\0') {
		str[i] = toupper(inputstr[i]);
		i++;
	}
	str[i] = '\0';

	if (!strcmp(str, "JUL_OFF") || !strcmp(str, "OFF")) {
		return LTTNG_LOGLEVEL_JUL_OFF;
	} else if (!strcmp(str, "JUL_SEVERE") || !strcmp(str, "SEVERE")) {
		return LTTNG_LOGLEVEL_JUL_SEVERE;
	} else if (!strcmp(str, "JUL_WARNING") || !strcmp(str, "WARNING")) {
		return LTTNG_LOGLEVEL_JUL_WARNING;
	} else if (!strcmp(str, "JUL_INFO") || !strcmp(str, "INFO")) {
		return LTTNG_LOGLEVEL_JUL_INFO;
	} else if (!strcmp(str, "JUL_CONFIG") || !strcmp(str, "CONFIG")) {
		return LTTNG_LOGLEVEL_JUL_CONFIG;
	} else if (!strcmp(str, "JUL_FINE") || !strcmp(str, "FINE")) {
		return LTTNG_LOGLEVEL_JUL_FINE;
	} else if (!strcmp(str, "JUL_FINER") || !strcmp(str, "FINER")) {
		return LTTNG_LOGLEVEL_JUL_FINER;
	} else if (!strcmp(str, "JUL_FINEST") || !strcmp(str, "FINEST")) {
		return LTTNG_LOGLEVEL_JUL_FINEST;
	} else if (!strcmp(str, "JUL_ALL") || !strcmp(str, "ALL")) {
		return LTTNG_LOGLEVEL_JUL_ALL;
	} else {
		return -1;
	}
}

/*
 * Maps Python loglevel from string to value
 */
LTTNG_HIDDEN
int loglevel_python_str_to_value(const char *inputstr)
{
	int i = 0;
	char str[LTTNG_SYMBOL_NAME_LEN];

	if (!inputstr || strlen(inputstr) == 0) {
		return -1;
	}

	/*
	 * Loop up to LTTNG_SYMBOL_NAME_LEN minus one because the NULL bytes is
	 * added at the end of the loop so a the upper bound we avoid the overflow.
	 */
	while (i < (LTTNG_SYMBOL_NAME_LEN - 1) && inputstr[i] != '\0') {
		str[i] = toupper(inputstr[i]);
		i++;
	}
	str[i] = '\0';

	if (!strcmp(str, "PYTHON_CRITICAL") || !strcmp(str, "CRITICAL")) {
		return LTTNG_LOGLEVEL_PYTHON_CRITICAL;
	} else if (!strcmp(str, "PYTHON_ERROR") || !strcmp(str, "ERROR")) {
		return LTTNG_LOGLEVEL_PYTHON_ERROR;
	} else if (!strcmp(str, "PYTHON_WARNING") || !strcmp(str, "WARNING")) {
		return LTTNG_LOGLEVEL_PYTHON_WARNING;
	} else if (!strcmp(str, "PYTHON_INFO") || !strcmp(str, "INFO")) {
		return LTTNG_LOGLEVEL_PYTHON_INFO;
	} else if (!strcmp(str, "PYTNON_DEBUG") || !strcmp(str, "DEBUG")) {
		return LTTNG_LOGLEVEL_PYTHON_DEBUG;
	} else if (!strcmp(str, "PYTHON_NOTSET") || !strcmp(str, "NOTSET")) {
		return LTTNG_LOGLEVEL_PYTHON_NOTSET;
	} else {
		return -1;
	}
}

/*
 * Maps loglevel from string to value
 */
LTTNG_HIDDEN
int loglevel_str_to_value(const char *inputstr)
{
	int i = 0;
	char str[LTTNG_SYMBOL_NAME_LEN];

	if (!inputstr || strlen(inputstr) == 0) {
		return -1;
	}

	/*
	 * Loop up to LTTNG_SYMBOL_NAME_LEN minus one because the NULL bytes is
	 * added at the end of the loop so a the upper bound we avoid the overflow.
	 */
	while (i < (LTTNG_SYMBOL_NAME_LEN - 1) && inputstr[i] != '\0') {
		str[i] = toupper(inputstr[i]);
		i++;
	}
	str[i] = '\0';
	if (!strcmp(str, "TRACE_EMERG") || !strcmp(str, "EMERG")) {
		return LTTNG_LOGLEVEL_EMERG;
	} else if (!strcmp(str, "TRACE_ALERT") || !strcmp(str, "ALERT")) {
		return LTTNG_LOGLEVEL_ALERT;
	} else if (!strcmp(str, "TRACE_CRIT") || !strcmp(str, "CRIT")) {
		return LTTNG_LOGLEVEL_CRIT;
	} else if (!strcmp(str, "TRACE_ERR") || !strcmp(str, "ERR")) {
		return LTTNG_LOGLEVEL_ERR;
	} else if (!strcmp(str, "TRACE_WARNING") || !strcmp(str, "WARNING")) {
		return LTTNG_LOGLEVEL_WARNING;
	} else if (!strcmp(str, "TRACE_NOTICE") || !strcmp(str, "NOTICE")) {
		return LTTNG_LOGLEVEL_NOTICE;
	} else if (!strcmp(str, "TRACE_INFO") || !strcmp(str, "INFO")) {
		return LTTNG_LOGLEVEL_INFO;
	} else if (!strcmp(str, "TRACE_DEBUG_SYSTEM") || !strcmp(str, "DEBUG_SYSTEM") || !strcmp(str, "SYSTEM")) {
		return LTTNG_LOGLEVEL_DEBUG_SYSTEM;
	} else if (!strcmp(str, "TRACE_DEBUG_PROGRAM") || !strcmp(str, "DEBUG_PROGRAM") || !strcmp(str, "PROGRAM")) {
		return LTTNG_LOGLEVEL_DEBUG_PROGRAM;
	} else if (!strcmp(str, "TRACE_DEBUG_PROCESS") || !strcmp(str, "DEBUG_PROCESS") || !strcmp(str, "PROCESS")) {
		return LTTNG_LOGLEVEL_DEBUG_PROCESS;
	} else if (!strcmp(str, "TRACE_DEBUG_MODULE") || !strcmp(str, "DEBUG_MODULE") || !strcmp(str, "MODULE")) {
		return LTTNG_LOGLEVEL_DEBUG_MODULE;
	} else if (!strcmp(str, "TRACE_DEBUG_UNIT") || !strcmp(str, "DEBUG_UNIT") || !strcmp(str, "UNIT")) {
		return LTTNG_LOGLEVEL_DEBUG_UNIT;
	} else if (!strcmp(str, "TRACE_DEBUG_FUNCTION") || !strcmp(str, "DEBUG_FUNCTION") || !strcmp(str, "FUNCTION")) {
		return LTTNG_LOGLEVEL_DEBUG_FUNCTION;
	} else if (!strcmp(str, "TRACE_DEBUG_LINE") || !strcmp(str, "DEBUG_LINE") || !strcmp(str, "LINE")) {
		return LTTNG_LOGLEVEL_DEBUG_LINE;
	} else if (!strcmp(str, "TRACE_DEBUG") || !strcmp(str, "DEBUG")) {
		return LTTNG_LOGLEVEL_DEBUG;
	} else {
		return -1;
	}
}

static
const char *print_channel_name(const char *name)
{
	return name ? : DEFAULT_CHANNEL_NAME;
}

static
const char *print_raw_channel_name(const char *name)
{
	return name ? : "<default>";
}

/*
 * Mi print exlcusion list
 */
static
int mi_print_exclusion(char **names)
{
	int i, ret;
	int count = names ? strutils_array_of_strings_len(names) : 0;

	assert(writer);

	if (count == 0) {
		ret = 0;
		goto end;
	}
	ret = mi_lttng_writer_open_element(writer, config_element_exclusions);
	if (ret) {
		goto end;
	}

	for (i = 0; i < count; i++) {
		ret = mi_lttng_writer_write_element_string(writer,
				config_element_exclusion, names[i]);
		if (ret) {
			goto end;
		}
	}

	/* Close exclusions element */
	ret = mi_lttng_writer_close_element(writer);

end:
	return ret;
}

/*
 * Return allocated string for pretty-printing exclusion names.
 */
static
char *print_exclusions(char **names)
{
	int length = 0;
	int i;
	const char preamble[] = " excluding ";
	char *ret;
	int count = names ? strutils_array_of_strings_len(names) : 0;

	if (count == 0) {
		return strdup("");
	}

	/* calculate total required length */
	for (i = 0; i < count; i++) {
		length += strlen(names[i]) + 4;
	}

	length += sizeof(preamble);
	ret = zmalloc(length);
	if (!ret) {
		return NULL;
	}
	strncpy(ret, preamble, length);
	for (i = 0; i < count; i++) {
		strcat(ret, "\"");
		strcat(ret, names[i]);
		strcat(ret, "\"");
		if (i != count - 1) {
			strcat(ret, ", ");
		}
	}

	return ret;
}

static
int check_exclusion_subsets(const char *event_name, const char *exclusion)
{
	bool warn = false;
	int ret = 0;
	const char *e = event_name;
	const char *x = exclusion;

	/* Scan both the excluder and the event letter by letter */
	while (true) {
		if (*e == '\\') {
			if (*x != *e) {
				warn = true;
				goto end;
			}

			e++;
			x++;
			goto cmp_chars;
		}

		if (*x == '*') {
			/* Event is a subset of the excluder */
			ERR("Event %s: %s excludes all events from %s",
				event_name, exclusion, event_name);
			goto error;
		}

		if (*e == '*') {
			/*
			 * Reached the end of the event name before the
			 * end of the exclusion: this is valid.
			 */
			goto end;
		}

cmp_chars:
		if (*x != *e) {
			warn = true;
			break;
		}

		x++;
		e++;
	}

	goto end;

error:
	ret = -1;

end:
	if (warn) {
		WARN("Event %s: %s does not exclude any events from %s",
			event_name, exclusion, event_name);
	}

	return ret;
}

static
int create_exclusion_list_and_validate(const char *event_name,
		const char *exclusions_arg,
		char ***exclusion_list)
{
	int ret = 0;
	char **exclusions = NULL;

	/* Event name must be a valid globbing pattern to allow exclusions. */
	if (!strutils_is_star_glob_pattern(event_name)) {
		ERR("Event %s: Exclusions can only be used with a globbing pattern",
			event_name);
		goto error;
	}

	/* Split exclusions. */
	exclusions = strutils_split(exclusions_arg, ',', true);
	if (!exclusions) {
		goto error;
	}

	/*
	 * If the event name is a star-at-end only globbing pattern,
	 * then we can validate the individual exclusions. Otherwise
	 * all exclusions are passed to the session daemon.
	 */
	if (strutils_is_star_at_the_end_only_glob_pattern(event_name)) {
		char * const *exclusion;

		for (exclusion = exclusions; *exclusion; exclusion++) {
			if (!strutils_is_star_glob_pattern(*exclusion) ||
					strutils_is_star_at_the_end_only_glob_pattern(*exclusion)) {
				ret = check_exclusion_subsets(event_name, *exclusion);
				if (ret) {
					goto error;
				}
			}
		}
	}

	*exclusion_list = exclusions;

	goto end;

error:
	ret = -1;
	strutils_free_null_terminated_array_of_strings(exclusions);

end:
	return ret;
}

static void warn_on_truncated_exclusion_names(char * const *exclusion_list,
	int *warn)
{
	char * const *exclusion;

	for (exclusion = exclusion_list; *exclusion; exclusion++) {
		if (strlen(*exclusion) >= LTTNG_SYMBOL_NAME_LEN) {
			WARN("Event exclusion \"%s\" will be truncated",
				*exclusion);
			*warn = 1;
		}
	}
}

/*
 * Enabling event using the lttng API.
 * Note: in case of error only the last error code will be return.
 */
static int enable_events(char *session_name)
{
	int ret = CMD_SUCCESS, command_ret = CMD_SUCCESS;
	int error_holder = CMD_SUCCESS, warn = 0, error = 0, success = 1;
	char *event_name, *channel_name = NULL;
	struct lttng_event *ev;
	struct lttng_domain dom;
	char **exclusion_list = NULL;

	memset(&dom, 0, sizeof(dom));

	ev = lttng_event_create();
	if (!ev) {
		ret = CMD_ERROR;
		goto error;
	}

	if (opt_kernel) {
		if (opt_loglevel) {
			WARN("Kernel loglevels are not supported.");
		}
	}

	/* Create lttng domain */
	if (opt_kernel) {
		dom.type = LTTNG_DOMAIN_KERNEL;
		dom.buf_type = LTTNG_BUFFER_GLOBAL;
	} else if (opt_userspace) {
		dom.type = LTTNG_DOMAIN_UST;
		/* Default. */
		dom.buf_type = LTTNG_BUFFER_PER_UID;
	} else if (opt_jul) {
		dom.type = LTTNG_DOMAIN_JUL;
		/* Default. */
		dom.buf_type = LTTNG_BUFFER_PER_UID;
	} else if (opt_log4j) {
		dom.type = LTTNG_DOMAIN_LOG4J;
		/* Default. */
		dom.buf_type = LTTNG_BUFFER_PER_UID;
	} else if (opt_python) {
		dom.type = LTTNG_DOMAIN_PYTHON;
		/* Default. */
		dom.buf_type = LTTNG_BUFFER_PER_UID;
	} else {
		/* Checked by the caller. */
		assert(0);
	}

	if (opt_exclude) {
		switch (dom.type) {
		case LTTNG_DOMAIN_KERNEL:
		case LTTNG_DOMAIN_JUL:
		case LTTNG_DOMAIN_LOG4J:
		case LTTNG_DOMAIN_PYTHON:
			ERR("Event name exclusions are not yet implemented for %s events",
					get_domain_str(dom.type));
			ret = CMD_ERROR;
			goto error;
		case LTTNG_DOMAIN_UST:
			/* Exclusions supported */
			break;
		default:
			assert(0);
		}
	}

	/*
	 * Adding a filter to a probe, function or userspace-probe would be
	 * denied by the kernel tracer as it's not supported at the moment. We
	 * do an early check here to warn the user.
	 */
	if (opt_filter && opt_kernel) {
		switch (opt_event_type) {
		case LTTNG_EVENT_ALL:
		case LTTNG_EVENT_TRACEPOINT:
		case LTTNG_EVENT_SYSCALL:
			break;
		case LTTNG_EVENT_PROBE:
		case LTTNG_EVENT_USERSPACE_PROBE:
		case LTTNG_EVENT_FUNCTION:
			ERR("Filter expressions are not supported for %s events",
					get_event_type_str(opt_event_type));
			ret = CMD_ERROR;
			goto error;
		default:
			ret = CMD_UNDEFINED;
			goto error;
		}
	}

	channel_name = opt_channel_name;

	handle = lttng_create_handle(session_name, &dom);
	if (handle == NULL) {
		ret = -1;
		goto error;
	}

	/* Prepare Mi */
	if (lttng_opt_mi) {
		/* Open a events element */
		ret = mi_lttng_writer_open_element(writer, config_element_events);
		if (ret) {
			ret = CMD_ERROR;
			goto error;
		}
	}

	if (opt_enable_all) {
		/* Default setup for enable all */
		if (opt_kernel) {
			ev->type = opt_event_type;
			strcpy(ev->name, "*");
			/* kernel loglevels not implemented */
			ev->loglevel_type = LTTNG_EVENT_LOGLEVEL_ALL;
		} else {
			ev->type = LTTNG_EVENT_TRACEPOINT;
			strcpy(ev->name, "*");
			ev->loglevel_type = opt_loglevel_type;
			if (opt_loglevel) {
				assert(opt_userspace || opt_jul || opt_log4j || opt_python);
				if (opt_userspace) {
					ev->loglevel = loglevel_str_to_value(opt_loglevel);
				} else if (opt_jul) {
					ev->loglevel = loglevel_jul_str_to_value(opt_loglevel);
				} else if (opt_log4j) {
					ev->loglevel = loglevel_log4j_str_to_value(opt_loglevel);
				} else if (opt_python) {
					ev->loglevel = loglevel_python_str_to_value(opt_loglevel);
				}
				if (ev->loglevel == -1) {
					ERR("Unknown loglevel %s", opt_loglevel);
					ret = -LTTNG_ERR_INVALID;
					goto error;
				}
			} else {
				assert(opt_userspace || opt_jul || opt_log4j || opt_python);
				if (opt_userspace) {
					ev->loglevel = -1;
				} else if (opt_jul) {
					ev->loglevel = LTTNG_LOGLEVEL_JUL_ALL;
				} else if (opt_log4j) {
					ev->loglevel = LTTNG_LOGLEVEL_LOG4J_ALL;
				} else if (opt_python) {
					ev->loglevel = LTTNG_LOGLEVEL_PYTHON_DEBUG;
				}
			}
		}

		if (opt_exclude) {
			ret = create_exclusion_list_and_validate("*",
				opt_exclude, &exclusion_list);
			if (ret) {
				ret = CMD_ERROR;
				goto error;
			}

			ev->exclusion = 1;
			warn_on_truncated_exclusion_names(exclusion_list,
				&warn);
		}
		if (!opt_filter) {
			ret = lttng_enable_event_with_exclusions(handle,
					ev, channel_name,
					NULL,
					exclusion_list ? strutils_array_of_strings_len(exclusion_list) : 0,
					exclusion_list);
			if (ret < 0) {
				switch (-ret) {
				case LTTNG_ERR_KERN_EVENT_EXIST:
					WARN("Kernel events already enabled (channel %s, session %s)",
							print_channel_name(channel_name), session_name);
					warn = 1;
					break;
				case LTTNG_ERR_TRACE_ALREADY_STARTED:
				{
					const char *msg = "The command tried to enable an event in a new domain for a session that has already been started once.";
					ERR("Events: %s (channel %s, session %s)",
							msg,
							print_channel_name(channel_name),
							session_name);
					error = 1;
					break;
				}
				default:
					ERR("Events: %s (channel %s, session %s)",
							lttng_strerror(ret),
							ret == -LTTNG_ERR_NEED_CHANNEL_NAME
								? print_raw_channel_name(channel_name)
								: print_channel_name(channel_name),
							session_name);
					error = 1;
					break;
				}
				goto end;
			}

			switch (opt_event_type) {
			case LTTNG_EVENT_TRACEPOINT:
				if (opt_loglevel && dom.type != LTTNG_DOMAIN_KERNEL) {
					char *exclusion_string = print_exclusions(exclusion_list);

					if (!exclusion_string) {
						PERROR("Cannot allocate exclusion_string");
						error = 1;
						goto end;
					}
					MSG("All %s tracepoints%s are enabled in channel %s for loglevel %s",
							get_domain_str(dom.type),
							exclusion_string,
							print_channel_name(channel_name),
							opt_loglevel);
					free(exclusion_string);
				} else {
					char *exclusion_string = print_exclusions(exclusion_list);

					if (!exclusion_string) {
						PERROR("Cannot allocate exclusion_string");
						error = 1;
						goto end;
					}
					MSG("All %s tracepoints%s are enabled in channel %s",
							get_domain_str(dom.type),
							exclusion_string,
							print_channel_name(channel_name));
					free(exclusion_string);
				}
				break;
			case LTTNG_EVENT_SYSCALL:
				if (opt_kernel) {
					MSG("All %s system calls are enabled in channel %s",
							get_domain_str(dom.type),
							print_channel_name(channel_name));
				}
				break;
			case LTTNG_EVENT_ALL:
				if (opt_loglevel && dom.type != LTTNG_DOMAIN_KERNEL) {
					char *exclusion_string = print_exclusions(exclusion_list);

					if (!exclusion_string) {
						PERROR("Cannot allocate exclusion_string");
						error = 1;
						goto end;
					}
					MSG("All %s events%s are enabled in channel %s for loglevel %s",
							get_domain_str(dom.type),
							exclusion_string,
							print_channel_name(channel_name),
							opt_loglevel);
					free(exclusion_string);
				} else {
					char *exclusion_string = print_exclusions(exclusion_list);

					if (!exclusion_string) {
						PERROR("Cannot allocate exclusion_string");
						error = 1;
						goto end;
					}
					MSG("All %s events%s are enabled in channel %s",
							get_domain_str(dom.type),
							exclusion_string,
							print_channel_name(channel_name));
					free(exclusion_string);
				}
				break;
			default:
				/*
				 * We should not be here since lttng_enable_event should have
				 * failed on the event type.
				 */
				goto error;
			}
		}

		if (opt_filter) {
			command_ret = lttng_enable_event_with_exclusions(handle, ev, channel_name,
						opt_filter,
						exclusion_list ? strutils_array_of_strings_len(exclusion_list) : 0,
						exclusion_list);
			if (command_ret < 0) {
				switch (-command_ret) {
				case LTTNG_ERR_FILTER_EXIST:
					WARN("Filter on all events is already enabled"
							" (channel %s, session %s)",
						print_channel_name(channel_name), session_name);
					warn = 1;
					break;
				case LTTNG_ERR_TRACE_ALREADY_STARTED:
				{
					const char *msg = "The command tried to enable an event in a new domain for a session that has already been started once.";
					ERR("All events: %s (channel %s, session %s, filter \'%s\')",
							msg,
							print_channel_name(channel_name),
							session_name, opt_filter);
					error = 1;
					break;
				}
				default:
					ERR("All events: %s (channel %s, session %s, filter \'%s\')",
							lttng_strerror(command_ret),
							command_ret == -LTTNG_ERR_NEED_CHANNEL_NAME
								? print_raw_channel_name(channel_name)
								: print_channel_name(channel_name),
							session_name, opt_filter);
					error = 1;
					break;
				}
				error_holder = command_ret;
			} else {
				ev->filter = 1;
				MSG("Filter '%s' successfully set", opt_filter);
			}
		}

		if (lttng_opt_mi) {
			/* The wildcard * is used for kernel and ust domain to
			 * represent ALL. We copy * in event name to force the wildcard use
			 * for kernel domain
			 *
			 * Note: this is strictly for semantic and printing while in
			 * machine interface mode.
			 */
			strcpy(ev->name, "*");

			/* If we reach here the events are enabled */
			if (!error && !warn) {
				ev->enabled = 1;
			} else {
				ev->enabled = 0;
				success = 0;
			}
			ret = mi_lttng_event(writer, ev, 1, handle->domain.type);
			if (ret) {
				ret = CMD_ERROR;
				goto error;
			}

			/* print exclusion */
			ret = mi_print_exclusion(exclusion_list);
			if (ret) {
				ret = CMD_ERROR;
				goto error;
			}

			/* Success ? */
			ret = mi_lttng_writer_write_element_bool(writer,
					mi_lttng_element_command_success, success);
			if (ret) {
				ret = CMD_ERROR;
				goto error;
			}

			/* Close event element */
			ret = mi_lttng_writer_close_element(writer);
			if (ret) {
				ret = CMD_ERROR;
				goto error;
			}
		}

		goto end;
	}

	/* Strip event list */
	event_name = strtok(opt_event_list, ",");
	while (event_name != NULL) {
		/* Copy name and type of the event */
		strncpy(ev->name, event_name, LTTNG_SYMBOL_NAME_LEN);
		ev->name[LTTNG_SYMBOL_NAME_LEN - 1] = '\0';
		ev->type = opt_event_type;

		/* Kernel tracer action */
		if (opt_kernel) {
			DBG("Enabling kernel event %s for channel %s",
					event_name,
					print_channel_name(channel_name));

			switch (opt_event_type) {
			case LTTNG_EVENT_ALL:	/* Enable tracepoints and syscalls */
				/* If event name differs from *, select tracepoint. */
				if (strcmp(ev->name, "*")) {
					ev->type = LTTNG_EVENT_TRACEPOINT;
				}
				break;
			case LTTNG_EVENT_TRACEPOINT:
				break;
			case LTTNG_EVENT_PROBE:
				ret = parse_probe_opts(ev, opt_probe);
				if (ret) {
					ERR("Unable to parse probe options");
					ret = CMD_ERROR;
					goto error;
				}
				break;
			case LTTNG_EVENT_USERSPACE_PROBE:
				ret = parse_userspace_probe_opts(ev, opt_userspace_probe);
				if (ret) {
					switch (ret) {
					case CMD_UNSUPPORTED:
						/*
						 * Error message describing
						 * what is not supported was
						 * printed in the function.
						 */
						break;
					case CMD_ERROR:
					default:
						ERR("Unable to parse userspace probe options");
						break;
					}
					goto error;
				}
				break;
			case LTTNG_EVENT_FUNCTION:
				ret = parse_probe_opts(ev, opt_function);
				if (ret) {
					ERR("Unable to parse function probe options");
					ret = CMD_ERROR;
					goto error;
				}
				break;
			case LTTNG_EVENT_SYSCALL:
				ev->type = LTTNG_EVENT_SYSCALL;
				break;
			default:
				ret = CMD_UNDEFINED;
				goto error;
			}

			/* kernel loglevels not implemented */
			ev->loglevel_type = LTTNG_EVENT_LOGLEVEL_ALL;
		} else if (opt_userspace) {		/* User-space tracer action */
			DBG("Enabling UST event %s for channel %s, loglevel %s", event_name,
					print_channel_name(channel_name), opt_loglevel ? : "<all>");

			switch (opt_event_type) {
			case LTTNG_EVENT_ALL:	/* Default behavior is tracepoint */
				/* Fall-through */
			case LTTNG_EVENT_TRACEPOINT:
				/* Copy name and type of the event */
				ev->type = LTTNG_EVENT_TRACEPOINT;
				strncpy(ev->name, event_name, LTTNG_SYMBOL_NAME_LEN);
				ev->name[LTTNG_SYMBOL_NAME_LEN - 1] = '\0';
				break;
			case LTTNG_EVENT_PROBE:
			case LTTNG_EVENT_FUNCTION:
			case LTTNG_EVENT_SYSCALL:
			case LTTNG_EVENT_USERSPACE_PROBE:
			default:
				ERR("Event type not available for user-space tracing");
				ret = CMD_UNSUPPORTED;
				goto error;
			}

			if (opt_exclude) {
				ev->exclusion = 1;
				if (opt_event_type != LTTNG_EVENT_ALL && opt_event_type != LTTNG_EVENT_TRACEPOINT) {
					ERR("Exclusion option can only be used with tracepoint events");
					ret = CMD_ERROR;
					goto error;
				}
				/* Free previously allocated items */
				strutils_free_null_terminated_array_of_strings(
					exclusion_list);
				exclusion_list = NULL;
				ret = create_exclusion_list_and_validate(
					event_name, opt_exclude,
					&exclusion_list);
				if (ret) {
					ret = CMD_ERROR;
					goto error;
				}

				warn_on_truncated_exclusion_names(
					exclusion_list, &warn);
			}

			ev->loglevel_type = opt_loglevel_type;
			if (opt_loglevel) {
				ev->loglevel = loglevel_str_to_value(opt_loglevel);
				if (ev->loglevel == -1) {
					ERR("Unknown loglevel %s", opt_loglevel);
					ret = -LTTNG_ERR_INVALID;
					goto error;
				}
			} else {
				ev->loglevel = -1;
			}
		} else if (opt_jul || opt_log4j || opt_python) {
			if (opt_event_type != LTTNG_EVENT_ALL &&
					opt_event_type != LTTNG_EVENT_TRACEPOINT) {
				ERR("Event type not supported for domain.");
				ret = CMD_UNSUPPORTED;
				goto error;
			}

			ev->loglevel_type = opt_loglevel_type;
			if (opt_loglevel) {
				if (opt_jul) {
					ev->loglevel = loglevel_jul_str_to_value(opt_loglevel);
				} else if (opt_log4j) {
					ev->loglevel = loglevel_log4j_str_to_value(opt_loglevel);
				} else if (opt_python) {
					ev->loglevel = loglevel_python_str_to_value(opt_loglevel);
				}
				if (ev->loglevel == -1) {
					ERR("Unknown loglevel %s", opt_loglevel);
					ret = -LTTNG_ERR_INVALID;
					goto error;
				}
			} else {
				if (opt_jul) {
					ev->loglevel = LTTNG_LOGLEVEL_JUL_ALL;
				} else if (opt_log4j) {
					ev->loglevel = LTTNG_LOGLEVEL_LOG4J_ALL;
				} else if (opt_python) {
					ev->loglevel = LTTNG_LOGLEVEL_PYTHON_DEBUG;
				}
			}
			ev->type = LTTNG_EVENT_TRACEPOINT;
			strncpy(ev->name, event_name, LTTNG_SYMBOL_NAME_LEN);
			ev->name[LTTNG_SYMBOL_NAME_LEN - 1] = '\0';
		} else {
			assert(0);
		}

		if (!opt_filter) {
			char *exclusion_string;

			command_ret = lttng_enable_event_with_exclusions(handle,
					ev, channel_name,
					NULL,
					exclusion_list ? strutils_array_of_strings_len(exclusion_list) : 0,
					exclusion_list);
			exclusion_string = print_exclusions(exclusion_list);
			if (!exclusion_string) {
				PERROR("Cannot allocate exclusion_string");
				error = 1;
				goto end;
			}
			if (command_ret < 0) {
				/* Turn ret to positive value to handle the positive error code */
				switch (-command_ret) {
				case LTTNG_ERR_KERN_EVENT_EXIST:
					WARN("Kernel event %s%s already enabled (channel %s, session %s)",
							event_name,
							exclusion_string,
							print_channel_name(channel_name), session_name);
					warn = 1;
					break;
				case LTTNG_ERR_TRACE_ALREADY_STARTED:
				{
					const char *msg = "The command tried to enable an event in a new domain for a session that has already been started once.";
					ERR("Event %s%s: %s (channel %s, session %s)", event_name,
							exclusion_string,
							msg,
							print_channel_name(channel_name),
							session_name);
					error = 1;
					break;
				}
				case LTTNG_ERR_SDT_PROBE_SEMAPHORE:
					ERR("SDT probes %s guarded by semaphores are not supported (channel %s, session %s)",
							event_name, print_channel_name(channel_name),
							session_name);
					error = 1;
					break;
				default:
					ERR("Event %s%s: %s (channel %s, session %s)", event_name,
							exclusion_string,
							lttng_strerror(command_ret),
							command_ret == -LTTNG_ERR_NEED_CHANNEL_NAME
								? print_raw_channel_name(channel_name)
								: print_channel_name(channel_name),
							session_name);
					error = 1;
					break;
				}
				error_holder = command_ret;
			} else {
				switch (dom.type) {
				case LTTNG_DOMAIN_KERNEL:
				case LTTNG_DOMAIN_UST:
					MSG("%s event %s%s created in channel %s",
						get_domain_str(dom.type),
						event_name,
						exclusion_string,
						print_channel_name(channel_name));
					break;
				case LTTNG_DOMAIN_JUL:
				case LTTNG_DOMAIN_LOG4J:
				case LTTNG_DOMAIN_PYTHON:
					/*
					 * Don't print the default channel
					 * name for agent domains.
					 */
					MSG("%s event %s%s enabled",
						get_domain_str(dom.type),
						event_name,
						exclusion_string);
					break;
				default:
					assert(0);
				}
			}
			free(exclusion_string);
		}

		if (opt_filter) {
			char *exclusion_string;

			/* Filter present */
			ev->filter = 1;

			command_ret = lttng_enable_event_with_exclusions(handle, ev, channel_name,
					opt_filter,
					exclusion_list ? strutils_array_of_strings_len(exclusion_list) : 0,
					exclusion_list);
			exclusion_string = print_exclusions(exclusion_list);
			if (!exclusion_string) {
				PERROR("Cannot allocate exclusion_string");
				error = 1;
				goto end;
			}
			if (command_ret < 0) {
				switch (-command_ret) {
				case LTTNG_ERR_FILTER_EXIST:
					WARN("Filter on event %s%s is already enabled"
							" (channel %s, session %s)",
						event_name,
						exclusion_string,
						print_channel_name(channel_name), session_name);
					warn = 1;
					break;
				case LTTNG_ERR_TRACE_ALREADY_STARTED:
				{
					const char *msg = "The command tried to enable an event in a new domain for a session that has already been started once.";
					ERR("Event %s%s: %s (channel %s, session %s, filter \'%s\')", ev->name,
							exclusion_string,
							msg,
							print_channel_name(channel_name),
							session_name, opt_filter);
					error = 1;
					break;
				}
				default:
					ERR("Event %s%s: %s (channel %s, session %s, filter \'%s\')", ev->name,
							exclusion_string,
							lttng_strerror(command_ret),
							command_ret == -LTTNG_ERR_NEED_CHANNEL_NAME
								? print_raw_channel_name(channel_name)
								: print_channel_name(channel_name),
							session_name, opt_filter);
					error = 1;
					break;
				}
				error_holder = command_ret;

			} else {
				MSG("Event %s%s: Filter '%s' successfully set",
						event_name, exclusion_string,
						opt_filter);
			}
			free(exclusion_string);
		}

		if (lttng_opt_mi) {
			if (command_ret) {
				success = 0;
				ev->enabled = 0;
			} else {
				ev->enabled = 1;
			}

			ret = mi_lttng_event(writer, ev, 1, handle->domain.type);
			if (ret) {
				ret = CMD_ERROR;
				goto error;
			}

			/* print exclusion */
			ret = mi_print_exclusion(exclusion_list);
			if (ret) {
				ret = CMD_ERROR;
				goto error;
			}

			/* Success ? */
			ret = mi_lttng_writer_write_element_bool(writer,
					mi_lttng_element_command_success, success);
			if (ret) {
				ret = CMD_ERROR;
				goto end;
			}

			/* Close event element */
			ret = mi_lttng_writer_close_element(writer);
			if (ret) {
				ret = CMD_ERROR;
				goto end;
			}
		}

		/* Next event */
		event_name = strtok(NULL, ",");
		/* Reset warn, error and success */
		success = 1;
	}

end:
	/* Close Mi */
	if (lttng_opt_mi) {
		/* Close events element */
		ret = mi_lttng_writer_close_element(writer);
		if (ret) {
			ret = CMD_ERROR;
			goto error;
		}
	}
error:
	if (warn) {
		ret = CMD_WARNING;
	}
	if (error) {
		ret = CMD_ERROR;
	}
	lttng_destroy_handle(handle);
	strutils_free_null_terminated_array_of_strings(exclusion_list);

	/* Overwrite ret with error_holder if there was an actual error with
	 * enabling an event.
	 */
	ret = error_holder ? error_holder : ret;

	lttng_event_destroy(ev);
	return ret;
}

/*
 * Add event to trace session
 */
int cmd_enable_events(int argc, const char **argv)
{
	int opt, ret = CMD_SUCCESS, command_ret = CMD_SUCCESS, success = 1;
	static poptContext pc;
	char *session_name = NULL;
	const char *leftover = NULL;
	int event_type = -1;

	pc = poptGetContext(NULL, argc, argv, long_options, 0);
	poptReadDefaultConfig(pc, 0);

	/* Default event type */
	opt_event_type = LTTNG_EVENT_ALL;

	while ((opt = poptGetNextOpt(pc)) != -1) {
		switch (opt) {
		case OPT_HELP:
			SHOW_HELP();
			goto end;
		case OPT_TRACEPOINT:
			opt_event_type = LTTNG_EVENT_TRACEPOINT;
			break;
		case OPT_PROBE:
			opt_event_type = LTTNG_EVENT_PROBE;
			break;
		case OPT_USERSPACE_PROBE:
			opt_event_type = LTTNG_EVENT_USERSPACE_PROBE;
			break;
		case OPT_FUNCTION:
			opt_event_type = LTTNG_EVENT_FUNCTION;
			break;
		case OPT_SYSCALL:
			opt_event_type = LTTNG_EVENT_SYSCALL;
			break;
		case OPT_USERSPACE:
			opt_userspace = 1;
			break;
		case OPT_LOGLEVEL:
			opt_loglevel_type = LTTNG_EVENT_LOGLEVEL_RANGE;
			opt_loglevel = poptGetOptArg(pc);
			break;
		case OPT_LOGLEVEL_ONLY:
			opt_loglevel_type = LTTNG_EVENT_LOGLEVEL_SINGLE;
			opt_loglevel = poptGetOptArg(pc);
			break;
		case OPT_LIST_OPTIONS:
			list_cmd_options(stdout, long_options);
			goto end;
		case OPT_FILTER:
			break;
		case OPT_EXCLUDE:
			break;
		default:
			ret = CMD_UNDEFINED;
			goto end;
		}

		/* Validate event type. Multiple event type are not supported. */
		if (event_type == -1) {
			event_type = opt_event_type;
		} else {
			if (event_type != opt_event_type) {
				ERR("Multiple event type not supported.");
				ret = CMD_ERROR;
				goto end;
			}
		}
	}

	ret = print_missing_or_multiple_domains(
			opt_kernel + opt_userspace + opt_jul + opt_log4j +
					opt_python,
			true);
	if (ret) {
		ret = CMD_ERROR;
		goto end;
	}

	/* Mi check */
	if (lttng_opt_mi) {
		writer = mi_lttng_writer_create(fileno(stdout), lttng_opt_mi);
		if (!writer) {
			ret = -LTTNG_ERR_NOMEM;
			goto end;
		}

		/* Open command element */
		ret = mi_lttng_writer_command_open(writer,
				mi_lttng_element_command_enable_event);
		if (ret) {
			ret = CMD_ERROR;
			goto end;
		}

		/* Open output element */
		ret = mi_lttng_writer_open_element(writer,
				mi_lttng_element_command_output);
		if (ret) {
			ret = CMD_ERROR;
			goto end;
		}
	}

	opt_event_list = (char*) poptGetArg(pc);
	if (opt_event_list == NULL && opt_enable_all == 0) {
		ERR("Missing event name(s).\n");
		ret = CMD_ERROR;
		goto end;
	}

	leftover = poptGetArg(pc);
	if (leftover) {
		ERR("Unknown argument: %s", leftover);
		ret = CMD_ERROR;
		goto end;
	}

	if (!opt_session_name) {
		session_name = get_session_name();
		if (session_name == NULL) {
			command_ret = CMD_ERROR;
			success = 0;
			goto mi_closing;
		}
	} else {
		session_name = opt_session_name;
	}

	command_ret = enable_events(session_name);
	if (command_ret) {
		success = 0;
		goto mi_closing;
	}

mi_closing:
	/* Mi closing */
	if (lttng_opt_mi) {
		/* Close  output element */
		ret = mi_lttng_writer_close_element(writer);
		if (ret) {
			ret = CMD_ERROR;
			goto end;
		}

		ret = mi_lttng_writer_write_element_bool(writer,
				mi_lttng_element_command_success, success);
		if (ret) {
			ret = CMD_ERROR;
			goto end;
		}

		/* Command element close */
		ret = mi_lttng_writer_command_close(writer);
		if (ret) {
			ret = CMD_ERROR;
			goto end;
		}
	}

end:
	/* Mi clean-up */
	if (writer && mi_lttng_writer_destroy(writer)) {
		/* Preserve original error code */
		ret = ret ? ret : LTTNG_ERR_MI_IO_FAIL;
	}

	if (opt_session_name == NULL) {
		free(session_name);
	}

	/* Overwrite ret if an error occurred in enable_events */
	ret = command_ret ? command_ret : ret;

	poptFreeContext(pc);
	return ret;
}

