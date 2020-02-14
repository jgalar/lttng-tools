# Trigger notification example

## Description
This example is made-up of three executables.

### `notification-client`

```
Usage: notification-client TRIGGER_NAME
```

A simple client that subscribes to the notifications emitted by the `TRIGGER_NAME` trigger.

### `instrumented-app`

An application that emits the `trigger_example:my_event` event every 2 seconds.

### `demo.sh`

This script adds a trigger named `demo_trigger` which emits a notification when the user-space `trigger_example:my_event` event occurs.

Once the trigger has been setup, the notification-client is launched to print all notifications emitted by the `demo_trigger` trigger.

## Building

Simply run the included Makefile.

## Running the example

1) Launch a session daemon using:
        ```
        $ lttng-sessiond
        ```
2) Launch the `demo.sh` script
3) Launch the `instrumented-app`

The following output should be produced:

```
$ ./demo.sh
Registering a notification trigger named "demo_trigger" for the trigger_example:my_event user-space event
Trigger registered successfully.
Subscribed to notifications of trigger "demo_trigger"
[02-14-2020] 18:13:34.779766 - Received notification of event rule trigger "demo_trigger"
[02-14-2020] 18:13:36.779888 - Received notification of event rule trigger "demo_trigger"
[02-14-2020] 18:13:38.780514 - Received notification of event rule trigger "demo_trigger"
[02-14-2020] 18:13:40.780656 - Received notification of event rule trigger "demo_trigger"
```

```
$ ./instrumented-app
[02-14-2020] 18:13:34.779433 - Tracing event "trigger_example:my_event"
[02-14-2020] 18:13:36.779693 - Tracing event "trigger_example:my_event"
[02-14-2020] 18:13:38.780010 - Tracing event "trigger_example:my_event"
[02-14-2020] 18:13:40.780286 - Tracing event "trigger_example:my_event"
```
