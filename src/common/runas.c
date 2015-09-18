/*
 * Copyright (C) 2011 - David Goulet <david.goulet@polymtl.ca>
 *                      Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _GNU_SOURCE
#define _LGPL_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/signal.h>

#include <common/common.h>
#include <common/utils.h>
#include <common/compat/getenv.h>
#include <common/sessiond-comm/unix.h>

#include "runas.h"

struct run_as_data;
typedef int (*run_as_fct)(struct run_as_data *data);

struct run_as_mkdir_data {
	char path[PATH_MAX];
	mode_t mode;
};

struct run_as_open_data {
	char path[PATH_MAX];
	int flags;
	mode_t mode;
};

struct run_as_unlink_data {
	char path[PATH_MAX];
};

struct run_as_rmdir_recursive_data {
	char path[PATH_MAX];
};

enum run_as_cmd {
	RUN_AS_MKDIR,
	RUN_AS_OPEN,
	RUN_AS_UNLINK,
	RUN_AS_RMDIR_RECURSIVE,
	RUN_AS_MKDIR_RECURSIVE,
};

struct run_as_data {
	enum run_as_cmd cmd;
	union {
		struct run_as_mkdir_data mkdir;
		struct run_as_open_data open;
		struct run_as_unlink_data unlink;
		struct run_as_rmdir_recursive_data rmdir_recursive;
	} u;
	uid_t uid;
	gid_t gid;
};

struct run_as_ret {
	int ret;
	int _errno;
};

struct run_as_worker {
	pid_t pid;	/* Worker PID. */
	int sockpair[2];
	char *procname;
};

/* Single global worker per process (for now). */
static struct run_as_worker *global_worker;
/* Lock protecting the worker. */
static pthread_mutex_t worker_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef VALGRIND
static
int use_clone(void)
{
	return 0;
}
#else
static
int use_clone(void)
{
	return !lttng_secure_getenv("LTTNG_DEBUG_NOCLONE");
}
#endif

LTTNG_HIDDEN
int _utils_mkdir_recursive_unsafe(const char *path, mode_t mode);

/*
 * Create recursively directory using the FULL path.
 */
static
int _mkdir_recursive(struct run_as_data *data)
{
	const char *path;
	mode_t mode;

	path = data->u.mkdir.path;
	mode = data->u.mkdir.mode;

	/* Safe to call as we have transitioned to the requested uid/gid. */
	return _utils_mkdir_recursive_unsafe(path, mode);
}

static
int _mkdir(struct run_as_data *data)
{
	return mkdir(data->u.mkdir.path, data->u.mkdir.mode);
}

static
int _open(struct run_as_data *data)
{
	return open(data->u.open.path, data->u.open.flags, data->u.open.mode);
}

static
int _unlink(struct run_as_data *data)
{
	return unlink(data->u.unlink.path);
}

static
int _rmdir_recursive(struct run_as_data *data)
{
	return utils_recursive_rmdir(data->u.rmdir_recursive.path);
}

static
run_as_fct run_as_enum_to_fct(enum run_as_cmd cmd)
{
	switch (cmd) {
	case RUN_AS_MKDIR:
		return _mkdir;
	case RUN_AS_OPEN:
		return _open;
	case RUN_AS_UNLINK:
		return _unlink;
	case RUN_AS_RMDIR_RECURSIVE:
		return _rmdir_recursive;
	case RUN_AS_MKDIR_RECURSIVE:
		return _mkdir_recursive;
	default:
		ERR("Unknown command %d", (int) cmd)
		return NULL;
	}
}

static
int do_send_fd(struct run_as_worker *worker,
		enum run_as_cmd cmd, int fd)
{
	ssize_t len;

	switch (cmd) {
	case RUN_AS_OPEN:
		break;
	default:
		return 0;
	}
	if (fd < 0) {
		return 0;
	}
	len = lttcomm_send_fds_unix_sock(worker->sockpair[1], &fd, 1);
	if (len < 0) {
		PERROR("lttcomm_send_fds_unix_sock");
		return -1;
	}
	if (close(fd) < 0) {
		PERROR("close");
		return -1;
	}
	return 0;
}

