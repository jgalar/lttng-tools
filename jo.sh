#!/bin/bash -x
cleanup ()
{
	pkill lttng-sessiond
	pkill -9 lttng-consumerd
	pkill lttng-relayd
	pkill sample
	exit 0
}

trap cleanup SIGINT

../lttng-ust/doc/examples/easy-ust/sample &

while true
do
	#!/bin/bash
	lttng destroy -a

	pkill lttng-relayd
	pkill lttng-sessiond
	pkill -9 lttng-consumerd

	sleep 1

	lttng-sessiond -b -vvv --verbose-consumer > /tmp/sessiond.log 2>&1
	lttng-relayd -b -vvv > /tmp/relayd.log 2>&1

	lttng -n create --live 1000000  live1
	lttng -n enable-event -a -u
	lttng -n start live1
	babeltrace2 -i lttng-live net://localhost/host/Mercury/live1
	if [ $? -eq 100 ]; then
		cleanup
	fi
done
