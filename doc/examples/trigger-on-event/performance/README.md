# Trigger notification end-to-end latency analysis

## Description
This analysis is made-up of five executables.

### `producer`

```
Usage: producer UNIQUE_ID NB_EVENT DELAY_MS
```

An application that emits `NB_EVENT` times the `performance:hit` event every
`DELAY_MS` milliseconds.


### `consumer`
```
Usage: consumer  UNIQUE_ID NB_EVENT TRIGGER_NAME
```

A simple notification client that subscribes to the notifications emitted by the
`TRIGGER_NAME` trigger. The consumer expects `NB_EVENT` notification and on each
valid reception emits a `performance:receive` event.


### `perform-experience.sh`

```
Usage: perform-experience.sh SOURCE_ID TRACE_DIRECTORY_NAME DELAY_MS NB_EVENT`
```

This script performs a complete end-to-end trigger latency experience with
`DELAY_MS` between each trigger hit and `NB_EVENT` times.

The resulting lttng-ust trace is stored inside `$(pwd)/trace/TRACE_DIRECTORY_NAME`

### `generate-data.sh`

```
Usage: generate-data.sh
```

This script performs all configured experiences and apply the customs workload
as necessary.

The resulting traces are stored inside  `$(pwd)/trace/`

This script in its current form will run for about 25 hours.

This script depends on `perform-experience.sh`.

### `generate-graph.sh`

```
Usage: generate-graph.sh
```

This script generate all histograms and saved them individually as pdf files. It
also generate a `summary.pdf` files that contains all pdfs in order of trigger frequency.

This script does not have to run on the machine that produced the data.

This script requires the presence of the "trace/" folder to work.

This script depends on the `bt_plugin_plot.py` babeltrace 2 plugins. Hence this
script requires Babeltrace 2 with python bindings and python plugin support.

The `bt_plugin_plot.py` requires `matplotlib`.


## Building

Simply run the included Makefile.

## Running the complete 

1) Launch a session daemon using:
        ```
        $ lttng-sessiond
        ```
2) Launch `generate-data.sh`
3) Wait ~25 hours
3) Launch `generate-graph.sh`
