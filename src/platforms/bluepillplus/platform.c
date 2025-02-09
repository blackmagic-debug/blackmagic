/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
 * Written by ALTracer <11005378+ALTracer@users.noreply.github.com>
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

/* This file implements the platform specific functions for the WeActStudio.BluePill-Plus implementation. */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "aux_serial.h"

#include <libopencm3/cm3/vector.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/rcc.h>

volatile uint32_t magic[2] __attribute__((section(".noinit")));

static void platform_detach_usb(void);

void platform_request_boot(void)
{
	magic[0] = BOOTMAGIC0;
	magic[1] = BOOTMAGIC1;
	SCB_VTOR = 0;
	platform_detach_usb();
	scb_reset_system();
}

void platform_init(void)
{
	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRC);
	rcc_periph_clock_enable(RCC_USB);
#if SWO_ENCODING == 1 || SWO_ENCODING == 3
	/* Make sure to power up the timer used for trace */
	rcc_periph_clock_enable(SWO_TIM_CLK);
#endif
#if SWO_ENCODING == 2 || SWO_ENCODING == 3
	/* Enable relevant USART and DMA early in platform init */
	rcc_periph_clock_enable(SWO_UART_CLK);
	rcc_periph_clock_enable(SWO_DMA_CLK);
#endif

	rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);

	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_INPUT_FLOAT, TMS_PIN);
	gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_INPUT_FLOAT, TCK_PIN);
	gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);
	gpio_set_mode(TDO_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_INPUT_FLOAT, TDO_PIN);
	platform_nrst_set_val(false);

	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, LED_IDLE_RUN);

	/* Relocate interrupt vector table here */
	SCB_VTOR = (uintptr_t)&vector_table;

	platform_timing_init();
	platform_detach_usb();
	blackmagic_usb_init();
	aux_serial_init();
	/* By default, do not drive the SWD bus too fast. */
	platform_max_frequency_set(2000000);
}

void platform_nrst_set_val(bool assert)
{
	if (assert) {
		gpio_set_mode(NRST_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, NRST_PIN);
		gpio_clear(NRST_PORT, NRST_PIN);
	} else {
		gpio_set_mode(NRST_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, NRST_PIN);
		gpio_set(NRST_PORT, NRST_PIN);
	}
}

bool platform_nrst_get_val(void)
{
	return gpio_get(NRST_PORT, NRST_PIN) == 0;
}

void platform_target_clk_output_enable(bool enable)
{
	if (enable) {
		gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
		SWDIO_MODE_DRIVE();
	} else {
		SWDIO_MODE_FLOAT();
		gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_INPUT_FLOAT, TCK_PIN);
	}
}

const char *platform_target_voltage(void)
{
	return "Unknown";
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

int platform_hwversion(void)
{
	return 0;
}

void platform_detach_usb(void)
{
	/*
	 * Disconnect USB after reset:
	 * Pull USB_DP low. Device will reconnect automatically
	 * when USB is set up later, as Pull-Up is hard wired
	 */
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_reset_pulse(RST_USB);

	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
	gpio_clear(GPIOA, GPIO12);
	for (volatile uint32_t counter = 10000; counter > 0; --counter)
		continue;
}
