/*
 * Copyright (C) 2011 - David Goulet <david.goulet@polymtl.ca>
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
#include <assert.h>
#include <ctype.h>
#include <popt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include <common/mi-lttng.h>

#include "../command.h"
#include "../utils.h"

#include <common/defaults.h>
#include <common/sessiond-comm/sessiond-comm.h>
#include <common/uri.h>
#include <common/utils.h>
#include <lttng/snapshot.h>

static char *opt_output_path;
static char *opt_session_name;
static char *opt_url;
static char *opt_ctrl_url;
static char *opt_data_url;
static char *opt_shm_path;
static int opt_no_consumer;
static int opt_no_output;
static int opt_snapshot;
static unsigned int opt_live_timer;

enum {
	OPT_HELP = 1,
	OPT_LIST_OPTIONS,
	OPT_LIVE_TIMER,
};

enum {
	OUTPUT_UNKNOWN = 1,
	OUTPUT_NONE,
	OUTPUT_LOCAL,
	OUTPUT_NET,
};
enum {
	SESSION_UNKNOWN = 1,
	SESSION_NORMAL,
	SESSION_LIVE,
	SESSION_SNAPSHOT,
};


static struct mi_writer *writer;

static struct poptOption long_options[] = {
	/* longName, shortName, argInfo, argPtr, value, descrip, argDesc */
	{"help", 'h', POPT_ARG_NONE, NULL, OPT_HELP, NULL, NULL},
	{"output", 'o', POPT_ARG_STRING, &opt_output_path, 0, NULL, NULL},
	{"list-options", 0, POPT_ARG_NONE, NULL, OPT_LIST_OPTIONS, NULL, NULL},
	{"set-url",        'U', POPT_ARG_STRING, &opt_url, 0, 0, 0},
	{"ctrl-url",       'C', POPT_ARG_STRING, &opt_ctrl_url, 0, 0, 0},
	{"data-url",       'D', POPT_ARG_STRING, &opt_data_url, 0, 0, 0},
	{"no-output",       0, POPT_ARG_VAL, &opt_no_output, 1, 0, 0},
	{"no-consumer",     0, POPT_ARG_VAL, &opt_no_consumer, 1, 0, 0},
	{"snapshot",        0, POPT_ARG_VAL, &opt_snapshot, 1, 0, 0},
	{"live",            0, POPT_ARG_INT | POPT_ARGFLAG_OPTIONAL, 0, OPT_LIVE_TIMER, 0, 0},
	{"shm-path",        0, POPT_ARG_STRING, &opt_shm_path, 0, 0, 0},
	{0, 0, 0, 0, 0, 0, 0}
};

/*
 * Please have a look at src/lib/lttng-ctl/lttng-ctl.c for more information on
 * why this declaration exists and used ONLY in for this command.
 */
extern int _lttng_create_session_ext(const char *name, const char *url,
		const char *datetime);

/*
 * Retrieve the created session and mi output it based on provided argument
 * This is currently a summary of what was pretty printed and is subject to
 * enhancements.
 */
static int mi_created_session(const char *session_name)
{
	int ret, i, count, found;
	struct lttng_session *sessions;

	/* session_name should not be null */
	assert(session_name);
	assert(writer);

	count = lttng_list_sessions(&sessions);
	if (count < 0) {
		ret = count;
		ERR("%s", lttng_strerror(ret));
		goto error;
	}

	if (count == 0) {
		ERR("Error session creation failed: session %s not found", session_name);
		ret = -LTTNG_ERR_SESS_NOT_FOUND;
		goto end;
	}

	found = 0;
	for (i = 0; i < count; i++) {
		if (strncmp(sessions[i].name, session_name, NAME_MAX) == 0) {
			found = 1;
			ret = mi_lttng_session(writer, &sessions[i], 0);
			if (ret) {
				goto error;
			}
			break;
		}
	}

	if (!found) {
		ret = -LTTNG_ERR_SESS_NOT_FOUND;
	} else {
		ret = CMD_SUCCESS;
	}

error:
	free(sessions);
end:
	return ret;
}

/*
 * For a session name, set the consumer URLs.
 */
