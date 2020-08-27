/*
 * Copyright (C) 2021 Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include "loglevel.h"
#include <string.h>
#include <ctype.h>
#include <assert.h>

#define LOGLEVEL_NAME_VALUE_ARRAY_COUNT(name) (sizeof(name) / sizeof(struct loglevel_name_value))

struct loglevel_name_value {
	const char *name;
	int value;
};

static const struct loglevel_name_value loglevel_values[] = {
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

static const struct loglevel_name_value loglevel_log4j_values[] = {
"LOG4J_OFF" LTTNG_LOGLEVEL_LOG4J_OFF
"OFF" LTTNG_LOGLEVEL_LOG4J_OFF

"LOG4J_FATAL" LTTNG_LOGLEVEL_LOG4J_FATAL
"FATAL" LTTNG_LOGLEVEL_LOG4J_FATAL

"LOG4J_ERROR" LTTNG_LOGLEVEL_LOG4J_ERROR
"ERROR" LTTNG_LOGLEVEL_LOG4J_ERROR

"LOG4J_WARN" LTTNG_LOGLEVEL_LOG4J_WARN
"WARN" LTTNG_LOGLEVEL_LOG4J_WARN

"LOG4J_INFO" LTTNG_LOGLEVEL_LOG4J_INFO
"INFO" LTTNG_LOGLEVEL_LOG4J_INFO

"LOG4J_DEBUG" LTTNG_LOGLEVEL_LOG4J_DEBUG
"DEBUG" LTTNG_LOGLEVEL_LOG4J_DEBUG

"LOG4J_TRACE" LTTNG_LOGLEVEL_LOG4J_TRACE
"TRACE" LTTNG_LOGLEVEL_LOG4J_TRACE

"LOG4J_ALL" LTTNG_LOGLEVEL_LOG4J_ALL
"ALL" LTTNG_LOGLEVEL_LOG4J_ALL

};

static
bool string_equal_insensitive(const char *a, const char *b)
{
	size_t i;
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
			LOGLEVEL_NAME_VALUE_ARRAY_COUNT(loglevel_values), name);

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
			LOGLEVEL_NAME_VALUE_ARRAY_COUNT(loglevel_log4j_values),
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
			LOGLEVEL_NAME_VALUE_ARRAY_COUNT(loglevel_jul_values),
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
			LOGLEVEL_NAME_VALUE_ARRAY_COUNT(loglevel_python_values),
			name);

	if (ret >= 0) {
		*loglevel = (typeof(*loglevel)) ret;
		ret = 0;
	}

	return ret;
}

LTTNG_HIDDEN
int loglevel_log4j_name_to_value(const char *name)
{
	int i = 0;
	char str[LTTNG_SYMBOL_NAME_LEN];

	if (!name || strlen(name) == 0) {
		return -1;
	}

	/*
	 * Loop up to LTTNG_SYMBOL_NAME_LEN minus one because the NULL bytes is
	 * added at the end of the loop so a the upper bound we avoid the overflow.
	 */
	while (i < (LTTNG_SYMBOL_NAME_LEN - 1) && name[i] != '\0') {
		str[i] = toupper(name[i]);
		i++;
	}
	str[i] = '\0';

	if (!strcmp(str, "LOG4J_OFF") || !strcmp(str, "OFF")) {
		return LTTNG_LOGLEVEL_LOG4J_OFF;
	} else if (!strcmp(str, "LOG4J_FATAL") || !strcmp(str, "FATAL")) {
		return LTTNG_LOGLEVEL_LOG4J_FATAL;
	} else if (!strcmp(str, "LOG4J_ERROR") || !strcmp(str, "ERROR")) {
		return LTTNG_LOGLEVEL_LOG4J_ERROR;
	} else if (!strcmp(str, "LOG4J_WARN") || !strcmp(str, "WARN")) {
		return LTTNG_LOGLEVEL_LOG4J_WARN;
	} else if (!strcmp(str, "LOG4J_INFO") || !strcmp(str, "INFO")) {
		return LTTNG_LOGLEVEL_LOG4J_INFO;
	} else if (!strcmp(str, "LOG4J_DEBUG") || !strcmp(str, "DEBUG")) {
		return LTTNG_LOGLEVEL_LOG4J_DEBUG;
	} else if (!strcmp(str, "LOG4J_TRACE") || !strcmp(str, "TRACE")) {
		return LTTNG_LOGLEVEL_LOG4J_TRACE;
	} else if (!strcmp(str, "LOG4J_ALL") || !strcmp(str, "ALL")) {
		return LTTNG_LOGLEVEL_LOG4J_ALL;
	} else {
		return -1;
	}
}