static
int do_recv_fd(struct run_as_worker *worker,
		enum run_as_cmd cmd, int *fd)
{
	ssize_t len;

	switch (cmd) {
	case RUN_AS_OPEN:
		break;
	default:
		return 0;
	}
	if (*fd < 0) {
		return 0;
	}
	len = lttcomm_recv_fds_unix_sock(worker->sockpair[0], fd, 1);
	if (len < 0) {
		PERROR("lttcomm_recv_fds_unix_sock");
		return -1;
	}
	return 0;
}

/*
 * Return < 0 on error, 0 if OK, 1 on hangup.
 */
static
int handle_one_cmd(struct run_as_worker *worker)
{
	int ret;
	struct run_as_data data;
	ssize_t readlen, writelen;
	struct run_as_ret sendret;
	run_as_fct cmd;
	uid_t prev_euid;

	/* Read data */
	readlen = lttcomm_recv_unix_sock(worker->sockpair[1], &data,
			sizeof(data));
	if (readlen == 0) {
		/* hang up */
		return 1;
	}
	if (readlen < sizeof(data)) {
		PERROR("lttcomm_recv_unix_sock error");
		return -1;
	}

	cmd = run_as_enum_to_fct(data.cmd);
	if (!cmd) {
		return -1;
	}

	prev_euid = getuid();
	if (data.gid != getegid()) {
		ret = setegid(data.gid);
		if (ret < 0) {
			PERROR("setegid");
			goto write_return;
		}
	}
	if (data.uid != prev_euid) {
		ret = seteuid(data.uid);
		if (ret < 0) {
			PERROR("seteuid");
			goto write_return;
		}
	}
	/*
	 * Also set umask to 0 for mkdir executable bit.
	 */
	umask(0);
	ret = (*cmd)(&data);

write_return:
	sendret.ret = ret;
	sendret._errno = errno;
	/* send back return value */
	writelen = lttcomm_send_unix_sock(worker->sockpair[1], &sendret,
			sizeof(sendret));
	if (writelen < sizeof(sendret)) {
		PERROR("lttcomm_send_unix_sock error");
		return -1;
	}
	ret = do_send_fd(worker, data.cmd, ret);
	if (ret) {
		PERROR("do_send_fd error");
		return -1;
	}
	if (seteuid(prev_euid) < 0) {
		PERROR("seteuid");
		return -1;
	}
	return 0;
}

static
int run_as_worker(struct run_as_worker *worker)
{
	ssize_t writelen;
	struct run_as_ret sendret;
	size_t proc_orig_len;

	/*
	 * Initialize worker. Set a different process cmdline.
	 */
	proc_orig_len = strlen(worker->procname);
	memset(worker->procname, 0, proc_orig_len);
	strncpy(worker->procname, "lttng-runas", proc_orig_len);

	sendret.ret = 0;
	sendret._errno = 0;
	writelen = lttcomm_send_unix_sock(worker->sockpair[1], &sendret,
			sizeof(sendret));
	if (writelen < sizeof(sendret)) {
		PERROR("lttcomm_send_unix_sock error");
		return EXIT_FAILURE;
	}

	for (;;) {
		int ret;

		ret = handle_one_cmd(worker);
		if (ret < 0) {
			return EXIT_FAILURE;
		} else if (ret > 0) {
			break;
		} else {
			continue;	/* Next command. */
		}
	}
	return EXIT_SUCCESS;
}

