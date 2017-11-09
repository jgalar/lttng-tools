/*
 * Copyright (C) 2017 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
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

#include "sessiond-config.h"
#include <assert.h>
#include "lttng-ust-ctl.h"
#include <common/defaults.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <common/error.h>
#include <common/utils.h>
#include <common/compat/getenv.h>

static
struct sessiond_config sessiond_config_build_defaults = {
	.quiet =		        false,
	.verbose = 			0,
	.verbose_consumer = 		0,

	.agent_tcp_port = 		DEFAULT_AGENT_TCP_PORT,
	.app_socket_timeout = 		DEFAULT_APP_SOCKET_RW_TIMEOUT,

	.no_kernel = 			false,
	.background = 			false,
	.daemonize = 			false,
	.sig_parent = 			false,

	.tracing_group_name.value = 	DEFAULT_TRACING_GROUP,
	.kmod_probes_list.value =	NULL,
	.kmod_extra_probes_list.value =	NULL,

	/* Paths are dynamically initialized. */
	.rundir.value =			NULL,

	.apps_unix_sock_path.value = 	NULL,
	.client_unix_sock_path.value = 	NULL,
	.wait_shm_path.value = 		NULL,
	.health_unix_sock_path.value = 	NULL,
	.lttng_ust_clock_plugin.value =	NULL,
	.pid_file_path.value =		NULL,
	.lock_file_path.value =		NULL,
	.agent_port_file_path.value =	NULL,
	.load_session_path.value =	NULL,

	.consumerd32_bin_path.value =	NULL,
	.consumerd32_lib_dir.value =	NULL,
	.consumerd32_err_unix_sock_path.value = NULL,
	.consumerd32_cmd_unix_sock_path.value = NULL,

	.consumerd64_bin_path.value =	NULL,
	.consumerd64_lib_dir.value =	NULL,
	.consumerd64_err_unix_sock_path.value = NULL,
	.consumerd64_cmd_unix_sock_path.value = NULL,
};

static
void config_string_fini(struct config_string *str)
{
	config_string_set(str, NULL);
}

static
void config_string_set_static(struct config_string *config_str,
		const char *value)
{
	config_string_set(config_str, (char *) value);
	config_str->should_free = false;
}

/* Only use for dynamically-allocated strings. */
LTTNG_HIDDEN
void config_string_set(struct config_string *config_str, char *value)
{
	assert(config_str);
	if (config_str->should_free) {
		free(config_str->value);
		config_str->should_free = false;
	}

	config_str->should_free = !!value;
	config_str->value = value;
}

LTTNG_HIDDEN
int sessiond_config_apply_env_config(struct sessiond_config *config)
{
	int ret = 0;
	const char *env_value;

	env_value = getenv(DEFAULT_APP_SOCKET_TIMEOUT_ENV);
	if (env_value) {
		char *endptr;
		long int_val;

		errno = 0;
		int_val = strtoul(env_value, &endptr, 0);
		if (errno != 0 || !isdigit(*env_value) || int_val > INT_MAX ||
				(int_val < 0 && int_val != -1)) {
			ERR("Invalid value \"%s\" used for \"%s\" environment variable",
					env_value, DEFAULT_APP_SOCKET_TIMEOUT_ENV);
			ret = -1;
			goto end;
		}
	}

	env_value = lttng_secure_getenv("LTTNG_CONSUMERD32_BIN");
	if (env_value) {
		config_string_set_static(&config->consumerd32_bin_path,
				env_value);
	}
	env_value = lttng_secure_getenv("LTTNG_CONSUMERD64_BIN");
	if (env_value) {
		config_string_set_static(&config->consumerd64_bin_path,
				env_value);
	}

	env_value = lttng_secure_getenv("LTTNG_CONSUMERD32_LIBDIR");
	if (env_value) {
		config_string_set_static(&config->consumerd32_lib_dir,
				env_value);
	}
	env_value = lttng_secure_getenv("LTTNG_CONSUMERD64_LIBDIR");
	if (env_value) {
		config_string_set_static(&config->consumerd64_lib_dir,
				env_value);
	}

	/* TODO see funny business in spawn_consumerd() */
end:
	return ret;
}

