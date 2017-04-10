/*
 * Copyright (C) 2017 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2 only, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <common/compat/getenv.h>
#include <common/pipe.h>
#include <common/error.h>
#include <unistd.h>
#include <stdbool.h>
#include <lttng/constant.h>
#include <fcntl.h>
#include <dlfcn.h>

static char *pause_pipe_path;
static struct lttng_pipe *pause_pipe;
static bool *data_consumption_paused;

static
void __attribute__((destructor)) pause_pipe_fini(void)
{
	int ret;

	if (pause_pipe_path) {
		ret = unlink(pause_pipe_path);
		if (ret) {
			PERROR("unlink pause pipe");
		}
	}

	lttng_pipe_destroy(pause_pipe);
}

/*
 * We use this testpoint, invoked at the start of the consumerd's data handling
 * thread to create a named pipe/FIFO which a test application can use to either
 * pause or resume the consumption of data.
 */
int __testpoint_consumerd_thread_data(void)
{
	int ret = 0;

	pause_pipe_path = lttng_secure_getenv("CONSUMER_PAUSE_PIPE_PATH");
	if (!pause_pipe_path) {
		ret = -1;
		goto end;
	}

	DBG("Creating pause pipe at %s", pause_pipe_path);
	data_consumption_paused = dlsym(NULL, "data_consumption_paused");
	assert(data_consumption_paused);

	pause_pipe = lttng_pipe_named_open(pause_pipe_path,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, O_NONBLOCK);
	if (!pause_pipe) {
		ERR("Failed to create pause pipe at %s", pause_pipe_path);
		ret = -1;
		goto end;
	}

	/* Only the read end of the pipe is useful to us. */
	ret = lttng_pipe_write_close(pause_pipe);
end:
	return ret;
}

int __testpoint_consumerd_thread_data_poll(void)
{
	int ret = 0;
	uint8_t value;
	bool value_read = false;

	if (!pause_pipe) {
		ret = -1;
		goto end;
	}

	/* Purge pipe and only consider the freshest value. */
	do {
		ret = lttng_pipe_read(pause_pipe, &value, sizeof(value));
		if (ret == sizeof(value)) {
			value_read = true;
		}
	} while (ret == sizeof(value));

	ret = (errno == EAGAIN) ? 0 : -errno;

	if (value_read) {
		*data_consumption_paused = !!value;
		DBG("Message received on pause pipe: %s data consumption",
				*data_consumption_paused ? "paused" : "resumed");
	}
end:
	return ret;
}
