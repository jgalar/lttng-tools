/*
 * This is zf_log.h, modified with LTTng prefixes.
 * See <https://github.com/wonder-mice/zf_log/>.
 * See logging/LICENSE in the LTTng source tree.
 */

#pragma once

#ifndef LTTNG_LOGGING_INTERNAL_H
#define LTTNG_LOGGING_INTERNAL_H

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <common/logging/logging-defs.h>
#include <common/macros.h>
#include <assert.h>

/* To detect incompatible changes you can define LTTNG_LOG_VERSION_REQUIRED to be
 * the current value of LTTNG_LOG_VERSION before including this file (or via
 * compiler command line):
 *
 *   #define LTTNG_LOG_VERSION_REQUIRED 4
 *   #include "logging.h"
 *
 * Compilation will fail when included file has different version.
 */
#define LTTNG_LOG_VERSION 4
#if defined(LTTNG_LOG_VERSION_REQUIRED)
	#if LTTNG_LOG_VERSION_REQUIRED != LTTNG_LOG_VERSION
		#error different lttng_log version required
	#endif
#endif

/* Log level guideline:
 * - LTTNG_LOG_FATAL - happened something impossible and absolutely unexpected.
 *   Process can't continue and must be terminated.
 *   Example: division by zero, unexpected modifications from other thread.
 * - LTTNG_LOG_ERROR - happened something possible, but highly unexpected. The
 *   process is able to recover and continue execution.
 *   Example: out of memory (could also be FATAL if not handled properly).
 * - LTTNG_LOG_WARNING - happened something that *usually* should not happen and
 *   significantly changes application behavior for some period of time.
 *   Example: configuration file not found, auth error.
 * - LTTNG_LOG_INFO - happened significant life cycle event or major state
 *   transition.
 *   Example: app started, user logged in.
 * - LTTNG_LOG_DEBUG - minimal set of events that could help to reconstruct the
 *   execution path. Usually disabled in release builds.
 * - LTTNG_LOG_TRACE - all other events. Usually disabled in release builds.
 *
 * *Ideally*, log file of debugged, well tested, production ready application
 * should be empty or very small. Choosing a right log level is as important as
 * providing short and self descriptive log message.
 */
#define LTTNG_LOG_TRACE   __LTTNG_LOGGING_LEVEL_TRACE
#define LTTNG_LOG_DEBUG   __LTTNG_LOGGING_LEVEL_DEBUG
#define LTTNG_LOG_INFO    __LTTNG_LOGGING_LEVEL_INFO
#define LTTNG_LOG_WARNING __LTTNG_LOGGING_LEVEL_WARNING
#define LTTNG_LOG_ERROR   __LTTNG_LOGGING_LEVEL_ERROR
#define LTTNG_LOG_FATAL   __LTTNG_LOGGING_LEVEL_FATAL
#define LTTNG_LOG_NONE    __LTTNG_LOGGING_LEVEL_NONE

/* "Current" log level is a compile time check and has no runtime overhead. Log
 * level that is below current log level it said to be "disabled".
 * Otherwise, it's "enabled". Log messages that are disabled has no
 * runtime overhead - they are converted to no-op by preprocessor and
 * then eliminated by compiler. Current log level is configured per
 * compilation module (.c/.cpp/.m file) by defining LTTNG_LOG_DEF_LEVEL or
 * LTTNG_MINIMAL_LOG_LEVEL. LTTNG_MINIMAL_LOG_LEVEL has higer priority and
 * when defined overrides value provided by LTTNG_LOG_DEF_LEVEL.
 *
 * Common practice is to define default current log level with LTTNG_LOG_DEF_LEVEL
 * in build script (e.g. Makefile, CMakeLists.txt, gyp, etc.) for the entire
 * project or target:
 *
 *   CC_ARGS := -DLTTNG_LOG_DEF_LEVEL=LTTNG_LOG_INFO
 *
 * And when necessary to override it with LTTNG_MINIMAL_LOG_LEVEL in .c/.cpp/.m files
 * before including lttng_log.h:
 *
 *   #define LTTNG_MINIMAL_LOG_LEVEL LTTNG_LOG_TRACE
 *   #include "logging.h"
 *
 * If both LTTNG_LOG_DEF_LEVEL and LTTNG_MINIMAL_LOG_LEVEL are undefined, then
 * LTTNG_LOG_INFO will be used for release builds (LTTNG_DEBUG_MODE is NOT
 * defined) and LTTNG_LOG_DEBUG otherwise (LTTNG_DEBUG_MODE is defined).
 */
#if defined(LTTNG_MINIMAL_LOG_LEVEL)
	#define _LTTNG_MINIMAL_LOG_LEVEL LTTNG_MINIMAL_LOG_LEVEL
#elif defined(LTTNG_LOG_DEF_LEVEL)
	#define _LTTNG_MINIMAL_LOG_LEVEL LTTNG_LOG_DEF_LEVEL
#else
	#ifdef LTTNG_DEBUG_MODE
		#define _LTTNG_MINIMAL_LOG_LEVEL LTTNG_LOG_DEBUG
	#else
		#define _LTTNG_MINIMAL_LOG_LEVEL LTTNG_LOG_INFO
	#endif
#endif

/* "Output" log level is a runtime check. When log level is below output log
 * level it said to be "turned off" (or just "off" for short). Otherwise
 * it's "turned on" (or just "on"). Log levels that were "disabled" (see
 * LTTNG_MINIMAL_LOG_LEVEL and LTTNG_LOG_DEF_LEVEL) can't be "turned on", but
 * "enabled" log levels could be "turned off". Only messages with log
 * level which is "turned on" will reach output facility. All other
 * messages will be ignored (and their arguments will not be evaluated).
 * Output log level is a global property and configured per process
 * using lttng_log_set_output_level() function which can be called at any
 * time.
 *
 * Though in some cases it could be useful to configure output log level per
 * compilation module or per library. There are two ways to achieve that:
 * - Define LTTNG_LOG_OUTPUT_LEVEL to expresion that evaluates to desired output
 *   log level.
 * - Copy lttng_log.h and lttng_log.c files into your library and build it with
 *   LTTNG_LOG_LIBRARY_PREFIX defined to library specific prefix. See
 *   LTTNG_LOG_LIBRARY_PREFIX for more details.
 *
 * When defined, LTTNG_LOG_OUTPUT_LEVEL must evaluate to integral value
 * that corresponds to desired output log level. Use it only when
 * compilation module is required to have output log level which is
 * different from global output log level set by
 * lttng_log_set_output_level() function. For other cases, consider
 * defining LTTNG_MINIMAL_LOG_LEVEL or using lttng_log_set_output_level()
 * function.
 *
 * Example:
 *
 *   #define LTTNG_LOG_OUTPUT_LEVEL g_module_log_level
 *   #include "logging.h"
 *   static int g_module_log_level = LTTNG_LOG_INFO;
 *   static void foo() {
 *       LTTNG_LOGI("Will check g_module_log_level for output log level");
 *   }
 *   void debug_log(bool on) {
 *       g_module_log_level = on? LTTNG_LOG_DEBUG: LTTNG_LOG_INFO;
 *   }
 *
 * Note on performance. This expression will be evaluated each time
 * message is logged (except when message log level is "disabled" - see
 * LTTNG_MINIMAL_LOG_LEVEL for details). Keep this expression as simple as
 * possible, otherwise it will not only add runtime overhead, but also
 * will increase size of call site (which will result in larger
 * executable). The prefered way is to use integer variable (as in
 * example above). If structure must be used, log_level field must be
 * the first field in this structure:
 *
 *   #define LTTNG_LOG_OUTPUT_LEVEL (g_config.log_level)
 *   #include "logging.h"
 *   struct config {
 *       int log_level;
 *       unsigned other_field;
 *       [...]
 *   };
 *   static config g_config = {LTTNG_LOG_INFO, 0, ...};
 *
 * This allows compiler to generate more compact load instruction (no need to
 * specify offset since it's zero). Calling a function to get output log level
 * is generaly a bad idea, since it will increase call site size and runtime
 * overhead even further.
 */
