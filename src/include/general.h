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
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <inttypes.h>
#include <sys/types.h>

#include "maths_utils.h"
#include "timing.h"
#include "platform_support.h"
#include "align.h"

#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#define FREQ_FIXED 0xffffffffU

#if PC_HOSTED == 0
/*
 * XXX: This entire system needs replacing with something better thought out
 *
 * When built as firmware, if the target supports debugging, DEBUG_ERROR, DEBUG_WARN and
 * DEBUG_INFO get defined to a macro that turns them into printf() calls. The rest of the
 * levels turn into no-ops.
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
#define DEBUG_ERROR(...) PLATFORM_PRINTF(__VA_ARGS__)
#define DEBUG_WARN(...)  PLATFORM_PRINTF(__VA_ARGS__)
#define DEBUG_INFO(...)  PLATFORM_PRINTF(__VA_ARGS__)
#else
#define DEBUG_ERROR(...) PRINT_NOOP(__VA_ARGS__)
#define DEBUG_WARN(...)  PRINT_NOOP(__VA_ARGS__)
#define DEBUG_INFO(...)  PRINT_NOOP(__VA_ARGS__)
#endif
#define DEBUG_GDB(...)    PRINT_NOOP(__VA_ARGS__)
#define DEBUG_TARGET(...) PRINT_NOOP(__VA_ARGS__)
#define DEBUG_PROTO(...)  PRINT_NOOP(__VA_ARGS__)
#define DEBUG_PROBE(...)  PRINT_NOOP(__VA_ARGS__)
#define DEBUG_WIRE(...)   PRINT_NOOP(__VA_ARGS__)

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

#endif /* INCLUDE_GENERAL_H */