static int set_consumer_url(const char *session_name, const char *ctrl_url,
		const char *data_url)
{
	int ret;
	struct lttng_handle *handle;

	assert(session_name);

	/*
	 * Set handle with the session_name, but no domain. This implies that
	 * the actions taken with this handle apply on the tracing session
	 * rather then the domain-specific session.
	 */
	handle = lttng_create_handle(session_name, NULL);
	if (handle == NULL) {
		ret = CMD_FATAL;
		goto error;
	}

	ret = lttng_set_consumer_url(handle, ctrl_url, data_url);
	if (ret < 0) {
		goto error;
	}

error:
	lttng_destroy_handle(handle);
	return ret;
}

static int add_snapshot_output(const char *session_name, const char *ctrl_url,
		const char *data_url)
{
	int ret;
	struct lttng_snapshot_output *output = NULL;

	assert(session_name);

	output = lttng_snapshot_output_create();
	if (!output) {
		ret = CMD_FATAL;
		goto error_create;
	}

	if (ctrl_url) {
		ret = lttng_snapshot_output_set_ctrl_url(ctrl_url, output);
		if (ret < 0) {
			goto error;
		}
	}

	if (data_url) {
		ret = lttng_snapshot_output_set_data_url(data_url, output);
		if (ret < 0) {
			goto error;
		}
	}

	/* This call, if successful, populates the id of the output object. */
	ret = lttng_snapshot_add_output(session_name, output);
	if (ret < 0) {
		goto error;
	}

error:
	lttng_snapshot_output_destroy(output);
error_create:
	return ret;
}

/*
 * Validate the combinations of passed options
 *
 * CMD_ERROR on error
 * CMD_SUCCESS on success
 */
static int validate_command_options(void)
{
	int ret = CMD_SUCCESS;
	if (opt_snapshot && opt_live_timer) {
		ERR("Snapshot and live modes are mutually exclusive.");
		ret = CMD_ERROR;
		goto error;
	}

	if ((!opt_ctrl_url && opt_data_url) || (opt_ctrl_url && !opt_data_url)) {
		ERR("You need both control and data URL.");
		ret = CMD_ERROR;
		goto error;
	}

error:
	return ret;
}

/*
 * Create a session via direct calls to liblttng-ctl.
 *
 * Return CMD_SUCCESS on success, negative value on internal lttng errors and positive
 * value on command errors.
 */
static int create_session_basic (const char *session_name,
		int session_type,
		int live_timer,
		int output_type,
		const char* url,
		const char* ctrl_url,
		const char* data_url,
		const char* shm_path,
		const char* datetime)
{
	/* Create session based on session creation */
	int ret = CMD_SUCCESS;
	const char *pathname;

	assert(datetime);

	if (opt_relayd_path) {
		pathname = opt_relayd_path;
	} else {
		pathname = INSTALL_BIN_PATH "/lttng-relayd";
	}

	switch (session_type) {
	case SESSION_NORMAL:
		ret = _lttng_create_session_ext(session_name, url, datetime);
		break;
	case SESSION_SNAPSHOT:
		if (output_type == OUTPUT_NONE) {
			ERR("--no-output on a snapshot session is invalid");
			ret = CMD_UNSUPPORTED;
			goto error;
		}
		ret = lttng_create_session_snapshot(session_name, url);
		break;
	case SESSION_LIVE:
		if (output_type == OUTPUT_NONE) {
			ERR("--no-output on a live session is invalid");
			ret = CMD_UNSUPPORTED;
			goto error;
		}

		if (output_type == OUTPUT_LOCAL) {
			ERR("Local file output on a live session is invalid");
			ret = CMD_UNSUPPORTED;
			goto error;
		}
		if (output_type != OUTPUT_NET && !check_relayd() &&
				spawn_relayd(pathname, 0) < 0) {
			ret = CMD_FATAL;
			goto error;
		}
		ret = lttng_create_session_live(session_name, url, live_timer);
		break;
	default:
		ERR("Unknown session type");
		ret = CMD_UNDEFINED;
		goto error;
	}

	if (ret < 0) {
		/* Don't set ret so lttng can interpret the sessiond error. */
		switch (-ret) {
		case LTTNG_ERR_EXIST_SESS:
			WARN("Session %s already exists", session_name);
			break;
		default:
			break;
		}
		goto error;
	}

	/* Configure the session based on the output type */
	switch (output_type) {
	case OUTPUT_LOCAL:
		break;
	case OUTPUT_NET:
		if (session_type == SESSION_SNAPSHOT) {
			ret = add_snapshot_output(session_name, ctrl_url,
					data_url);
		} else if (ctrl_url && data_url) {
			/*
			 * Normal sessions and live sessions behave the same way
			 * regarding consumer url.
			 */
			ret = set_consumer_url(session_name, ctrl_url, data_url);
		}
		if (ret < 0) {
			/* Destroy created session on errors */
			lttng_destroy_session(session_name);
			goto error;
		}
		break;
	case OUTPUT_NONE:
		break;
	default:
		ERR("Unknown output type");
		ret = CMD_UNDEFINED;
		goto error;
	}

	/*
	 * Set the session shared memory path
	 */
	if (shm_path) {
		ret = lttng_set_session_shm_path(session_name, shm_path);
		if (ret < 0) {
			lttng_destroy_session(session_name);
			goto error;
		}
	}
error:
	return ret;
}