#if defined(LTTNG_LOG_OUTPUT_LEVEL)
	#define _LTTNG_LOG_OUTPUT_LEVEL LTTNG_LOG_OUTPUT_LEVEL
#else
	/*
	 * We disallow this to make sure modules always
	 * have their own local log level.
	 */
	#error No log level symbol specified: please define LTTNG_LOG_OUTPUT_LEVEL before including this header.
#endif

/* "Tag" is a compound string that could be associated with a log message. It
 * consists of tag prefix and tag (both are optional).
 *
 * Tag prefix is a global property and configured per process using
 * lttng_log_set_tag_prefix() function. Tag prefix identifies context in which
 * component or module is running (e.g. process name). For example, the same
 * library could be used in both client and server processes that work on the
 * same machine. Tag prefix could be used to easily distinguish between them.
 * For more details about tag prefix see lttng_log_set_tag_prefix() function. Tag
 * prefix
 *
 * Tag identifies component or module. It is configured per compilation module
 * (.c/.cpp/.m file) by defining LTTNG_LOG_TAG or LTTNG_LOG_DEF_TAG. LTTNG_LOG_TAG has
 * higer priority and when defined overrides value provided by LTTNG_LOG_DEF_TAG.
 * When defined, value must evaluate to (const char *), so for strings double
 * quotes must be used.
 *
 * Default tag could be defined with LTTNG_LOG_DEF_TAG in build script (e.g.
 * Makefile, CMakeLists.txt, gyp, etc.) for the entire project or target:
 *
 *   CC_ARGS := -DLTTNG_LOG_DEF_TAG=\"MISC\"
 *
 * And when necessary could be overriden with LTTNG_LOG_TAG in .c/.cpp/.m files
 * before including lttng_log.h:
 *
 *   #define LTTNG_LOG_TAG "MAIN"
 *   #include "logging.h"
 *
 * If both LTTNG_LOG_DEF_TAG and LTTNG_LOG_TAG are undefined no tag will be added to
 * the log message (tag prefix still could be added though).
 *
 * Output example:
 *
 *   04-29 22:43:20.244 40059  1299 I hello.MAIN Number of arguments: 1
 *                                    |     |
 *                                    |     +- tag (e.g. module)
 *                                    +- tag prefix (e.g. process name)
 */
#if defined(LTTNG_LOG_TAG)
	#define _LTTNG_LOG_TAG LTTNG_LOG_TAG
#elif defined(LTTNG_LOG_DEF_TAG)
	#define _LTTNG_LOG_TAG LTTNG_LOG_DEF_TAG
#else
	#define _LTTNG_LOG_TAG 0
#endif

/* Source location is part of a log line that describes location (function or
 * method name, file name and line number, e.g. "runloop@main.cpp:68") of a
 * log statement that produced it.
 * Source location formats are:
 * - LTTNG_LOG_SRCLOC_NONE - don't add source location to log line.
 * - LTTNG_LOG_SRCLOC_SHORT - add source location in short form (file and line
 *   number, e.g. "@main.cpp:68").
 * - LTTNG_LOG_SRCLOC_LONG - add source location in long form (function or method
 *   name, file and line number, e.g. "runloop@main.cpp:68").
 */
#define LTTNG_LOG_SRCLOC_NONE  0
#define LTTNG_LOG_SRCLOC_SHORT 1
#define LTTNG_LOG_SRCLOC_LONG  2

#define _LTTNG_LOG_SRCLOC LTTNG_LOG_SRCLOC_LONG

#if LTTNG_LOG_SRCLOC_LONG == _LTTNG_LOG_SRCLOC
	#define _LTTNG_LOG_SRCLOC_FUNCTION _LTTNG_LOG_FUNCTION
#else
	#define _LTTNG_LOG_SRCLOC_FUNCTION 0
#endif

/* Censoring provides conditional logging of secret information, also known as
 * Personally Identifiable Information (PII) or Sensitive Personal Information
 * (SPI). Censoring can be either enabled (LTTNG_LOG_CENSORED) or disabled
 * (LTTNG_LOG_UNCENSORED). When censoring is enabled, log statements marked as
 * "secrets" will be ignored and will have zero overhead (arguments also will
 * not be evaluated).
 */
#define LTTNG_LOG_CENSORED   1
#define LTTNG_LOG_UNCENSORED 0

/* Censoring is configured per compilation module (.c/.cpp/.m file) by defining
 * LTTNG_LOG_DEF_CENSORING or LTTNG_LOG_CENSORING. LTTNG_LOG_CENSORING has higer priority
 * and when defined overrides value provided by LTTNG_LOG_DEF_CENSORING.
 *
 * Common practice is to define default censoring with LTTNG_LOG_DEF_CENSORING in
 * build script (e.g. Makefile, CMakeLists.txt, gyp, etc.) for the entire
 * project or target:
 *
 *   CC_ARGS := -DLTTNG_LOG_DEF_CENSORING=LTTNG_LOG_CENSORED
 *
 * And when necessary to override it with LTTNG_LOG_CENSORING in .c/.cpp/.m files
 * before including lttng_log.h (consider doing it only for debug purposes and be
 * very careful not to push such temporary changes to source control):
 *
 *   #define LTTNG_LOG_CENSORING LTTNG_LOG_UNCENSORED
 *   #include "logging.h"
 *
 * If both LTTNG_LOG_DEF_CENSORING and LTTNG_LOG_CENSORING are undefined, then
 * LTTNG_LOG_CENSORED will be used for release builds (LTTNG_DEBUG_MODE is NOT
 * defined) and LTTNG_LOG_UNCENSORED otherwise (LTTNG_DEBUG_MODE is defined).
 */
#if defined(LTTNG_LOG_CENSORING)
	#define _LTTNG_LOG_CENSORING LTTNG_LOG_CENSORING
#elif defined(LTTNG_LOG_DEF_CENSORING)
	#define _LTTNG_LOG_CENSORING LTTNG_LOG_DEF_CENSORING
#else
	#ifdef LTTNG_DEBUG_MODE
		#define _LTTNG_LOG_CENSORING LTTNG_LOG_UNCENSORED
	#else
		#define _LTTNG_LOG_CENSORING LTTNG_LOG_CENSORED
	#endif
#endif

/* Check censoring at compile time. Evaluates to true when censoring is disabled
 * (i.e. when secrets will be logged). For example:
 *
 *   #if LTTNG_LOG_SECRETS
 *       char ssn[16];
 *       getSocialSecurityNumber(ssn);
 *       LTTNG_LOGI("Customer ssn: %s", ssn);
 *   #endif
 *
 * See LTTNG_LOG_SECRET() macro for a more convenient way of guarding single log
 * statement.
 */
