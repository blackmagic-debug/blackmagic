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

/* This file implements the platform specific functions for the F4 Discovery implementation. */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "aux_serial.h"
#include "morse.h"
#include "exception.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/cortex.h>

jmp_buf fatal_error_jmpbuf;
extern uint32_t _ebss; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

int platform_hwversion(void)
{
	return 0;
}

void platform_init(void)
{
	volatile uint32_t *magic = &_ebss;
	/* Enable GPIO peripherals */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_GPIOD);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
	/* Check the USER button */
	if (gpio_get(GPIOA, GPIO0) || (magic[0] == BOOTMAGIC0 && magic[1] == BOOTMAGIC1)) {
		magic[0] = 0;
		magic[1] = 0;
		/* Assert blue LED as indicator we are in the firmware */
		gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_BOOTLOADER);
		gpio_set(LED_PORT, LED_BOOTLOADER);
		/*
		 * Jump to the built in bootloader by mapping System flash.
		 * As we just come out of reset, no other deinit is needed!
		 */
		rcc_periph_clock_enable(RCC_SYSCFG);
		SYSCFG_MEMRM &= ~3U;
		SYSCFG_MEMRM |= 1U;
		scb_reset_core();
	}
#pragma GCC diagnostic pop
	rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_168MHZ]);

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_OTGFS);
	rcc_periph_clock_enable(RCC_CRC);

	/* Set up USB pins and alternate function */
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO9 | GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO9 | GPIO10 | GPIO11 | GPIO12);

	GPIOC_OSPEEDR &= ~0xf30U;
	GPIOC_OSPEEDR |= 0xa20U;

	gpio_mode_setup(JTAG_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TCK_PIN | TDI_PIN);
	gpio_mode_setup(JTAG_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TMS_PIN);
	gpio_set_output_options(JTAG_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TCK_PIN | TDI_PIN | TMS_PIN);
	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TDO_PIN);
	gpio_set_output_options(TDO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TDO_PIN | TMS_PIN);

	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_IDLE_RUN | LED_ERROR | LED_BOOTLOADER);

	gpio_mode_setup(LED_PORT_UART, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_UART);

#ifdef PLATFORM_HAS_POWER_SWITCH
	gpio_set(PWR_BR_PORT, PWR_BR_PIN);
	gpio_mode_setup(PWR_BR_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PWR_BR_PIN);
#endif

	platform_timing_init();
	blackmagic_usb_init();
	aux_serial_init();
}

void platform_nrst_set_val(bool assert)
{
	(void)assert;
}

bool platform_nrst_get_val(void)
{
	return false;
}

const char *platform_target_voltage(void)
{
	return NULL;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"

void platform_request_boot(void)
{
	uint32_t *magic = (uint32_t *)&_ebss;
	magic[0] = BOOTMAGIC0;
	magic[1] = BOOTMAGIC1;
	scb_reset_system();
}

#pragma GCC diagnostic pop

#ifdef PLATFORM_HAS_POWER_SWITCH
bool platform_target_get_power(void)
{
	return !gpio_get(PWR_BR_PORT, PWR_BR_PIN);
}

bool platform_target_set_power(const bool power)
{
	gpio_set_val(PWR_BR_PORT, PWR_BR_PIN, !power);
	return true;
}
#endif

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
