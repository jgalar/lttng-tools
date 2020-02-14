#!/bin/bash
#
# Copyright (C) 2020 Jérémie Galarneau <jeremie.galarneau@efficios.com>
#
# SPDX-License-Identifier: MIT

EVENT_NAME=trigger_example:my_event
TRIGGER_NAME=demo_trigger

lttng list > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "Could not connect to session daemon, are you sure it is running?"
    exit 1
fi

echo "Registering a notification trigger named \"$TRIGGER_NAME\" for the $EVENT_NAME user-space event"
lttng add-trigger --id $TRIGGER_NAME --condition on-event --userspace $EVENT_NAME --action notify

./notification-client $TRIGGER_NAME