#define LTTNG_LOG_SECRETS (LTTNG_LOG_UNCENSORED == _LTTNG_LOG_CENSORING)

/* Static (compile-time) initialization support allows to configure logging
 * before entering main() function. This mostly useful in C++ where functions
 * and methods could be called during initialization of global objects. Those
 * functions and methods could record log messages too and for that reason
 * static initialization of logging configuration is customizable.
 *
 * Macros below allow to specify values to use for initial configuration:
 * - LTTNG_LOG_EXTERN_TAG_PREFIX - tag prefix (default: none)
 * - LTTNG_LOG_EXTERN_GLOBAL_FORMAT - global format options (default: see
 *   LTTNG_LOG_MEM_WIDTH in lttng_log.c)
 * - LTTNG_LOG_EXTERN_GLOBAL_OUTPUT - global output facility (default: stderr or
 *   platform specific, see LTTNG_LOG_USE_XXX macros in lttng_log.c)
 * - LTTNG_LOG_EXTERN_GLOBAL_OUTPUT_LEVEL - global output log level (default: 0 -
 *   all levals are "turned on")
 *
 * For example, in log_config.c:
 *
 *   #include "logging.h"
 *   LTTNG_LOG_DEFINE_TAG_PREFIX = "MyApp";
 *   LTTNG_LOG_DEFINE_GLOBAL_FORMAT = {CUSTOM_MEM_WIDTH};
 *   LTTNG_LOG_DEFINE_GLOBAL_OUTPUT = {LTTNG_LOG_PUT_STD, custom_output_callback, 0};
 *   LTTNG_LOG_DEFINE_GLOBAL_OUTPUT_LEVEL = LTTNG_LOG_INFO;
 *
 * However, to use any of those macros lttng_log library must be compiled with
 * following macros defined:
 * - to use LTTNG_LOG_DEFINE_TAG_PREFIX define LTTNG_LOG_EXTERN_TAG_PREFIX
 * - to use LTTNG_LOG_DEFINE_GLOBAL_FORMAT define LTTNG_LOG_EXTERN_GLOBAL_FORMAT
 * - to use LTTNG_LOG_DEFINE_GLOBAL_OUTPUT define LTTNG_LOG_EXTERN_GLOBAL_OUTPUT
 * - to use LTTNG_LOG_DEFINE_GLOBAL_OUTPUT_LEVEL define
 *   LTTNG_LOG_EXTERN_GLOBAL_OUTPUT_LEVEL
 *
 * When lttng_log library compiled with one of LTTNG_LOG_EXTERN_XXX macros defined,
 * corresponding LTTNG_LOG_DEFINE_XXX macro MUST be used exactly once somewhere.
 * Otherwise build will fail with link error (undefined symbol).
 */
#define LTTNG_LOG_DEFINE_TAG_PREFIX LTTNG_HIDDEN const char *_lttng_log_tag_prefix
#define LTTNG_LOG_DEFINE_GLOBAL_FORMAT LTTNG_HIDDEN lttng_log_format _lttng_log_global_format
#define LTTNG_LOG_DEFINE_GLOBAL_OUTPUT LTTNG_HIDDEN lttng_log_output _lttng_log_global_output
#define LTTNG_LOG_DEFINE_GLOBAL_OUTPUT_LEVEL LTTNG_HIDDEN int _lttng_log_global_output_lvl

/* Pointer to global format options. Direct modification is not allowed. Use
 * lttng_log_set_mem_width() instead. Could be used to initialize lttng_log_spec
 * structure:
 *
 *   const lttng_log_output g_output = {LTTNG_LOG_PUT_STD, output_callback, 0};
 *   const lttng_log_spec g_spec = {LTTNG_LOG_GLOBAL_FORMAT, &g_output};
 *   LTTNG_LOGI_AUX(&g_spec, "Hello");
 */
#define LTTNG_LOG_GLOBAL_FORMAT ((const lttng_log_format *)&_lttng_log_global_format)

/* Pointer to global output variable. Direct modification is not allowed. Use
 * lttng_log_set_output_v() or lttng_log_set_output_p() instead. Could be used to
 * initialize lttng_log_spec structure:
 *
 *   const lttng_log_format g_format = {40};
 *   const lttng_log_spec g_spec = {g_format, LTTNG_LOG_GLOBAL_OUTPUT};
 *   LTTNG_LOGI_AUX(&g_spec, "Hello");
 */
#define LTTNG_LOG_GLOBAL_OUTPUT ((const lttng_log_output *)&_lttng_log_global_output)

/* When defined, all library symbols produced by linker will be prefixed with
 * provided value. That allows to use lttng_log library privately in another
 * libraries without exposing lttng_log symbols in their original form (to avoid
 * possible conflicts with other libraries / components that also could use
 * lttng_log for logging). Value must be without quotes, for example:
 *
 *   CC_ARGS := -DLTTNG_LOG_LIBRARY_PREFIX=my_lib_
 *
 * Note, that in this mode LTTNG_LOG_LIBRARY_PREFIX must be defined when building
 * lttng_log library AND it also must be defined to the same value when building
 * a library that uses it. For example, consider fictional KittyHttp library
 * that wants to use lttng_log for logging. First approach that could be taken is
 * to add lttng_log.h and lttng_log.c to the KittyHttp's source code tree directly.
 * In that case it will be enough just to define LTTNG_LOG_LIBRARY_PREFIX in
 * KittyHttp's build script:
 *
 *   // KittyHttp/CMakeLists.txt
 *   target_compile_definitions(KittyHttp PRIVATE
 *                              "LTTNG_LOG_LIBRARY_PREFIX=KittyHttp_")
 *
 * If KittyHttp doesn't want to include lttng_log source code in its source tree
 * and wants to build lttng_log as a separate library than lttng_log library must be
 * built with LTTNG_LOG_LIBRARY_PREFIX defined to KittyHttp_ AND KittyHttp library
 * itself also needs to define LTTNG_LOG_LIBRARY_PREFIX to KittyHttp_. It can do
 * so either in its build script, as in example above, or by providing a
 * wrapper header that KittyHttp library will need to use instead of lttng_log.h:
 *
 *   // KittyHttpLogging.h
 *   #define LTTNG_LOG_LIBRARY_PREFIX KittyHttp_
 *   #include "logging.h"
 *
 * Regardless of the method chosen, the end result is that lttng_log symbols will
 * be prefixed with "KittyHttp_", so if a user of KittyHttp (say DogeBrowser)
 * also uses lttng_log for logging, they will not interferer with each other. Both
 * will have their own log level, output facility, format options etc.
 */
