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

/* This file implements the low-level JTAG TAP interface.  */

#include <stdio.h>

#include "general.h"
#include "jtagtap.h"
#include "gdb_packet.h"

jtag_proc_t jtag_proc;

static void jtagtap_reset(void);
static void jtagtap_tms_seq(uint32_t tms_states, int ticks);
static void jtagtap_tdi_tdo_seq(uint8_t *data_out, uint8_t final_tms, const uint8_t *data_in, int ticks);
static void jtagtap_tdi_seq(uint8_t final_tms, const uint8_t *DI, int ticks);
static uint8_t jtagtap_next(uint8_t tms, uint8_t tdi);

int jtagtap_init()
{
	TMS_SET_MODE();

	jtag_proc.jtagtap_reset = jtagtap_reset;
	jtag_proc.jtagtap_next = jtagtap_next;
	jtag_proc.jtagtap_tms_seq = jtagtap_tms_seq;
	jtag_proc.jtagtap_tdi_tdo_seq = jtagtap_tdi_tdo_seq;
	jtag_proc.jtagtap_tdi_seq = jtagtap_tdi_seq;

	/* Go to JTAG mode for SWJ-DP */
	for (size_t i = 0; i <= 50U; ++i)
		jtagtap_next(1, 0);      /* Reset SW-DP */
	jtagtap_tms_seq(0xe73cU, 16U); /* SWD to JTAG sequence */
	jtagtap_soft_reset();

	return 0;
}

static void jtagtap_reset(void)
{
#ifdef TRST_PORT
	if (platform_hwversion() == 0) {
		gpio_clear(TRST_PORT, TRST_PIN);
		for (volatile size_t i = 0; i < 10000; i++)
			asm("nop");
		gpio_set(TRST_PORT, TRST_PIN);
	}
#endif
	jtagtap_soft_reset();
}

static uint8_t jtagtap_next(const uint8_t tms, const uint8_t tdi)
{
	register volatile int32_t cnt;

	gpio_set_val(TMS_PORT, TMS_PIN, tms);
	gpio_set_val(TDI_PORT, TDI_PIN, tdi);
	gpio_set(TCK_PORT, TCK_PIN);
	for (cnt = swd_delay_cnt - 2U; cnt > 0; cnt--)
		continue;
	const uint16_t result = gpio_get(TDO_PORT, TDO_PIN);
	gpio_clear(TCK_PORT, TCK_PIN);
	for (cnt = swd_delay_cnt - 2U; cnt > 0; cnt--)
		continue;

	//DEBUG("jtagtap_next(TMS = %u, TDI = %u) = %u\n", tms, tdi, result);

	return result != 0;
}

