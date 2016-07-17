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

/* This file provides generic forms of the low-level jtagtap functions
 * for platforms that don't require optimised forms.
 */
#include "general.h"
#include "jtagtap.h"

void __attribute__((weak))
jtagtap_tms_seq(uint32_t MS, int ticks)
{
	while(ticks--) {
		jtagtap_next(MS & 1, 1);
		MS >>= 1;
	}
}

void __attribute__((weak))
jtagtap_tdi_tdo_seq(uint8_t *DO, const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	uint8_t index = 1;
	while(ticks--) {
		if(jtagtap_next(ticks?0:final_tms, *DI & index)) {
			*DO |= index;
		} else {
			*DO &= ~index;
		}
		if(!(index <<= 1)) {
			index = 1;
			DI++; DO++;
		}
	}
}

void __attribute__((weak))
jtagtap_tdi_seq(const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	uint8_t index = 1;
	while(ticks--) {
		jtagtap_next(ticks?0:final_tms, *DI & index);
		if(!(index <<= 1)) {
			index = 1;
			DI++;
		}
	}
}