#ifdef LTTNG_LOG_LIBRARY_PREFIX
	#define _LTTNG_LOG_DECOR__(prefix, name) prefix ## name
	#define _LTTNG_LOG_DECOR_(prefix, name) _LTTNG_LOG_DECOR__(prefix, name)
	#define _LTTNG_LOG_DECOR(name) _LTTNG_LOG_DECOR_(LTTNG_LOG_LIBRARY_PREFIX, name)

	#define lttng_log_set_tag_prefix _LTTNG_LOG_DECOR(lttng_log_set_tag_prefix)
	#define lttng_log_set_mem_width _LTTNG_LOG_DECOR(lttng_log_set_mem_width)
	#define lttng_log_set_output_level _LTTNG_LOG_DECOR(lttng_log_set_output_level)
	#define lttng_log_set_output_v _LTTNG_LOG_DECOR(lttng_log_set_output_v)
	#define lttng_log_set_output_p _LTTNG_LOG_DECOR(lttng_log_set_output_p)
	#define lttng_log_out_stderr_callback _LTTNG_LOG_DECOR(lttng_log_out_stderr_callback)
	#define _lttng_log_tag_prefix _LTTNG_LOG_DECOR(_lttng_log_tag_prefix)
	#define _lttng_log_global_format _LTTNG_LOG_DECOR(_lttng_log_global_format)
	#define _lttng_log_global_output _LTTNG_LOG_DECOR(_lttng_log_global_output)
	#define _lttng_log_global_output_lvl _LTTNG_LOG_DECOR(_lttng_log_global_output_lvl)
	#define _lttng_log_write_d _LTTNG_LOG_DECOR(_lttng_log_write_d)
	#define _lttng_log_write_aux_d _LTTNG_LOG_DECOR(_lttng_log_write_aux_d)
	#define _lttng_log_write _LTTNG_LOG_DECOR(_lttng_log_write)
	#define _lttng_log_write_aux _LTTNG_LOG_DECOR(_lttng_log_write_aux)
	#define _lttng_log_write_mem_d _LTTNG_LOG_DECOR(_lttng_log_write_mem_d)
	#define _lttng_log_write_mem_aux_d _LTTNG_LOG_DECOR(_lttng_log_write_mem_aux_d)
	#define _lttng_log_write_mem _LTTNG_LOG_DECOR(_lttng_log_write_mem)
	#define _lttng_log_write_mem_aux _LTTNG_LOG_DECOR(_lttng_log_write_mem_aux)
	#define _lttng_log_stderr_spec _LTTNG_LOG_DECOR(_lttng_log_stderr_spec)
#endif

#if defined(__printflike)
	#define _LTTNG_LOG_PRINTFLIKE(str_index, first_to_check) \
		__printflike(str_index, first_to_check)
#elif defined(__MINGW_PRINTF_FORMAT)
	#define _LTTNG_LOG_PRINTFLIKE(str_index, first_to_check) \
		__attribute__((format(__MINGW_PRINTF_FORMAT, str_index, first_to_check)))
#elif defined(__GNUC__)
	#define _LTTNG_LOG_PRINTFLIKE(str_index, first_to_check) \
		__attribute__((format(__printf__, str_index, first_to_check)))
#else
	#define _LTTNG_LOG_PRINTFLIKE(str_index, first_to_check)
#endif

#if (defined(_WIN32) || defined(_WIN64)) && !defined(__GNUC__)
	#define _LTTNG_LOG_FUNCTION __FUNCTION__
#else
	#define _LTTNG_LOG_FUNCTION __func__
#endif

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
	#define _LTTNG_LOG_INLINE __inline
	#define _LTTNG_LOG_IF(cond) \
		__pragma(warning(push)) \
		__pragma(warning(disable:4127)) \
		if(cond) \
		__pragma(warning(pop))
	#define _LTTNG_LOG_WHILE(cond) \
		__pragma(warning(push)) \
		__pragma(warning(disable:4127)) \
		while(cond) \
		__pragma(warning(pop))
#else
	#define _LTTNG_LOG_INLINE inline
	#define _LTTNG_LOG_IF(cond) if(cond)
	#define _LTTNG_LOG_WHILE(cond) while(cond)
#endif
#define _LTTNG_LOG_NEVER _LTTNG_LOG_IF(0)
#define _LTTNG_LOG_ONCE _LTTNG_LOG_WHILE(0)

#ifdef __cplusplus
extern "C" {
#endif

/* Set tag prefix. Prefix will be separated from the tag with dot ('.').
 * Use 0 or empty string to disable (default). Common use is to set it to
 * the process (or build target) name (e.g. to separate client and server
 * processes). Function will NOT copy provided prefix string, but will store the
 * pointer. Hence specified prefix string must remain valid. See
 * LTTNG_LOG_DEFINE_TAG_PREFIX for a way to set it before entering main() function.
 * See LTTNG_LOG_TAG for more information about tag and tag prefix.
 */
void lttng_log_set_tag_prefix(const char *const prefix);

/* Set number of bytes per log line in memory (ASCII-HEX) output. Example:
 *
 *   I hello.MAIN 4c6f72656d20697073756d20646f6c6f  Lorem ipsum dolo
 *                |<-          w bytes         ->|  |<-  w chars ->|
 *
 * See LTTNG_LOGF_MEM and LTTNG_LOGF_MEM_AUX for more details.
 */
void lttng_log_set_mem_width(const unsigned w);

/* Set "output" log level. See LTTNG_MINIMAL_LOG_LEVEL and LTTNG_LOG_OUTPUT_LEVEL for more
 * info about log levels.
 */
void lttng_log_set_output_level(const int lvl);

/* Put mask is a set of flags that define what fields will be added to each
 * log message. Default value is LTTNG_LOG_PUT_STD and other flags could be used to
 * alter its behavior. See lttng_log_set_output_v() for more details.
 *
 * Note about LTTNG_LOG_PUT_SRC: it will be added only in debug builds
 * (LTTNG_DEBUG_MODE is defined).
 */
enum
{
	LTTNG_LOG_PUT_CTX = 1 << 0, /* context (time, pid, tid, log level) */
	LTTNG_LOG_PUT_TAG = 1 << 1, /* tag (including tag prefix) */
	LTTNG_LOG_PUT_SRC = 1 << 2, /* source location (file, line, function) */
	LTTNG_LOG_PUT_MSG = 1 << 3, /* message text (formatted string) */
	LTTNG_LOG_PUT_STD = 0xffff, /* everything (default) */
};

typedef struct lttng_log_message
{
	int lvl; /* Log level of the message */
	const char *tag; /* Associated tag (without tag prefix) */
	char *buf; /* Buffer start */
	char *e; /* Buffer end (last position where EOL with 0 could be written) */
	char *p; /* Buffer content end (append position) */
	char *tag_b; /* Prefixed tag start */
	char *tag_e; /* Prefixed tag end (if != tag_b, points to msg separator) */
	char *msg_b; /* Message start (expanded format string) */
}
lttng_log_message;

/* Type of output callback function. It will be called for each log line allowed
 * by both "current" and "output" log levels ("enabled" and "turned on").
 * Callback function is allowed to modify content of the buffers pointed by the
 * msg, but it's not allowed to modify any of msg fields. Buffer pointed by msg
 * is UTF-8 encoded (no BOM mark).
 */
typedef void (*lttng_log_output_cb)(const lttng_log_message *msg, void *arg);

/* Format options. For more details see lttng_log_set_mem_width().
 */
typedef struct lttng_log_format
{
	unsigned mem_width; /* Bytes per line in memory (ASCII-HEX) dump */
}
lttng_log_format;

/* Output facility.
 */
typedef struct lttng_log_output
{
	unsigned mask; /* What to put into log line buffer (see LTTNG_LOG_PUT_XXX) */
	void *arg; /* User provided output callback argument */
	lttng_log_output_cb callback; /* Output callback function */
}
lttng_log_output;

/* Set output callback function.
 *
 * Mask allows to control what information will be added to the log line buffer
 * before callback function is invoked. Default mask value is LTTNG_LOG_PUT_STD.
 */
void lttng_log_set_output_v(const unsigned mask, void *const arg,
						 const lttng_log_output_cb callback);
static _LTTNG_LOG_INLINE void lttng_log_set_output_p(const lttng_log_output *const output)
{
	lttng_log_set_output_v(output->mask, output->arg, output->callback);
}

/* Used with _AUX macros and allows to override global format and output
 * facility. Use LTTNG_LOG_GLOBAL_FORMAT and LTTNG_LOG_GLOBAL_OUTPUT for values from
 * global configuration. Example:
 *
 *   static const lttng_log_output module_output = {
 *       LTTNG_LOG_PUT_STD, 0, custom_output_callback
 *   };
 *   static const lttng_log_spec module_spec = {
 *       LTTNG_LOG_GLOBAL_FORMAT, &module_output
 *   };
 *   LTTNG_LOGI_AUX(&module_spec, "Position: %ix%i", x, y);
 *
 * See LTTNG_LOGF_AUX and LTTNG_LOGF_MEM_AUX for details.
 */
typedef struct lttng_log_spec
{
	const lttng_log_format *format;
	const lttng_log_output *output;
}
lttng_log_spec;

#ifdef __cplusplus
}
#endif

