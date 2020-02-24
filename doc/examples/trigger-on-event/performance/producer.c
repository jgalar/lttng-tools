/*
 * Copyright (C) 2020 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 * Copyright (C) 2020 Jonathan Rajotte-Julien <jonathan.rajotte-julein@efficios.com>
 *
 * SPDX-License-Identifier: MIT
 *
 */
#include "performance.h"

#include <lttng/tracepoint.h>

#include <lttng/condition/event-rule.h>
#include <lttng/lttng.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>

int main(int argc, char **argv)
{
	int ret = 0;
	int nb_hit;
	int id;
	long long sleep_ms;
	struct timespec sleep_time;
	struct timespec sleep_rm;

	if (argc != 4) {
		fprintf(stderr, "Missing unique_id\n");
		fprintf(stderr, "Missing number of event \n");
		fprintf(stderr, "Missing delay between event in ms \n");
		fprintf(stderr, "Usage: producer id bn_event delay_ms\n");
		ret = 1;
		goto end;
	}
	
	id = atoi(argv[1]);
	nb_hit = atoi(argv[2]);
	sleep_ms = atoll(argv[3]);

	sleep_time.tv_sec = sleep_ms / 1000;
	sleep_time.tv_nsec = (sleep_ms % 1000) * 1000000;

	for (int i = 0; i < nb_hit; i++) {
		tracepoint(performance, hit, 0, i);
		nanosleep(&sleep_time, &sleep_rm);
	}

end:
	return !!ret;
}
