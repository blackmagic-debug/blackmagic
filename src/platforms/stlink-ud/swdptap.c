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

#define SWD_XDELAY 0
#define SWD_XDELAY2 0

volatile int	swd_counter;
#ifdef SWD_XDELAY
void swd_xdelay(void)
{
    for (swd_counter = 0; swd_counter < SWD_XDELAY; swd_counter++)
        ;
}
void swd_xdelay2(void)
{
    for (swd_counter = 0; swd_counter < SWD_XDELAY2; swd_counter++)
        ;
}
#else
#define swd_xdelay()
#define swd_xdelay2()
#endif

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

	if(dir == SWDIO_STATUS_FLOAT) {
            gpio_set(SWDDIR_PORT, SWDDIR_PIN);
            SWDIO_MODE_FLOAT_Z();
        }
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
        swd_xdelay();
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
        swd_xdelay();
	if(dir == SWDIO_STATUS_DRIVE) {
            gpio_clear(SWDDIR_PORT, SWDDIR_PIN);
#if 0
            SWDIO_MODE_DRIVE();
#endif
        }
        swd_xdelay2();
}

bool swdptap_bit_in(void)
{
	uint16_t ret;

	swdptap_turnaround(SWDIO_STATUS_FLOAT);

        swd_xdelay();
	ret = gpio_get(SWDIO_PORT, SWDIO_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
        swd_xdelay();
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
        swd_xdelay();

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

	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	while (len--) {
		int res;
                swd_xdelay();
		res = gpio_get(SWDIO_PORT, SWDIO_PIN);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
                swd_xdelay();
		if (res)
			ret |= index;
		index <<= 1;
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
	}
        swd_xdelay();

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

	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	while (len--) {
		bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
                swd_xdelay();
		if (bit) {
			res |= index;
			parity ^= 1;
		}
		index <<= 1;
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
                swd_xdelay();
	}
	bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
        swd_xdelay();
	if (bit)
		parity ^= 1;
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
        swd_xdelay();
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

	swdptap_turnaround(SWDIO_STATUS_DRIVE);

	gpio_set_val(SWDOUT_PORT, SWDOUT_PIN, val);
        swd_xdelay();
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
        swd_xdelay();
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
        swd_xdelay();
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
        swd_xdelay();
}
void
swdptap_seq_out(uint32_t MS, int ticks)
{
	int data = MS & 1;
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < ticks; i++)
		DEBUG("%d", (MS & (1 << i)) ? 1 : 0);
#endif
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	while (ticks--) {
		gpio_set_val(SWDOUT_PORT, SWDOUT_PIN, data);
                swd_xdelay();
		MS >>= 1;
		data = MS & 1;
		gpio_set(SWCLK_PORT, SWCLK_PIN);
		gpio_set(SWCLK_PORT, SWCLK_PIN);
                swd_xdelay();
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
	}
        swd_xdelay();
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
	swdptap_turnaround(SWDIO_STATUS_DRIVE);

	while (ticks--) {
		gpio_set_val(SWDOUT_PORT, SWDOUT_PIN, data);
                swd_xdelay();
		parity ^= MS;
		MS >>= 1;
		gpio_set(SWCLK_PORT, SWCLK_PIN);
                swd_xdelay();
		data = MS & 1;
		gpio_clear(SWCLK_PORT, SWCLK_PIN);
	}
        swd_xdelay();
	gpio_set_val(SWDOUT_PORT, SWDOUT_PIN, parity & 1);
        swd_xdelay();
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
        swd_xdelay();
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
        swd_xdelay();
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
        swd_xdelay();
}
