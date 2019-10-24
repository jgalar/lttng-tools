/*
 * Copyright (c) 2018 EfficiOS Inc. and Linux Foundation
 * Copyright (c) 2018 Philippe Proulx <pproulx@efficios.com>
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

#include <common/macros.h>
#include <common/logging/color.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *lttng_color_code_reset = "";
static const char *lttng_color_code_bold = "";
static const char *lttng_color_code_fg_default = "";
static const char *lttng_color_code_fg_red = "";
static const char *lttng_color_code_fg_green = "";
static const char *lttng_color_code_fg_yellow = "";
static const char *lttng_color_code_fg_blue = "";
static const char *lttng_color_code_fg_magenta = "";
static const char *lttng_color_code_fg_cyan = "";
static const char *lttng_color_code_fg_light_gray = "";
static const char *lttng_color_code_bg_default = "";
static const char *lttng_color_code_bg_red = "";
static const char *lttng_color_code_bg_green = "";
static const char *lttng_color_code_bg_yellow = "";
static const char *lttng_color_code_bg_blue = "";
static const char *lttng_color_code_bg_magenta = "";
static const char *lttng_color_code_bg_cyan = "";
static const char *lttng_color_code_bg_light_gray = "";

static
void __attribute__((constructor)) lttng_color_ctor(void)
{
	if (lttng_colors_supported()) {
		lttng_color_code_reset = LTTNG_COLOR_RESET;
		lttng_color_code_bold = LTTNG_COLOR_BOLD;
		lttng_color_code_fg_default = LTTNG_COLOR_FG_DEFAULT;
		lttng_color_code_fg_red = LTTNG_COLOR_FG_RED;
		lttng_color_code_fg_green = LTTNG_COLOR_FG_GREEN;
		lttng_color_code_fg_yellow = LTTNG_COLOR_FG_YELLOW;
		lttng_color_code_fg_blue = LTTNG_COLOR_FG_BLUE;
		lttng_color_code_fg_magenta = LTTNG_COLOR_FG_MAGENTA;
		lttng_color_code_fg_cyan = LTTNG_COLOR_FG_CYAN;
		lttng_color_code_fg_light_gray = LTTNG_COLOR_FG_LIGHT_GRAY;
		lttng_color_code_bg_default = LTTNG_COLOR_BG_DEFAULT;
		lttng_color_code_bg_red = LTTNG_COLOR_BG_RED;
		lttng_color_code_bg_green = LTTNG_COLOR_BG_GREEN;
		lttng_color_code_bg_yellow = LTTNG_COLOR_BG_YELLOW;
		lttng_color_code_bg_blue = LTTNG_COLOR_BG_BLUE;
		lttng_color_code_bg_magenta = LTTNG_COLOR_BG_MAGENTA;
		lttng_color_code_bg_cyan = LTTNG_COLOR_BG_CYAN;
		lttng_color_code_bg_light_gray = LTTNG_COLOR_BG_LIGHT_GRAY;
	}
}

static
bool isarealtty(int fd)
{
	bool istty = false;
	struct stat tty_stats;

	if (!isatty(fd)) {
		/* Not a TTY */
		goto end;
	}

	if (fstat(fd, &tty_stats) == 0) {
		if (!S_ISCHR(tty_stats.st_mode)) {
			/* Not a character device: not a TTY */
			goto end;
		}
	}

	istty = true;

end:
	return istty;
}

LTTNG_HIDDEN
bool lttng_colors_supported(void)
{
	static bool supports_colors = false;
	static bool supports_colors_set = false;
	const char *term_env_var;
	const char *term_color_env_var;

	if (supports_colors_set) {
		goto end;
	}

	supports_colors_set = true;

	/*
	 * `LTTNG_TERM_COLOR` environment variable always overrides
	 * the automatic color support detection.
	 */
	term_color_env_var = getenv("LTTNG_TERM_COLOR");
	if (term_color_env_var) {
		if (strcmp(term_color_env_var, "always") == 0) {
			/* Force colors */
			supports_colors = true;
		} else if (strcmp(term_color_env_var, "never") == 0) {
			/* Force no colors */
			goto end;
		}
	}

	/* We need a compatible, known terminal */
	term_env_var = getenv("TERM");
	if (!term_env_var) {
		goto end;
	}

	if (strncmp(term_env_var, "xterm", 5) != 0 &&
			strncmp(term_env_var, "rxvt", 4) != 0 &&
			strncmp(term_env_var, "konsole", 7) != 0 &&
			strncmp(term_env_var, "gnome", 5) != 0 &&
			strncmp(term_env_var, "screen", 5) != 0 &&
			strncmp(term_env_var, "tmux", 4) != 0 &&
			strncmp(term_env_var, "putty", 5) != 0) {
		goto end;
	}

	/* Both standard output and error streams need to be TTYs */
	if (!isarealtty(STDOUT_FILENO) || !isarealtty(STDERR_FILENO)) {
		goto end;
	}

	supports_colors = true;

end:
	return supports_colors;
}

LTTNG_HIDDEN
const char *lttng_color_reset(void)
{
	return lttng_color_code_reset;
}

LTTNG_HIDDEN
const char *lttng_color_bold(void)
{
	return lttng_color_code_bold;
}

LTTNG_HIDDEN
const char *lttng_color_fg_default(void)
{
	return lttng_color_code_fg_default;
}

LTTNG_HIDDEN
const char *lttng_color_fg_red(void)
{
	return lttng_color_code_fg_red;
}

LTTNG_HIDDEN
const char *lttng_color_fg_green(void)
{
	return lttng_color_code_fg_green;
}

LTTNG_HIDDEN
const char *lttng_color_fg_yellow(void)
{
	return lttng_color_code_fg_yellow;
}

LTTNG_HIDDEN
const char *lttng_color_fg_blue(void)
{
	return lttng_color_code_fg_blue;
}

LTTNG_HIDDEN
const char *lttng_color_fg_magenta(void)
{
	return lttng_color_code_fg_magenta;
}

LTTNG_HIDDEN
const char *lttng_color_fg_cyan(void)
{
	return lttng_color_code_fg_cyan;
}

LTTNG_HIDDEN
const char *lttng_color_fg_light_gray(void)
{
	return lttng_color_code_fg_light_gray;
}

LTTNG_HIDDEN
const char *lttng_color_bg_default(void)
{
	return lttng_color_code_bg_default;
}

LTTNG_HIDDEN
const char *lttng_color_bg_red(void)
{
	return lttng_color_code_bg_red;
}

LTTNG_HIDDEN
const char *lttng_color_bg_green(void)
{
	return lttng_color_code_bg_green;
}

LTTNG_HIDDEN
const char *lttng_color_bg_yellow(void)
{
	return lttng_color_code_bg_yellow;
}

LTTNG_HIDDEN
const char *lttng_color_bg_blue(void)
{
	return lttng_color_code_bg_blue;
}

LTTNG_HIDDEN
const char *lttng_color_bg_magenta(void)
{
	return lttng_color_code_bg_magenta;
}

LTTNG_HIDDEN
const char *lttng_color_bg_cyan(void)
{
	return lttng_color_code_bg_cyan;
}

LTTNG_HIDDEN
const char *lttng_color_bg_light_gray(void)
{
	return lttng_color_code_bg_light_gray;
}
