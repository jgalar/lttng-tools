#!/bin/bash
#
# Copyright (C) 2020 Jonathan Rajotte-Julien <jonathan.rajotte-julien@efficios.com>
#
# SPDX-License-Identifier: MIT

EVENT_NAME_HIT=performance:hit
EVENT_NAME_RECEIVE=performance:receive
TRIGGER_NAME=performance_hit

if [ -z "$1" ]; then
	echo "missing source id int value"
	exit 1
fi

if [ -z "$2" ]; then
	echo "missing trace directory name"
	exit 1
fi

if [ -z "$3" ]; then
	echo "missing loop delay"
	exit 1
fi

if [ -z "$4" ]; then
	echo "missing loop count"
	exit 1
fi

key_id="$1"
trace_directory="$(pwd)/trace/$2"
delay=$3
count=$4

if ! lttng list > /dev/null 2>&1 ; then
    echo "Could not connect to session daemon, are you sure it is running?"
    exit 1
fi

lttng create performance --output="$trace_directory"
lttng enable-event -u $EVENT_NAME_HIT,$EVENT_NAME_RECEIVE -s performance
lttng start

filter="source==$key_id"
lttng add-trigger --id ${TRIGGER_NAME} --condition on-event --userspace $EVENT_NAME_HIT --filter="$filter" --action notify

./consumer "$key_id" "$count" $TRIGGER_NAME &

# Cheap way to synchronize and ensure that the consumer is ready to consume
sleep 2

./producer "$key_id" "$count" "$delay" &

wait

lttng remove-trigger ${TRIGGER_NAME}

lttng stop
lttng destroy performance

