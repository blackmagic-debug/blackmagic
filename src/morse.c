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
static const struct {
	uint16_t code;
	uint8_t bits;
} morse_letter[] = {
	{0b0000000000011101, 8},  // 'A' .-
	{0b0000000101010111, 12}, // 'B' -...
	{0b0000010111010111, 14}, // 'C' -.-.
	{0b0000000001010111, 10}, // 'D' -..
	{0b0000000000000001, 4},  // 'E' .
	{0b0000000101110101, 12}, // 'F' ..-.
	{0b0000000101110111, 12}, // 'G' --.
	{0b0000000001010101, 10}, // 'H' ....
	{0b0000000000000101, 6},  // 'I' ..
	{0b0001110111011101, 16}, // 'J' .---
	{0b0000000111010111, 12}, // 'K' -.-
	{0b0000000101011101, 12}, // 'L' .-..
	{0b0000000001110111, 10}, // 'M' --
	{0b0000000000010111, 8},  // 'N' -.
	{0b0000011101110111, 14}, // 'O' ---
	{0b0000010111011101, 14}, // 'P' .--.
	{0b0001110101110111, 16}, // 'Q' --.-
	{0b0000000001011101, 10}, // 'R' .-.
	{0b0000000000010101, 8},  // 'S' ...
	{0b0000000000000111, 6},  // 'T' -
	{0b0000000001110101, 10}, // 'U' ..-
	{0b0000000111010101, 12}, // 'V' ...-
	{0b0000000111011101, 12}, // 'W' .--
	{0b0000011101010111, 14}, // 'X' -..-
	{0b0001110111010111, 16}, // 'Y' -.--
	{0b0000010101110111, 14}, // 'Z' --..
};

const char *morse_msg = NULL;
static volatile size_t msg_index = SIZE_MAX;
static bool morse_repeat = false;

void morse(const char *const msg, const bool repeat)
{
#if PC_HOSTED == 1
	if (msg)
		DEBUG_WARN("%s\n", msg);
	(void)repeat;
#else
	morse_msg = msg;
	msg_index = 0;
	morse_repeat = repeat;
#endif
}

bool morse_update(void)
{
	static uint16_t code;
	static uint8_t bits;

	if (msg_index == SIZE_MAX)
		return false;

	if (!bits) {
		char c = morse_msg[msg_index++];
		if (!c) {
			if (morse_repeat) {
				c = morse_msg[0];
				msg_index = 1U;
			} else {
				msg_index = SIZE_MAX;
				return false;
			}
		}
		if (c >= 'A' && c <= 'Z') {
			c -= 'A';
			code = morse_letter[c].code;
			bits = morse_letter[c].bits;
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
