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

#ifndef INCLUDE_HEX_UTILS_H
#define INCLUDE_HEX_UTILS_H

#include <stdint.h>
#include <stddef.h>

char *hexify(char *hex, const void *buf, size_t size);
char *unhexify(void *buf, const char *hex, size_t size);

char hex_digit(uint8_t value);
uint8_t unhex_digit(char hex);

uint64_t hex_string_to_num(size_t max_digits, const char *str);

static inline bool is_hex(const char x)
{
	return (x >= '0' && x <= '9') || (x >= 'A' && x <= 'F') || (x >= 'a' && x <= 'f');
}

#define READ_HEX_NO_FOLLOW '\xff'

bool read_unum32(const char *input, const char **rest, uint32_t *val, char follow, int base);

static inline bool read_hex32(const char *input, const char **rest, uint32_t *val, char follow)
{
	return read_unum32(input, rest, val, follow, 16);
}

static inline bool read_dec32(const char *input, const char **rest, uint32_t *val, char follow)
{
	return read_unum32(input, rest, val, follow, 10);
}

#endif /* INCLUDE_HEX_UTILS_H */
