/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by mean00
 * Timing fixes by ALTracer
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

/* This file implements the RVSW interface used by WCH chips */

#include "general.h"
#include "platform.h"
#include "timing.h"
#include "swd.h"
#include "maths_utils.h"
#include "swdptap_common.h"
#include "jep106.h"
#include "riscv_debug.h"

bool bmp_rvswd_scan(void);
// -- protocol part --
static bool rv_dm_reset(void);
static bool rv_start_frame(uint32_t adr, bool wr);
static bool rv_end_frame(uint32_t *status);

/* Busy-looping delay for GPIO bitbanging operations. SUBS+BNE.N take 4 cycles. */
static inline void platform_delay_busy(const uint32_t loops) __attribute__((always_inline));

void platform_delay_busy(const uint32_t loops)
{
	register uint32_t i = loops;
	do {
		__asm__("");
	} while (--i > 0U);
}

#define CLK_OFF()                                    \
	{                                                \
		gpio_clear(SWCLK_PORT, SWCLK_PIN);           \
		platform_delay_busy(target_clk_divider + 2); \
	}
#define CLK_ON()                                     \
	{                                                \
		gpio_set(SWCLK_PORT, SWCLK_PIN);             \
		platform_delay_busy(target_clk_divider + 2); \
	}

#define IO_OFF()                                     \
	{                                                \
		gpio_clear(SWDIO_PORT, SWDIO_PIN);           \
		platform_delay_busy(target_clk_divider + 2); \
	}
#define IO_ON()                                      \
	{                                                \
		gpio_set(SWDIO_PORT, SWDIO_PIN);             \
		platform_delay_busy(target_clk_divider + 2); \
	}

static void rv_write_nbits(int n, uint32_t value)
{
	value <<= (uint32_t)(32 - n);
	const uint32_t mask = 0x80000000UL;
	for (int i = 0; i < n; i++) {
		CLK_OFF();
		if (value & mask)
			IO_ON()
		else
			IO_OFF();
		CLK_ON();
		value <<= 1;
	}
}

static void rv_start_bit(void)
{
	SWDIO_MODE_DRIVE();
	IO_OFF();
}

static void rv_stop_bit(void)
{
	CLK_OFF();
	SWDIO_MODE_DRIVE();
	IO_OFF();
	CLK_ON();
	IO_ON();
}

static uint32_t rv_read_nbits(int n)
{
	SWDIO_MODE_FLOAT();
	uint32_t out = 0;
	for (int i = 0; i < n; i++) {
		CLK_OFF();
		CLK_ON();
		out = (out << 1) + (!!gpio_get(SWDIO_IN_PORT, SWDIO_IN_PIN)); // read bit on rising edge
	}
	return out;
}

static bool rv_dm_reset(void)
{
	// toggle the clock 100 times
	SWDIO_MODE_DRIVE();
	IO_ON();
	for (int i = 0; i < 5; i++) // 100 bits to 1
	{
		rv_write_nbits(20, 0xfffff);
	}
	IO_OFF();
	IO_ON();
	platform_delay(10);
	return true;
}

static bool rv_start_frame(uint32_t adr, bool wr)
{
	rv_start_bit();
	adr = (adr << 1) + wr;
	uint8_t parity = calculate_odd_parity(adr);
	adr = adr << 2 | (parity + parity + parity);
	rv_write_nbits(10, adr);
	return true;
}

static bool rv_end_frame(uint32_t *status)
{
	uint32_t out = 0;

	// now get the reply - 4 bits
	out = rv_read_nbits(4);
	rv_stop_bit();

	*status = out;
	if (out != 3 && out != 7) {
		DEBUG_ERROR("Status error : 0x%x\n", out);
		return false;
	}
	return out;
}

bool rv_dm_write(uint32_t adr, uint32_t val)
{
	rv_start_frame(adr, true);

	rv_write_nbits(4, 0);
	// Now data
	uint8_t parity2 = calculate_odd_parity(val);
	rv_write_nbits(32, val);

	// data parity (twice)
	rv_write_nbits(2, parity2 + parity2 + parity2);

	uint32_t st = 0;
	if (!rv_end_frame(&st)) {
		DEBUG_ERROR("Write failed Adr=0x%x Value=0x%x status=0x%x\n", adr, val, st);
		return false;
	}
	return true;
}

bool rv_dm_read(uint32_t adr, uint32_t *output)
{
	rv_start_frame(adr, false);
	rv_write_nbits(4, 1); // 000 1
	*output = rv_read_nbits(32);
	rv_read_nbits(2); // parity

	uint32_t st = 0;
	if (!rv_end_frame(&st)) {
		DEBUG_ERROR("Read failed Adr=0x%x Value=0x%x status=0x%x\n", adr, *output, st);
		return false;
	}
	return true;
}

static bool rv_dm_probe(uint32_t *chip_id)
{
	*chip_id = 0;
	uint32_t out = 0; // scratch
	// This follows exactly what the wchlink does
	rv_dm_write(0x10, 0x80000001UL); // write DM CTRL = 0x800000001
	platform_delay(1);
	rv_dm_write(0x10, 0x80000001UL); // write DM CTRL = 0x800000001
	platform_delay(1);
	rv_dm_read(0x11, &out); // read DM_STATUS
	platform_delay(1);
	rv_dm_read(0x7f, chip_id); // read 0x7f
	return ((*chip_id) & 0x7fff) != 0x7fff;
}

//---------------------- RVSWD DMI -----------------------
static bool rvswdp_riscv_dmi_read(riscv_dmi_s *dmi, uint32_t address, uint32_t *value);
static bool rvswdp_riscv_dmi_write(riscv_dmi_s *dmi, uint32_t address, uint32_t value);

static bool rvswdp_riscv_dmi_read(riscv_dmi_s *const dmi, const uint32_t address, uint32_t *const value)
{
	int retries = 1;
	while (1) {
		if (!retries) {
			dmi->fault = RV_DMI_FAILURE;
			return false;
		}
		const bool result = rv_dm_read(address, value);
		if (result) {
			dmi->fault = RV_DMI_SUCCESS;
			return true;
		}
		retries--;
	}
}

static bool rvswdp_riscv_dmi_write(riscv_dmi_s *const dmi, const uint32_t address, const uint32_t value)
{
	const bool result = rv_dm_write(address, value);
	if (result) {
		dmi->fault = RV_DMI_SUCCESS;
		return true;
	}
	dmi->fault = RV_DMI_FAILURE;
	return false;
}

bool bmp_rvswd_scan(void)
{
	uint32_t id = 0;
	rv_dm_reset();
	target_list_free();
	if (!rv_dm_probe(&id)) {
		return false;
	}
	DEBUG_INFO("WCH : found 0x%x device\n", id);
	riscv_dmi_s *dmi = (riscv_dmi_s *)calloc(1, sizeof(riscv_dmi_s));
	if (!dmi) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	dmi->designer_code = JEP106_MANUFACTURER_WCH;
	dmi->version = RISCV_DEBUG_0_13; /* Assumption, unverified */
	/*dmi->version = RISCV_DEBUG_NONSTANDARD;*/
	dmi->address_width = 8U;
	dmi->read = rvswdp_riscv_dmi_read;
	dmi->write = rvswdp_riscv_dmi_write;

	riscv_dmi_init(dmi);
	/* If we failed to find any DMs or Harts, free the structure */
	if (!dmi->ref_count) {
		free(dmi);
		return false;
	}
	return true;
}

// EOF
