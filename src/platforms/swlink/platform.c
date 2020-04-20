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

/* This file implements the platform specific functions for ST-Link
 * on the STM8S discovery and STM32F103 Minimum System Development Board, also
 * known as bluepill.
 */

#include "general.h"
#include "cdcacm.h"
#include "usbuart.h"

#include <libopencm3/stm32/f1/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/f1/adc.h>

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
#ifdef ENABLE_DEBUG
	void initialise_monitor_handles(void);
	initialise_monitor_handles();
#endif
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	rev =  detect_rev();
	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRC);

	/* Unmap JTAG Pins so we can reuse as GPIO */
	data = AFIO_MAPR;
	data &= ~AFIO_MAPR_SWJ_MASK;
	data |= AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_OFF;
	AFIO_MAPR = data;
	/* Setup JTAG GPIO ports */
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_50_MHZ,
			GPIO_CNF_INPUT_FLOAT, TMS_PIN);
	gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_50_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
	gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_50_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);

	gpio_set_mode(TDO_PORT, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_FLOAT, TDO_PIN);

	switch (rev) {
	case 0:
		/* LED GPIO already set in detect_rev()*/
		led_error_port = GPIOA;
		led_error_pin = GPIO8;
		adc_init();
		break;
	case 1:
		led_error_port = GPIOC;
		led_error_pin = GPIO13;
		/* Enable MCO Out on PA8*/
		RCC_CFGR &= ~(0xf << 24);
		RCC_CFGR |= (RCC_CFGR_MCO_HSE << 24);
		gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
					  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);
		break;
	}
	platform_srst_set_val(false);

	/* Remap TIM2 TIM2_REMAP[1]
	 * TIM2_CH1_ETR -> PA15 (TDI, set as output above)
	 * TIM2_CH2     -> PB3  (TDO)
	 */
	data = AFIO_MAPR;
	data &= ~AFIO_MAPR_TIM2_REMAP_FULL_REMAP;
	data |=  AFIO_MAPR_TIM2_REMAP_PARTIAL_REMAP1;
	AFIO_MAPR = data;

	/* Relocate interrupt vector table here */
	extern int vector_table;
	SCB_VTOR = (uint32_t)&vector_table;

	platform_timing_init();
	cdcacm_init();
	usbuart_init();
}

void platform_srst_set_val(bool assert)
{
	/* We reuse JSRST as SRST.*/
	if (assert) {
		gpio_set_mode(JRST_PORT, GPIO_MODE_OUTPUT_50_MHZ,
		              GPIO_CNF_OUTPUT_OPENDRAIN, JRST_PIN);
		/* Wait until requested value is active.*/
		while (gpio_get(JRST_PORT, JRST_PIN))
			gpio_clear(JRST_PORT, JRST_PIN);
	} else {
		gpio_set_mode(JRST_PORT, GPIO_MODE_INPUT,
					  GPIO_CNF_INPUT_PULL_UPDOWN, JRST_PIN);
		/* Wait until requested value is active.*/
		while (!gpio_get(JRST_PORT, JRST_PIN))
			gpio_set(JRST_PORT, JRST_PIN);
	}
}

bool platform_srst_get_val(void)
{
	return gpio_get(JRST_PORT, JRST_PIN) == 0;
}

static void adc_init(void)
{
	rcc_periph_clock_enable(RCC_ADC1);
	/* PA0 measures CN7 Pin 1 VDD divided by two.*/
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
				  GPIO_CNF_INPUT_ANALOG, GPIO0);
	adc_power_off(ADC1);
	adc_disable_scan_mode(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_28DOT5CYC);

	adc_power_on(ADC1);

	/* Wait for ADC starting up. */
	for (int i = 0; i < 800000; i++)	/* Wait a bit. */
		__asm__("nop");

	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);
}

const char *platform_target_voltage(void)
{
	static char ret[] = "0.0V";
	const uint8_t channel = 0;
	switch (rev) {
	case 0:
		adc_set_regular_sequence(ADC1, 1, (uint8_t*)&channel);
		adc_start_conversion_direct(ADC1);
		/* Wait for end of conversion. */
		while (!adc_eoc(ADC1));
		/* Referencevoltage is 3.3 Volt, measured voltage is half of
		 * actual voltag. */
		uint32_t val_in_100mV = (adc_read_regular(ADC1) * 33 * 2) / 4096;
		ret[0] = '0' + val_in_100mV / 10;
		ret[2] = '0' + val_in_100mV % 10;
		return ret;
	}
	return NULL;
}

void set_idle_state(int state)
{
	switch (rev) {
	case 0:
		gpio_set_val(GPIOA, GPIO8, state);
		break;
	case 1:
		gpio_set_val(GPIOC, GPIO13, (!state));
		break;
	}
}