/* Execute log statement if condition is true. Example:
 *
 *   LTTNG_LOG_IF(1 < 2, LTTNG_LOGI("Log this"));
 *   LTTNG_LOG_IF(1 > 2, LTTNG_LOGI("Don't log this"));
 *
 * Keep in mind though, that if condition can't be evaluated at compile time,
 * then it will be evaluated at run time. This will increase exectuable size
 * and can have noticeable performance overhead. Try to limit conditions to
 * expressions that can be evaluated at compile time.
 */
#define LTTNG_LOG_IF(cond, f) do { _LTTNG_LOG_IF((cond)) { f; } } _LTTNG_LOG_ONCE

/* Mark log statement as "secret". Log statements that are marked as secrets
 * will NOT be executed when censoring is enabled (see LTTNG_LOG_CENSORED).
 * Example:
 *
 *   LTTNG_LOG_SECRET(LTTNG_LOGI("Credit card: %s", credit_card));
 *   LTTNG_LOG_SECRET(LTTNG_LOGD_MEM(cipher, cipher_sz, "Cipher bytes:"));
 */
#define LTTNG_LOG_SECRET(f) LTTNG_LOG_IF(LTTNG_LOG_SECRETS, f)

/* Check "current" log level at compile time (ignoring "output" log level).
 * Evaluates to true when specified log level is enabled. For example:
 *
 *   #if LTTNG_LOG_ENABLED_DEBUG
 *       const char *const g_enum_strings[] = {
 *           "enum_value_0", "enum_value_1", "enum_value_2"
 *       };
 *   #endif
 *   // ...
 *   #if LTTNG_LOG_ENABLED_DEBUG
 *       LTTNG_LOGD("enum value: %s", g_enum_strings[v]);
 *   #endif
 *
 * See LTTNG_MINIMAL_LOG_LEVEL for details.
 */
#define LTTNG_LOG_ENABLED(lvl)     ((lvl) >= _LTTNG_MINIMAL_LOG_LEVEL)
#define LTTNG_LOG_ENABLED_TRACE    LTTNG_LOG_ENABLED(LTTNG_LOG_TRACE)
#define LTTNG_LOG_ENABLED_DEBUG    LTTNG_LOG_ENABLED(LTTNG_LOG_DEBUG)
#define LTTNG_LOG_ENABLED_INFO     LTTNG_LOG_ENABLED(LTTNG_LOG_INFO)
#define LTTNG_LOG_ENABLED_WARNING  LTTNG_LOG_ENABLED(LTTNG_LOG_WARNING)
#define LTTNG_LOG_ENABLED_ERROR    LTTNG_LOG_ENABLED(LTTNG_LOG_ERROR)
#define LTTNG_LOG_ENABLED_FATAL    LTTNG_LOG_ENABLED(LTTNG_LOG_FATAL)

/* Check "output" log level at run time (taking into account "current" log
 * level as well). Evaluates to true when specified log level is turned on AND
 * enabled. For example:
 *
 *   if (LTTNG_LOG_ON_DEBUG)
 *   {
 *       char hash[65];
 *       sha256(data_ptr, data_sz, hash);
 *       LTTNG_LOGD("data: len=%u, sha256=%s", data_sz, hash);
 *   }
 *
 * See LTTNG_LOG_OUTPUT_LEVEL for details.
 */
#define LTTNG_LOG_ON_CUR_LVL(lvl, cur_lvl) \
		(LTTNG_LOG_ENABLED((lvl)) && (lvl) >= (cur_lvl))
#define LTTNG_LOG_ON(lvl) \
		(LTTNG_LOG_ENABLED((lvl)) && (lvl) >= _LTTNG_LOG_OUTPUT_LEVEL)
#define LTTNG_LOG_ON_TRACE     LTTNG_LOG_ON(LTTNG_LOG_TRACE)
#define LTTNG_LOG_ON_DEBUG     LTTNG_LOG_ON(LTTNG_LOG_DEBUG)
#define LTTNG_LOG_ON_INFO      LTTNG_LOG_ON(LTTNG_LOG_INFO)
#define LTTNG_LOG_ON_WARNING   LTTNG_LOG_ON(LTTNG_LOG_WARNING)
#define LTTNG_LOG_ON_ERROR     LTTNG_LOG_ON(LTTNG_LOG_ERROR)
#define LTTNG_LOG_ON_FATAL     LTTNG_LOG_ON(LTTNG_LOG_FATAL)

#ifdef __cplusplus
extern "C" {
#endif

extern const char *_lttng_log_tag_prefix;
extern lttng_log_format _lttng_log_global_format;
extern lttng_log_output _lttng_log_global_output;
extern int _lttng_log_global_output_lvl;
extern const lttng_log_spec _lttng_log_stderr_spec;

LTTNG_HIDDEN
void _lttng_log_write_d(
		const char *const func, const char *const file, const unsigned line,
		const int lvl, const char *const tag,
		const char *const fmt, ...) _LTTNG_LOG_PRINTFLIKE(6, 7);

LTTNG_HIDDEN
void _lttng_log_write_aux_d(
		const char *const func, const char *const file, const unsigned line,
		const lttng_log_spec *const log, const int lvl, const char *const tag,
		const char *const fmt, ...) _LTTNG_LOG_PRINTFLIKE(7, 8);

LTTNG_HIDDEN
void _lttng_log_write(
		const int lvl, const char *const tag,
		const char *const fmt, ...) _LTTNG_LOG_PRINTFLIKE(3, 4);

LTTNG_HIDDEN
void _lttng_log_write_aux(
		const lttng_log_spec *const log, const int lvl, const char *const tag,
		const char *const fmt, ...) _LTTNG_LOG_PRINTFLIKE(4, 5);

LTTNG_HIDDEN
void _lttng_log_write_mem_d(
		const char *const func, const char *const file, const unsigned line,
		const int lvl, const char *const tag,
		const void *const d, const unsigned d_sz,
		const char *const fmt, ...) _LTTNG_LOG_PRINTFLIKE(8, 9);

LTTNG_HIDDEN
void _lttng_log_write_mem_aux_d(
		const char *const func, const char *const file, const unsigned line,
		const lttng_log_spec *const log, const int lvl, const char *const tag,
		const void *const d, const unsigned d_sz,
		const char *const fmt, ...) _LTTNG_LOG_PRINTFLIKE(9, 10);

LTTNG_HIDDEN
void _lttng_log_write_mem(
		const int lvl, const char *const tag,
		const void *const d, const unsigned d_sz,
		const char *const fmt, ...) _LTTNG_LOG_PRINTFLIKE(5, 6);

LTTNG_HIDDEN
void _lttng_log_write_mem_aux(
		const lttng_log_spec *const log, const int lvl, const char *const tag,
		const void *const d, const unsigned d_sz,
		const char *const fmt, ...) _LTTNG_LOG_PRINTFLIKE(6, 7);

#ifdef __cplusplus
}
#endif