static
int config_set_paths_root(struct sessiond_config *config)
{
	int ret = 0;

	config_string_set(&config->rundir, strdup(DEFAULT_LTTNG_RUNDIR));
	if (!config->rundir.value) {
		ERR("Failed to set rundir");
		ret = -1;
		goto end;
	}

	config_string_set_static(&config->apps_unix_sock_path,
			DEFAULT_GLOBAL_APPS_UNIX_SOCK);
	config_string_set_static(&config->client_unix_sock_path,
			DEFAULT_GLOBAL_CLIENT_UNIX_SOCK);
	config_string_set_static(&config->wait_shm_path,
			DEFAULT_GLOBAL_APPS_WAIT_SHM_PATH);
	config_string_set_static(&config->health_unix_sock_path,
			DEFAULT_GLOBAL_HEALTH_UNIX_SOCK);
	config_string_set_static(&config->kconsumerd_err_unix_sock_path,
			DEFAULT_KCONSUMERD_ERR_SOCK_PATH);
	config_string_set_static(&config->kconsumerd_cmd_unix_sock_path,
			DEFAULT_KCONSUMERD_CMD_SOCK_PATH);
end:
	return ret;
}

static
int config_set_paths_non_root(struct sessiond_config *config)
{
	int ret = 0;
	const char *home_path = utils_get_home_dir();
	char *str;

	if (home_path == NULL) {
		ERR("Can't get HOME directory for sockets creation.");
		ret = -1;
		goto end;
	}

	/*
	 * Create rundir from home path. This will create something like
	 * $HOME/.lttng
	 */
	ret = asprintf(&str, DEFAULT_LTTNG_HOME_RUNDIR, home_path);
	if (ret < 0) {
		ERR("Failed to set rundir");
		goto end;
	}
	config_string_set(&config->rundir, str);
	str = NULL;

	ret = asprintf(&str, DEFAULT_HOME_APPS_UNIX_SOCK, home_path);
	if (ret < 0) {
		ERR("Failed to set default home apps unix socket path");
		goto end;
	}
	config_string_set(&config->apps_unix_sock_path, str);
	str = NULL;

	ret = asprintf(&str, DEFAULT_HOME_CLIENT_UNIX_SOCK, home_path);
	if (ret < 0) {
		ERR("Failed to set default home client unix socket path");
		goto end;
	}
	config_string_set(&config->client_unix_sock_path, str);
	str = NULL;

	ret = asprintf(&str, DEFAULT_HOME_APPS_WAIT_SHM_PATH, getuid());
	if (ret < 0) {
		ERR("Failed to set default home apps wait shm path");
		goto end;
	}
	config_string_set(&config->wait_shm_path, str);
	str = NULL;

	ret = asprintf(&str, DEFAULT_HOME_HEALTH_UNIX_SOCK, home_path);
	if (ret < 0) {
		ERR("Failed to set default home health UNIX socket path");
		goto end;
	}
	config_string_set(&config->health_unix_sock_path, str);
	str = NULL;

	ret = 0;
end:
	return ret;
}

