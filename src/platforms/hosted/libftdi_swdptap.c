/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2018 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

/* MPSSE bit-banging SW-DP interface over FTDI with loop unrolled.
 * Speed is sensible.
 */

#include <stdio.h>
#include <assert.h>

#include "general.h"
#include <libftdi1/ftdi.h>
#include "platform.h"
#include "ftdi_bmp.h"

enum  swdio_status{
	SWDIO_STATUS_DRIVE = 0,
	SWDIO_STATUS_FLOAT,
};

static enum swdio_status olddir;
static bool do_mpsse;
static bool direct_bb_swd;

#define MPSSE_MASK (MPSSE_DO | MPSSE_DI | MPSSE_CS)
#define MPSSE_TD_MASK (MPSSE_DO | MPSSE_DI)
#define MPSSE_TMS_SHIFT (MPSSE_WRITE_TMS | MPSSE_LSB |\
						 MPSSE_BITMODE | MPSSE_WRITE_NEG)
#define MPSSE_TDO_SHIFT (MPSSE_DO_WRITE | MPSSE_LSB |\
						 MPSSE_BITMODE | MPSSE_WRITE_NEG)
static void swdptap_turnaround(enum swdio_status dir)
{
	if (dir == olddir)
		return;
	olddir = dir;
	DEBUG_PROBE("Turnaround %s\n", (dir == SWDIO_STATUS_FLOAT) ? "float": "drive");
	if (do_mpsse) {
		if (dir == SWDIO_STATUS_FLOAT)	/* SWDIO goes to input */ {
			active_state.data_low |=  active_cable->mpsse_swd_read.set_data_low | MPSSE_DO;
			active_state.data_low &= ~active_cable->mpsse_swd_read.clr_data_low;
			active_state.ddr_low &= ~MPSSE_DO;
			active_state.data_high |=  active_cable->mpsse_swd_read.set_data_high;
			active_state.data_high &= ~active_cable->mpsse_swd_read.clr_data_high;
			uint8_t cmd_read[6] = {
				SET_BITS_LOW,  active_state.data_low,
				active_state.ddr_low,
				SET_BITS_HIGH, active_state.data_high, active_state.ddr_high};
			libftdi_buffer_write(cmd_read, 6);
		}
		uint8_t cmd[] = {MPSSE_TDO_SHIFT, 0, 0}; /* One clock cycle */
		libftdi_buffer_write(cmd, sizeof(cmd));
		if (dir == SWDIO_STATUS_DRIVE)  /* SWDIO goes to output */ {
			active_state.data_low |=  active_cable->mpsse_swd_write.set_data_low | MPSSE_DO;
			active_state.data_low &= ~active_cable->mpsse_swd_write.clr_data_low;
			active_state.ddr_low |= MPSSE_DO;
			active_state.data_high |=  active_cable->mpsse_swd_write.set_data_high;
			active_state.data_high &= ~active_cable->mpsse_swd_write.clr_data_high;
			uint8_t cmd_write[6] = {
				SET_BITS_LOW,  active_state.data_low,
				active_state.ddr_low,
				SET_BITS_HIGH, active_state.data_high, active_state.ddr_high};
			libftdi_buffer_write(cmd_write, 6);
		}
	} else {
		uint8_t cmd[9];
		int index = 0;

		if(dir == SWDIO_STATUS_FLOAT)	  { /* SWDIO goes to input */
			if (direct_bb_swd) {
				active_state.data_low |=  MPSSE_CS;
				active_state.ddr_low  &= ~MPSSE_CS;
			} else {
				active_state.data_low  |=  active_cable->bb_swd_read.set_data_low;
				active_state.data_low  &= ~active_cable->bb_swd_read.clr_data_low;
				active_state.data_high |=  active_cable->bb_swd_read.set_data_high;
				active_state.data_high &= ~active_cable->bb_swd_read.clr_data_high;
			}
			cmd[index++] = SET_BITS_LOW;
			cmd[index++] = active_state.data_low;
			cmd[index++] = active_state.ddr_low;
			cmd[index++] = SET_BITS_HIGH;
			cmd[index++] = active_state.data_high;
			cmd[index++] = active_state.ddr_high;
		}
		/* One clock cycle */
		cmd[index++] = MPSSE_TMS_SHIFT;
		cmd[index++] = 0;
		cmd[index++] = 0;
		if (dir == SWDIO_STATUS_DRIVE) {
			if (direct_bb_swd) {
				active_state.data_low |=  MPSSE_CS;
				active_state.ddr_low  |=  MPSSE_CS;
			} else {
				active_state.data_low  |=  active_cable->bb_swd_write.set_data_low;
				active_state.data_low  &= ~active_cable->bb_swd_write.clr_data_low;
				active_state.data_high |=  active_cable->bb_swd_write.set_data_high;
				active_state.data_high &= ~active_cable->bb_swd_write.clr_data_high;
			}
			cmd[index++] = SET_BITS_LOW;
			cmd[index++] = active_state.data_low;
			cmd[index++] = active_state.ddr_low;
			cmd[index++] = SET_BITS_HIGH;
			cmd[index++] = active_state.data_high;
			cmd[index++] = active_state.ddr_high;
		}
		libftdi_buffer_write(cmd, index);
	}
}

