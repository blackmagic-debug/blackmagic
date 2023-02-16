/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
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

/* Convenience functions to convert to/from ascii strings of hex digits. */

#include "general.h"
#include "hex_utils.h"

char hex_digit(const uint8_t value)
{
	char digit = (char)value;
	if (value > 9U)
		digit += 'A' - '0' - 10U;
	digit += '0';
	return digit;
}

char *hexify(char *const hex, const void *const buf, const size_t size)
{
	char *dst = hex;
	const uint8_t *const src = buf;

	for (size_t idx = 0; idx < size; ++idx) {
		*dst++ = hex_digit(src[idx] >> 4U);
		*dst++ = hex_digit(src[idx] & 0xfU);
	}
	*dst = 0;

	return hex;
}

uint8_t unhex_digit(const char hex)
{
	uint8_t tmp = hex - '0';
	if (tmp > 9U)
		tmp -= 'A' - '0' - 10U;
	if (tmp > 16U)
		tmp -= 'a' - 'A';
	return tmp;
}

char *unhexify(void *const buf, const char *hex, const size_t size)
{
	uint8_t *const dst = buf;
	for (size_t idx = 0; idx < size; ++idx, hex += 2U)
		dst[idx] = (unhex_digit(hex[0]) << 4U) | unhex_digit(hex[1]);
	return buf;
}