static int generate_output(const char *session_name,
		int session_type,
		int live_timer,
		int output_type,
		const char* url,
		const char* ctrl_url,
		const char* data_url,
		const char* shm_path)
{
	int ret = CMD_SUCCESS;

	/*
	 * TODO move this to after session name
	 * for now we only emulate previous behaviour.
	 */
	if (session_type != SESSION_SNAPSHOT) {
		if (ctrl_url) {
			MSG("Control URL %s set for session %s", ctrl_url, session_name);
		}

		if (data_url) {
			MSG("Data URL %s set for session %s", data_url, session_name);
		}
	}

	if (url && output_type == OUTPUT_LOCAL) {
		/* Remove the file:// */
		if (strlen(url) > strlen("file://")){
			url = url + strlen("file://");
		}
	}

	MSG("Session %s created.", session_name);
	if (url && session_type != SESSION_SNAPSHOT) {
		MSG("Traces will be written in %s", url);

		if (live_timer) {
			MSG("Live timer set to %u usec", live_timer);
		}
	} else if (session_type == SESSION_SNAPSHOT) {
		if (url) {
			MSG("Default snapshot output set to: %s", url);
		}
		MSG("Snapshot mode set. Every channel enabled for that session will "
				"be set in overwrite mode and mmap output.");
	}

	if (shm_path) {
		MSG("Session %s set to shm_path: %s.", session_name,
				shm_path);
	}

	/* Mi output */
	if (lttng_opt_mi) {
		ret = mi_created_session(session_name);
		if (ret) {
			ret = CMD_ERROR;
			goto error;
		}
	}
error:
	return ret;
}

/*
 *  Create a tracing session.
 *  If no name is specified, a default name is generated.
 *
 *  Returns one of the CMD_* result constants.
 */
