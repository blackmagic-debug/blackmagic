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
 * Issues:
 * Should share interface with swdptap.c or at least clean up...
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <assert.h>

#include "general.h"
#include "ftdi_bmp.h"

extern cable_desc_t *active_cable;
extern struct ftdi_context *ftdic;

static void jtagtap_reset(void);
static void jtagtap_tms_seq(uint32_t MS, int ticks);
static void jtagtap_tdi_seq(
	const uint8_t final_tms, const uint8_t *DI, int ticks);
static uint8_t jtagtap_next(uint8_t dTMS, uint8_t dTDI);

int libftdi_jtagtap_init(jtag_proc_t *jtag_proc)
{
	if ((active_cable->swd_read.set_data_low == MPSSE_DO) &&
		(active_cable->swd_write.set_data_low == MPSSE_DO)) {
		printf("Jtag not possible with resistor SWD!\n");
			return -1;
	}
	assert(ftdic != NULL);
	/* select new buffer flush function if libftdi 1.5 */
#ifdef _Ftdi_Pragma
	int err = ftdi_tcioflush(ftdic);
#else
	int err = ftdi_usb_purge_buffers(ftdic);
#endif
	if (err != 0) {
		DEBUG_WARN("ftdi_usb_purge_buffer: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		abort();
	}
	/* Reset MPSSE controller. */
	err = ftdi_set_bitmode(ftdic, 0,  BITMODE_RESET);
	if (err != 0) {
		DEBUG_WARN("ftdi_set_bitmode: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		return -1;
	}
	/* Enable MPSSE controller. Pin directions are set later.*/
	err = ftdi_set_bitmode(ftdic, 0, BITMODE_MPSSE);
	if (err != 0) {
		DEBUG_WARN("ftdi_set_bitmode: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		return -1;
	}
	uint8_t ftdi_init[9] = {TCK_DIVISOR, 0x00, 0x00, SET_BITS_LOW, 0,0,
				SET_BITS_HIGH, 0,0};
	ftdi_init[4]= active_cable->dbus_data;
	ftdi_init[5]= active_cable->dbus_ddr;
	ftdi_init[7]= active_cable->cbus_data;
	ftdi_init[8]= active_cable->cbus_ddr;
	libftdi_buffer_write(ftdi_init, 9);
	libftdi_buffer_flush();

	jtag_proc->jtagtap_reset = jtagtap_reset;
	jtag_proc->jtagtap_next =jtagtap_next;
	jtag_proc->jtagtap_tms_seq = jtagtap_tms_seq;
	jtag_proc->jtagtap_tdi_tdo_seq = libftdi_jtagtap_tdi_tdo_seq;
	jtag_proc->jtagtap_tdi_seq = jtagtap_tdi_seq;

	return 0;
}

static void jtagtap_reset(void)
{
	jtagtap_soft_reset();
}

static void jtagtap_tms_seq(uint32_t MS, int ticks)
{
	uint8_t tmp[3] = {
		MPSSE_WRITE_TMS | MPSSE_LSB | MPSSE_BITMODE| MPSSE_READ_NEG, 0, 0};
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

//	DEBUG("jtagtap_next(TMS = %d, TDI = %d) = %02X\n", dTMS, dTDI, ret);

	return ret;
}