/* Message logging macros:
 * - LTTNG_LOGT("format string", args, ...)
 * - LTTNG_LOGD("format string", args, ...)
 * - LTTNG_LOGI("format string", args, ...)
 * - LTTNG_LOGW("format string", args, ...)
 * - LTTNG_LOGE("format string", args, ...)
 * - LTTNG_LOGF("format string", args, ...)
 *
 * Message and error string (errno) logging macros:
 * - LTTNG_LOGT_ERRNO("initial message", "format string", args, ...)
 * - LTTNG_LOGD_ERRNO("initial message", "format string", args, ...)
 * - LTTNG_LOGI_ERRNO("initial message", "format string", args, ...)
 * - LTTNG_LOGW_ERRNO("initial message", "format string", args, ...)
 * - LTTNG_LOGE_ERRNO("initial message", "format string", args, ...)
 * - LTTNG_LOGF_ERRNO("initial message", "format string", args, ...)
 *
 * Memory logging macros:
 * - LTTNG_LOGT_MEM(data_ptr, data_sz, "format string", args, ...)
 * - LTTNG_LOGD_MEM(data_ptr, data_sz, "format string", args, ...)
 * - LTTNG_LOGI_MEM(data_ptr, data_sz, "format string", args, ...)
 * - LTTNG_LOGW_MEM(data_ptr, data_sz, "format string", args, ...)
 * - LTTNG_LOGE_MEM(data_ptr, data_sz, "format string", args, ...)
 * - LTTNG_LOGF_MEM(data_ptr, data_sz, "format string", args, ...)
 *
 * Auxiliary logging macros:
 * - LTTNG_LOGT_AUX(&log_instance, "format string", args, ...)
 * - LTTNG_LOGD_AUX(&log_instance, "format string", args, ...)
 * - LTTNG_LOGI_AUX(&log_instance, "format string", args, ...)
 * - LTTNG_LOGW_AUX(&log_instance, "format string", args, ...)
 * - LTTNG_LOGE_AUX(&log_instance, "format string", args, ...)
 * - LTTNG_LOGF_AUX(&log_instance, "format string", args, ...)
 *
 * Auxiliary memory logging macros:
 * - LTTNG_LOGT_MEM_AUX(&log_instance, data_ptr, data_sz, "format string", args, ...)
 * - LTTNG_LOGD_MEM_AUX(&log_instance, data_ptr, data_sz, "format string", args, ...)
 * - LTTNG_LOGI_MEM_AUX(&log_instance, data_ptr, data_sz, "format string", args, ...)
 * - LTTNG_LOGW_MEM_AUX(&log_instance, data_ptr, data_sz, "format string", args, ...)
 * - LTTNG_LOGE_MEM_AUX(&log_instance, data_ptr, data_sz, "format string", args, ...)
 * - LTTNG_LOGF_MEM_AUX(&log_instance, data_ptr, data_sz, "format string", args, ...)
 *
 * Preformatted string logging macros:
 * - LTTNG_LOGT_STR("preformatted string");
 * - LTTNG_LOGD_STR("preformatted string");
 * - LTTNG_LOGI_STR("preformatted string");
 * - LTTNG_LOGW_STR("preformatted string");
 * - LTTNG_LOGE_STR("preformatted string");
 * - LTTNG_LOGF_STR("preformatted string");
 *
 * Explicit log level and tag macros:
 * - LTTNG_LOG_WRITE(level, tag, "format string", args, ...)
 * - LTTNG_LOG_WRITE_MEM(level, tag, data_ptr, data_sz, "format string", args, ...)
 * - LTTNG_LOG_WRITE_AUX(&log_instance, level, tag, "format string", args, ...)
 * - LTTNG_LOG_WRITE_MEM_AUX(&log_instance, level, tag, data_ptr, data_sz,
 *                        "format string", args, ...)
 *
 * Explicit log level, current log level, and tag:
 * - LTTNG_LOG_WRITE_CUR_LVL(level, cur_level, tag, "format string", args, ...)
 *
 * Format string follows printf() conventions. Both data_ptr and data_sz could
 * be 0. Tag can be 0 as well. Most compilers will verify that type of arguments
 * match format specifiers in format string.
 *
 * Library assuming UTF-8 encoding for all strings (char *), including format
 * string itself.
 */
#if LTTNG_LOG_SRCLOC_NONE == _LTTNG_LOG_SRCLOC
	#define LTTNG_LOG_WRITE(lvl, tag, ...) \
			do { \
				if (LTTNG_LOG_ON(lvl)) \
					_lttng_log_write(lvl, tag, __VA_ARGS__); \
			} _LTTNG_LOG_ONCE
	#define LTTNG_LOG_WRITE_CUR_LVL(lvl, cur_lvl, tag, ...) \
			do { \
				if (LTTNG_LOG_ON_CUR_LVL((lvl), (cur_lvl))) \
					_lttng_log_write(lvl, tag, __VA_ARGS__); \
			} _LTTNG_LOG_ONCE
	#define LTTNG_LOG_WRITE_MEM(lvl, tag, d, d_sz, ...) \
			do { \
				if (LTTNG_LOG_ON(lvl)) \
					_lttng_log_write_mem(lvl, tag, d, d_sz, __VA_ARGS__); \
			} _LTTNG_LOG_ONCE
	#define LTTNG_LOG_WRITE_AUX(log, lvl, tag, ...) \
			do { \
				if (LTTNG_LOG_ON(lvl)) \
					_lttng_log_write_aux(log, lvl, tag, __VA_ARGS__); \
			} _LTTNG_LOG_ONCE
	#define LTTNG_LOG_WRITE_MEM_AUX(log, lvl, tag, d, d_sz, ...) \
			do { \
				if (LTTNG_LOG_ON(lvl)) \
					_lttng_log_write_mem_aux(log, lvl, tag, d, d_sz, __VA_ARGS__); \
			} _LTTNG_LOG_ONCE
