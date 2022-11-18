/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright(C) 2018 - 2021 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

#include "general.h"
#include <assert.h>

#include <ftdi.h>
#include "ftdi_bmp.h"

typedef enum swdio_status {
	SWDIO_STATUS_DRIVE,
	SWDIO_STATUS_FLOAT,
} swdio_status_e;

static enum swdio_status olddir;
static bool do_mpsse;
static bool direct_bb_swd;

#define MPSSE_MASK      (MPSSE_DO | MPSSE_DI | MPSSE_CS)
#define MPSSE_TD_MASK   (MPSSE_DO | MPSSE_DI)
#define MPSSE_TMS_SHIFT (MPSSE_WRITE_TMS | MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG)
#define MPSSE_TDO_SHIFT (MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG)

static bool swdptap_seq_in_parity(uint32_t *res, size_t clock_cycles);
static uint32_t swdptap_seq_in(size_t clock_cycles);
static void swdptap_seq_out(uint32_t tms_states, size_t clock_cycles);
static void swdptap_seq_out_parity(uint32_t tms_states, size_t clock_cycles);

bool libftdi_swd_possible(void)
{
	const bool swd_read = active_cable->mpsse_swd_read.set_data_low || active_cable->mpsse_swd_read.clr_data_low ||
		active_cable->mpsse_swd_read.set_data_high || active_cable->mpsse_swd_read.clr_data_high;
	const bool swd_write = active_cable->mpsse_swd_write.set_data_low || active_cable->mpsse_swd_write.clr_data_low ||
		active_cable->mpsse_swd_write.set_data_high || active_cable->mpsse_swd_write.clr_data_high;
	do_mpsse = swd_read && swd_write;
	if (do_mpsse)
		return true;

	const bool bb_swd_read = active_cable->bb_swd_read.set_data_low || active_cable->bb_swd_read.clr_data_low ||
		active_cable->bb_swd_read.set_data_high || active_cable->bb_swd_read.clr_data_high;
	const bool bb_swd_write = active_cable->bb_swd_write.set_data_low || active_cable->bb_swd_write.clr_data_low ||
		active_cable->bb_swd_write.set_data_high || active_cable->bb_swd_write.clr_data_high;
	const bool bb_direct_possible =
		active_cable->bb_swdio_in_port_cmd == GET_BITS_LOW && active_cable->bb_swdio_in_pin == MPSSE_CS;
	if (!bb_swd_read && !bb_swd_write && !bb_direct_possible)
		return false;
	direct_bb_swd = true;
	return true;
}

int libftdi_swdptap_init(ADIv5_DP_t *dp)
{
	if (!libftdi_swd_possible()) {
		DEBUG_WARN("SWD not possible or missing item in cable description.\n");
		return -1;
	}
	active_state.data_low |= MPSSE_CS | MPSSE_DI | MPSSE_DO;
	active_state.data_low &= MPSSE_SK;
	active_state.ddr_low |= MPSSE_SK;
	active_state.ddr_low &= ~(MPSSE_CS | MPSSE_DI | MPSSE_DO);
	if (do_mpsse) {
		DEBUG_INFO("Using genuine MPSSE for SWD.\n");
		active_state.data_low |= active_cable->mpsse_swd_read.set_data_low;
		active_state.data_low &= ~(active_cable->mpsse_swd_read.clr_data_low);
		active_state.data_high |= active_cable->mpsse_swd_read.set_data_high;
		active_state.data_high &= ~(active_cable->mpsse_swd_read.clr_data_high);
	} else if (direct_bb_swd) {
		DEBUG_INFO("Using direct bitbang with SWDIO %cBUS%d.\n",
			(active_cable->bb_swdio_in_port_cmd == GET_BITS_LOW) ? 'C' : 'D',
			__builtin_ctz(active_cable->bb_swdio_in_pin));
	} else {
		DEBUG_INFO("Using switched bitbang for SWD.\n");
		active_state.data_low |= active_cable->bb_swd_read.set_data_low;
		active_state.data_low &= ~(active_cable->bb_swd_read.clr_data_low);
		active_state.data_high |= active_cable->bb_swd_read.set_data_high;
		active_state.data_high &= ~(active_cable->bb_swd_read.clr_data_high);
		active_state.ddr_low |= MPSSE_CS;
		if (active_cable->bb_swdio_in_port_cmd == GET_BITS_LOW)
			active_state.ddr_low &= ~active_cable->bb_swdio_in_pin;
		else if (active_cable->bb_swdio_in_port_cmd == GET_BITS_HIGH)
			active_state.ddr_high &= ~active_cable->bb_swdio_in_pin;
	}
	uint8_t cmd_write[6] = {SET_BITS_LOW, active_state.data_low, active_state.ddr_low, SET_BITS_HIGH,
		active_state.data_high, active_state.ddr_high};
	libftdi_buffer_write(cmd_write, 6);
	libftdi_buffer_flush();
	olddir = SWDIO_STATUS_FLOAT;

	dp->seq_in = swdptap_seq_in;
	dp->seq_in_parity = swdptap_seq_in_parity;
	dp->seq_out = swdptap_seq_out;
	dp->seq_out_parity = swdptap_seq_out_parity;
	dp->dp_read = firmware_swdp_read;
	dp->error = firmware_swdp_error;
	dp->low_access = firmware_swdp_low_access;
	dp->abort = firmware_swdp_abort;
	return 0;
}

