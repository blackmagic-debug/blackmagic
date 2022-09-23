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

/* Convenience function to convert to/from ascii strings of hex digits. */

#include "general.h"
#include "hex_utils.h"

static const char hexdigits[] = "0123456789abcdef";

char *hexify(char *hex, const void *buf, const size_t size)
{
	char *dst = hex;
	const uint8_t *const src = buf;

	for (size_t idx = 0; idx < size; ++idx) {
		*dst++ = hexdigits[src[idx] >> 4];
		*dst++ = hexdigits[src[idx] & 0xF];
	}
	*dst++ = 0;

	return hex;
}

static uint8_t unhex_digit(char hex)
{
	uint8_t tmp = hex - '0';
	if (tmp > 9)
		tmp -= 'A' - '0' - 10;
	if (tmp > 16)
		tmp -= 'a' - 'A';
	return tmp;
}

char *unhexify(void *buf, const char *hex, const size_t size)
{
	uint8_t *const dst = buf;
	for (size_t idx = 0; idx < size; ++idx, hex += 2) {
		dst[idx] = (unhex_digit(hex[0]) << 4) | unhex_digit(hex[1]);
	}
	return buf;
}
