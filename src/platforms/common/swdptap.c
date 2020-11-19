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
#include "timing.h"

enum {
	SWDIO_STATUS_FLOAT = 0,
	SWDIO_STATUS_DRIVE
};
static void swdptap_turnaround(int dir) __attribute__ ((optimize(3)));
static uint32_t swdptap_seq_in(int ticks) __attribute__ ((optimize(3)));
static bool swdptap_seq_in_parity(uint32_t *ret, int ticks)
	__attribute__ ((optimize(3)));
static void swdptap_seq_out(uint32_t MS, int ticks)
	__attribute__ ((optimize(3)));
static void swdptap_seq_out_parity(uint32_t MS, int ticks)
	__attribute__ ((optimize(3)));

static void swdptap_turnaround(int dir)
{
	static int olddir = SWDIO_STATUS_FLOAT;
	register volatile int32_t cnt;

	/* Don't turnaround if direction not changing */
	if(dir == olddir) return;
	olddir = dir;

#ifdef DEBUG_SWD_BITS
	DEBUG("%s", dir ? "\n-> ":"\n<- ");
#endif

	if(dir == SWDIO_STATUS_FLOAT)
		SWDIO_MODE_FLOAT();
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	for(cnt = swd_delay_cnt; --cnt > 0;);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	for(cnt = swd_delay_cnt; --cnt > 0;);
	if(dir == SWDIO_STATUS_DRIVE)
		SWDIO_MODE_DRIVE();
}

static uint32_t swdptap_seq_in(int ticks)
{
	uint32_t index = 1;
	uint32_t ret = 0;
	int len = ticks;
	register volatile int32_t cnt;

	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	if (swd_delay_cnt) {
		while (len--) {
			int res;
			res = gpio_get(SWDIO_PORT, SWDIO_PIN);
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
			ret |= (res) ? index : 0;
			index <<= 1;
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
		}
	} else {
		volatile int res;
		while (len--) {
			res = gpio_get(SWDIO_PORT, SWDIO_PIN);
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			ret |= (res) ? index : 0;
			index <<= 1;
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
		}
	}
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < len; i++)
		DEBUG("%d", (ret & (1 << i)) ? 1 : 0);
#endif
	return ret;
}

static bool swdptap_seq_in_parity(uint32_t *ret, int ticks)
{
	uint32_t index = 1;
	uint32_t res = 0;
	bool bit;
	int len = ticks;
	register volatile int32_t cnt;

	swdptap_turnaround(SWDIO_STATUS_FLOAT);
	if (swd_delay_cnt) {
		while (len--) {
			bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
			res |= (bit) ? index : 0;
			index <<= 1;
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
		}
	} else {
		while (len--) {
			bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			res |= (bit) ? index : 0;
			index <<= 1;
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
		}
	}
	int parity = __builtin_popcount(res);
	bit = gpio_get(SWDIO_PORT, SWDIO_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	for(cnt = swd_delay_cnt; --cnt > 0;);
	parity += (bit) ? 1 : 0;
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	for(cnt = swd_delay_cnt; --cnt > 0;);
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < len; i++)
		DEBUG("%d", (res & (1 << i)) ? 1 : 0);
#endif
	*ret = res;
	/* Terminate the read cycle now */
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	return (parity & 1);
}

static void swdptap_seq_out(uint32_t MS, int ticks)
{
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < ticks; i++)
		DEBUG("%d", (MS & (1 << i)) ? 1 : 0);
#endif
	register volatile int32_t cnt;
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS & 1);
	if (swd_delay_cnt) {
		while (ticks--) {
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
			MS >>= 1;
			gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS & 1);
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
		}
	} else {
		while (ticks--) {
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			MS >>= 1;
			gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS & 1);
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
		}
	}
}

static void swdptap_seq_out_parity(uint32_t MS, int ticks)
{
	int parity = __builtin_popcount(MS);
#ifdef DEBUG_SWD_BITS
	for (int i = 0; i < ticks; i++)
		DEBUG("%d", (MS & (1 << i)) ? 1 : 0);
#endif
	register volatile int32_t cnt;
	swdptap_turnaround(SWDIO_STATUS_DRIVE);
	gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS & 1);
	MS >>= 1;
	if (swd_delay_cnt) {
		while (ticks--) {
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
			gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS & 1);
			MS >>= 1;
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
			for(cnt = swd_delay_cnt; --cnt > 0;);
		}
	} else {
		while (ticks--) {
			gpio_set(SWCLK_PORT, SWCLK_PIN);
			gpio_set_val(SWDIO_PORT, SWDIO_PIN, MS & 1);
			MS >>= 1;
			gpio_clear(SWCLK_PORT, SWCLK_PIN);
		}
	}
	gpio_set_val(SWDIO_PORT, SWDIO_PIN, parity & 1);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	for(cnt = swd_delay_cnt; --cnt > 0;);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	for(cnt = swd_delay_cnt; --cnt > 0;);
}

swd_proc_t swd_proc;

int swdptap_init(void)
{
	swd_proc.swdptap_seq_in  = swdptap_seq_in;
	swd_proc.swdptap_seq_in_parity  = swdptap_seq_in_parity;
	swd_proc.swdptap_seq_out = swdptap_seq_out;
	swd_proc.swdptap_seq_out_parity  = swdptap_seq_out_parity;

	return 0;
}
