/*
 * Copyright (C) 2013 - Julien Desfossez <jdesfossez@efficios.com>
 *                      David Goulet <dgoulet@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2 only, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _LGPL_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include <common/common.h>
#include <common/defaults.h>
#include <common/utils.h>

#include "lttng-relayd.h"
#include "utils.h"

#define DATETIME_STRING_SIZE 16

#define DATETIME_REGEX ".*-[0-9][0-9][0-9][0-9][0-1][0-9][0-3][0-9]-[0-2][0-9][0-5][0-9][0-5][0-9]$"

static char *get_filesystem_per_session(const char *path, const char *local_session_name) {
	int ret;
	char *local_copy = NULL;
	char *session_name = NULL;
	char *datetime = NULL;
	char *extra_path = NULL;
	char *hostname_ptr;
	const char *second_token_ptr;
	char *leftover_ptr;
	char *filepath_per_session = NULL;
	regex_t regex;

	/* Get a local copy for strtok */
	local_copy = strdup(path);
	if (!local_copy) {
		ERR("strdup of local copy failed");
		goto error;
	}

	/*
	 * The use of strtok with '/' as delimiter is valid since we refuse '/'
	 * in session name and '/' is not a valid hostname character based on
	 * RFC-952 [1], RFC-921 [2] and refined in RFC-1123 [2].
	 * [1] https://tools.ietf.org/html/rfc952
	 * [2] https://tools.ietf.org/html/rfc921
	 * [3] https://tools.ietf.org/html/rfc1123#page-13
	 */

	/*
	 * Get the hostname and possible session_name.
	 * Note that we can get the hostname and session name from the
	 * relay_session object we already have. Still, it is easier to
	 * tokenized the passed path to obtain the start of the path leftover.
	 */
	hostname_ptr = strtok_r(local_copy, "/", &leftover_ptr);
	if (!hostname_ptr) {
		ERR("hostname token not found");
		goto error;
	}

	second_token_ptr = strtok_r(NULL, "/", &leftover_ptr);
	if (!second_token_ptr) {
		ERR("Session name token not found");
		goto error;
	}

	/*
	 * Check if the second token is a extra path set at url level. This is
	 * legal in streaming, live and snapshot [1]. Otherwise it is the
	 * session name with possibly a datetime attached [2]. Note that when
	 * "adding" snapshot output (lttng snapshot add-output), no session name
	 * is present in the path by default. The handling for "extra path" take
	 * care of this case as well.
	 * [1] e.g --set-url net://localhost/my_marvellous_path
	 * [2] Can be:
	 *            <session_name>
	 *                When using --snapshot on session create.
	 *            <session_name>-<date>-<time>
	 *            auto-<date>-<time>
	 */
	if (strncmp(second_token_ptr, local_session_name, strlen(local_session_name)) != 0) {
		/* Match */
		extra_path = strdup(second_token_ptr);
		/*
		 * Point the second token ptr to local_session_name for further
		 * information extraction based on the session name
		 */
		second_token_ptr = local_session_name;
	} else {
		extra_path = strdup("");
	}
	if (!extra_path) {
		ERR("strdup extra path failed");
		goto error;
	}

	/*
	 * The recovery of the session datetime is a best effort here.
	 * We use a regex to validate that a datetime is present.
	 * We can end up in corner case were the end of a
	 * session name is the same format as our datetime but is not really a
	 * datetime. This is not so much of an issue since most of the time the
	 * datetime will be appended and result in the correct case.
	 * Possible cases:
	 *            <session_name>
	 *            <session_name>-<date>-<time>
	 *            auto-<date>-<time>
	 */
	ret = regcomp(&regex, DATETIME_REGEX, 0);
	if (ret) {
		ERR("Regex compilation failed with %d", ret);
		goto error;
	}

	ret = regexec(&regex, second_token_ptr, 0, NULL, 0);
	if (!ret) {
		/* Match */
		session_name = strndup(second_token_ptr, strlen(second_token_ptr) - DATETIME_STRING_SIZE);
		datetime = strdup(&second_token_ptr[strlen(second_token_ptr) - DATETIME_STRING_SIZE +1]);
	} else {
		/* No datetime present */
		session_name = strdup(second_token_ptr);
		datetime = strdup("");
	}

	if (!session_name) {
		ERR("strdup session_name on regex match failed");
		goto error_regex;
	}
	if (!datetime) {
		ERR("strdup datetime on regex match failed");
		goto error_regex;
	}

	ret = asprintf(&filepath_per_session, "%s/%s%s%s/%s/%s", session_name,
			hostname_ptr,
			datetime[0] != '\0' ? "-" : "",
			datetime, extra_path, leftover_ptr);
	if (ret < 0) {
		filepath_per_session = NULL;
		goto error;
	}
error_regex:
	regfree(&regex);
error:
	free(extra_path);
	free(local_copy);
	free(session_name);
	free(datetime);
	return filepath_per_session;
}

static char *create_output_path_auto(const char *path_name)
{
	int ret;
	char *traces_path = NULL;
	char *alloc_path = NULL;
	char *default_path;

	default_path = utils_get_home_dir();
	if (default_path == NULL) {
		ERR("Home path not found.\n \
				Please specify an output path using -o, --output PATH");
		goto exit;
	}
	alloc_path = strdup(default_path);
	if (alloc_path == NULL) {
		PERROR("Path allocation");
		goto exit;
	}
	ret = asprintf(&traces_path, "%s/" DEFAULT_TRACE_DIR_NAME
			"/%s", alloc_path, path_name);
	if (ret < 0) {
		PERROR("asprintf trace dir name");
		goto exit;
	}
exit:
	free(alloc_path);
	return traces_path;
}

static char *create_output_path_noauto(char *path_name)
{
	int ret;
	char *traces_path = NULL;
	char *full_path;

	full_path = utils_expand_path(opt_output_path);
	if (!full_path) {
		goto exit;
	}

	ret = asprintf(&traces_path, "%s/%s", full_path, path_name);
	if (ret < 0) {
		PERROR("asprintf trace dir name");
		goto exit;
	}
exit:
	free(full_path);
	return traces_path;
}

/*
 * Create the output trace directory path name string.
 *
 * Return the allocated string containing the path name or else NULL.
 */
char *create_output_path(const char *path_name, char *session_name)
{
	char *real_path = NULL;
	char *return_path = NULL;
	assert(path_name);

	if (opt_group_output_by_session) {
		real_path = get_filesystem_per_session(path_name, session_name);
	} else if (opt_group_output_by_host) {
		/* By default the output is by host */
		real_path = strdup(path_name);
	} else {
		ERR("Configuration error");
		assert(0);
	}

	if (!real_path) {
		goto error;
	}

	if (opt_output_path == NULL) {
		return_path = create_output_path_auto(real_path);
	} else {
		return_path = create_output_path_noauto(real_path);
	}
error:
	free(real_path);
	return return_path;
}
