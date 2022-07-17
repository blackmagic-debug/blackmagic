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
static void jtagtap_tms_seq(uint32_t tms_states, size_t ticks);
static void jtagtap_tdi_tdo_seq(uint8_t *data_out, bool final_tms, const uint8_t *data_in, size_t ticks);
static void jtagtap_tdi_seq(bool final_tms, const uint8_t *data_in, size_t ticks);
static bool jtagtap_next(bool tms, bool tdi);
static void jtagtap_cycle(bool tms, bool tdi, size_t clock_cycles);

int jtagtap_init()
{
	TMS_SET_MODE();

	jtag_proc.jtagtap_reset = jtagtap_reset;
	jtag_proc.jtagtap_next = jtagtap_next;
	jtag_proc.jtagtap_tms_seq = jtagtap_tms_seq;
	jtag_proc.jtagtap_tdi_tdo_seq = jtagtap_tdi_tdo_seq;
	jtag_proc.jtagtap_tdi_seq = jtagtap_tdi_seq;
	jtag_proc.jtagtap_cycle = jtagtap_cycle;

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
			__asm__("nop");
		gpio_set(TRST_PORT, TRST_PIN);
	}
#endif
	jtagtap_soft_reset();
}

static bool jtagtap_next_swd_delay()
{
	gpio_set(TCK_PORT, TCK_PIN);
	for (volatile int32_t cnt = swd_delay_cnt - 2U; cnt > 0; cnt--)
		continue;
	const uint16_t result = gpio_get(TDO_PORT, TDO_PIN);
	gpio_clear(TCK_PORT, TCK_PIN);
	for (volatile int32_t cnt = swd_delay_cnt - 2U; cnt > 0; cnt--)
		continue;
	return result != 0;
}

static bool jtagtap_next_no_delay()
{
	gpio_set(TCK_PORT, TCK_PIN);
	const uint16_t result = gpio_get(TDO_PORT, TDO_PIN);
	gpio_clear(TCK_PORT, TCK_PIN);
	return result != 0;
}

