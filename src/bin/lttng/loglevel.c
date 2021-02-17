/*
 * Copyright (C) 2021 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "loglevel.h"
#include <string.h>
#include <ctype.h>
#include <assert.h>

struct loglevel_name_value {
	const char *name;
	int value;
};

static
const struct loglevel_name_value loglevel_values[] = {
	{ .name = "TRACE_EMERG", .value = LTTNG_LOGLEVEL_EMERG },
	{ .name = "EMERG", .value = LTTNG_LOGLEVEL_EMERG },
	{ .name = "TRACE_ALERT", .value = LTTNG_LOGLEVEL_ALERT },
	{ .name = "ALERT", .value = LTTNG_LOGLEVEL_ALERT },
	{ .name = "TRACE_CRIT", .value = LTTNG_LOGLEVEL_CRIT },
	{ .name = "CRIT", .value = LTTNG_LOGLEVEL_CRIT },
	{ .name = "TRACE_ERR", .value = LTTNG_LOGLEVEL_ERR },
	{ .name = "ERR", .value = LTTNG_LOGLEVEL_ERR },
	{ .name = "TRACE_WARNING", .value = LTTNG_LOGLEVEL_WARNING },
	{ .name = "WARNING", .value = LTTNG_LOGLEVEL_WARNING },
	{ .name = "TRACE_NOTICE", .value = LTTNG_LOGLEVEL_NOTICE },
	{ .name = "NOTICE", .value = LTTNG_LOGLEVEL_NOTICE },
	{ .name = "TRACE_INFO", .value = LTTNG_LOGLEVEL_INFO },
	{ .name = "INFO", .value = LTTNG_LOGLEVEL_INFO },
	{ .name = "TRACE_DEBUG_SYSTEM", .value = LTTNG_LOGLEVEL_DEBUG_SYSTEM },
	{ .name = "DEBUG_SYSTEM", .value = LTTNG_LOGLEVEL_DEBUG_SYSTEM },
	{ .name = "SYSTEM", .value = LTTNG_LOGLEVEL_DEBUG_SYSTEM },
	{ .name = "TRACE_DEBUG_PROGRAM", .value = LTTNG_LOGLEVEL_DEBUG_PROGRAM },
	{ .name = "DEBUG_PROGRAM", .value = LTTNG_LOGLEVEL_DEBUG_PROGRAM },
	{ .name = "PROGRAM", .value = LTTNG_LOGLEVEL_DEBUG_PROGRAM },
	{ .name = "TRACE_DEBUG_PROCESS", .value = LTTNG_LOGLEVEL_DEBUG_PROCESS },
	{ .name = "DEBUG_PROCESS", .value = LTTNG_LOGLEVEL_DEBUG_PROCESS },
	{ .name = "PROCESS", .value = LTTNG_LOGLEVEL_DEBUG_PROCESS },
	{ .name = "TRACE_DEBUG_MODULE", .value = LTTNG_LOGLEVEL_DEBUG_MODULE },
	{ .name = "DEBUG_MODULE", .value = LTTNG_LOGLEVEL_DEBUG_MODULE },
	{ .name = "MODULE", .value = LTTNG_LOGLEVEL_DEBUG_MODULE },
	{ .name = "TRACE_DEBUG_UNIT", .value = LTTNG_LOGLEVEL_DEBUG_UNIT },
	{ .name = "DEBUG_UNIT", .value = LTTNG_LOGLEVEL_DEBUG_UNIT },
	{ .name = "UNIT", .value = LTTNG_LOGLEVEL_DEBUG_UNIT },
	{ .name = "TRACE_DEBUG_FUNCTION", .value = LTTNG_LOGLEVEL_DEBUG_FUNCTION },
	{ .name = "DEBUG_FUNCTION", .value = LTTNG_LOGLEVEL_DEBUG_FUNCTION },
	{ .name = "FUNCTION", .value = LTTNG_LOGLEVEL_DEBUG_FUNCTION },
	{ .name = "TRACE_DEBUG_LINE", .value = LTTNG_LOGLEVEL_DEBUG_LINE },
	{ .name = "DEBUG_LINE", .value = LTTNG_LOGLEVEL_DEBUG_LINE },
	{ .name = "LINE", .value = LTTNG_LOGLEVEL_DEBUG_LINE },
	{ .name = "TRACE_DEBUG", .value = LTTNG_LOGLEVEL_DEBUG },
	{ .name = "DEBUG", .value = LTTNG_LOGLEVEL_DEBUG },
};

static
const struct loglevel_name_value loglevel_log4j_values[] = {
	{ .name = "LOG4J_OFF", .value = LTTNG_LOGLEVEL_LOG4J_OFF },
	{ .name = "OFF", .value = LTTNG_LOGLEVEL_LOG4J_OFF },
	{ .name = "LOG4J_FATAL", .value = LTTNG_LOGLEVEL_LOG4J_FATAL },
	{ .name = "FATAL", .value = LTTNG_LOGLEVEL_LOG4J_FATAL },
	{ .name = "LOG4J_ERROR", .value = LTTNG_LOGLEVEL_LOG4J_ERROR },
	{ .name = "ERROR", .value = LTTNG_LOGLEVEL_LOG4J_ERROR },
	{ .name = "LOG4J_WARN", .value = LTTNG_LOGLEVEL_LOG4J_WARN },
	{ .name = "WARN", .value = LTTNG_LOGLEVEL_LOG4J_WARN },
	{ .name = "LOG4J_INFO", .value = LTTNG_LOGLEVEL_LOG4J_INFO },
	{ .name = "INFO", .value = LTTNG_LOGLEVEL_LOG4J_INFO },
	{ .name = "LOG4J_DEBUG", .value = LTTNG_LOGLEVEL_LOG4J_DEBUG },
	{ .name = "DEBUG", .value = LTTNG_LOGLEVEL_LOG4J_DEBUG },
	{ .name = "LOG4J_TRACE", .value = LTTNG_LOGLEVEL_LOG4J_TRACE },
	{ .name = "TRACE", .value = LTTNG_LOGLEVEL_LOG4J_TRACE },
	{ .name = "LOG4J_ALL", .value = LTTNG_LOGLEVEL_LOG4J_ALL },
	{ .name = "ALL", .value = LTTNG_LOGLEVEL_LOG4J_ALL },
};

static
const struct loglevel_name_value loglevel_jul_values[] = {
	{ .name = "JUL_OFF", .value = LTTNG_LOGLEVEL_JUL_OFF },
	{ .name = "OFF", .value = LTTNG_LOGLEVEL_JUL_OFF },
	{ .name = "JUL_SEVERE", .value = LTTNG_LOGLEVEL_JUL_SEVERE },
	{ .name = "SEVERE", .value = LTTNG_LOGLEVEL_JUL_SEVERE },
	{ .name = "JUL_WARNING", .value = LTTNG_LOGLEVEL_JUL_WARNING },
	{ .name = "WARNING", .value = LTTNG_LOGLEVEL_JUL_WARNING },
	{ .name = "JUL_INFO", .value = LTTNG_LOGLEVEL_JUL_INFO },
	{ .name = "INFO", .value = LTTNG_LOGLEVEL_JUL_INFO },
	{ .name = "JUL_CONFIG", .value = LTTNG_LOGLEVEL_JUL_CONFIG },
	{ .name = "CONFIG", .value = LTTNG_LOGLEVEL_JUL_CONFIG },
	{ .name = "JUL_FINE", .value = LTTNG_LOGLEVEL_JUL_FINE },
	{ .name = "FINE", .value = LTTNG_LOGLEVEL_JUL_FINE },
	{ .name = "JUL_FINER", .value = LTTNG_LOGLEVEL_JUL_FINER },
	{ .name = "FINER", .value = LTTNG_LOGLEVEL_JUL_FINER },
	{ .name = "JUL_FINEST", .value = LTTNG_LOGLEVEL_JUL_FINEST },
	{ .name = "FINEST", .value = LTTNG_LOGLEVEL_JUL_FINEST },
	{ .name = "JUL_ALL", .value = LTTNG_LOGLEVEL_JUL_ALL },
	{ .name = "ALL", .value = LTTNG_LOGLEVEL_JUL_ALL },
};

static
const struct loglevel_name_value loglevel_python_values[] = {
	{ .name = "PYTHON_CRITICAL", .value = LTTNG_LOGLEVEL_PYTHON_CRITICAL },
	{ .name = "CRITICAL", .value = LTTNG_LOGLEVEL_PYTHON_CRITICAL },
	{ .name = "PYTHON_ERROR", .value = LTTNG_LOGLEVEL_PYTHON_ERROR },
	{ .name = "ERROR", .value = LTTNG_LOGLEVEL_PYTHON_ERROR },
	{ .name = "PYTHON_WARNING", .value = LTTNG_LOGLEVEL_PYTHON_WARNING },
	{ .name = "WARNING", .value = LTTNG_LOGLEVEL_PYTHON_WARNING },
	{ .name = "PYTHON_INFO", .value = LTTNG_LOGLEVEL_PYTHON_INFO },
	{ .name = "INFO", .value = LTTNG_LOGLEVEL_PYTHON_INFO },
	{ .name = "PYTNON_DEBUG", .value = LTTNG_LOGLEVEL_PYTHON_DEBUG },
	{ .name = "DEBUG", .value = LTTNG_LOGLEVEL_PYTHON_DEBUG },
	{ .name = "PYTHON_NOTSET", .value = LTTNG_LOGLEVEL_PYTHON_NOTSET },
	{ .name = "NOTSET", .value = LTTNG_LOGLEVEL_PYTHON_NOTSET },
};

static
bool string_equal_insensitive(const char *a, const char *b)
{
	bool result;

	assert(a && b);

	while (*a && *b) {
		if (toupper(*a) != toupper(*b)) {
			result = false;
			goto end;
		}

		a++;
		b++;
	}

	/* If a and b don't have the same length, consider them unequal. */
	result = *a == *b;

