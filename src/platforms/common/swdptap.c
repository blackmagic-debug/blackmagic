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

/* This file implements the SW-DP interface. */

#include "general.h"
#include "swdptap.h"

//#define DEBUG_SWD_BITS

enum {
	SWDIO_STATUS_FLOAT = 0,
	SWDIO_STATUS_DRIVE
};

int swdptap_init(void)
{
	return 0;
}

static void swdptap_turnaround(int dir)
{
	static int olddir = SWDIO_STATUS_FLOAT;

	/* Don't turnaround if direction not changing */
	if(dir == olddir) return;
	olddir = dir;

#ifdef DEBUG_SWD_BITS
	DEBUG("%s", dir ? "\n-> ":"\n<- ");
#endif

	if(dir == SWDIO_STATUS_FLOAT)
		SWDIO_MODE_FLOAT();
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	if(dir == SWDIO_STATUS_DRIVE)
		SWDIO_MODE_DRIVE();
}

bool swdptap_bit_in(void)
{
	uint16_t ret;

	swdptap_turnaround(SWDIO_STATUS_FLOAT);

	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	ret = !!gpio_get(SWDIO_PORT, SWDIO_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);

#ifdef DEBUG_SWD_BITS
	DEBUG("%d", ret?1:0);
#endif

	return ret;
}

uint32_t
swdptap_seq_in(int ticks)
{
	uint32_t ret = 0;

	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	for (int i=0; i<ticks; i++) {
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
		uint32_t res = !!gpio_get(SWDIO_PORT, SWDIO_PIN);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		ret |= (res&1) << i;
	}

#ifdef DEBUG_SWD_BITS
	for (int i=0; i<ticks; i++)
		DEBUG("%d", (ret>>i)&1);
#endif
	return ret;
}

bool
swdptap_seq_in_parity(uint32_t *ret, int ticks)
{
	uint8_t parity = 0;
	uint32_t res = 0;

	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	for (int i=0; i<ticks; i++) {
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
		uint8_t bit = !!gpio_get(SWDIO_PORT, SWDIO_PIN);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		parity ^= bit;
		res |= (uint32_t)bit << i;
	}
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	uint8_t bit = !!gpio_get(SWDIO_PORT, SWDIO_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
        parity ^= bit;
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < ticks; i++)
		DEBUG("%d", (res & (1 << i)) ? 1 : 0);
#endif
	*ret = res;
	return parity;
}

void swdptap_bit_out(bool val)
{
	swdptap_turnaround(SWDIO_STATUS_DRIVE);

#ifdef DEBUG_SWD_BITS
	DEBUG("%d", val);
#endif

	gpio_set_val(SWDIO_PORT, SWDIO_PIN, val);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
}

void
swdptap_seq_out(uint32_t MS, int ticks)
{
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < ticks; i++)
		DEBUG("%d", (MS & (1 << i)) ? 1 : 0);
#endif
	while (ticks--) {
		gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS&1);
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		MS >>= 1;
	}
}

void
swdptap_seq_out_parity(uint32_t MS, int ticks)
{
	uint8_t parity = 0;
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < ticks; i++)
		DEBUG("%d", (MS & (1 << i)) ? 1 : 0);
#endif
	while (ticks--) {
		gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS&1);
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		parity ^= MS;
		MS >>= 1;
	}
	gpio_set_val(SWDIO_PORT, SWDIO_PIN, parity & 1);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
}
