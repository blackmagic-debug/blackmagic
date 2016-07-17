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

/* Quick hack for bit-banging SW-DP interface over FT2232.
 * Intended as proof of concept, not for production.
 */

#include <stdio.h>
#include <assert.h>
#include <ftdi.h>

#include "general.h"
#include "swdptap.h"

static uint8_t olddir = 0;

int swdptap_init(void)
{
	int err;

	assert(ftdic != NULL);

	if((err = ftdi_set_bitmode(ftdic, 0xAB, BITMODE_BITBANG)) != 0) {
		fprintf(stderr, "ftdi_set_bitmode: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		abort();
	}

	assert(ftdi_write_data(ftdic, (void*)"\xAB\xA8", 2) == 2);
	olddir = 0;

	return 0;
}

static void swdptap_turnaround(uint8_t dir)
{
	platform_buffer_flush();

	if (dir == olddir)
		return;
	olddir = dir;

	if(dir)	/* SWDIO goes to input */
		assert(ftdi_set_bitmode(ftdic, 0xA3, BITMODE_BITBANG) == 0);

	/* One clock cycle */
	ftdi_write_data(ftdic, (void *)"\xAB\xA8", 2);

	if(!dir) /* SWDIO goes to output */
		assert(ftdi_set_bitmode(ftdic, 0xAB, BITMODE_BITBANG) == 0);
}

bool swdptap_bit_in(void)
{
	uint8_t ret;

	swdptap_turnaround(1);

	ftdi_read_pins(ftdic, &ret);
	ret &= 0x08;
	ftdi_write_data(ftdic, (void *)"\xA1\xA0", 2);

	return ret;
}

void swdptap_bit_out(bool val)
{
	uint8_t buf[3] = "\xA0\xA1\xA0";

	swdptap_turnaround(0);

	if (val) {
		for(int i = 0; i < 3; i++)
			buf[i] |= 0x08;
	}
	platform_buffer_write(buf, 3);
}