LTTNG_HIDDEN
int loglevel_jul_name_to_value(const char *name)
{
	int i = 0;
	char str[LTTNG_SYMBOL_NAME_LEN];

	if (!name || strlen(name) == 0) {
		return -1;
	}

	/*
	 * Loop up to LTTNG_SYMBOL_NAME_LEN minus one because the NULL bytes is
	 * added at the end of the loop so a the upper bound we avoid the overflow.
	 */
	while (i < (LTTNG_SYMBOL_NAME_LEN - 1) && name[i] != '\0') {
		str[i] = toupper(name[i]);
		i++;
	}
	str[i] = '\0';

	if (!strcmp(str, "JUL_OFF") || !strcmp(str, "OFF")) {
		return LTTNG_LOGLEVEL_JUL_OFF;
	} else if (!strcmp(str, "JUL_SEVERE") || !strcmp(str, "SEVERE")) {
		return LTTNG_LOGLEVEL_JUL_SEVERE;
	} else if (!strcmp(str, "JUL_WARNING") || !strcmp(str, "WARNING")) {
		return LTTNG_LOGLEVEL_JUL_WARNING;
	} else if (!strcmp(str, "JUL_INFO") || !strcmp(str, "INFO")) {
		return LTTNG_LOGLEVEL_JUL_INFO;
	} else if (!strcmp(str, "JUL_CONFIG") || !strcmp(str, "CONFIG")) {
		return LTTNG_LOGLEVEL_JUL_CONFIG;
	} else if (!strcmp(str, "JUL_FINE") || !strcmp(str, "FINE")) {
		return LTTNG_LOGLEVEL_JUL_FINE;
	} else if (!strcmp(str, "JUL_FINER") || !strcmp(str, "FINER")) {
		return LTTNG_LOGLEVEL_JUL_FINER;
	} else if (!strcmp(str, "JUL_FINEST") || !strcmp(str, "FINEST")) {
		return LTTNG_LOGLEVEL_JUL_FINEST;
	} else if (!strcmp(str, "JUL_ALL") || !strcmp(str, "ALL")) {
		return LTTNG_LOGLEVEL_JUL_ALL;
	} else {
		return -1;
	}
}

LTTNG_HIDDEN
int loglevel_python_name_to_value(const char *name)
{
	int i = 0;
	char str[LTTNG_SYMBOL_NAME_LEN];

	if (!name || strlen(name) == 0) {
		return -1;
	}

	/*
	 * Loop up to LTTNG_SYMBOL_NAME_LEN minus one because the NULL bytes is
	 * added at the end of the loop so a the upper bound we avoid the overflow.
	 */
	while (i < (LTTNG_SYMBOL_NAME_LEN - 1) && name[i] != '\0') {
		str[i] = toupper(name[i]);
		i++;
	}
	str[i] = '\0';

	if (!strcmp(str, "PYTHON_CRITICAL") || !strcmp(str, "CRITICAL")) {
		return LTTNG_LOGLEVEL_PYTHON_CRITICAL;
	} else if (!strcmp(str, "PYTHON_ERROR") || !strcmp(str, "ERROR")) {
		return LTTNG_LOGLEVEL_PYTHON_ERROR;
	} else if (!strcmp(str, "PYTHON_WARNING") || !strcmp(str, "WARNING")) {
		return LTTNG_LOGLEVEL_PYTHON_WARNING;
	} else if (!strcmp(str, "PYTHON_INFO") || !strcmp(str, "INFO")) {
		return LTTNG_LOGLEVEL_PYTHON_INFO;
	} else if (!strcmp(str, "PYTNON_DEBUG") || !strcmp(str, "DEBUG")) {
		return LTTNG_LOGLEVEL_PYTHON_DEBUG;
	} else if (!strcmp(str, "PYTHON_NOTSET") || !strcmp(str, "NOTSET")) {
		return LTTNG_LOGLEVEL_PYTHON_NOTSET;
	} else {
		return -1;
	}
}
