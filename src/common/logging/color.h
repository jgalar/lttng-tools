/*
 * LTTng color handling functions
 *
 * Copyright 2016 Philippe Proulx <pproulx@efficios.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef LTTNG_COLOR_INTERNAL_H
#define LTTNG_COLOR_INTERNAL_H

#include <common/macros.h>
#include <stdbool.h>

#define LTTNG_COLOR_RESET              "\033[0m"
#define LTTNG_COLOR_BOLD               "\033[1m"
#define LTTNG_COLOR_FG_DEFAULT         "\033[39m"
#define LTTNG_COLOR_FG_RED             "\033[31m"
#define LTTNG_COLOR_FG_GREEN           "\033[32m"
#define LTTNG_COLOR_FG_YELLOW          "\033[33m"
#define LTTNG_COLOR_FG_BLUE            "\033[34m"
#define LTTNG_COLOR_FG_MAGENTA         "\033[35m"
#define LTTNG_COLOR_FG_CYAN            "\033[36m"
#define LTTNG_COLOR_FG_LIGHT_GRAY      "\033[37m"
#define LTTNG_COLOR_BG_DEFAULT         "\033[49m"
#define LTTNG_COLOR_BG_RED             "\033[41m"
#define LTTNG_COLOR_BG_GREEN           "\033[42m"
#define LTTNG_COLOR_BG_YELLOW          "\033[43m"
#define LTTNG_COLOR_BG_BLUE            "\033[44m"
#define LTTNG_COLOR_BG_MAGENTA         "\033[45m"
#define LTTNG_COLOR_BG_CYAN            "\033[46m"
#define LTTNG_COLOR_BG_LIGHT_GRAY      "\033[47m"

/*
 * Returns `true` if terminal color codes are supported for this
 * process.
 */
LTTNG_HIDDEN
bool lttng_colors_supported(void);

LTTNG_HIDDEN
const char *lttng_color_reset(void);

LTTNG_HIDDEN
const char *lttng_color_bold(void);

LTTNG_HIDDEN
const char *lttng_color_fg_default(void);

LTTNG_HIDDEN
const char *lttng_color_fg_red(void);

LTTNG_HIDDEN
const char *lttng_color_fg_green(void);

LTTNG_HIDDEN
const char *lttng_color_fg_yellow(void);

LTTNG_HIDDEN
const char *lttng_color_fg_blue(void);

LTTNG_HIDDEN
const char *lttng_color_fg_magenta(void);

LTTNG_HIDDEN
const char *lttng_color_fg_cyan(void);

LTTNG_HIDDEN
const char *lttng_color_fg_light_gray(void);

LTTNG_HIDDEN
const char *lttng_color_bg_default(void);

LTTNG_HIDDEN
const char *lttng_color_bg_red(void);

LTTNG_HIDDEN
const char *lttng_color_bg_green(void);

LTTNG_HIDDEN
const char *lttng_color_bg_yellow(void);

LTTNG_HIDDEN
const char *lttng_color_bg_blue(void);

LTTNG_HIDDEN
const char *lttng_color_bg_magenta(void);

LTTNG_HIDDEN
const char *lttng_color_bg_cyan(void);

LTTNG_HIDDEN
const char *lttng_color_bg_light_gray(void);

#endif /* LTTNG_COLOR_INTERNAL_H */
