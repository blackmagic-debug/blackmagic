/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2008  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
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

/* Low level JTAG implementation using FTDI parts via libftdi. */

#include "general.h"
#include <unistd.h>
#include <assert.h>
#include <ftdi.h>
#include "ftdi_bmp.h"

static void ftdi_jtag_reset(void);
static void ftdi_jtag_tms_seq(uint32_t tms_states, size_t clock_cycles);
static void ftdi_jtag_tdi_seq(bool final_tms, const uint8_t *data_in, size_t clock_cycles);
static bool ftdi_jtag_next(bool tms, bool tdi);

/*
 * Throughout this file you will see command buffers being built which have the following basic form:
 *
 * The command block (3 bytes):
 * ┌─────────┬─────────────┬───────────┐
 * │    0    │      1      │     2     │
 * ├─────────┼─────────────┼───────────┤
 * │ Command │ Cycle count │ Data bits │
 * │         │     (-1)    │    LBE    │
 * └─────────┴─────────────┴───────────┘
 * where LBE == Little Bit Endian.
 *
 * These are then sequenced into command buffers:
 * ┌─────────────────┬─────┐
 * │ Command block 0 │ ... │
 * └─────────────────┴─────┘
 *
 * Each command block is allowed to handle at most 7 clock cycles - why not 8 is undocumented.
 */

void ftdi_jtag_drain_potential_garbage(void)
{
	uint8_t data[16];
	int garbage = ftdi_read_data(bmda_probe_info.ftdi_ctx, data, sizeof(data));
	if (garbage > 0) {
		DEBUG_WARN("FTDI JTAG init got garbage:");
		for (int i = 0; i < garbage; i++)
			DEBUG_WARN(" %02x", data[i]);
		DEBUG_WARN("\n");
	}
}

bool ftdi_jtag_init(void)
{
	if (active_cable.mpsse_swd_read.set_data_low == MPSSE_DO && active_cable.mpsse_swd_write.set_data_low == MPSSE_DO) {
		DEBUG_ERROR("JTAG not possible with resistor SWD!\n");
		return false;
	}

	jtag_proc.jtagtap_reset = ftdi_jtag_reset;
	jtag_proc.jtagtap_next = ftdi_jtag_next;
	jtag_proc.jtagtap_tms_seq = ftdi_jtag_tms_seq;
	jtag_proc.jtagtap_tdi_tdo_seq = ftdi_jtag_tdi_tdo_seq;
	jtag_proc.jtagtap_tdi_seq = ftdi_jtag_tdi_seq;
	jtag_proc.tap_idle_cycles = 1;

	active_state.data[0] |= active_cable.jtag.set_data_low | MPSSE_CS | MPSSE_DI | MPSSE_DO;
	active_state.data[0] &= ~(active_cable.jtag.clr_data_low | MPSSE_SK);
	active_state.dirs[0] |= MPSSE_CS | MPSSE_DO | MPSSE_SK;
	active_state.dirs[0] &= ~MPSSE_DI;
	active_state.data[1] |= active_cable.jtag.set_data_high;
	active_state.data[1] &= ~active_cable.jtag.clr_data_high;
	ftdi_jtag_drain_potential_garbage();

	const uint8_t cmd[6] = {
		SET_BITS_LOW,
		active_state.data[0],
		active_state.dirs[0],
		SET_BITS_HIGH,
		active_state.data[1],
		active_state.dirs[1],
	};
	ftdi_buffer_write_arr(cmd);
	ftdi_buffer_flush();
	/* Write out start condition and pull garbage from read buffer.
	 * FT2232D otherwise misbehaves on runs following the first run.*/
	ftdi_jtag_drain_potential_garbage();

	/* Ensure we're in JTAG mode */
	for (size_t i = 0; i <= 50U; ++i)
		ftdi_jtag_next(true, false); /* 50 + 1 idle cycles for SWD reset */
	ftdi_jtag_tms_seq(0xe73cU, 16U); /* SWD to JTAG sequence */
	return true;
}

static void ftdi_jtag_reset(void)
{
	jtagtap_soft_reset();
}

static void ftdi_jtag_tms_seq(uint32_t tms_states, const size_t clock_cycles)
{
	for (size_t cycle = 0U; cycle < clock_cycles; cycle += 7U) {
		const uint8_t cmd[3U] = {
			MPSSE_WRITE_TMS | MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG,
			MIN(7U, clock_cycles - cycle) - 1U,
			0x80U | (tms_states & 0x7fU),
		};
		tms_states >>= 7U;
		ftdi_buffer_write_arr(cmd);
	}
}

static void ftdi_jtag_tdi_seq(const bool final_tms, const uint8_t *const data_in, const size_t clock_cycles)
{
	ftdi_jtag_tdi_tdo_seq(NULL, final_tms, data_in, clock_cycles);
}

static bool ftdi_jtag_next(const bool tms, const bool tdi)
{
	const uint8_t cmd[3] = {
		MPSSE_WRITE_TMS | MPSSE_DO_READ | MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG,
		0,
		(tdi ? 0x80U : 0U) | (tms ? 0x01U : 0U),
	};
	ftdi_buffer_write_arr(cmd);

	uint8_t ret = 0;
	ftdi_buffer_read_val(ret);
	return ret & 0x80U;
}