static void swdptap_turnaround_mpsse(const swdio_status_e dir)
{
	if (dir == SWDIO_STATUS_FLOAT) { /* SWDIO goes to input */
		active_state.data_low |= active_cable->mpsse_swd_read.set_data_low | MPSSE_DO;
		active_state.data_low &= ~active_cable->mpsse_swd_read.clr_data_low;
		active_state.ddr_low &= ~MPSSE_DO;
		active_state.data_high |= active_cable->mpsse_swd_read.set_data_high;
		active_state.data_high &= ~active_cable->mpsse_swd_read.clr_data_high;
		const uint8_t cmd_read[6] = {
			SET_BITS_LOW,
			active_state.data_low,
			active_state.ddr_low,
			SET_BITS_HIGH,
			active_state.data_high,
			active_state.ddr_high,
		};
		libftdi_buffer_write_arr(cmd_read);
	}
	/* One clock cycle */
	const uint8_t cmd[3] = {
		MPSSE_TDO_SHIFT,
		0,
		0,
	};
	libftdi_buffer_write_arr(cmd);
	if (dir == SWDIO_STATUS_DRIVE) { /* SWDIO goes to output */
		active_state.data_low |= active_cable->mpsse_swd_write.set_data_low | MPSSE_DO;
		active_state.data_low &= ~active_cable->mpsse_swd_write.clr_data_low;
		active_state.ddr_low |= MPSSE_DO;
		active_state.data_high |= active_cable->mpsse_swd_write.set_data_high;
		active_state.data_high &= ~active_cable->mpsse_swd_write.clr_data_high;
		const uint8_t cmd_write[6] = {
			SET_BITS_LOW,
			active_state.data_low,
			active_state.ddr_low,
			SET_BITS_HIGH,
			active_state.data_high,
			active_state.ddr_high,
		};
		libftdi_buffer_write_arr(cmd_write);
	}
}

