#!/bin/bash -x
#
# Copyright (C) 2020 Jonathan Rajotte-Julien <jonathan.rajotte-julien@efficios.com>
#
# SPDX-License-Identifier: MIT

cpu_stressor=$(nproc)

while read -r load delay count; do
	stress-ng --cpu-load "$load" --cpu "$cpu_stressor" &
	stress_ng_id=$!

	# Let the workload stabilize
	sleep 5

	./perform-experience.sh 0 "${load}_cpuload_${delay}ms" "$delay" "$count"
	kill "$stress_ng_id"
	wait
done << EOF
0 1 10000
25 1 10000
50 1 10000
75 1 10000
100 1 10000
0 10 5000
25 10 5000
50 10 5000
75 10 5000
100 10 5000
0 100 6000
25 100 6000
50 100 6000
75 100 6000
100 100 6000
0 1000 6000
25 1000 6000
50 1000 6000
75 1000 6000
100 1000 6000
0 10000 600
25 10000 600
50 10000 600
75 10000 600
100 10000 600
0 60000 100
25 60000 100
50 60000 100
75 60000 100
100 60000 60
EOF
