/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdarg.h>
#include "general.h"
#include "debug.h"

uint16_t bmda_debug_flags = BMD_DEBUG_ERROR | BMD_DEBUG_WARNING;

static void debug_print(const uint16_t level, const char *format, va_list args)
{
	/* Check if the required level is enabled */
	if (!(bmda_debug_flags & level))
		return;
	/* Check to see which of stderr and stdout the message should go to */
	FILE *const where = bmda_debug_flags & BMD_DEBUG_USE_STDERR ? stderr : stdout;
	/* And shoot the message to the correct place */
	(void)vfprintf(where, format, args);
	/* Note: we have no useful way to use the output of the above call, so we ignore it. */
}

#define DEBUG_PRINT(level)              \
	va_list args;                       \
	va_start(args, format);             \
	debug_print((level), format, args); \
	va_end(args)

void debug_error(const char *format, ...)
{
	DEBUG_PRINT(BMD_DEBUG_ERROR);
}

void debug_warning(const char *format, ...)
{
	DEBUG_PRINT(BMD_DEBUG_WARNING);
}

void debug_info(const char *format, ...)
{
	DEBUG_PRINT(BMD_DEBUG_INFO);
}

void debug_gdb(const char *format, ...)
{
	DEBUG_PRINT(BMD_DEBUG_GDB);
}

void debug_target(const char *format, ...)
{
	DEBUG_PRINT(BMD_DEBUG_TARGET);
}

void debug_protocol(const char *format, ...)
{
	DEBUG_PRINT(BMD_DEBUG_PROTO);
}

void debug_probe(const char *format, ...)
{
	DEBUG_PRINT(BMD_DEBUG_PROBE);
}

void debug_wire(const char *format, ...)
{
	DEBUG_PRINT(BMD_DEBUG_WIRE);
}