static void swdptap_turnaround_raw(const swdio_status_e dir)
{
	uint8_t cmd[9];
	if (dir == SWDIO_STATUS_FLOAT) { /* SWDIO goes to input */
		if (direct_bb_swd) {
			active_state.data_low |= MPSSE_CS;
			active_state.ddr_low &= ~MPSSE_CS;
		} else {
			active_state.data_low |= active_cable->bb_swd_read.set_data_low;
			active_state.data_low &= ~active_cable->bb_swd_read.clr_data_low;
			active_state.data_high |= active_cable->bb_swd_read.set_data_high;
			active_state.data_high &= ~active_cable->bb_swd_read.clr_data_high;
		}
		cmd[0] = SET_BITS_LOW;
		cmd[1] = active_state.data_low;
		cmd[2] = active_state.ddr_low;
		cmd[3] = SET_BITS_HIGH;
		cmd[4] = active_state.data_high;
		cmd[5] = active_state.ddr_high;
		/* One clock cycle */
		cmd[6] = MPSSE_TMS_SHIFT;
		cmd[7] = 0;
		cmd[8] = 0;
	} else if (dir == SWDIO_STATUS_DRIVE) {
		/* One clock cycle */
		cmd[0] = MPSSE_TMS_SHIFT;
		cmd[1] = 0;
		cmd[2] = 0;
		if (direct_bb_swd) {
			active_state.data_low |= MPSSE_CS;
			active_state.ddr_low |= MPSSE_CS;
		} else {
			active_state.data_low |= active_cable->bb_swd_write.set_data_low;
			active_state.data_low &= ~active_cable->bb_swd_write.clr_data_low;
			active_state.data_high |= active_cable->bb_swd_write.set_data_high;
			active_state.data_high &= ~active_cable->bb_swd_write.clr_data_high;
		}
		cmd[3] = SET_BITS_LOW;
		cmd[4] = active_state.data_low;
		cmd[5] = active_state.ddr_low;
		cmd[6] = SET_BITS_HIGH;
		cmd[7] = active_state.data_high;
		cmd[8] = active_state.ddr_high;
	}
	libftdi_buffer_write_arr(cmd);
}

static void swdptap_turnaround(const swdio_status_e dir)
{
	if (dir == olddir)
		return;
	olddir = dir;
	DEBUG_PROBE("Turnaround %s\n", dir == SWDIO_STATUS_FLOAT ? "float" : "drive");
	if (do_mpsse)
		swdptap_turnaround_mpsse(dir);
	else
		swdptap_turnaround_raw(dir);
}

bool swdptap_bit_in_mpsse(void)
{
	const uint8_t cmd[2] = {
		MPSSE_DO_READ | MPSSE_LSB | MPSSE_BITMODE,
		0,
	};
	libftdi_buffer_write_arr(cmd);
	uint8_t data;
	libftdi_buffer_read_val(data);
	return data & 0x80U;
}

bool swdptap_bit_in_raw(void)
{
	const uint8_t cmd[4] = {
		active_cable->bb_swdio_in_port_cmd,
		MPSSE_TMS_SHIFT,
		0,
		0,
	};
	libftdi_buffer_write_arr(cmd);
	uint8_t data;
	libftdi_buffer_read_val(data);
	return data & active_cable->bb_swdio_in_pin;
}

bool swdptap_bit_in(void)
{
	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	if (do_mpsse)
		return swdptap_bit_in_mpsse();
	return swdptap_bit_in_raw();
}

void swdptap_bit_out(bool val)
{
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	if (do_mpsse) {
		uint8_t cmd[3] = {MPSSE_TDO_SHIFT, 0, (val) ? 1 : 0};
		libftdi_buffer_write(cmd, sizeof(cmd));
	} else {
		uint8_t cmd[3];
		cmd[0] = MPSSE_TMS_SHIFT;
		cmd[1] = 0;
		cmd[2] = (val) ? 1 : 0;
		libftdi_buffer_write(cmd, sizeof(cmd));
	}
}

