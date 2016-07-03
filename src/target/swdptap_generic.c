/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2016  Black Sphere Technologies Ltd.
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
#include "swdptap.h"

uint32_t __attribute__((weak))
swdptap_seq_in(int ticks)
{
	uint32_t index = 1;
	uint32_t ret = 0;

	while (ticks--) {
		if (swdptap_bit_in())
			ret |= index;
		index <<= 1;
	}

	return ret;
}

bool __attribute__((weak))
swdptap_seq_in_parity(uint32_t *ret, int ticks)
{
	uint32_t index = 1;
	uint8_t parity = 0;
	*ret = 0;

	while (ticks--) {
		if (swdptap_bit_in()) {
			*ret |= index;
			parity ^= 1;
		}
		index <<= 1;
	}
	if (swdptap_bit_in())
		parity ^= 1;

	return parity;
}

void __attribute__((weak))
swdptap_seq_out(uint32_t MS, int ticks)
{
	while (ticks--) {
		swdptap_bit_out(MS & 1);
		MS >>= 1;
	}
}

void __attribute__((weak))
swdptap_seq_out_parity(uint32_t MS, int ticks)
{
	uint8_t parity = 0;

	while (ticks--) {
		swdptap_bit_out(MS & 1);
		parity ^= MS;
		MS >>= 1;
	}
	swdptap_bit_out(parity & 1);
}