static
int run_as_cmd(struct run_as_worker *worker,
		enum run_as_cmd cmd,
		struct run_as_data *data,
		uid_t uid, gid_t gid)
{
	ssize_t readlen, writelen;
	struct run_as_ret recvret;

	pthread_mutex_lock(&worker_lock);
	/*
	 * If we are non-root, we can only deal with our own uid.
	 */
	if (geteuid() != 0) {
		if (uid != geteuid()) {
			recvret.ret = -1;
			recvret._errno = EPERM;
			ERR("Client (%d)/Server (%d) UID mismatch (and sessiond is not root)",
				uid, geteuid());
			goto end;
		}
	}

	data->cmd = cmd;
	data->uid = uid;
	data->gid = gid;

	writelen = lttcomm_send_unix_sock(worker->sockpair[0], data,
			sizeof(*data));
	if (writelen < sizeof(*data)) {
		PERROR("Error writing message to run_as");
		recvret.ret = -1;
		recvret._errno = errno;
		goto end;
	}

	/* receive return value */
	readlen = lttcomm_recv_unix_sock(worker->sockpair[0], &recvret,
			sizeof(recvret));
	if (readlen < sizeof(recvret)) {
		PERROR("Error reading response from run_as");
		recvret.ret = -1;
		recvret._errno = errno;
	}
	if (do_recv_fd(worker, cmd, &recvret.ret)) {
		recvret.ret = -1;
		recvret._errno = -EIO;
	}

end:
	pthread_mutex_unlock(&worker_lock);
	errno = recvret._errno;
	return recvret.ret;
}

/*
 * That this is for debuging ONLY, and should not be considered secure.
 */
static
int run_as_noworker(enum run_as_cmd cmd,
		struct run_as_data *data, uid_t uid, gid_t gid)
{
	int ret, saved_errno;
	mode_t old_mask;
	run_as_fct fct;

	fct = run_as_enum_to_fct(cmd);
	if (!fct) {
		errno = -ENOSYS;
		return -1;
	}
	old_mask = umask(0);
	ret = fct(data);
	saved_errno = errno;
	umask(old_mask);
	errno = saved_errno;

	return ret;
}

static
int run_as(struct run_as_worker *worker,
		enum run_as_cmd cmd,
		struct run_as_data *data, uid_t uid, gid_t gid)
{
	if (worker) {
		int ret;

		DBG("Using run_as worker");
		ret = run_as_cmd(worker, cmd, data, uid, gid);
		return ret;
	} else {
		DBG("Using run_as without worker");
		return run_as_noworker(cmd, data, uid, gid);
	}
}

LTTNG_HIDDEN
int run_as_mkdir_recursive(const char *path, mode_t mode, uid_t uid, gid_t gid)
{
	struct run_as_worker *worker = global_worker;
	struct run_as_data data;

	DBG3("mkdir() recursive %s with mode %d for uid %d and gid %d",
			path, mode, uid, gid);
	strncpy(data.u.mkdir.path, path, PATH_MAX - 1);
	data.u.mkdir.path[PATH_MAX - 1] = '\0';
	data.u.mkdir.mode = mode;
	return run_as(worker, RUN_AS_MKDIR_RECURSIVE, &data, uid, gid);
}

LTTNG_HIDDEN
int run_as_mkdir(const char *path, mode_t mode, uid_t uid, gid_t gid)
{
	struct run_as_worker *worker = global_worker;
	struct run_as_data data;

	DBG3("mkdir() %s with mode %d for uid %d and gid %d",
			path, mode, uid, gid);
	strncpy(data.u.mkdir.path, path, PATH_MAX - 1);
	data.u.mkdir.path[PATH_MAX - 1] = '\0';
	data.u.mkdir.mode = mode;
	return run_as(worker, RUN_AS_MKDIR, &data, uid, gid);
}

/*
 * Note: open_run_as is currently not working. We'd need to pass the fd
 * opened in the child to the parent.
 */
LTTNG_HIDDEN
int run_as_open(const char *path, int flags, mode_t mode, uid_t uid, gid_t gid)
{
	struct run_as_worker *worker = global_worker;
	struct run_as_data data;

	DBG3("open() %s with flags %X mode %d for uid %d and gid %d",
			path, flags, mode, uid, gid);
	strncpy(data.u.open.path, path, PATH_MAX - 1);
	data.u.open.path[PATH_MAX - 1] = '\0';
	data.u.open.flags = flags;
	data.u.open.mode = mode;
	return run_as(worker, RUN_AS_OPEN, &data, uid, gid);
}

