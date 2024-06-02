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

uint64_t hex_string_to_num(const size_t max_digits, const char *const str)
{
	uint64_t ret = 0;
	for (size_t offset = 0; offset < max_digits; ++offset) {
		const char value = str[offset];
		if (!is_hex(value))
			return ret;
		ret <<= 4U;
		ret |= unhex_digit(value);
	}
	return ret;
}

/*
 * This function attempts to read a number, starting from the given input pointer and stores the
 * result in val.
 * It performs a number of helpful additional tasks needed for number parsing in BMD.
 * It stores the location of the character after the last one considered into rest if non-NULL
 * this is useful for chaining these calls or to parse mutliple fields.
 * It accepts a char parameter called follow, which if not set to READ_HEX_NO_FOLLOW will check
 * the character after the number is equal to that character and report failure if it is not.
 * If the follow character is matched then the value stored into the rest pointer will be after
 * this following character.
 * This routine also accepts a base to operate on as it is the common function used by the two
 * wrappers read_hex32 and read_dec32 for hexadecimal and decimal numbers respectively.
 * 
 * This routine uses strtoul internally so all the number parsing behaviour of that apply,
 * it will accept a leading + or - and perform negation of the read number correctly.
 * If passed 0 as the base it will accept hex numbers prefixed with 0x or 0X and octal numbers
 * prefixed with 0, otherwise it will assume decimal.
 * It will skip white space preceding the number to be read.
 * 
 * The resturn value indicates whether a number has been successfully read, if the function returns
 * true then rest and val have been assigned if non-NULL.
 */
bool read_unum32(const char *input, const char **rest, uint32_t *val, char follow, int base)
{
	char *end = NULL;
	uint32_t result = strtoul(input, &end, base);
	if (end == NULL || end == input)
		return false;
	if (follow != READ_HEX_NO_FOLLOW) {
		if (*end == follow)
			end++;
		else
			return false;
	}
	if (rest)
		*rest = end;
	if (val)
		*val = result;
	return true;
}