LTTNG_HIDDEN
int sessiond_config_init(struct sessiond_config *config)
{
	int ret;
	bool is_root = (getuid() == 0);
	char *str;

	assert(config);
	memcpy(config, &sessiond_config_build_defaults, sizeof(*config));

	if (is_root) {
		ret = config_set_paths_root(config);
	} else {
		ret = config_set_paths_non_root(config);
	}

	/* 32 bits consumerd path setup */
	ret = asprintf(&str, DEFAULT_USTCONSUMERD32_ERR_SOCK_PATH,
			config->rundir.value);
	if (ret < 0) {
		ERR("Failed to set 32-bit consumer error socket path");
		goto end;
	}
	config_string_set(&config->consumerd32_err_unix_sock_path, str);
	str = NULL;

	ret = asprintf(&str, DEFAULT_USTCONSUMERD32_CMD_SOCK_PATH,
			config->rundir.value);
	if (ret < 0) {
		ERR("Failed to set 32-bit consumer command socket path");
		goto end;
	}
	config_string_set(&config->consumerd32_cmd_unix_sock_path, str);
	str = NULL;

	/* 64 bits consumerd path setup */
	ret = asprintf(&str, DEFAULT_USTCONSUMERD64_ERR_SOCK_PATH,
			config->rundir.value);
	if (ret < 0) {
		ERR("Failed to set 64-bit consumer error socket path");
		goto end;
	}
	config_string_set(&config->consumerd64_err_unix_sock_path, str);
	str = NULL;

	ret = asprintf(&str, DEFAULT_USTCONSUMERD64_CMD_SOCK_PATH,
			config->rundir.value);
	if (ret < 0) {
		ERR("Failed to set 64-bit consumer command socket path");
		goto end;
	}
	config_string_set(&config->consumerd64_cmd_unix_sock_path, str);
	str = NULL;

	ret = asprintf(&str, "%s/%s", config->rundir.value,
			DEFAULT_LTTNG_SESSIOND_PIDFILE);
	if (ret < 0) {
		ERR("Failed to set PID file path");
		goto end;
	}
	config_string_set(&config->pid_file_path, str);
	str = NULL;

	ret = asprintf(&str, "%s/%s", config->rundir.value,
			DEFAULT_LTTNG_SESSIOND_LOCKFILE);
	if (ret < 0) {
		ERR("Failed to set lock file path");
		goto end;
	}
	config_string_set(&config->lock_file_path, str);
	str = NULL;

	ret = asprintf(&str, "%s/%s", config->rundir.value,
			DEFAULT_LTTNG_SESSIOND_AGENTPORT_FILE);
	if (ret < 0) {
		ERR("Failed to set agent port file path");
		goto end;
	}
	config_string_set(&config->agent_port_file_path, str);
	str = NULL;

	/*
	 * Allow INSTALL_BIN_PATH to be used as a target path for the
	 * native architecture size consumer if CONFIG_CONSUMER*_PATH
	 * has not been defined.
	 */
#if (CAA_BITS_PER_LONG == 32)
	config_string_set_static(&config->consumerd32_bin_path,
			INSTALL_BIN_PATH "/" DEFAULT_CONSUMERD_FILE);
	config_string_set_static(&config->consumerd32_lib_dir,
			INSTALL_LIB_PATH);
#elif (CAA_BITS_PER_LONG == 64)
	config_string_set_static(&config->consumerd64_bin_path,
			INSTALL_BIN_PATH "/" DEFAULT_CONSUMERD_FILE);
	config_string_set_static(&config->consumerd64_lib_dir,
			INSTALL_LIB_PATH);
#else
#error "Unknown bitness"
#endif
	ret = 0;
end:
	return ret;
}

LTTNG_HIDDEN
void sessiond_config_fini(struct sessiond_config *config)
{
	config_string_fini(&config->tracing_group_name);
	config_string_fini(&config->apps_unix_sock_path);
	config_string_fini(&config->client_unix_sock_path);
	config_string_fini(&config->wait_shm_path);
	config_string_fini(&config->health_unix_sock_path);
	config_string_fini(&config->lttng_ust_clock_plugin);
	config_string_fini(&config->pid_file_path);
	config_string_fini(&config->lock_file_path);
	config_string_fini(&config->agent_port_file_path);
	config_string_fini(&config->load_session_path);
	config_string_fini(&config->consumerd32_bin_path);
	config_string_fini(&config->consumerd32_lib_dir);
	config_string_fini(&config->consumerd32_err_unix_sock_path);
	config_string_fini(&config->consumerd32_cmd_unix_sock_path);
	config_string_fini(&config->consumerd64_bin_path);
	config_string_fini(&config->consumerd64_lib_dir);
	config_string_fini(&config->consumerd64_err_unix_sock_path);
	config_string_fini(&config->consumerd64_cmd_unix_sock_path);
	config_string_fini(&config->kconsumerd_err_unix_sock_path);
	config_string_fini(&config->kconsumerd_cmd_unix_sock_path);
}

LTTNG_HIDDEN
int sessiond_config_resolve_paths(struct sessiond_config *config)
{
	return 0;
}