static bool jtagtap_next(const bool tms, const bool tdi)
{
	gpio_set_val(TMS_PORT, TMS_PIN, tms);
	gpio_set_val(TDI_PORT, TDI_PIN, tdi);
	if (swd_delay_cnt)
		return jtagtap_next_swd_delay();
	else // NOLINT(readability-else-after-return)
		return jtagtap_next_no_delay();
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

static void jtagtap_tms_seq(const uint32_t tms_states, const size_t ticks)
{
	gpio_set(TDI_PORT, TDI_PIN);
	if (swd_delay_cnt)
		jtagtap_tms_seq_swd_delay(tms_states, ticks);
	else
		jtagtap_tms_seq_no_delay(tms_states, ticks);
}

static void jtagtap_tdi_tdo_seq_swd_delay(const uint8_t *const data_in, uint8_t *const data_out, const bool final_tms, size_t clock_cycles)
{
	size_t byte = 0;
	size_t index = 0;
	uint8_t value = 0;
	while (clock_cycles--) {
		/* On the last cycle, assert final_tms to TMS_PIN */
		gpio_set_val(TMS_PORT, TMS_PIN, clock_cycles ? false : final_tms);
		/* Set up the TDI pin and start the clock cycle */
		gpio_set_val(TDI_PORT, TDI_PIN, data_in[byte] & (1U << index));
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

static void jtagtap_tdi_tdo_seq_no_delay(const uint8_t *const data_in, uint8_t *const data_out, const bool final_tms, size_t clock_cycles)
{
	size_t byte = 0;
	size_t index = 0;
	uint8_t value = 0;
	while (clock_cycles--) {
		/* On the last tick, assert final_tms to TMS_PIN */
		gpio_set_val(TMS_PORT, TMS_PIN, clock_cycles ? false : final_tms);
		/* Set up the TDI pin and start the clock cycle */
		gpio_set_val(TDI_PORT, TDI_PIN, data_in[byte] & (1U << index));
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

static void jtagtap_tdi_tdo_seq(uint8_t *const data_out, const bool final_tms, const uint8_t *const data_in, size_t ticks)
{
	gpio_clear(TMS_PORT, TMS_PIN);
	gpio_clear(TDI_PORT, TDI_PIN);
	if (swd_delay_cnt)
		jtagtap_tdi_tdo_seq_swd_delay(data_in, data_out, final_tms, ticks);
	else
		jtagtap_tdi_tdo_seq_no_delay(data_in, data_out, final_tms, ticks);
}

static void jtagtap_tdi_seq_swd_delay(const uint8_t *const data_in, const bool final_tms, size_t clock_cycles)
{
	size_t byte = 0;
	size_t index = 0;
	while (clock_cycles--) {
		/* On the last tick, assert final_tms to TMS_PIN */
		gpio_set_val(TMS_PORT, TMS_PIN, clock_cycles ? false : final_tms);
		/* Set up the TDI pin and start the clock cycle */
		gpio_set_val(TDI_PORT, TDI_PIN, data_in[byte] & (1U << index));
		gpio_set(TCK_PORT, TCK_PIN);
		for (volatile int32_t cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
			continue;
		/* If we've used a whole byte, reset state for the next */
		if (index++ == 7U) {
			++byte;
			index = 0;
		}
		/* Finish the clock cycle */
		gpio_clear(TCK_PORT, TCK_PIN);
		for (volatile int32_t cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
			continue;
	}
}

static void jtagtap_tdi_seq_no_delay(const uint8_t *const data_in, const bool final_tms, size_t clock_cycles)
{
	size_t byte = 0;
	size_t index = 0;
	while (clock_cycles--) {
		/* On the last tick, assert final_tms to TMS_PIN */
		gpio_set_val(TMS_PORT, TMS_PIN, clock_cycles ? false : final_tms);
		/* Set up the TDI pin and start the clock cycle */
		gpio_set_val(TDI_PORT, TDI_PIN, data_in[byte] & (1U << index));
		gpio_set(TCK_PORT, TCK_PIN);
		/* If we've used a whole byte, reset state for the next */
		if (index++ == 7U) {
			++byte;
			index = 0;
		}
		/* Finish the clock cycle */
		gpio_clear(TCK_PORT, TCK_PIN);
	}
}

static void jtagtap_tdi_seq(const bool final_tms, const uint8_t *const data_in, const size_t ticks)
{
	gpio_clear(TMS_PORT, TMS_PIN);
	if (swd_delay_cnt)
		jtagtap_tdi_seq_swd_delay(data_in, final_tms, ticks);
	else
		jtagtap_tdi_seq_no_delay(data_in, final_tms, ticks);
}

static void jtagtap_cycle_swd_delay(const size_t clock_cycles)
{
	for (size_t cycle = 0; cycle < clock_cycles; ++cycle) {
		gpio_set(TCK_PORT, TCK_PIN);
		for (volatile int32_t cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
			continue;
		gpio_clear(TCK_PORT, TCK_PIN);
		for (volatile int32_t cnt = swd_delay_cnt - 2; cnt > 0; cnt--)
			continue;
	}
}

static void jtagtap_cycle_no_delay(const size_t clock_cycles)
{
	for (size_t cycle = 0; cycle < clock_cycles; ++cycle) {
		gpio_set(TCK_PORT, TCK_PIN);
		gpio_set(TCK_PORT, TCK_PIN);
		gpio_clear(TCK_PORT, TCK_PIN);
	}
}

static void jtagtap_cycle(const bool tms, const bool tdi, const size_t clock_cycles)
{
	jtagtap_next(tms, tdi);
	if (swd_delay_cnt)
		jtagtap_cycle_swd_delay(clock_cycles - 1);
	else
		jtagtap_cycle_no_delay(clock_cycles - 1);
}
