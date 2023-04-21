/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

#include "general.h"

#ifdef PLATFORM_EXP_DEBUG
extern size_t platform_debug_usart_send(const char *buf, const size_t len);
#endif

/*
 * newlib defines _write as a weak link'd function for user code to override.
 *
 * This function forms the root of the implementation of a variety of functions
 * that can write to stdout/stderr, including printf().
 *
 * The result of this function is the number of bytes written.
 */
/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
int _write(const int file, const void *const ptr, const size_t len)
{
	(void)file;
#ifdef PLATFORM_HAS_DEBUG
	if (debug_bmp)
		return debug_serial_debug_write(ptr, len);
#else
	(void)ptr;
#endif
#ifdef PLATFORM_EXP_DEBUG
	return platform_debug_usart_send(ptr, len);
#endif
	return len;
}