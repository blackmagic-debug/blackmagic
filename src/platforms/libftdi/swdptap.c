/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2018 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de
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

/* Implementation of the SWD protocoll for FTDI DI/DO connected to SWDIO.
 *
 * Define requires switches in the cable definition as swd_read and swd_write
 * MPSSE sequences.
 *
 * Arm tells: SWD Target samples and drives on the positive edge. Check!
 *
 * Start with SWCK low and SWDIO high. Use negative edge for writing.
 * DO  is written _before_ positive clock edge. Sample DI on positive edge.
 *
 */

#include <stdio.h>
#include <assert.h>
#include <ftdi.h>
#include <unistd.h>

#include "general.h"
#include "swdptap.h"
#include "platform.h"
#include "jtagtap.h"

static uint8_t olddir;

int swdptap_init(void)
{
	assert(ftdic != NULL);

	int err = ftdi_usb_purge_buffers(ftdic);
	if (err != 0) {
		fprintf(stderr, "ftdi_usb_purge_buffer: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		return -1;
	}
	err = ftdi_set_bitmode(ftdic, active_cable->dbus_ddr, BITMODE_MPSSE);
	if (err != 0) {
		fprintf(stderr, "ftdi_set_bitmode: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		return -1;
	}

	uint8_t ftdi_init[9] = {TCK_DIVISOR, 0x01, 0, SET_BITS_LOW, 0,0,
				SET_BITS_HIGH, 0,0};
	ftdi_init[4]= active_cable->dbus_data;
	ftdi_init[5]= active_cable->dbus_ddr;
	ftdi_init[7]= active_cable->cbus_data;
	ftdi_init[8]= active_cable->cbus_ddr;
	platform_buffer_write(ftdi_init, 9);

	if (active_cable->swd_write[0])
		platform_buffer_write(active_cable->swd_write, 3);
	olddir = 0;
	return 0;
}

static void swdptap_turnaround(uint8_t dir)
{
	if (dir == olddir)
		return;
	olddir = dir;

	uint8_t data[2] = {CLK_BITS, 0};
	if (dir && active_cable->swd_read[0])	{/* SWDIO goes to input */
//		DEBUG("swd read\n");
		platform_buffer_write(active_cable->swd_read, 3);
	}
	/* One clock cycle */
	platform_buffer_write(data, 2);
	if(!dir && active_cable->swd_write[0]) { /* SWDIO goes to output */
		platform_buffer_write(active_cable->swd_write, 3);
//		DEBUG("swd write\n");
	}
}

bool swdptap_bit_in(void)
{
	swdptap_turnaround(1);

	uint8_t data[2] = {MPSSE_DO_READ | MPSSE_LSB | MPSSE_BITMODE, 0};
	platform_buffer_write(data, 2);
	platform_buffer_read(data, 1);
//	DEBUG("Read %c\n", (data[0] & 0x80)? '1':'0');
	return data[0] & 0x80;
}

void swdptap_bit_out(bool val)
{
//	DEBUG("bit_out\n");
	swdptap_turnaround(0);
	uint8_t data[3] = {MPSSE_DO_WRITE | MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG, 0, (val)? 1:0};
	platform_buffer_write(data, 3);
}

void swdptap_seq_out(uint32_t MS, int ticks)
{
	uint8_t DI[4];
	swdptap_turnaround(0);
	DI[0] = (MS >>  0) & 0xff;
	DI[1] = (MS >>  8) & 0xff;
	DI[2] = (MS >> 16) & 0xff;
	DI[3] = (MS >> 24) & 0xff;
	jtagtap_tdi_tdo_seq(NULL, 0, DI, ticks);
//	DEBUG("seq out %08x %d\n", MS, ticks);
}

void swdptap_seq_out_parity(uint32_t MS, int ticks)
{
	int parity = 0;
	uint8_t DI[5];
	swdptap_turnaround(0);
	DI[0] = (MS >>  0) & 0xff;
	DI[1] = (MS >>  8) & 0xff;
	DI[2] = (MS >> 16) & 0xff;
	DI[3] = (MS >> 24) & 0xff;
	while(MS) {
		parity ^= (MS & 1);
		MS >>= 1;
	}
	DI[4] = parity;
	jtagtap_tdi_tdo_seq(NULL, 0, DI, ticks + 1);
}

uint32_t swdptap_seq_in(int ticks)
{
	uint8_t DO[4];
	swdptap_turnaround(1);
	jtagtap_tdi_tdo_seq(DO, 0, NULL, ticks);
	uint32_t res = 0;
	for (int i = 0; i < (ticks >> 3) + (ticks  & 7)? 1: 0; i++) {
		res |= DO[i] << (8 * i);
	}
//	DEBUG("seq_in %d : %08x\n", ticks, res);
	return res;
}

bool swdptap_seq_in_parity(uint32_t *ret, int ticks)
{
	assert(ticks == 32);
	swdptap_turnaround(1);
	uint8_t DO[5];
	int parity = 0;
	jtagtap_tdi_tdo_seq(DO, 0, NULL, ticks + 1);
	uint32_t res;
	res = DO[0] + (DO[1] << 8) + (DO[2] << 16) + (DO[3] << 24);
	*ret = res;
	for (int i = 0; i < 32; i++) {
		parity ^= (res >> i) & 1;
	}
	parity ^= DO[4] & 1;
//	DEBUG("seq_in_parity %d : %08x, p %d\n", ticks, res, parity);
	return parity;
}
