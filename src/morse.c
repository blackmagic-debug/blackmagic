/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
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
#include "general.h"
#include "morse.h"

/* Morse code patterns and lengths */
typedef struct {
	uint16_t code;
	uint8_t bits;
} morse_char_s;

static const morse_char_s morse_char_lut[] = {
	{0x001dU, 8U},  // 'A' .-   0b0000000000011101
	{0x0157U, 12U}, // 'B' -... 0b0000000101010111
	{0x05d7U, 14U}, // 'C' -.-. 0b0000010111010111
	{0x0057U, 10U}, // 'D' -..  0b0000000001010111
	{0x0001U, 4U},  // 'E' .    0b0000000000000001
	{0x0175U, 12U}, // 'F' ..-. 0b0000000101110101
	{0x0177U, 12U}, // 'G' --.  0b0000000101110111
	{0x0055U, 10U}, // 'H' .... 0b0000000001010101
	{0x0005U, 6U},  // 'I' ..   0b0000000000000101
	{0x1dddU, 16U}, // 'J' .--- 0b0001110111011101
	{0x01d7U, 12U}, // 'K' -.-  0b0000000111010111
	{0x015dU, 12U}, // 'L' .-.. 0b0000000101011101
	{0x0077U, 10U}, // 'M' --   0b0000000001110111
	{0x0017U, 8U},  // 'N' -.   0b0000000000010111
	{0x0777U, 14U}, // 'O' ---  0b0000011101110111
	{0x05ddU, 14U}, // 'P' .--. 0b0000010111011101
	{0x1d77U, 16U}, // 'Q' --.- 0b0001110101110111
	{0x005dU, 10U}, // 'R' .-.  0b0000000001011101
	{0x0015U, 8U},  // 'S' ...  0b0000000000010101
	{0x0007U, 6U},  // 'T' -    0b0000000000000111
	{0x0075U, 10U}, // 'U' ..-  0b0000000001110101
	{0x01d5U, 12U}, // 'V' ...- 0b0000000111010101
	{0x01ddU, 12U}, // 'W' .--  0b0000000111011101
	{0x0757U, 14U}, // 'X' -..- 0b0000011101010111
	{0x1dd7U, 16U}, // 'Y' -.-- 0b0001110111010111
	{0x0577U, 14U}, // 'Z' --.. 0b0000010101110111
};

volatile const char *morse_msg = NULL;
static volatile size_t msg_index = SIZE_MAX;
static volatile bool morse_repeat = false;

void morse(const char *const msg, const bool repeat)
{
#if PC_HOSTED == 1
	if (msg)
		DEBUG_WARN("%s\n", msg);
	(void)repeat;
#else
	morse_repeat = repeat;
	msg_index = msg ? 0 : SIZE_MAX;
	morse_msg = msg;
#endif
}

bool morse_update(void)
{
	static uint16_t code;
	static uint8_t bits;

	if (msg_index == SIZE_MAX)
		return false;

	if (!bits) {
		char morse_char = morse_msg[msg_index++];
		if (!morse_char) {
			if (morse_repeat) {
				morse_char = morse_msg[0];
				msg_index = 1U;
			} else {
				msg_index = SIZE_MAX;
				return false;
			}
		}
		if (morse_char >= 'A' && morse_char <= 'Z') {
			const uint8_t morse_char_index = (uint8_t)morse_char - 'A';
			code = morse_char_lut[morse_char_index].code;
			bits = morse_char_lut[morse_char_index].bits;
		} else {
			code = 0U;
			bits = 4U;
		}
	}

	const bool result = code & 1U;
	code >>= 1U;
	--bits;

	return result;
}