bool swdptap_seq_in_parity(uint32_t *res, size_t clock_cycles)
{
	assert(clock_cycles == 32);
	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	unsigned int parity = 0;
	unsigned int result = 0;
	if (do_mpsse) {
		uint8_t DO[5];
		libftdi_jtagtap_tdi_tdo_seq(DO, 0, NULL, clock_cycles + 1);
		result = DO[0] + (DO[1] << 8) + (DO[2] << 16) + (DO[3] << 24);
		parity = __builtin_parity(result & ((1LL << clock_cycles) - 1)) & 1;
		parity ^= DO[4] & 1;
	} else {
		size_t index = clock_cycles + 1;
		uint8_t cmd[4];

		cmd[0] = active_cable->bb_swdio_in_port_cmd;
		cmd[1] = MPSSE_TMS_SHIFT;
		cmd[2] = 0;
		cmd[3] = 0;
		while (index--) {
			libftdi_buffer_write(cmd, sizeof(cmd));
		}
		uint8_t data[33];
		libftdi_buffer_read(data, clock_cycles + 1);
		if (data[clock_cycles] & active_cable->bb_swdio_in_pin)
			parity ^= 1;
		index = clock_cycles;
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

static uint32_t swdptap_seq_in(size_t clock_cycles)
{
	if (!clock_cycles)
		return 0;
	uint32_t result = 0;
	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	if (do_mpsse) {
		uint8_t DO[4];
		libftdi_jtagtap_tdi_tdo_seq(DO, 0, NULL, clock_cycles);
		int bytes = clock_cycles >> 3;
		if (clock_cycles & 7)
			bytes++;
		for (int i = 0; i < bytes; i++) {
			result |= DO[i] << (8 * i);
		}
	} else {
		size_t index = clock_cycles;
		uint8_t cmd[4];

		cmd[0] = active_cable->bb_swdio_in_port_cmd;
		cmd[1] = MPSSE_TMS_SHIFT;
		cmd[2] = 0;
		cmd[3] = 0;

		while (index--) {
			libftdi_buffer_write(cmd, sizeof(cmd));
		}
		uint8_t data[32];
		libftdi_buffer_read(data, clock_cycles);
		index = clock_cycles;
		while (index--) {
			if (data[index] & active_cable->bb_swdio_in_pin)
				result |= (1 << index);
		}
	}
	return result;
}

static void swdptap_seq_out(uint32_t tms_states, size_t clock_cycles)
{
	if (!clock_cycles)
		return;
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	if (do_mpsse) {
		uint8_t DI[4];
		DI[0] = (tms_states >> 0) & 0xff;
		DI[1] = (tms_states >> 8) & 0xff;
		DI[2] = (tms_states >> 16) & 0xff;
		DI[3] = (tms_states >> 24) & 0xff;
		libftdi_jtagtap_tdi_tdo_seq(NULL, 0, DI, clock_cycles);
	} else {
		uint8_t cmd[16];
		size_t index = 0;
		while (clock_cycles) {
			cmd[index++] = MPSSE_TMS_SHIFT;
			if (clock_cycles >= 7) {
				cmd[index++] = 6;
				cmd[index++] = tms_states & 0x7f;
				tms_states >>= 7;
				clock_cycles -= 7;
			} else {
				cmd[index++] = clock_cycles - 1;
				cmd[index++] = tms_states & 0x7f;
				clock_cycles = 0;
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
static void swdptap_seq_out_parity(uint32_t tms_states, size_t clock_cycles)
{
	(void)clock_cycles;
	int parity = __builtin_parity(tms_states) & 1;
	size_t index = 0;
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	if (do_mpsse) {
		uint8_t DI[8];
		DI[0] = (tms_states >> 0) & 0xff;
		DI[1] = (tms_states >> 8) & 0xff;
		DI[2] = (tms_states >> 16) & 0xff;
		DI[3] = (tms_states >> 24) & 0xff;
		DI[4] = parity;
		DI[5] = 0;
		libftdi_jtagtap_tdi_tdo_seq(NULL, 0, DI, 32 + 1 + 8);
	} else {
		uint8_t cmd[32];
		size_t steps = clock_cycles;
		while (steps) {
			cmd[index++] = MPSSE_TMS_SHIFT;
			cmd[index++] = 6;
			if (steps >= 7) {
				cmd[index++] = tms_states & 0x7f;
				tms_states >>= 7;
				steps -= 7;
			} else {
				cmd[index++] = (tms_states & 0x7f) | (parity << 4);
				steps = 0;
			}
		}
		cmd[index++] = MPSSE_TMS_SHIFT;
		cmd[index++] = 4;
		cmd[index++] = 0;
		libftdi_buffer_write(cmd, index);
	}
}
