function validate_test_chunks ()
{
	TRACE_PATH=$1
	today=$2
	app_path=$3
	domain=$4

	# YYYYMMDD-HHMMSS-YYYYMMDD-HHMMSS
	chunk_pattern="${today}-[0-9][0-9][0-9][0-9][0-9][0-9]-${today}-[0-9][0-9][0-9][0-9][0-9][0-9]"

	# Check if the 3 chunk folders exist and they contain a ${app_path}/metadata file.
	ls $TRACE_PATH/${chunk_pattern}-1/${app_path}/metadata >/dev/null
	ok $? "Chunk 1 exists"
	ls $TRACE_PATH/${chunk_pattern}-2/${app_path}/metadata >/dev/null
	ok $? "Chunk 2 exists"
	# FIXME
	ls $TRACE_PATH/${chunk_pattern}-3/${app_path}/metadata >/dev/null
	ok $? "Chunk 3 exists"

	# Make sure we don't have anything else in the first 2 chunk directories
	# besides the kernel folder.
	nr_stale=$(\ls $TRACE_PATH/${chunk_pattern}-1 | grep -v $domain | wc -l)
	ok $nr_stale "No stale folders in chunk 1 directory"
	nr_stale=$(\ls $TRACE_PATH/${chunk_pattern}-2 | grep -v $domain | wc -l)
	ok $nr_stale "No stale folders in chunk 2 directory"

	# We expect a session of 30 events
	validate_trace_count $EVENT_NAME $TRACE_PATH 30

	# Chunk 1: 10 events
	validate_trace_count $EVENT_NAME $TRACE_PATH/${chunk_pattern}-1 10
	if [ $? -eq 0 ]; then
		# Only delete if successful
		rm -rf $TRACE_PATH/${chunk_pattern}-1
	fi

	# Chunk 2: 20 events
	validate_trace_count $EVENT_NAME $TRACE_PATH/${chunk_pattern}-2 20
	if [ $? -eq 0 ]; then
		# Only delete if successful
		rm -rf $TRACE_PATH/${chunk_pattern}-2
	fi

	# Chunk 3: 0 event
	validate_trace_empty $TRACE_PATH/${chunk_pattern}-3
	if [ $? -eq 0 ]; then
		# Only delete if successful
		rm -rf $TRACE_PATH/${chunk_pattern}-3
	fi

	# The session folder after all chunks have been removed is empty
	test -z "$(\ls -A $TRACE_PATH)"
	empty=$?
	ok $empty "Trace folder is now empty"
	if [ $empty -eq 0 ]; then
   	# Only delete if successful
		rm -rf $TRACE_PATH/
	fi
}