static int create_session(void)
{
	int ret;

	/* Base data */
	int base_session_type = SESSION_UNKNOWN;
	int base_output_type = OUTPUT_UNKNOWN;
	char *base_session_name = NULL;
	char *base_url = NULL;
	char *base_ctrl_url = NULL;
	char *base_data_url = NULL;
	char *base_shm_path = NULL;
	int base_live_timer = 0;

	/* Time data */
	char datetime[16];
	time_t rawtime;
	struct tm *timeinfo = NULL;

	/* Temporary variables */
	char *traces_path = NULL;
	char *temp_url = NULL;
	char *session_name_date = NULL;
	char *tmp_url = NULL;
	char *tmp_home_path = NULL;
	struct lttng_uri *uris = NULL;
	ssize_t uri_array_size = 0;

	/* Option validation */
	if (validate_command_options() != CMD_SUCCESS) {
		ret = CMD_ERROR;
		goto error;
	}

	/* Get date and time for automatic session name/path */
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(datetime, sizeof(datetime), "%Y%m%d-%H%M%S", timeinfo);

	/* Find the session type based on options */
	if(base_session_type == SESSION_UNKNOWN) {
		if (opt_snapshot) {
			base_session_type = SESSION_SNAPSHOT;
		} else if (opt_live_timer) {
			base_session_type = SESSION_LIVE;
		} else {
			base_session_type = SESSION_NORMAL;
		}
	}

	/*
	 * Session name handling
	 */
	if (opt_session_name) {
		/* Override the session name */
		if (strlen(opt_session_name) > NAME_MAX) {
			ERR("Session name too long. Length must be lower or equal to %d",
					NAME_MAX);
			ret = LTTNG_ERR_SESSION_FAIL;
			free(session_name_date);
			goto error;
		}
		/*
		 * Check if the session name begins with "auto-" or is exactly "auto".
		 * Both are reserved for the default session name. See bug #449 to
		 * understand why we need to check both here.
		 */
		if ((strncmp(opt_session_name, DEFAULT_SESSION_NAME "-",
				strlen(DEFAULT_SESSION_NAME) + 1) == 0) ||
			(strncmp(opt_session_name, DEFAULT_SESSION_NAME,
				strlen(DEFAULT_SESSION_NAME)) == 0 &&
			 strlen(opt_session_name) == strlen(DEFAULT_SESSION_NAME))) {
			ERR("%s is a reserved keyword for default session(s)",
					DEFAULT_SESSION_NAME);

			ret = CMD_ERROR;
			goto error;
		}

		base_session_name = strndup(opt_session_name, NAME_MAX);
		if (!base_session_name) {
			PERROR("Strdup session name");
			ret = CMD_ERROR;
			goto error;
		}

		ret = asprintf(&session_name_date, "%s-%s", base_session_name, datetime);
		if (ret < 0) {
			PERROR("Asprintf session name");
			goto error;
		}
		DBG("Session name from command option set to %s", base_session_name);
	} else if (base_session_name) {
		ret = asprintf(&session_name_date, "%s-%s", base_session_name, datetime);
		if (ret < 0) {
			PERROR("Asprintf session name");
			goto error;
		}
	} else {
		/* Generate a name */
		/* TODO: use asprint */
		ret = asprintf(&base_session_name, DEFAULT_SESSION_NAME "-%s", datetime);
		if (ret < 0) {
			PERROR("Asprintf generated session name");
			goto error;
		}
		session_name_date = strdup(base_session_name);
		DBG("Auto session name set to %s", base_session_name);
	}


	/*
	 * Output handling
	 */

	/*
	 * If any of those options are present clear all output related data.
	 */
	if (opt_output_path || opt_url || (opt_ctrl_url && opt_data_url) || opt_no_output) {
		/* Overwrite output */
		free(base_url);
		free(base_ctrl_url);
		free(base_data_url);
		base_url = NULL;
		base_ctrl_url = NULL;
		base_data_url = NULL;
	}

	if (opt_output_path) {

		traces_path = utils_expand_path(opt_output_path);
		if (!traces_path) {
			ret = CMD_ERROR;
			goto error;
		}

		/* Create URL string from the local file system path */
		ret = asprintf(&temp_url, "file://%s", traces_path);
		if (ret < 0) {
			PERROR("asprintf url path");
			ret = CMD_FATAL;
			goto error;
		}

		base_url = temp_url;
	} else if (opt_url) { /* Handling URL (-U opt) */
		base_url = strdup(opt_url);
	} else if (opt_data_url && opt_ctrl_url) {
		/*
		 * With both control and data, we'll be setting the consumer URL
		 * after session creation thus use no URL.
		 */
		base_ctrl_url = strdup(opt_ctrl_url);
		base_data_url = strdup(opt_data_url);
	} else if (!(opt_no_output || base_output_type == OUTPUT_NONE ||
				base_url || base_ctrl_url || base_data_url)) {
		/* Generate default output depending on the session type */
		switch (base_session_type) {
		case SESSION_NORMAL:
			/* fallthrough */
		case SESSION_SNAPSHOT:
			/* Default to a local path */
			tmp_home_path = utils_get_home_dir();
			if (tmp_home_path == NULL) {
				ERR("HOME path not found.\n \
						Please specify an output path using -o, --output PATH");
				ret = CMD_FATAL;
				goto error;
			}

			ret = asprintf(&tmp_url,
					"file://%s/" DEFAULT_TRACE_DIR_NAME "/%s",
					tmp_home_path, session_name_date);

			if (ret < 0) {
				PERROR("asprintf trace dir name");
				ret = CMD_FATAL;
				goto error;
			}

			base_url = tmp_url ;
			break;
		case SESSION_LIVE:
			/* Default to a net output */
			ret = asprintf(&tmp_url, "net://127.0.0.1");
			if (ret < 0) {
				PERROR("asprintf default live URL");
				ret = CMD_FATAL;
				goto error;
			}
			base_url = tmp_url ;
			break;
		default:
			ERR("Unknown session type");
			ret = CMD_FATAL;
			goto error;
		}
	}

	 /*
	  * Shared memory path handling
	  */
	if (opt_shm_path) {
		ret = asprintf(&base_shm_path, "%s/%s", opt_shm_path, session_name_date);
		if (ret < 0) {
			PERROR("asprintf shm_path");
			goto error;
		}
	}

	 /*
	  * Live timer handling
	  */
	if (opt_live_timer) {
		base_live_timer = opt_live_timer;
	}

	/* Get output type from urls */
	if (base_url) {
		/* Get lttng uris from single url */
		uri_array_size = uri_parse_str_urls(base_url, NULL, &uris);
		if (uri_array_size < 0) {
			ret = CMD_ERROR;
			goto error;
		}
	} else if (base_ctrl_url && base_data_url) {
		uri_array_size = uri_parse_str_urls(base_ctrl_url, base_data_url, &uris);
		if (uri_array_size < 0) {
			ret = CMD_ERROR;
			goto error;
		}
	} else {
		/* --no-output */
		uri_array_size = 0;
	}

	switch (uri_array_size) {
	case 0:
		base_output_type = OUTPUT_NONE;
		break;
	case 1:
		base_output_type = OUTPUT_LOCAL;
		break;
	case 2:
		base_output_type = OUTPUT_NET;
		break;
	default:
		ret = CMD_ERROR;
		goto error;
	}

	ret = create_session_basic (base_session_name,
			base_session_type,
			base_live_timer,
			base_output_type,
			base_url,
			base_ctrl_url,
			base_data_url,
			base_shm_path,
			datetime);
	if (ret) {
		goto error;
	}

	ret = generate_output (base_session_name,
			base_session_type,
			base_live_timer,
			base_output_type,
			base_url,
			base_ctrl_url,
			base_data_url,
			base_shm_path);
	if (ret) {
		goto error;
	}

	/* Init lttng session config */
	ret = config_init(base_session_name);
	if (ret < 0) {
		ret = CMD_ERROR;
		goto error;
	}

	ret = CMD_SUCCESS;

error:

	/* Session temp stuff */
	free(session_name_date);

	free(uris);

	if (ret < 0) {
		ERR("%s", lttng_strerror(ret));
	}
	free(base_session_name);
	free(base_url);
	free(base_ctrl_url);
	free(base_data_url);
	free(base_shm_path);
	return ret;
}

