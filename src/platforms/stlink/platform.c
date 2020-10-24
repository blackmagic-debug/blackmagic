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

/* This file implements the platform specific functions for the ST-Link
 * implementation.
 */

#include "general.h"
#include "cdcacm.h"
#include "usbuart.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/adc.h>

uint16_t led_idle_run;
uint16_t srst_pin;
static uint32_t rev;
static void adc_init(void);

int platform_hwversion(void)
{
	return rev;
}

void platform_init(void)
{
	rev = detect_rev();
	SCS_DEMCR |= SCS_DEMCR_VC_MON_EN;
#ifdef ENABLE_DEBUG
	void initialise_monitor_handles(void);
	initialise_monitor_handles();
#endif
	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	if (rev == 0) {
		led_idle_run = GPIO8;
		srst_pin = SRST_PIN_V1;
	} else {
		led_idle_run = GPIO9;
		srst_pin = SRST_PIN_V2;
	}
	/* Setup GPIO ports */
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_2_MHZ,
	              GPIO_CNF_INPUT_FLOAT, TMS_PIN);
	gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_2_MHZ,
	              GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
	gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_2_MHZ,
	              GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);

	platform_srst_set_val(false);

	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ,
	              GPIO_CNF_OUTPUT_PUSHPULL, led_idle_run);

	/* Relocate interrupt vector table here */
	extern int vector_table;
	SCB_VTOR = (uint32_t)&vector_table;

	platform_timing_init();
	if (rev > 1) /* Reconnect USB */
		gpio_set(GPIOA, GPIO15);
	cdcacm_init();
	/* Don't enable UART if we're being debugged. */
	if (!(SCS_DEMCR & SCS_DEMCR_TRCENA))
		usbuart_init();
        adc_init();
}

void platform_srst_set_val(bool assert)
{
	if (assert) {
		gpio_set_mode(SRST_PORT, GPIO_MODE_OUTPUT_2_MHZ,
		              GPIO_CNF_OUTPUT_OPENDRAIN, srst_pin);
		gpio_clear(SRST_PORT, srst_pin);
	} else {
		gpio_set_mode(SRST_PORT, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_PULL_UPDOWN, srst_pin);
		gpio_set(SRST_PORT, srst_pin);
	}
}

bool platform_srst_get_val()
{
	return gpio_get(SRST_PORT, srst_pin) == 0;
}

static void adc_init(void)
{
	rcc_periph_clock_enable(RCC_ADC1);

	gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_ANALOG, GPIO0);

	adc_power_off(ADC1);
	adc_disable_scan_mode(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_28DOT5CYC);
        adc_enable_temperature_sensor();
	adc_power_on(ADC1);

	/* Wait for ADC starting up. */
	for (int i = 0; i < 800000; i++)    /* Wait a bit. */
		__asm__("nop");

	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);
}

const char *platform_target_voltage(void)
{
	static char ret[] = "0.00V";
	const uint8_t channel = 0;
	adc_set_regular_sequence(ADC1, 1, (uint8_t*)&channel);
	adc_start_conversion_direct(ADC1);
	/* Wait for end of conversion. */
	while (!adc_eoc(ADC1));
	uint32_t platform_adc_value = adc_read_regular(ADC1);

	const uint8_t ref_channel = 17;
	adc_set_regular_sequence(ADC1, 1, (uint8_t*)&ref_channel);
	adc_start_conversion_direct(ADC1);
	/* Wait for end of conversion. */
	while (!adc_eoc(ADC1));
	uint32_t vrefint_value = adc_read_regular(ADC1);

	/* Value in mV*/
	uint32_t val = (platform_adc_value * 2400) / vrefint_value;
	ret[0] = '0' +	val / 1000;
	ret[2] = '0' + (val /  100) % 10;
	ret[3] = '0' + (val /	10) % 10;

	return ret;
}
