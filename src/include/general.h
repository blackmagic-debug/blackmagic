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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#if !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <inttypes.h>
#include <sys/types.h>

#include "platform.h"
#include "platform_support.h"

#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

extern uint32_t delay_cnt;

#define BMP_DEBUG_NONE   0U
#define BMP_DEBUG_INFO   (1U << 0U)
#define BMP_DEBUG_GDB    (1U << 1U)
#define BMP_DEBUG_TARGET (1U << 2U)
#define BMP_DEBUG_PROBE  (1U << 3U)
#define BMP_DEBUG_WIRE   (1U << 4U)
#define BMP_DEBUG_MAX    (1U << 5U)
#define BMP_DEBUG_STDOUT (1U << 15U)

#define FREQ_FIXED 0xffffffffU

#if PC_HOSTED == 0
/*
 * XXX: This entire system needs replacing with something better thought out
 * XXX: This has no error diagnostic level.
 *
 * When built as firmware, if the target supports debugging, DEBUG_WARN and DEBUG_INFO
 * get defined to a macro that turns them into printf() calls. The rest of the levels
 * turn into no-ops.
 *
 * When built as BMDA, the debug macros all turn into various kinds of console-printing
 * function, w/ gating for diagnostics other than warnings and info.
 *
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
#if defined(ENABLE_DEBUG)
#define DEBUG_WARN(...) PLATFORM_PRINTF(__VA_ARGS__)
#define DEBUG_INFO(...) PLATFORM_PRINTF(__VA_ARGS__)
#else
#define DEBUG_WARN(...) PRINT_NOOP(__VA_ARGS__)
#define DEBUG_INFO(...) PRINT_NOOP(__VA_ARGS__)
#endif
#define DEBUG_GDB(...)      PRINT_NOOP(__VA_ARGS__)
#define DEBUG_TARGET(...)   PRINT_NOOP(__VA_ARGS__)
#define DEBUG_PROBE(...)    PRINT_NOOP(__VA_ARGS__)
#define DEBUG_WIRE(...)     PRINT_NOOP(__VA_ARGS__)
#define DEBUG_GDB_WIRE(...) PRINT_NOOP(__VA_ARGS__)

void debug_serial_send_stdout(const uint8_t *data, size_t len);
#else
#include <stdarg.h>
extern int cl_debuglevel;

static inline void DEBUG_WARN(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	return;
}

static inline void DEBUG_INFO(const char *format, ...)
{
	if (~cl_debuglevel & BMP_DEBUG_INFO)
		return;
	va_list ap;
	va_start(ap, format);
	if (cl_debuglevel & BMP_DEBUG_STDOUT)
		vfprintf(stdout, format, ap);
	else
		vfprintf(stderr, format, ap);
	va_end(ap);
	return;
}

static inline void DEBUG_GDB(const char *format, ...)
{
	if (~cl_debuglevel & BMP_DEBUG_GDB)
		return;
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	return;
}

static inline void DEBUG_GDB_WIRE(const char *format, ...)
{
	if ((cl_debuglevel & (BMP_DEBUG_GDB | BMP_DEBUG_WIRE)) != (BMP_DEBUG_GDB | BMP_DEBUG_WIRE))
		return;
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	return;
}

static inline void DEBUG_TARGET(const char *format, ...)
{
	if (~cl_debuglevel & BMP_DEBUG_TARGET)
		return;
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	return;
}

static inline void DEBUG_PROBE(const char *format, ...)
{
	if (~cl_debuglevel & BMP_DEBUG_PROBE)
		return;
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	return;
}

static inline void DEBUG_WIRE(const char *format, ...)
{
	if (~cl_debuglevel & BMP_DEBUG_WIRE)
		return;
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	return;
}
#endif

#define ALIGN(x, n) (((x) + (n)-1) & ~((n)-1))
#undef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#undef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

#if !defined(SYSTICKHZ)
#define SYSTICKHZ 100U
#endif

#define SYSTICKMS (1000U / SYSTICKHZ)
#define MORSECNT  ((SYSTICKHZ / 10U) - 1U)

#endif /* INCLUDE_GENERAL_H */
