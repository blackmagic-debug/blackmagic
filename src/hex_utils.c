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

/* Convenience function to convert to/from ascii strings of hex digits.
 */

#include "general.h"
#include "hex_utils.h"

static char hexdigits[] = "0123456789abcdef";

char * hexify(char *hex, const unsigned char *buf, int size)
{
	char *tmp = hex;

	while(size--) {
		*tmp++ = hexdigits[*buf >> 4];
		*tmp++ = hexdigits[*buf++ & 0xF];
	}
	*tmp++ = 0;

	return hex;
}

static uint8_t unhex_digit(char hex)
{
	uint8_t tmp = hex - '0';
	if(tmp > 9)
		tmp -= 'A' - '0' - 10;
	if(tmp > 16)
		tmp -= 'a' - 'A';
	return tmp;
}

char * unhexify(unsigned char *buf, const char *hex, int size)
{
	while(size--) {
		*buf = unhex_digit(*hex++) << 4;
		*buf++ |= unhex_digit(*hex++);
	}
	return buf;
}

