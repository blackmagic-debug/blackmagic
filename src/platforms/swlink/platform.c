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

/*
 * This file implements the platform specific functions for the "swlink" (ST-Link clones) implementation.
 * This is targeted to STM8S discovery and STM32F103 Minimum System Development Board (also known as the bluepill).
 */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "aux_serial.h"

#include <libopencm3/cm3/vector.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/adc.h>

#include "platform_common.h"

uint32_t led_error_port;
uint16_t led_error_pin;
static uint8_t rev;

static void adc_init(void);

int platform_hwversion(void)
{
	return rev;
}

void platform_init(void)
{
	uint32_t data;
	SCS_DEMCR |= SCS_DEMCR_VC_MON_EN;
	rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);
	rev = detect_rev();
	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRC);

	/* Unmap JTAG Pins so we can reuse as GPIO */
	data = AFIO_MAPR;
	data &= ~AFIO_MAPR_SWJ_MASK;
	data |= AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_OFF;
	AFIO_MAPR = data;
	/* Setup JTAG GPIO ports */
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_INPUT_FLOAT, TMS_PIN);
	gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
	gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);

	gpio_set_mode(TDO_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, TDO_PIN);

	switch (rev) {
	case 0:
		/* LED GPIO already set in detect_rev() */
		led_error_port = GPIOA;
		led_error_pin = GPIO8;
		adc_init();
		break;
	case 1:
		led_error_port = GPIOC;
		led_error_pin = GPIO13;
		/* Enable MCO Out on PA8 */
		RCC_CFGR &= ~(0xfU << 24U);
		RCC_CFGR |= (RCC_CFGR_MCO_HSE << 24U);
		gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);
		break;
	}
	platform_nrst_set_val(false);

	/*
	 * Remap TIM2 TIM2_REMAP[1]
	 * TIM2_CH1_ETR -> PA15 (TDI, set as output above)
	 * TIM2_CH2     -> PB3  (TDO)
	 */
	data = AFIO_MAPR;
	data &= ~AFIO_MAPR_TIM2_REMAP_FULL_REMAP;
	data |= AFIO_MAPR_TIM2_REMAP_PARTIAL_REMAP1;
	AFIO_MAPR = data;

	/* Relocate interrupt vector table here */
	SCB_VTOR = (uintptr_t)&vector_table;

	platform_timing_init();
	blackmagic_usb_init();
	aux_serial_init();
}

void platform_nrst_set_val(bool assert)
{
	/* We reuse nTRST as nRST. */
	if (assert) {
		gpio_set_mode(TRST_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, TRST_PIN);
		/* Wait until requested value is active. */
		while (gpio_get(TRST_PORT, TRST_PIN))
			gpio_clear(TRST_PORT, TRST_PIN);
	} else {
		gpio_set_mode(TRST_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, TRST_PIN);
		/* Wait until requested value is active .*/
		while (!gpio_get(TRST_PORT, TRST_PIN))
			gpio_set(TRST_PORT, TRST_PIN);
	}
}

bool platform_nrst_get_val(void)
{
	return gpio_get(TRST_PORT, TRST_PIN) == 0;
}

static void adc_init(void)
{
	rcc_periph_clock_enable(RCC_ADC1);
	/* PA0 measures CN7 Pin 1 VDD divided by two. */
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, GPIO0);
	adc_power_off(ADC1);
	adc_disable_scan_mode(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_28DOT5CYC);

	adc_power_on(ADC1);

	/* Wait for the ADC to finish starting up */
	for (volatile size_t i = 0; i < 800000U; ++i)
		continue;

	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);
}

const char *platform_target_voltage(void)
{
	static char ret[] = "0.0V";
	const uint8_t channel = 0;
	switch (rev) {
	case 0:
		adc_set_regular_sequence(ADC1, 1, &channel);
		adc_start_conversion_direct(ADC1);
		/* Wait for end of conversion. */
		while (!adc_eoc(ADC1))
			continue;
		/*
		 * Reference voltage is 3.3V.
		 * We expect the measured voltage to be half of the actual voltage.
		 * The computed value read is expressed in 0.1mV steps
		 */
		uint32_t value = (adc_read_regular(ADC1) * 66U) / 4096U;
		ret[0] = '0' + value / 10U;
		ret[2] = '0' + value % 10U;
		return ret;
	}
	return "Unknown";
}

void set_idle_state(int state)
{
	switch (rev) {
	case 0:
		gpio_set_val(GPIOA, GPIO8, state);
		break;
	case 1:
		gpio_set_val(GPIOC, GPIO13, !state);
		break;
	}
}

void platform_target_clk_output_enable(bool enable)
{
	(void)enable;
}

bool platform_spi_init(const spi_bus_e bus)
{
	(void)bus;
	return false;
}

bool platform_spi_deinit(const spi_bus_e bus)
{
	(void)bus;
	return false;
}

bool platform_spi_chip_select(const uint8_t device_select)
{
	(void)device_select;
	return false;
}

uint8_t platform_spi_xfer(const spi_bus_e bus, const uint8_t value)
{
	(void)bus;
	return value;
}

#ifdef PLATFORM_HAS_CUSTOM_COMMANDS

#include "swd.h"
#include "gdb_packet.h"
#include "target_internal.h"
#include <libopencm3/cm3/dwt.h>

static bool cmd_swdptap_calibration(target_s *target, int argc, const char **argv);

const command_s platform_cmd_list[] = {
	{"calibrate_swd", cmd_swdptap_calibration, "Calibrate SW-DP TAP timings"},
	{NULL, NULL, NULL},
};

static void swdptap_linereset_measured(bool no_delay)
{
	const uint32_t ts_pre = dwt_read_cycle_counter();
	/* for robustness, we use 60 HIGH cycles and 4 idle cycles */
	swd_proc.seq_out(0xffffffffU, 32U);
	swd_proc.seq_out(0x0fffffffU, 32U);
	const uint32_t ts_post = dwt_read_cycle_counter();
	const uint32_t cycles_spent = ts_post - ts_pre;
	/* Subtract the overhead of function calls */
	const uint32_t fncall_corr = no_delay ? (88U) : (140U);
	/* Split the *64 into 16*4 for 216-240MHz clocks to not overflow a uint32_t */
	const uint32_t freq_measured = rcc_ahb_frequency * 16U / (cycles_spent - fncall_corr) * 4U;
	gdb_outf("Estimating %lu Hz (%lu cycles - %ld corr)\n", freq_measured, cycles_spent, fncall_corr);
}

static bool cmd_swdptap_calibration(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	if (target && target->attached)
		target_detach(target);
	platform_target_clk_output_enable(true);
	if (!swd_proc.seq_out)
		swdptap_init();
	dwt_enable_cycle_counter();

	uint32_t freq = 0;
	gdb_outf("Platform core clock %lu\n", rcc_ahb_frequency);

	/* Emit a _no_delay waveform */
	target_clk_divider = UINT32_MAX;
	freq = platform_max_frequency_get();
	gdb_outf("Changing frequency to %lu (no_delay)\n", freq);
	swdptap_linereset_measured(true);

	/* Loop through a few _delay values */
	for (uint32_t i = 0; i < 8; i++) {
		target_clk_divider = i;
		freq = platform_max_frequency_get();
		gdb_outf("Changing frequency to %lu (divider=%lu)\n", freq, target_clk_divider);
		swdptap_linereset_measured(false);
	}

	/* Reset frequency to medium */
	platform_max_frequency_set(3000000U);
	return true;
}

#endif
