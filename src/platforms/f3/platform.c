/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2017 Uwe Bonnes bon@elektron,ikp,physik.tu-darmstadt.de
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

/* This file implements the platform specific functions for the STM32F3-IF implementation. */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "aux_serial.h"
#include "morse.h"

#include <libopencm3/stm32/f3/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/flash.h>

static void adc_init(void);

extern uint32_t _ebss; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

int platform_hwversion(void)
{
	return 0;
}

void platform_init(void)
{
	volatile uint32_t *magic = &_ebss;
	/*
	 * If RCC_CFGR is not at it's reset value, the bootloader
	 * was executed and SET_ADDRESS got us to this place.
	 * On the STM32F3, without any further effort, the application
	 * does not start in that case - so issue an reset to allow a clean start!
	 */
	if (RCC_CFGR)
		scb_reset_system();
	SYSCFG_MEMRM &= ~3U;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
	/* Button is BOOT0, so button is already evaluated!*/
	if (magic[0] == BOOTMAGIC0 && magic[1] == BOOTMAGIC1) {
		magic[0] = 0;
		magic[1] = 0;
		/*
		 * Jump to the built in bootloader by mapping system Flash.
		 * As we just come out of reset, no other deinit is needed!
		 */
		SYSCFG_MEMRM |= 1U;
		scb_reset_core();
	}
#pragma GCC diagnostic pop

	rcc_clock_setup_pll(&rcc_hse8mhz_configs[RCC_CLOCK_HSE8_72MHZ]);

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_CRC);
	rcc_periph_clock_enable(RCC_USB);

	/*
	 * Disconnect USB after reset:
	 * Pull USB_DP low. Device will reconnect automatically
	 * when USB is set up later, as a pull-up resistor is hard wired
	 */
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO12);
	gpio_clear(GPIOA, GPIO12);
	gpio_set_output_options(GPIOA, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, GPIO12);
	rcc_periph_reset_pulse(RST_USB);

	GPIOA_OSPEEDR &= ~0xf00cU;
	GPIOA_OSPEEDR |= 0x5004U; /* Set medium speed on PA1, PA6 and PA7 */
	gpio_mode_setup(JTAG_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN | TCK_PIN | TDI_PIN);
	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TDO_PIN);
	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_UART | LED_IDLE_RUN | LED_ERROR | LED_BOOTLOADER);
	gpio_mode_setup(NRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, NRST_PIN);
	gpio_set(NRST_PORT, NRST_PIN);
	gpio_set_output_options(NRST_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, NRST_PIN);

	adc_init();

	platform_timing_init();
	/* Set up USB pins and alternate function */
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF14, GPIO11 | GPIO12);
	blackmagic_usb_init();
	aux_serial_init();
}

void platform_nrst_set_val(bool assert)
{
	gpio_set_val(NRST_PORT, NRST_PIN, !assert);
}

bool platform_nrst_get_val(void)
{
	return (gpio_get(NRST_PORT, NRST_PIN)) ? false : true;
}


static void adc_init(void)
{

	//ADC
	rcc_periph_clock_enable(RCC_ADC12);
	//ADC
	gpio_mode_setup(VTREF_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, VTREF_PIN);
	//gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO1);
	adc_power_off(ADC1);
	adc_set_clk_prescale(ADC1, ADC_CCR_CKMODE_DIV2);
	adc_set_single_conversion_mode(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_right_aligned(ADC1);
	/* We want to read the temperature sensor, so we have to enable it. */
	//adc_enable_temperature_sensor();
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_601DOT5CYC);
	uint8_t channel_array[] = { 1 }; /* ADC1_IN1 (PA0) */
	adc_calibrate(ADC1);
	adc_set_regular_sequence(ADC1, 1, channel_array);
	adc_set_resolution(ADC1, ADC_CFGR1_RES_12_BIT);
	adc_power_on(ADC1);

	/* Wait for ADC starting up. */
	int i;
	for (i = 0; i < 800000; i++)
		__asm__("nop");
	


}


uint32_t platform_target_voltage_sense(void)
{
	/*
	 * Returns the voltage in tenths of a volt (so 33 means 3.3V)
	 */
	adc_start_conversion_regular(ADC1);
	while (!(adc_eoc(ADC1)))
		continue;
	uint32_t val=adc_read_regular(ADC1);
	return (val * 99U) / 8191U;;
	//return val;
	
	return 0;
}



const char *platform_target_voltage(void)
{
	static char ret[] = "0.0V";
	uint32_t val = platform_target_voltage_sense();
	ret[0] = '0' + val / 10U;
	ret[2] = '0' + val % 10U;

	/*static char ret[] = "0000000000";
	uint32_t val = platform_target_voltage_sense();
	for(uint8_t i =  10; i > 0; i--){
		ret[i-1] = '0' + val % 10U;
		val = val / 10;
	}*/
	
	return ret;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"

void platform_request_boot(void)
{
	/* Bootloader cares for reenumeration */
	uint32_t *magic = &_ebss;
	magic[0] = BOOTMAGIC0;
	magic[1] = BOOTMAGIC1;
	scb_reset_system();
}

#pragma GCC diagnostic pop

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