static void jtagtap_tms_seq_swd_delay(uint32_t tms_states, size_t ticks)
{
	while (ticks) {
		const bool state = tms_states & 1;
		gpio_set_val(TMS_PORT, TMS_PIN, state);
		gpio_set(TCK_PORT, TCK_PIN);
		for (volatile int32_t cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
			continue;
		tms_states >>= 1;
		ticks--;
		gpio_clear(TCK_PORT, TCK_PIN);
		for (volatile int32_t cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
			continue;
	}
}

static void jtagtap_tms_seq_no_delay(uint32_t tms_states, size_t ticks)
{
	while (ticks) {
		const bool state = tms_states & 1;
		gpio_set_val(TMS_PORT, TMS_PIN, state);
		gpio_set(TCK_PORT, TCK_PIN);
		tms_states >>= 1;
		ticks--;
		gpio_clear(TCK_PORT, TCK_PIN);
	}
}

static void jtagtap_tms_seq(uint32_t tms_states, int ticks)
{
	gpio_set(TDI_PORT, TDI_PIN);
	if (swd_delay_cnt)
		jtagtap_tms_seq_swd_delay(tms_states, ticks);
	else
		jtagtap_tms_seq_no_delay(tms_states, ticks);
}

static void jtagtap_tdi_tdo_seq_swd_delay(const uint8_t *data_in, uint8_t *data_out, const bool final_tms, size_t clock_cycles)
{
	size_t byte = 0;
	size_t index = 0;
	uint8_t value = 0;
	while (clock_cycles--) {
		/* On the last cycle, assert final_tms to TMS_PIN */
		if (!clock_cycles)
			gpio_set_val(TMS_PORT, TMS_PIN, final_tms);
		/* Set up the TDI pin and start the clock cycle */
		gpio_set_val(TDI_PORT, TDI_PIN, data_in[byte] & (1 << index));
		gpio_set(TCK_PORT, TCK_PIN);
		for (volatile int32_t cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
			continue;
		/* If TDO is high, store a 1 in the appropriate position in the value being accumulated */
		if (gpio_get(TDO_PORT, TDO_PIN))
			value |= (1 << index);
		if (index++ == 7U) {
			data_out[byte++] = value;
			index = 0;
			value = 0;
		}
		/* Finish the clock cycle */
		gpio_clear(TCK_PORT, TCK_PIN);
		for (volatile int32_t cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
			continue;
	}
	if (index)
		data_out[byte] = value;
}

static void jtagtap_tdi_tdo_seq_no_delay(const uint8_t *data_in, uint8_t *data_out, const bool final_tms, size_t clock_cycles)
{
	size_t byte = 0;
	size_t index = 0;
	uint8_t value = 0;
	while (clock_cycles--) {
		/* On the last tick, assert final_tms to TMS_PIN */
		if (!clock_cycles)
			gpio_set_val(TMS_PORT, TMS_PIN, final_tms);
		/* Set up the TDI pin and start the clock cycle */
		gpio_set_val(TDI_PORT, TDI_PIN, data_in[byte] & (1 << index));
		gpio_set(TCK_PORT, TCK_PIN);
		/* If TDO is high, store a 1 in the appropriate position in the value being accumulated */
		if (gpio_get(TDO_PORT, TDO_PIN))
			value |= (1 << index);
		/* If we've got to the next whole byte, store the accumulated value and reset state */
		if (index++ == 7U) {
			data_out[byte++] = value;
			index = 0;
			value = 0;
		}
		/* Finish the clock cycle */
		gpio_clear(TCK_PORT, TCK_PIN);
	}
	if (index)
		data_out[byte] = value;
}

static void jtagtap_tdi_tdo_seq(uint8_t *data_out, const uint8_t final_tms, const uint8_t *data_in, int ticks)
{
	gpio_clear(TDI_PORT, TDI_PIN);
	if (swd_delay_cnt)
		jtagtap_tdi_tdo_seq_swd_delay(data_in, data_out, final_tms, ticks);
	else
		jtagtap_tdi_tdo_seq_no_delay(data_in, data_out, final_tms, ticks);
}

static void jtagtap_tdi_seq(const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	uint8_t index = 1;
	register volatile int32_t cnt;
	if (swd_delay_cnt) {
		while (ticks--) {
			gpio_set_val(TMS_PORT, TMS_PIN, ticks ? 0 : final_tms);
			gpio_set_val(TDI_PORT, TDI_PIN, *DI & index);
			gpio_set(TCK_PORT, TCK_PIN);
			for (cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
				;
			if (!(index <<= 1)) {
				index = 1;
				DI++;
			}
			gpio_clear(TCK_PORT, TCK_PIN);
			for (cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
				;
		}
	} else {
		while (ticks--) {
			gpio_set_val(TMS_PORT, TMS_PIN, ticks ? 0 : final_tms);
			gpio_set_val(TDI_PORT, TDI_PIN, *DI & index);
			gpio_set(TCK_PORT, TCK_PIN);
			if (!(index <<= 1)) {
				index = 1;
				DI++;
			}
			gpio_clear(TCK_PORT, TCK_PIN);
		}
	}
}