LTTNG_HIDDEN
int run_as_unlink(const char *path, uid_t uid, gid_t gid)
{
	struct run_as_worker *worker = global_worker;
	struct run_as_data data;

	DBG3("unlink() %s with for uid %d and gid %d",
			path, uid, gid);
	strncpy(data.u.unlink.path, path, PATH_MAX - 1);
	data.u.unlink.path[PATH_MAX - 1] = '\0';
	return run_as(worker, RUN_AS_UNLINK, &data, uid, gid);
}

LTTNG_HIDDEN
int run_as_rmdir_recursive(const char *path, uid_t uid, gid_t gid)
{
	struct run_as_worker *worker = global_worker;
	struct run_as_data data;

	DBG3("rmdir_recursive() %s with for uid %d and gid %d",
			path, uid, gid);
	strncpy(data.u.rmdir_recursive.path, path, PATH_MAX - 1);
	data.u.rmdir_recursive.path[PATH_MAX - 1] = '\0';
	return run_as(worker, RUN_AS_RMDIR_RECURSIVE, &data, uid, gid);
}

int run_as_create_worker(char *procname)
{
	struct run_as_worker *worker;
	pid_t pid;
	ssize_t readlen;
	struct run_as_ret recvret;
	int i;

	if (!use_clone()) {
		return 0;
	}
	worker = zmalloc(sizeof(*worker));
	if (!worker) {
		return -1;
	}
	worker->procname = procname;
	/* Create unix socket. */
	if (lttcomm_create_anon_unix_socketpair(worker->sockpair) < 0) {
		goto error_sock;
	}
	/* Fork worker. */
	pid = fork();
	if (pid < 0) {
		PERROR("fork");
		goto error_fork;
	} else if (pid == 0) {
		int ret;

		/* Child */

		/* Just close, no shutdown. */
		if (close(worker->sockpair[0])) {
			PERROR("close");
			exit(EXIT_FAILURE);
		}
		worker->sockpair[0] = -1;
		ret = run_as_worker(worker);
		if (lttcomm_close_unix_sock(worker->sockpair[1])) {
			PERROR("close");
			ret = -1;
		}
		worker->sockpair[1] = -1;
		if (ret) {
			exit(EXIT_FAILURE);
		} else {
			exit(EXIT_SUCCESS);
		}
	} else {
		/* Parent */

		/* Just close, no shutdown. */
		if (close(worker->sockpair[1])) {
			PERROR("close");
		}
		worker->sockpair[1] = -1;
		worker->pid = pid;
		/* Wait for worker to become ready. */
		readlen = lttcomm_recv_unix_sock(worker->sockpair[0],
				&recvret, sizeof(recvret));
		if (readlen < sizeof(recvret)) {
			ERR("readlen: %zd", readlen);
			PERROR("Error reading response from run_as at creation");
			goto error_fork;
		}
		global_worker = worker;
	}
	return 0;

	/* Error handling. */
error_fork:
	for (i = 0; i < 2; i++) {
		if (worker->sockpair[i] < 0) {
			continue;
		}
		if (lttcomm_close_unix_sock(worker->sockpair[i])) {
			PERROR("close");
		}
		worker->sockpair[i] = -1;
	}
error_sock:
	free(worker);
	return -1;
}

void run_as_destroy_worker(void)
{
	struct run_as_worker *worker = global_worker;
	int status;
	pid_t pid;

	if (!worker) {
		return;
	}
	/* Close unix socket */
	if (lttcomm_close_unix_sock(worker->sockpair[0])) {
		PERROR("close");
	}
	worker->sockpair[0] = -1;
	/* Wait for worker. */
	pid = waitpid(worker->pid, &status, 0);
	if (pid < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		PERROR("wait");
	}
	free(worker);
	global_worker = NULL;
}