/*
 *  spawn_sessiond
 *
 *  Spawn a session daemon by forking and execv.
 */
static int spawn_sessiond(char *pathname)
{
	int ret = 0;
	pid_t pid;

	MSG("Spawning a session daemon");
	pid = fork();
	if (pid == 0) {
		/*
		 * Spawn session daemon in daemon mode.
		 */
		execlp(pathname, "lttng-sessiond",
				"--daemonize", NULL);
		/* execlp only returns if error happened */
		if (errno == ENOENT) {
			ERR("No session daemon found. Use --sessiond-path.");
		} else {
			PERROR("execlp");
		}
		kill(getppid(), SIGTERM);	/* wake parent */
		exit(EXIT_FAILURE);
	} else if (pid > 0) {
		/*
		 * In daemon mode (--daemonize), sessiond only exits when
		 * it's ready to accept commands.
		 */
		for (;;) {
			int status;
			pid_t wait_pid_ret = waitpid(pid, &status, 0);

			if (wait_pid_ret < 0) {
				if (errno == EINTR) {
					continue;
				}
				PERROR("waitpid");
				ret = -errno;
				goto end;
			}

			if (WIFSIGNALED(status)) {
				ERR("Session daemon was killed by signal %d",
						WTERMSIG(status));
				ret = -1;
			        goto end;
			} else if (WIFEXITED(status)) {
				DBG("Session daemon terminated normally (exit status: %d)",
						WEXITSTATUS(status));

				if (WEXITSTATUS(status) != 0) {
					ERR("Session daemon terminated with an error (exit status: %d)",
							WEXITSTATUS(status));
					ret = -1;
				        goto end;
				}
				break;
			}
		}

		goto end;
	} else {
		PERROR("fork");
		ret = -1;
		goto end;
	}

end:
	return ret;
}

