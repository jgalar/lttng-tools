/*
 * Copyright (C) 2020 - EfficiOS, inc.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License, version 2.1 only,
 * as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef LTTNG_DOMAIN_INTERNAL_H
#define LTTNG_DOMAIN_INTERNAL_H

#include "lttng/domain.h"
#include "common/macros.h"

#ifdef __cplusplus
extern "C" {
#endif

LTTNG_HIDDEN
const char *lttng_domain_type_str(enum lttng_domain_type domain_type);

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_DOMAIN_INTERNAL_H */