end:
	return result;
}

static
int lookup_value_from_name(const struct loglevel_name_value values[],
		size_t values_count, const char *name)
{
	size_t i;
	int ret = -1;

	if (!name) {
		goto end;
	}

	for (i = 0; i < values_count; i++) {
		if (string_equal_insensitive(values[i].name, name)) {
			/* Match found. */
			ret = values[i].value;
			goto end;
		}
	}

end:
	return ret;
}

LTTNG_HIDDEN
int loglevel_name_to_value(const char *name, enum lttng_loglevel *loglevel)
{
	int ret = lookup_value_from_name(loglevel_values,
			ARRAY_SIZE(loglevel_values), name);

	if (ret >= 0) {
		*loglevel = (typeof(*loglevel)) ret;
		ret = 0;
	}

	return ret;
}

LTTNG_HIDDEN
int loglevel_log4j_name_to_value(
		const char *name, enum lttng_loglevel_log4j *loglevel)
{
	int ret = lookup_value_from_name(loglevel_log4j_values,
			ARRAY_SIZE(loglevel_log4j_values),
			name);

	if (ret >= 0) {
		*loglevel = (typeof(*loglevel)) ret;
		ret = 0;
	}

	return ret;
}

LTTNG_HIDDEN
int loglevel_jul_name_to_value(
		const char *name, enum lttng_loglevel_jul *loglevel)
{
	int ret = lookup_value_from_name(loglevel_jul_values,
			ARRAY_SIZE(loglevel_jul_values),
			name);

	if (ret >= 0) {
		*loglevel = (typeof(*loglevel)) ret;
		ret = 0;
	}

	return ret;
}

LTTNG_HIDDEN
int loglevel_python_name_to_value(
		const char *name, enum lttng_loglevel_python *loglevel)
{
	int ret = lookup_value_from_name(loglevel_python_values,
			ARRAY_SIZE(loglevel_python_values),
			name);

	if (ret >= 0) {
		*loglevel = (typeof(*loglevel)) ret;
		ret = 0;
	}

	return ret;
}
