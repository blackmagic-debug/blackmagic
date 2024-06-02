/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INCLUDE_GENERAL_H
#define INCLUDE_GENERAL_H

#if !defined(__cplusplus) && defined(__STDC__)
#if __STDC__ == 1
#define BMD_IS_STDC 1
#endif // __STDC__
#endif

// MSVC 2022 >= 17.2 can do __STDC__
#if defined(_MSC_VER)
#if _MSC_VER <= 1932
#define BMD_MSVC_PRE_172
#endif // _MSC_VER <= 1932
#endif

#if !defined(BMD_IS_STDC) && !defined(BMD_MSVC_PRE_172)
#error "Black Magic Debug must be built in a standards compliant C mode"
#endif

#ifndef _GNU_SOURCE
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
#define _DEFAULT_SOURCE
#endif
#if !defined(__USE_MINGW_ANSI_STDIO)
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
#define __USE_MINGW_ANSI_STDIO 1
#endif
#if defined(_WIN32) || defined(__CYGWIN__)
#include <malloc.h>
/* `alloca()` on FreeBSD is visible from <stdlib.h>, and <alloca.h> does not exist */
#elif !defined(__FreeBSD__)
#include <alloca.h>
#endif
// IWYU pragma: begin_keep
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <inttypes.h>
#include <sys/types.h>

#include "timing.h"
#include "platform_support.h"
#include "align.h"
// IWYU pragma: end_keep

#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#define FREQ_FIXED 0xffffffffU

#if PC_HOSTED == 0
/*
 * XXX: This is not really the proper place for all this as this is too intrusive into
 * the rest of the code base. The correct way to do this would be to define a debug
 * logging layer and allow BMDA to override the default logging subsystem via
 * weak symbols. This would also allow a user to choose to compile, eg, wire
 * debugging into the firmware which may be useful for development.
 */
#if !defined(PLATFORM_PRINTF)
#define PLATFORM_PRINTF printf
#endif
#define PRINT_NOOP(...) \
	do {                \
	} while (false)

#ifndef ENABLE_DEBUG
#define ENABLE_DEBUG 0
#endif

#if ENABLE_DEBUG == 1
#define DEBUG_ERROR(...) PLATFORM_PRINTF(__VA_ARGS__)
#define DEBUG_WARN(...)  PLATFORM_PRINTF(__VA_ARGS__)
#define DEBUG_INFO(...)  PLATFORM_PRINTF(__VA_ARGS__)
#else
#define DEBUG_ERROR(...) PRINT_NOOP(__VA_ARGS__)
#define DEBUG_ERROR_IS_NOOP
#define DEBUG_WARN(...) PRINT_NOOP(__VA_ARGS__)
#define DEBUG_WARN_IS_NOOP
#define DEBUG_INFO(...) PRINT_NOOP(__VA_ARGS__)
#define DEBUG_INFO_IS_NOOP
#endif
#define DEBUG_GDB(...) PRINT_NOOP(__VA_ARGS__)
#define DEBUG_GDB_IS_NOOP
#define DEBUG_TARGET(...) PRINT_NOOP(__VA_ARGS__)
#define DEBUG_TARGET_IS_NOOP
#define DEBUG_PROTO(...) PRINT_NOOP(__VA_ARGS__)
#define DEBUG_PROTO_IS_NOOP
#define DEBUG_PROBE(...) PRINT_NOOP(__VA_ARGS__)
#define DEBUG_PROBE_IS_NOOP
#define DEBUG_WIRE(...) PRINT_NOOP(__VA_ARGS__)
#define DEBUG_WIRE_IS_NOOP

void debug_serial_send_stdout(const uint8_t *data, size_t len);
#else
#include "debug.h"

#define DEBUG_ERROR(...)  debug_error(__VA_ARGS__)
#define DEBUG_WARN(...)   debug_warning(__VA_ARGS__)
#define DEBUG_INFO(...)   debug_info(__VA_ARGS__)
#define DEBUG_GDB(...)    debug_gdb(__VA_ARGS__)
#define DEBUG_TARGET(...) debug_target(__VA_ARGS__)
#define DEBUG_PROTO(...)  debug_protocol(__VA_ARGS__)
#define DEBUG_PROBE(...)  debug_probe(__VA_ARGS__)
#define DEBUG_WIRE(...)   debug_wire(__VA_ARGS__)
#endif

#undef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#undef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

/* Define this macro helper if the compiler doesn't for us */
#ifndef __has_c_attribute
#define __has_c_attribute(x) 0
#endif

/* If we're in C23 mode or newer, we can use the proper fallthrough attribute */
#if __STDC_VERSION__ >= 202311L && __has_c_attribute(fallthrough)
#define BMD_FALLTHROUGH [[fallthrough]];
/* If we're on Clang, we can use the old style attribute on Clang 10 and newer */
#elif defined(__clang__) && __clang_major__ >= 10
#define BMD_FALLTHROUGH __attribute__((fallthrough));
/* If we're on GCC then we have to be on at least GCC 7 for the attribute */
#elif defined(__GNUC__) && __GNUC__ >= 7
#define BMD_FALLTHROUGH __attribute__((fallthrough));
/* If none of the above is true, make the annotation a no-op */
#else
#define BMD_FALLTHROUGH
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define BMD_UNUSED
#else
#define BMD_UNUSED __attribute__((unused))
#endif

#ifdef _MSC_VER
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp

// FIXME: BMDA still uses this function in gdb_packet.c
// It's defined here as an export from utils.c would pollute the ABI of libbmd
static inline int vasprintf(char **strp, const char *const fmt, va_list ap)
{
	const int actual_size = vsnprintf(NULL, 0, fmt, ap);
	if (actual_size < 0)
		return -1;

	*strp = malloc(actual_size + 1);
	if (!*strp)
		return -1;

	return vsnprintf(*strp, actual_size + 1, fmt, ap);
}

#endif /* _MSC_VER */

#endif /* INCLUDE_GENERAL_H */