#else
	#define LTTNG_LOG_WRITE(lvl, tag, ...) \
			do { \
				if (LTTNG_LOG_ON(lvl)) \
					_lttng_log_write_d(_LTTNG_LOG_SRCLOC_FUNCTION, __FILE__, __LINE__, \
							lvl, tag, __VA_ARGS__); \
			} _LTTNG_LOG_ONCE
	#define LTTNG_LOG_WRITE_CUR_LVL(lvl, cur_lvl, tag, ...) \
			do { \
				if (LTTNG_LOG_ON_CUR_LVL((lvl), (cur_lvl))) \
					_lttng_log_write_d(_LTTNG_LOG_SRCLOC_FUNCTION, __FILE__, __LINE__, \
							lvl, tag, __VA_ARGS__); \
			} _LTTNG_LOG_ONCE
	#define LTTNG_LOG_WRITE_MEM(lvl, tag, d, d_sz, ...) \
			do { \
				if (LTTNG_LOG_ON(lvl)) \
					_lttng_log_write_mem_d(_LTTNG_LOG_SRCLOC_FUNCTION, __FILE__, __LINE__, \
							lvl, tag, d, d_sz, __VA_ARGS__); \
			} _LTTNG_LOG_ONCE
	#define LTTNG_LOG_WRITE_AUX(log, lvl, tag, ...) \
			do { \
				if (LTTNG_LOG_ON(lvl)) \
					_lttng_log_write_aux_d(_LTTNG_LOG_SRCLOC_FUNCTION, __FILE__, __LINE__, \
							log, lvl, tag, __VA_ARGS__); \
			} _LTTNG_LOG_ONCE
	#define LTTNG_LOG_WRITE_MEM_AUX(log, lvl, tag, d, d_sz, ...) \
			do { \
				if (LTTNG_LOG_ON(lvl)) \
					_lttng_log_write_mem_aux_d(_LTTNG_LOG_SRCLOC_FUNCTION, __FILE__, __LINE__, \
							log, lvl, tag, d, d_sz, __VA_ARGS__); \
			} _LTTNG_LOG_ONCE
#endif

