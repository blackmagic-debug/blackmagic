/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2008  Black Sphere Technologies Ltd.
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

/* Low level JTAG implementation using FT2232 with libftdi.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <libftdi1/ftdi.h>
#include "platform.h"
#include "ftdi_bmp.h"

#include "general.h"

extern cable_desc_t *active_cable;
extern struct ftdi_context *ftdic;

static void jtagtap_reset(void);
static void jtagtap_tms_seq(uint32_t MS, int ticks);
static void jtagtap_tdi_seq(
	const uint8_t final_tms, const uint8_t *DI, int ticks);
static uint8_t jtagtap_next(uint8_t dTMS, uint8_t dTDI);

int libftdi_jtagtap_init(jtag_proc_t *jtag_proc)
{
	if ((active_cable->mpsse_swd_read.set_data_low == MPSSE_DO) &&
		(active_cable->mpsse_swd_write.set_data_low == MPSSE_DO)) {
		printf("Jtag not possible with resistor SWD!\n");
			return -1;
	}
	jtag_proc->jtagtap_reset = jtagtap_reset;
	jtag_proc->jtagtap_next =jtagtap_next;
	jtag_proc->jtagtap_tms_seq = jtagtap_tms_seq;
	jtag_proc->jtagtap_tdi_tdo_seq = libftdi_jtagtap_tdi_tdo_seq;
	jtag_proc->jtagtap_tdi_seq = jtagtap_tdi_seq;

	active_state.data_low  |=   active_cable->jtag.set_data_low |
		MPSSE_CS | MPSSE_DI | MPSSE_DO;
	active_state.data_low  &= ~(active_cable->jtag.clr_data_low | MPSSE_SK);
	active_state.ddr_low   |=   MPSSE_CS | MPSSE_DO | MPSSE_SK;
	active_state.ddr_low   &= ~(MPSSE_DI);
	active_state.data_high |=   active_cable->jtag.set_data_high;
	active_state.data_high &= ~(active_cable->jtag.clr_data_high);
	uint8_t gab[16];
	int garbage =  ftdi_read_data(ftdic, gab, sizeof(gab));
	if (garbage > 0) {
		DEBUG_WARN("FTDI JTAG init got garbage:");
		for (int i = 0; i < garbage; i++)
			DEBUG_WARN(" %02x", gab[i]);
		DEBUG_WARN("\n");
	}
	uint8_t cmd_write[16] = {
		SET_BITS_LOW,  active_state.data_low,
		active_state.ddr_low,
		SET_BITS_HIGH, active_state.data_high, active_state.ddr_high};
	libftdi_buffer_write(cmd_write, 6);
	libftdi_buffer_flush();
	/* Write out start condition and pull garbage from read buffer.
	 * FT2232D otherwise misbehaves on runs follwoing the first run.*/
	garbage =  ftdi_read_data(ftdic, cmd_write, sizeof(cmd_write));
	if (garbage > 0) {
		DEBUG_WARN("FTDI JTAG end init got garbage:");
		for (int i = 0; i < garbage; i++)
			DEBUG_WARN(" %02x", cmd_write[i]);
		DEBUG_WARN("\n");
	}
	/* Go to JTAG mode for SWJ-DP */
	for (int i = 0; i <= 50; i++)
		jtag_proc->jtagtap_next(1, 0);          /* Reset SW-DP */
	jtag_proc->jtagtap_tms_seq(0xE73C, 16);		/* SWD to JTAG sequence */
	jtag_proc->jtagtap_tms_seq(0x1F, 6);

	return 0;
}

static void jtagtap_reset(void)
{
	jtagtap_soft_reset();
}

static void jtagtap_tms_seq(uint32_t MS, int ticks)
{
	uint8_t tmp[3] = {
		MPSSE_WRITE_TMS | MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG, 0, 0};
	while(ticks >= 0) {
		tmp[1] = ticks<7?ticks-1:6;
		tmp[2] = 0x80 | (MS & 0x7F);

		libftdi_buffer_write(tmp, 3);
		MS >>= 7; ticks -= 7;
	}
}

static void jtagtap_tdi_seq(
	const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	return libftdi_jtagtap_tdi_tdo_seq(NULL,  final_tms, DI, ticks);
}

static uint8_t jtagtap_next(uint8_t dTMS, uint8_t dTDI)
{
	uint8_t ret;
	uint8_t tmp[3] = {MPSSE_WRITE_TMS | MPSSE_DO_READ | MPSSE_LSB |
					  MPSSE_BITMODE | MPSSE_WRITE_NEG, 0, 0};
	tmp[2] = (dTDI?0x80:0) | (dTMS?0x01:0);
	libftdi_buffer_write(tmp, 3);
	libftdi_buffer_read(&ret, 1);

	ret &= 0x80;

	return ret;
}