static bool swdptap_seq_in_parity(uint32_t *res, int ticks);
static uint32_t swdptap_seq_in(int ticks);
static void swdptap_seq_out(uint32_t MS, int ticks);
static void swdptap_seq_out_parity(uint32_t MS, int ticks);

bool libftdi_swd_possible(bool *do_mpsse, bool *direct_bb_swd)
{
	bool swd_read =
		active_cable->mpsse_swd_read.set_data_low ||
		active_cable->mpsse_swd_read.clr_data_low ||
		active_cable->mpsse_swd_read.set_data_high ||
		active_cable->mpsse_swd_read.clr_data_high;
	bool swd_write =
		active_cable->mpsse_swd_write.set_data_low ||
		active_cable->mpsse_swd_write.clr_data_low ||
		active_cable->mpsse_swd_write.set_data_high ||
		active_cable->mpsse_swd_write.clr_data_high;
	bool mpsse = swd_read && swd_write;
	if (do_mpsse)
		*do_mpsse = mpsse;
	if (!mpsse) {
		bool bb_swd_read =
			active_cable->bb_swd_read.set_data_low ||
			active_cable->bb_swd_read.clr_data_low ||
			active_cable->bb_swd_read.set_data_high ||
			active_cable->bb_swd_read.clr_data_high;
		bool bb_swd_write =
			active_cable->bb_swd_write.set_data_low ||
			active_cable->bb_swd_write.clr_data_low ||
			active_cable->bb_swd_write.set_data_high ||
			active_cable->bb_swd_write.clr_data_high;
		bool bb_direct_possible =
			active_cable->bb_swdio_in_port_cmd == GET_BITS_LOW &&
			active_cable->bb_swdio_in_pin == MPSSE_CS;
		if (!bb_swd_read && !bb_swd_write) {
			if (!bb_direct_possible)
				return false;
		}
		if (direct_bb_swd)
			*direct_bb_swd = true;
	}
	return true;
}