#define LTTNG_LOG_WRITE_ERRNO_CUR_LVL(lvl, cur_lvl, tag, _msg, _fmt, args...) \
		do { \
			const char *error_str; \
			error_str = g_strerror(errno); \
			LTTNG_LOG_WRITE_CUR_LVL(lvl, cur_lvl, tag, _msg ": %s" _fmt, error_str, ## args); \
		} _LTTNG_LOG_ONCE

#define LTTNG_LOG_WRITE_ERRNO(lvl, tag, _msg, _fmt, args...) \
		do { \
			LTTNG_LOG_WRITE_ERRNO_CUR_LVL(lvl, _LTTNG_LOG_OUTPUT_LEVEL, tag, _msg, _fmt, ## args); \
		} _LTTNG_LOG_ONCE

static _LTTNG_LOG_INLINE void _lttng_log_unused(const int dummy, ...) {(void)dummy;}

#define _LTTNG_LOG_UNUSED(...) \
		do { _LTTNG_LOG_NEVER _lttng_log_unused(0, __VA_ARGS__); } _LTTNG_LOG_ONCE

#if LTTNG_LOG_ENABLED_TRACE
	#define LTTNG_LOGT(...) \
			LTTNG_LOG_WRITE(LTTNG_LOG_TRACE, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGT_ERRNO(...) \
			LTTNG_LOG_WRITE_ERRNO(LTTNG_LOG_TRACE, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGT_AUX(log, ...) \
			LTTNG_LOG_WRITE_AUX(log, LTTNG_LOG_TRACE, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGT_MEM(d, d_sz, ...) \
			LTTNG_LOG_WRITE_MEM(LTTNG_LOG_TRACE, _LTTNG_LOG_TAG, d, d_sz, __VA_ARGS__)
	#define LTTNG_LOGT_MEM_AUX(log, d, d_sz, ...) \
			LTTNG_LOG_WRITE_MEM(log, LTTNG_LOG_TRACE, _LTTNG_LOG_TAG, d, d_sz, __VA_ARGS__)
#else
	#define LTTNG_LOGT(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGT_AUX(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGT_MEM(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGT_MEM_AUX(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
#endif

#if LTTNG_LOG_ENABLED_DEBUG
	#define LTTNG_LOGD(...) \
			LTTNG_LOG_WRITE(LTTNG_LOG_DEBUG, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGD_ERRNO(...) \
			LTTNG_LOG_WRITE_ERRNO(LTTNG_LOG_DEBUG, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGD_AUX(log, ...) \
			LTTNG_LOG_WRITE_AUX(log, LTTNG_LOG_DEBUG, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGD_MEM(d, d_sz, ...) \
			LTTNG_LOG_WRITE_MEM(LTTNG_LOG_DEBUG, _LTTNG_LOG_TAG, d, d_sz, __VA_ARGS__)
	#define LTTNG_LOGD_MEM_AUX(log, d, d_sz, ...) \
			LTTNG_LOG_WRITE_MEM_AUX(log, LTTNG_LOG_DEBUG, _LTTNG_LOG_TAG, d, d_sz, __VA_ARGS__)
#else
	#define LTTNG_LOGD(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGD_AUX(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGD_MEM(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGD_MEM_AUX(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
#endif

#if LTTNG_LOG_ENABLED_INFO
	#define LTTNG_LOGI(...) \
			LTTNG_LOG_WRITE(LTTNG_LOG_INFO, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGI_ERRNO(...) \
			LTTNG_LOG_WRITE_ERRNO(LTTNG_LOG_INFO, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGI_AUX(log, ...) \
			LTTNG_LOG_WRITE_AUX(log, LTTNG_LOG_INFO, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGI_MEM(d, d_sz, ...) \
			LTTNG_LOG_WRITE_MEM(LTTNG_LOG_INFO, _LTTNG_LOG_TAG, d, d_sz, __VA_ARGS__)
	#define LTTNG_LOGI_MEM_AUX(log, d, d_sz, ...) \
			LTTNG_LOG_WRITE_MEM_AUX(log, LTTNG_LOG_INFO, _LTTNG_LOG_TAG, d, d_sz, __VA_ARGS__)
#else
	#define LTTNG_LOGI(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGI_AUX(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGI_MEM(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGI_MEM_AUX(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
#endif

#if LTTNG_LOG_ENABLED_WARNING
	#define LTTNG_LOGW(...) \
			LTTNG_LOG_WRITE(LTTNG_LOG_WARNING, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGW_ERRNO(...) \
			LTTNG_LOG_WRITE_ERRNO(LTTNG_LOG_WARNING, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGW_AUX(log, ...) \
			LTTNG_LOG_WRITE_AUX(log, LTTNG_LOG_WARNING, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGW_MEM(d, d_sz, ...) \
			LTTNG_LOG_WRITE_MEM(LTTNG_LOG_WARNING, _LTTNG_LOG_TAG, d, d_sz, __VA_ARGS__)
	#define LTTNG_LOGW_MEM_AUX(log, d, d_sz, ...) \
			LTTNG_LOG_WRITE_MEM_AUX(log, LTTNG_LOG_WARNING, _LTTNG_LOG_TAG, d, d_sz, __VA_ARGS__)
#else
	#define LTTNG_LOGW(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGW_AUX(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGW_MEM(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGW_MEM_AUX(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
#endif

#if LTTNG_LOG_ENABLED_ERROR
	#define LTTNG_LOGE(...) \
			LTTNG_LOG_WRITE(LTTNG_LOG_ERROR, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGE_ERRNO(...) \
			LTTNG_LOG_WRITE_ERRNO(LTTNG_LOG_ERROR, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGE_AUX(log, ...) \
			LTTNG_LOG_WRITE_AUX(log, LTTNG_LOG_ERROR, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGE_MEM(d, d_sz, ...) \
			LTTNG_LOG_WRITE_MEM(LTTNG_LOG_ERROR, _LTTNG_LOG_TAG, d, d_sz, __VA_ARGS__)
	#define LTTNG_LOGE_MEM_AUX(log, d, d_sz, ...) \
			LTTNG_LOG_WRITE_MEM_AUX(log, LTTNG_LOG_ERROR, _LTTNG_LOG_TAG, d, d_sz, __VA_ARGS__)
#else
	#define LTTNG_LOGE(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGE_AUX(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGE_MEM(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGE_MEM_AUX(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
#endif

#if LTTNG_LOG_ENABLED_FATAL
	#define LTTNG_LOGF(...) \
			LTTNG_LOG_WRITE(LTTNG_LOG_FATAL, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGF_ERRNO(...) \
			LTTNG_LOG_WRITE_ERRNO(LTTNG_LOG_FATAL, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGF_AUX(log, ...) \
			LTTNG_LOG_WRITE_AUX(log, LTTNG_LOG_FATAL, _LTTNG_LOG_TAG, __VA_ARGS__)
	#define LTTNG_LOGF_MEM(d, d_sz, ...) \
			LTTNG_LOG_WRITE_MEM(LTTNG_LOG_FATAL, _LTTNG_LOG_TAG, d, d_sz, __VA_ARGS__)
	#define LTTNG_LOGF_MEM_AUX(log, d, d_sz, ...) \
			LTTNG_LOG_WRITE_MEM_AUX(log, LTTNG_LOG_FATAL, _LTTNG_LOG_TAG, d, d_sz, __VA_ARGS__)
#else
	#define LTTNG_LOGF(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGF_AUX(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGF_MEM(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
	#define LTTNG_LOGF_MEM_AUX(...) _LTTNG_LOG_UNUSED(__VA_ARGS__)
#endif

#define LTTNG_LOGT_STR(s) LTTNG_LOGT("%s", (s))
#define LTTNG_LOGD_STR(s) LTTNG_LOGD("%s", (s))
#define LTTNG_LOGI_STR(s) LTTNG_LOGI("%s", (s))
#define LTTNG_LOGW_STR(s) LTTNG_LOGW("%s", (s))
#define LTTNG_LOGE_STR(s) LTTNG_LOGE("%s", (s))
#define LTTNG_LOGF_STR(s) LTTNG_LOGF("%s", (s))

#ifdef __cplusplus
extern "C" {
#endif

/* Output to standard error stream. Library uses it by default, though in few
 * cases it could be necessary to specify it explicitly. For example, when
 * lttng_log library is compiled with LTTNG_LOG_EXTERN_GLOBAL_OUTPUT, application must
 * define and initialize global output variable:
 *
 *   LTTNG_LOG_DEFINE_GLOBAL_OUTPUT = {LTTNG_LOG_OUT_STDERR};
 *
 * Another example is when using custom output, stderr could be used as a
 * fallback when custom output facility failed to initialize:
 *
 *   lttng_log_set_output_v(LTTNG_LOG_OUT_STDERR);
 */
enum { LTTNG_LOG_OUT_STDERR_MASK = LTTNG_LOG_PUT_STD };

LTTNG_HIDDEN
void lttng_log_out_stderr_callback(const lttng_log_message *const msg, void *arg);
#define LTTNG_LOG_OUT_STDERR LTTNG_LOG_OUT_STDERR_MASK, 0, lttng_log_out_stderr_callback

/* Predefined spec for stderr. Uses global format options (LTTNG_LOG_GLOBAL_FORMAT)
 * and LTTNG_LOG_OUT_STDERR. Could be used to force output to stderr for a
 * particular message. Example:
 *
 *   f = fopen("foo.log", "w");
 *   if (!f)
 *       LTTNG_LOGE_AUX(LTTNG_LOG_STDERR, "Failed to open log file");
 */
#define LTTNG_LOG_STDERR (&_lttng_log_stderr_spec)

/*
 * Returns the equivalent letter of the log level `level`.
 *
 * `level` must be a valid log level.
 */
static inline
char lttng_log_get_letter_from_level(int level)
{
	char letter;

	switch (level) {
	case LTTNG_LOG_TRACE:
		letter = 'T';
		break;
	case LTTNG_LOG_DEBUG:
		letter = 'D';
		break;
	case LTTNG_LOG_INFO:
		letter = 'I';
		break;
	case LTTNG_LOG_WARNING:
		letter = 'W';
		break;
	case LTTNG_LOG_ERROR:
		letter = 'E';
		break;
	case LTTNG_LOG_FATAL:
		letter = 'F';
		break;
	case LTTNG_LOG_NONE:
		letter = 'N';
		break;
	default:
		abort();
	}

	return letter;
}

/*
 * Returns the log level for the string `str`, or -1 if `str` is not a
 * valid log level string.
 */
static inline
int lttng_log_get_level_from_string(const char *str)
{
	int level = -1;

        assert(str);

	if (strcmp(str, "TRACE") == 0 ||
			strcmp(str, "T") == 0) {
		level = LTTNG_LOG_TRACE;
	} else if (strcmp(str, "DEBUG") == 0 ||
			strcmp(str, "D") == 0) {
		level = LTTNG_LOG_DEBUG;
	} else if (strcmp(str, "INFO") == 0 ||
			strcmp(str, "I") == 0) {
		level = LTTNG_LOG_INFO;
	} else if (strcmp(str, "WARN") == 0 ||
			strcmp(str, "WARNING") == 0 ||
			strcmp(str, "W") == 0) {
		level = LTTNG_LOG_WARNING;
	} else if (strcmp(str, "ERROR") == 0 ||
			strcmp(str, "E") == 0) {
		level = LTTNG_LOG_ERROR;
	} else if (strcmp(str, "FATAL") == 0 ||
			strcmp(str, "F") == 0) {
		level = LTTNG_LOG_FATAL;
	} else if (strcmp(str, "NONE") == 0 ||
			strcmp(str, "N") == 0) {
		level = LTTNG_LOG_NONE;
	} else {
		/* FIXME: Should we warn here? How? */
	}

	return level;
}

/*
 * Returns the log level for the letter `letter`, or -1 if `letter` is
 * not a valid log level string.
 */
static inline
int lttng_log_get_level_from_letter(char letter)
{
	char str[] = {letter, '\0'};

	return lttng_log_get_level_from_string(str);
}

static inline
int lttng_log_get_level_from_env(const char *var)
{
	const char *varval = getenv(var);
	int level = LTTNG_LOG_NONE;

	if (!varval) {
		goto end;
	}

	level = lttng_log_get_level_from_string(varval);
	if (level < 0) {
		/* FIXME: Should we warn here? How? */
		level = LTTNG_LOG_NONE;
	}

end:
	return level;
}

#define LTTNG_LOG_LEVEL_EXTERN_SYMBOL(_level_sym)				\
	extern int _level_sym

#define LTTNG_LOG_INIT_LOG_LEVEL(_level_sym, _env_var)			\
	LTTNG_HIDDEN int _level_sym = LTTNG_LOG_NONE;				\
	static								\
	void __attribute__((constructor)) _lttng_log_level_ctor(void)	\
	{								\
		_level_sym = lttng_log_get_level_from_env(_env_var);	\
	}

#define LTTNG_LOG_SUPPORTED

#ifdef __cplusplus
}
#endif

#endif /* LTTNG_LOGGING_INTERNAL_H */
