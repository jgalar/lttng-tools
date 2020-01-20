/*
 * Copyright (C) 2020 - EfficiOS, inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2 only, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "lttng/domain-internal.h"
#include "common/macros.h"

LTTNG_HIDDEN
const char *lttng_domain_type_str(enum lttng_domain_type domain_type)
{
	switch (domain_type) {
	case LTTNG_DOMAIN_NONE:
		return "none";

	case LTTNG_DOMAIN_KERNEL:
		return "kernel";

	case LTTNG_DOMAIN_UST:
		return "ust";

	case LTTNG_DOMAIN_JUL:
		return "jul";

	case LTTNG_DOMAIN_LOG4J:
		return "log4j";

	case LTTNG_DOMAIN_PYTHON:
		return "python";

	default:
		return "???";
	}
}