int libftdi_swdptap_init(swd_proc_t *swd_proc)
{
	if (!libftdi_swd_possible(&do_mpsse, &direct_bb_swd)) {
		DEBUG_WARN("SWD not possible or missing item in cable description.\n");
		return -1;
	}
	active_state.data_low |=   MPSSE_CS | MPSSE_DI | MPSSE_DO;
	active_state.data_low &=   MPSSE_SK;
	active_state.ddr_low  |=   MPSSE_SK;
	active_state.ddr_low  &= ~(MPSSE_CS | MPSSE_DI | MPSSE_DO);
	if (do_mpsse) {
		DEBUG_INFO("Using genuine MPSSE for SWD.\n");
		active_state.data_low  |=   active_cable->mpsse_swd_read.set_data_low;
		active_state.data_low  &= ~(active_cable->mpsse_swd_read.clr_data_low);
		active_state.data_high |=   active_cable->mpsse_swd_read.set_data_high;
		active_state.data_high &= ~(active_cable->mpsse_swd_read.clr_data_high);
	} else if (direct_bb_swd) {
		DEBUG_INFO("Using direct bitbang with SWDIO %cBUS%d.\n",
				   (active_cable->bb_swdio_in_port_cmd == GET_BITS_LOW) ? 'C' : 'D',
				   __builtin_ctz(active_cable->bb_swdio_in_pin));
	} else {
		DEBUG_INFO("Using switched bitbang for SWD.\n");
		active_state.data_low  |=   active_cable->bb_swd_read.set_data_low;
		active_state.data_low  &= ~(active_cable->bb_swd_read.clr_data_low);
		active_state.data_high |=   active_cable->bb_swd_read.set_data_high;
		active_state.data_high &= ~(active_cable->bb_swd_read.clr_data_high);
		active_state.ddr_low  |=  MPSSE_CS;
		if (active_cable->bb_swdio_in_port_cmd == GET_BITS_LOW)
			active_state.ddr_low  &= ~active_cable->bb_swdio_in_pin;
		else if (active_cable->bb_swdio_in_port_cmd == GET_BITS_HIGH)
			active_state.ddr_high &= ~active_cable->bb_swdio_in_pin;
	}
	uint8_t cmd_write[6] = {
		SET_BITS_LOW,  active_state.data_low,
		active_state.ddr_low,
		SET_BITS_HIGH, active_state.data_high, active_state.ddr_high};
	libftdi_buffer_write(cmd_write, 6);
	libftdi_buffer_flush();
	olddir = SWDIO_STATUS_FLOAT;

	swd_proc->swdptap_seq_in  = swdptap_seq_in;
	swd_proc->swdptap_seq_in_parity  = swdptap_seq_in_parity;
	swd_proc->swdptap_seq_out = swdptap_seq_out;
	swd_proc->swdptap_seq_out_parity  = swdptap_seq_out_parity;

	return 0;
}

bool swdptap_bit_in(void)
{
	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	uint8_t cmd[4];
	int index = 0;
	bool result = false;

	if (do_mpsse) {
		uint8_t cmd[2] = {MPSSE_DO_READ | MPSSE_LSB | MPSSE_BITMODE, 0};
		libftdi_buffer_write(cmd, sizeof(cmd));
		uint8_t data[1];
		libftdi_buffer_read(data, sizeof(data));
		result = (data[0] & 0x80);
	} else {
		cmd[index++] = active_cable->bb_swdio_in_port_cmd;
		cmd[index++] = MPSSE_TMS_SHIFT;
		cmd[index++] = 0;
		cmd[index++] = 0;
		libftdi_buffer_write(cmd, index);
		uint8_t data[1];
		libftdi_buffer_read(data, sizeof(data));
		result = (data[0] &= active_cable->bb_swdio_in_pin);
	}
	return result;
}

void swdptap_bit_out(bool val)
{
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	if (do_mpsse) {
		uint8_t cmd[3] = {MPSSE_TDO_SHIFT, 0, (val)? 1:0};
		libftdi_buffer_write(cmd, sizeof(cmd));
	} else {
		uint8_t cmd[3];
		cmd[0] = MPSSE_TMS_SHIFT;
		cmd[1] = 0;
		cmd[2] = (val)? 1 : 0;
		libftdi_buffer_write(cmd, sizeof(cmd));
	}
}

bool swdptap_seq_in_parity(uint32_t *res, int ticks)
{
	assert(ticks == 32);
	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	unsigned int parity = 0;
	unsigned int result = 0;
	if (do_mpsse) {
		uint8_t DO[5];
		libftdi_jtagtap_tdi_tdo_seq(DO, 0, NULL, ticks + 1);
		result = DO[0] + (DO[1] << 8) + (DO[2] << 16) + (DO[3] << 24);
		parity =  __builtin_parity(result & ((1LL << ticks) - 1)) & 1;
		parity ^= DO[4] & 1;
	} else {
		int index = ticks + 1;
		uint8_t cmd[4];

		cmd[0] = active_cable->bb_swdio_in_port_cmd;
		cmd[1] = MPSSE_TMS_SHIFT;
		cmd[2] = 0;
		cmd[3] = 0;
		while (index--) {
			libftdi_buffer_write(cmd, sizeof(cmd));
		}
		uint8_t data[33];
		libftdi_buffer_read(data, ticks + 1);
		if (data[ticks] & active_cable->bb_swdio_in_pin)
			parity ^= 1;
		index = ticks;
		while (index--) {
			if (data[index] & active_cable->bb_swdio_in_pin) {
				parity ^= 1;
				result |= (1 << index);
			}
		}
	}
	*res = result;
	return parity;
}