/*
 *  launch_sessiond
 *
 *  Check if the session daemon is available using
 *  the liblttngctl API for the check. If not, try to
 *  spawn a daemon.
 */
static int launch_sessiond(void)
{
	int ret;
	char *pathname = NULL;

	ret = lttng_session_daemon_alive();
	if (ret) {
		/* Sessiond is alive, not an error */
		ret = 0;
		goto end;
	}

	/* Try command line option path */
	pathname = opt_sessiond_path;

	/* Try LTTNG_SESSIOND_PATH env variable */
	if (pathname == NULL) {
		pathname = getenv(DEFAULT_SESSIOND_PATH_ENV);
	}

	/* Try with configured path */
	if (pathname == NULL) {
		if (CONFIG_SESSIOND_BIN[0] != '\0') {
			pathname = CONFIG_SESSIOND_BIN;
		}
	}

	/* Try the default path */
	if (pathname == NULL) {
		pathname = INSTALL_BIN_PATH "/lttng-sessiond";
	}

	DBG("Session daemon binary path: %s", pathname);

	/* Check existence and permissions */
	ret = access(pathname, F_OK | X_OK);
	if (ret < 0) {
		ERR("No such file or access denied: %s", pathname);
		goto end;
	}

	ret = spawn_sessiond(pathname);
end:
	if (ret) {
		ERR("Problem occurred while launching session daemon (%s)",
				pathname);
	}
	return ret;
}

/*
 *  The 'create <options>' first level command
 *
 *  Returns one of the CMD_* result constants.
 */
int cmd_create(int argc, const char **argv)
{
	int opt, ret = CMD_SUCCESS, command_ret = CMD_SUCCESS, success = 1;
	char *opt_arg = NULL;
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
		case OPT_LIVE_TIMER:
		{
			unsigned long v;

			errno = 0;
			opt_arg = poptGetOptArg(pc);
			if (!opt_arg) {
				/* Set up default values. */
				opt_live_timer = (uint32_t) DEFAULT_LTTNG_LIVE_TIMER;
				DBG("Session live timer interval set to default value %d",
						opt_live_timer);
				break;
			}

			v = strtoul(opt_arg, NULL, 0);
			if (errno != 0 || !isdigit(opt_arg[0])) {
				ERR("Wrong value in --live parameter: %s", opt_arg);
				ret = CMD_ERROR;
				goto end;
			}
			if (v != (uint32_t) v) {
				ERR("32-bit overflow in --live parameter: %s", opt_arg);
				ret = CMD_ERROR;
				goto end;
			}
			if (v == 0) {
				ERR("Live timer interval must be greater than zero");
				ret = CMD_ERROR;
				goto end;
			}
			opt_live_timer = (uint32_t) v;
			DBG("Session live timer interval set to %d", opt_live_timer);
			break;
		}
		default:
			ret = CMD_UNDEFINED;
			goto end;
		}
	}

	if (opt_no_consumer) {
		MSG("The option --no-consumer is obsolete. Use --no-output now.");
		ret = CMD_WARNING;
		goto end;
	}

	/* Spawn a session daemon if needed */
	if (!opt_no_sessiond) {
		ret = launch_sessiond();
		if (ret) {
			ret = CMD_ERROR;
			goto end;
		}
	}

	/* MI initialization */
	if (lttng_opt_mi) {
		writer = mi_lttng_writer_create(fileno(stdout), lttng_opt_mi);
		if (!writer) {
			ret = -LTTNG_ERR_NOMEM;
			goto end;
		}

		/* Open command element */
		ret = mi_lttng_writer_command_open(writer,
				mi_lttng_element_command_create);
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
	opt_session_name = (char*) poptGetArg(pc);

	command_ret = create_session();

	if (command_ret) {
		success = 0;
	}

	if (lttng_opt_mi) {
		/* Close  output element */
		ret = mi_lttng_writer_close_element(writer);
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

	/* Overwrite ret if an error occurred in create_session() */
	ret = command_ret ? command_ret : ret;

	poptFreeContext(pc);
	return ret;
}
