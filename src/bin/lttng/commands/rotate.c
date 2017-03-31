/*
 * Copyright (C) 2017 - Julien Desfossez <jdesfossez@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _LGPL_SOURCE
#include <popt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <common/sessiond-comm/sessiond-comm.h>
#include <common/mi-lttng.h>

#include "../command.h"
#include <lttng/rotate.h>

static char *opt_session_name;
static int opt_no_wait;
static struct mi_writer *writer;

enum {
	OPT_HELP = 1,
	OPT_LIST_OPTIONS,
};

static struct poptOption long_options[] = {
	/* longName, shortName, argInfo, argPtr, value, descrip, argDesc */
	{"help",      'h', POPT_ARG_NONE, 0, OPT_HELP, 0, 0},
	{"list-options", 0, POPT_ARG_NONE, NULL, OPT_LIST_OPTIONS, NULL, NULL},
	{"no-wait",   'n', POPT_ARG_VAL, &opt_no_wait, 1, 0, 0},
	{0, 0, 0, 0, 0, 0, 0}
};

static int mi_print_session(char *session_name, int enabled)
{
	int ret;

	/* Open session element */
	ret = mi_lttng_writer_open_element(writer, config_element_session);
	if (ret) {
		goto end;
	}

	/* Print session name element */
	ret = mi_lttng_writer_write_element_string(writer, config_element_name,
			session_name);
	if (ret) {
		goto end;
	}

	ret = mi_lttng_writer_write_element_bool(writer, config_element_enabled,
			enabled);
	if (ret) {
		goto end;
	}

	/* Close session element */
	ret = mi_lttng_writer_close_element(writer);

end:
	return ret;
}

static int rotate_tracing(void)
{
	int ret;
	char *session_name = NULL, *path = NULL;
	struct lttng_rotate_session_attr *attr = NULL;
	struct lttng_rotate_session_handle *handle = NULL;
	enum lttng_rotate_status rotate_status;

	attr = lttng_rotate_session_attr_create();
	if (!attr) {
		goto error;
	}

	if (opt_session_name == NULL) {
		session_name = get_session_name();
		if (session_name == NULL) {
			goto error;
		}
	} else {
		session_name = opt_session_name;
	}

	ret = lttng_rotate_session_attr_set_session_name(attr, session_name);
	if (ret < 0) {
		goto error;
	}

	DBG("Rotating the output files of session %s", session_name);

	ret = lttng_rotate_session(attr, &handle);
	if (ret < 0) {
		switch (-ret) {
		case LTTNG_ERR_SESSION_NOT_STARTED:
			WARN("Tracing session %s not started yet", session_name);
			break;
		default:
			ERR("%s", lttng_strerror(ret));
			break;
		}
		goto error;
	}

	if (!opt_no_wait) {
		_MSG("Waiting for data availability");
		fflush(stdout);
		do {
			ret = lttng_rotate_session_pending(handle);
			if (ret < 0) {
				goto error;
			}

			/*
			 * Data sleep time before retrying (in usec). Don't sleep if the call
			 * returned value indicates availability.
			 */
			if (ret) {
				usleep(DEFAULT_DATA_AVAILABILITY_WAIT_TIME);
				_MSG(".");
				fflush(stdout);
			}
		} while (ret == 1);
		MSG("");
	}

	rotate_status = lttng_rotate_session_get_status(handle);
	switch(rotate_status) {
	case LTTNG_ROTATE_COMPLETED:
		lttng_rotate_session_get_output_path(handle, &path);
		MSG("Output files of session %s rotated to %s", session_name, path);
		ret = CMD_SUCCESS;
		goto end;
	case LTTNG_ROTATE_STARTED:
		MSG("Rotation started for session %s", session_name);
		free(path);
		if (lttng_opt_mi) {
			ret = mi_print_session(session_name, 1);
			if (ret) {
				ret = CMD_ERROR;
				goto error;
			}
		}

		ret = CMD_SUCCESS;
		goto end;
	case LTTNG_ROTATE_EXPIRED:
		MSG("Output files of session %s rotated, but handle expired", session_name);
		if (lttng_opt_mi) {
			ret = mi_print_session(session_name, 1);
			if (ret) {
				ret = CMD_ERROR;
				goto error;
			}
		}

		ret = CMD_SUCCESS;
		goto end;
	case LTTNG_ROTATE_ERROR:
		MSG("An error occurred with the rotation of session %s", session_name);
		if (lttng_opt_mi) {
			ret = mi_print_session(session_name, 1);
			if (ret) {
				ret = CMD_ERROR;
				goto error;
			}
		}

		ret = CMD_SUCCESS;
		goto end;
	}

error:
	ret = CMD_ERROR;
end:
	if (opt_session_name == NULL) {
		free(session_name);
	}
	lttng_rotate_session_handle_destroy(handle);
	lttng_rotate_session_attr_destroy(attr);
	return ret;
}

/*
 *  cmd_rotate
 *
 *  The 'rotate <options>' first level command
 */
int cmd_rotate(int argc, const char **argv)
{
	int opt, ret = CMD_SUCCESS, command_ret = CMD_SUCCESS, success = 1;
	static poptContext pc;

	pc = poptGetContext(NULL, argc, argv, long_options, 0);
	poptReadDefaultConfig(pc, 0);

	while ((opt = poptGetNextOpt(pc)) != -1) {
		switch (opt) {
		case OPT_HELP:
			SHOW_HELP();
			goto end;
		case OPT_LIST_OPTIONS:
			list_cmd_options(stdout, long_options);
			goto end;
		default:
			ret = CMD_UNDEFINED;
			goto end;
		}
	}

	opt_session_name = (char*) poptGetArg(pc);

	/* Mi check */
	if (lttng_opt_mi) {
		writer = mi_lttng_writer_create(fileno(stdout), lttng_opt_mi);
		if (!writer) {
			ret = -LTTNG_ERR_NOMEM;
			goto end;
		}

		/* Open command element */
		ret = mi_lttng_writer_command_open(writer,
				mi_lttng_element_command_start);
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

		/*
		 * Open sessions element
		 * For validation purpose
		 */
		ret = mi_lttng_writer_open_element(writer,
			config_element_sessions);
		if (ret) {
			ret = CMD_ERROR;
			goto end;
		}
	}

	command_ret = rotate_tracing();
	if (command_ret) {
		success = 0;
	}

	/* Mi closing */
	if (lttng_opt_mi) {
		/* Close  sessions and output element */
		ret = mi_lttng_close_multi_element(writer, 2);
		if (ret) {
			ret = CMD_ERROR;
			goto end;
		}

		/* Success ? */
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
		ret = ret ? ret : -LTTNG_ERR_MI_IO_FAIL;
	}

	/* Overwrite ret if an error occurred with start_tracing */
	ret = command_ret ? command_ret : ret;
	poptFreeContext(pc);
	return ret;
}
