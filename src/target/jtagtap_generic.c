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

void jtagtap_tms_seq(const uint32_t tms_states, const size_t clock_cycles)
{
	for (size_t cycle = 0; cycle < clock_cycles; ++cycle) {
		const bool tms = (tms_states >> cycle) & 1U;
		jtag_proc.jtagtap_next(tms, true);
	}
}

void jtagtap_tdi_tdo_seq(
	uint8_t *const data_out, const uint8_t final_tms, const uint8_t *const data_in, const size_t clock_cycles)
{
	uint8_t value = 0;
	for (size_t cycle = 0; cycle < clock_cycles; ++cycle) {
		const size_t bit = cycle & 7U;
		const size_t byte = cycle >> 3U;
		const bool tms = cycle + 1U >= clock_cycles && final_tms;
		const bool tdi = data_in[byte] & (1U << bit);

		if (jtag_proc.jtagtap_next(tms, tdi))
			value |= 1U << bit;

		if (bit == 7U) {
			data_out[byte] = value;
			value = 0;
		}
	}
}

void jtagtap_tdi_seq(const uint8_t final_tms, const uint8_t *const data_in, const size_t clock_cycles)
{
	for (size_t cycle = 0; cycle < clock_cycles; ++cycle) {
		const size_t bit = cycle & 7U;
		const size_t byte = cycle >> 3U;
		const bool tms = cycle + 1U >= clock_cycles && final_tms;
		const bool tdi = data_in[byte] & (1U << bit);
		jtag_proc.jtagtap_next(tms, tdi);
	}
}