static uint32_t swdptap_seq_in(int ticks)
{
	if (!ticks)
		return 0;
	uint32_t result = 0;
	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	if (do_mpsse) {
		uint8_t DO[4];
		libftdi_jtagtap_tdi_tdo_seq(DO, 0, NULL, ticks);
		for (int i = 0; i < (ticks >> 3) + (ticks  & 7)? 1: 0; i++) {
			result |= DO[i] << (8 * i);
		}
	} else {
		int index = ticks;
		uint8_t cmd[4];

		cmd[0] = active_cable->bb_swdio_in_port_cmd;
		cmd[1] = MPSSE_TMS_SHIFT;
		cmd[2] = 0;
		cmd[3] = 0;

		while (index--) {
			libftdi_buffer_write(cmd, sizeof(cmd));
		}
		uint8_t data[32];
		libftdi_buffer_read(data, ticks);
		index = ticks;
		while (index--) {
			if (data[index] & active_cable->bb_swdio_in_pin)
				result |= (1 << index);
		}
	}
	return result;
}

static void swdptap_seq_out(uint32_t MS, int ticks)
{
	if (!ticks)
		return;
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	if (do_mpsse) {
		uint8_t DI[4];
		DI[0] = (MS >>  0) & 0xff;
		DI[1] = (MS >>  8) & 0xff;
		DI[2] = (MS >> 16) & 0xff;
		DI[3] = (MS >> 24) & 0xff;
		libftdi_jtagtap_tdi_tdo_seq(NULL, 0, DI, ticks);
	} else {
		uint8_t cmd[16];
		unsigned int index = 0;
		while (ticks) {
			cmd[index++] = MPSSE_TMS_SHIFT;
			if (ticks >= 7) {
				cmd[index++] = 6;
				cmd[index++] = MS & 0x7f;
				MS >>= 7;
				ticks -= 7;
			} else {
				cmd[index++] = ticks - 1;
				cmd[index++] = MS & 0x7f;
				ticks = 0;
			}
		}
		libftdi_buffer_write(cmd, index);
	}
}

/* ARM Debug Interface Architecture Specification ADIv5.0 to ADIv5.2
 * tells to clock the data through SW-DP to either :
 * - immediate start a new transaction
 * - continue to drive idle cycles
 * - or clock at least 8 idle cycles
 *
 * Implement last option to favour correctness over
 *   slight speed decrease
 */
static void swdptap_seq_out_parity(uint32_t MS, int ticks)
{
	(void) ticks;
	int parity = __builtin_parity(MS) & 1;
	unsigned int index = 0;
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	if (do_mpsse) {
		uint8_t DI[8];
		DI[0] = (MS >>  0) & 0xff;
		DI[1] = (MS >>  8) & 0xff;
		DI[2] = (MS >> 16) & 0xff;
		DI[3] = (MS >> 24) & 0xff;
		DI[4] = parity;
		DI[5] = 0;
		libftdi_jtagtap_tdi_tdo_seq(NULL, 0, DI, 32 + 1 + 8);
	} else {
		uint8_t cmd[32];
		int steps = ticks;
		while (steps) {
			cmd[index++] = MPSSE_TMS_SHIFT;
			cmd[index++] = 6;
			if (steps >= 7) {
				cmd[index++] = MS & 0x7f;
				MS >>= 7;
				steps -= 7;
			} else {
				cmd[index++] = (MS & 0x7f) | (parity << 4);
				steps = 0;
			}
		}
		cmd[index++] = MPSSE_TMS_SHIFT;
		cmd[index++] = 4;
		cmd[index++] = 0;
		libftdi_buffer_write(cmd, index);
	}
}
