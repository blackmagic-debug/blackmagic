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

#include <ftdi.h>

#include "general.h"
#include "jtagtap.h"

int jtagtap_init(void)
{
	assert(ftdic != NULL);
	int err = ftdi_usb_purge_buffers(ftdic);
	if (err != 0) {
		fprintf(stderr, "ftdi_usb_purge_buffer: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		abort();
	}
	/* Reset MPSSE controller. */
	err = ftdi_set_bitmode(ftdic, 0,  BITMODE_RESET);
	if (err != 0) {
		fprintf(stderr, "ftdi_set_bitmode: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		return -1;;
	}
	/* Enable MPSSE controller. Pin directions are set later.*/
	err = ftdi_set_bitmode(ftdic, 0, BITMODE_MPSSE);
	if (err != 0) {
		fprintf(stderr, "ftdi_set_bitmode: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		return -1;;
	}
	uint8_t ftdi_init[9] = {TCK_DIVISOR, 0x00, 0x00, SET_BITS_LOW, 0,0,
				SET_BITS_HIGH, 0,0};
	ftdi_init[4]= active_cable->dbus_data;
	ftdi_init[5]= active_cable->dbus_ddr;
	ftdi_init[7]= active_cable->cbus_data;
	ftdi_init[8]= active_cable->cbus_ddr;
	platform_buffer_write(ftdi_init, 9);
	platform_buffer_flush();

	/* Go to JTAG mode for SWJ-DP */
	for (int i = 0; i <= 50; i++)
		jtagtap_next(1, 0);		/* Reset SW-DP */
	jtagtap_tms_seq(0xE73C, 16);		/* SWD to JTAG sequence */
	jtagtap_soft_reset();

	return 0;
}

void jtagtap_reset(void)
{
	jtagtap_soft_reset();
}

void
jtagtap_tms_seq(uint32_t MS, int ticks)
{
	uint8_t tmp[3] = {MPSSE_WRITE_TMS | MPSSE_LSB | MPSSE_BITMODE| MPSSE_READ_NEG, 0, 0};
	while(ticks >= 0) {
		tmp[1] = ticks<7?ticks-1:6;
		tmp[2] = 0x80 | (MS & 0x7F);

		platform_buffer_write(tmp, 3);
		MS >>= 7; ticks -= 7;
	}
}

void
jtagtap_tdi_seq(const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	uint8_t *tmp;
	int index = 0;
	int rticks;

	if(!ticks) return;

	if(final_tms) ticks--;
	rticks = ticks & 7;
	ticks >>= 3;
	tmp = alloca(ticks + 9);

	if(ticks) {
		tmp[index++] = MPSSE_DO_WRITE |  MPSSE_LSB | MPSSE_WRITE_NEG;
		tmp[index++] = ticks - 1;
		tmp[index++] = 0;
		while(ticks--) tmp[index++] = *DI++;
	}

	if(rticks) {
		tmp[index++] = MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG;
		tmp[index++] = rticks - 1;
		tmp[index++] = *DI;
	}

	if(final_tms) {
		tmp[index++] = MPSSE_WRITE_TMS | MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG;
		tmp[index++] = 0;
		tmp[index++] = (*DI)>>rticks?0x81:0x01;
	}
	platform_buffer_write(tmp, index);
}

void
jtagtap_tdi_tdo_seq(uint8_t *DO, const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	uint8_t *tmp;
	int index = 0, rsize;
	int rticks;

	if(!ticks) return;

//	printf("ticks: %d\n", ticks);
	if(final_tms) ticks--;
	rticks = ticks & 7;
	ticks >>= 3;
	tmp = alloca(ticks + 9);
	rsize = ticks;
	if(ticks) {
		tmp[index++] = MPSSE_DO_READ | MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_WRITE_NEG;
		tmp[index++] = ticks - 1;
		tmp[index++] = 0;
		while(ticks--) tmp[index++] = *DI++;
	}

	if(rticks) {
		rsize++;
		tmp[index++] = MPSSE_DO_READ | MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG;
		tmp[index++] = rticks - 1;
		tmp[index++] = *DI;
	}

	if(final_tms) {
		rsize++;
		tmp[index++] = MPSSE_WRITE_TMS | MPSSE_DO_READ | MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG;
		tmp[index++] = 0;
		tmp[index++] = (*DI)>>rticks?0x81:0x01;
	}
	platform_buffer_write(tmp, index);
	platform_buffer_read(tmp, rsize);
	/*for(int index = 0; index < rsize; index++)
		printf("%02X ", tmp[index]);
	printf("\n");*/
	index = 0;
	if(final_tms) rsize--;

	while(rsize--) {
		/*if(rsize) printf("%02X ", tmp[index]);*/
		*DO++ = tmp[index++];
	}
	if (rticks == 0)
		*DO++ = 0;
	if(final_tms) {
		rticks++;
		*(--DO) >>= 1;
		*DO |= tmp[index] & 0x80;
	} else DO--;
	if(rticks) {
		*DO >>= (8-rticks);
	}
	/*printf("%02X\n", *DO);*/
}

uint8_t jtagtap_next(uint8_t dTMS, uint8_t dTDI)
{
	uint8_t ret;
	uint8_t tmp[3] = {MPSSE_WRITE_TMS | MPSSE_DO_READ | MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG, 0, 0};
	tmp[2] = (dTDI?0x80:0) | (dTMS?0x01:0);
	platform_buffer_write(tmp, 3);
	platform_buffer_read(&ret, 1);

	ret &= 0x80;

//	DEBUG("jtagtap_next(TMS = %d, TDI = %d) = %02X\n", dTMS, dTDI, ret);

	return ret;
}

