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

/* This file implements the low-level SW-DP interface. */

#include "general.h"
#include "swdptap.h"

int swdptap_init(void)
{
	return 0;
}

static void swdptap_turnaround(uint8_t dir)
{
	static uint8_t olddir = 0;

	/* Don't turnaround if direction not changing */
	if(dir == olddir) return;
	olddir = dir;

#ifdef DEBUG_SWD_BITS
	DEBUG("%s", dir ? "\n-> ":"\n<- ");
#endif

	if(dir)
		SWDIO_MODE_FLOAT();
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	if(!dir)
		SWDIO_MODE_DRIVE();
}

bool swdptap_bit_in(void)
{
	uint16_t ret;

	swdptap_turnaround(1);

	ret = gpio_get(SWDIO_PORT, SWDIO_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);

#ifdef DEBUG_SWD_BITS
	DEBUG("%d", ret?1:0);
#endif

	return ret != 0;
}

uint32_t
swdptap_seq_in(int ticks)
{
	uint32_t index = 1;
	uint32_t ret = 0;
	int len = ticks;

	swdptap_turnaround(1);
	while (len--) {
		int res;
		res = gpio_get(SWDIO_PORT, SWDIO_PIN);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		if (res)
			ret |= index;
		index <<= 1;
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
	}

#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < len; i++)
		DEBUG("%d", (ret & (1 << i)) ? 1 : 0);
#endif
	return ret;
}

bool
swdptap_seq_in_parity(uint32_t *ret, int ticks)
{
	uint32_t index = 1;
	uint8_t parity = 0;
	uint32_t res = 0;
	bool bit;
	int len = ticks;

	swdptap_turnaround(1);
	while (len--) {
		bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		if (bit) {
			res |= index;
			parity ^= 1;
		}
		index <<= 1;
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
	}
	bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	if (bit)
		parity ^= 1;
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < len; i++)
		DEBUG("%d", (res & (1 << i)) ? 1 : 0);
#endif
	*ret = res;
	return parity;
}

void swdptap_bit_out(bool val)
{
#ifdef DEBUG_SWD_BITS
	DEBUG("%d", val);
#endif

	swdptap_turnaround(0);

	gpio_set_val(SWDIO_PORT, SWDIO_PIN, val);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
}
void
swdptap_seq_out(uint32_t MS, int ticks)
{
	int data = MS & 1;
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < ticks; i++)
		DEBUG("%d", (MS & (1 << i)) ? 1 : 0);
#endif
	swdptap_turnaround(0);
	while (ticks--) {
		gpio_set_val(SWDIO_PORT, SWDIO_PIN, data);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		MS >>= 1;
		data = MS & 1;
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
	}
}

void
swdptap_seq_out_parity(uint32_t MS, int ticks)
{
	uint8_t parity = 0;
	int data = MS & 1;
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < ticks; i++)
		DEBUG("%d", (MS & (1 << i)) ? 1 : 0);
#endif
	swdptap_turnaround(0);

	while (ticks--) {
		gpio_set_val(SWDIO_PORT, SWDIO_PIN, data);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		parity ^= MS;
		MS >>= 1;
		data = MS & 1;
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
	}
	gpio_set_val(SWDIO_PORT, SWDIO_PIN, parity & 1);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
}
