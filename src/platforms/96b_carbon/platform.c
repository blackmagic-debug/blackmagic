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

/* This file implements the platform specific functions for the STM32
 * implementation.
 */

#include "general.h"
#include "cdcacm.h"
#include "usbuart.h"
#include "morse.h"

#include <libopencm3/stm32/f4/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/usb/usbd.h>

jmp_buf fatal_error_jmpbuf;

void platform_init(void)
{
	rcc_clock_setup_pll(&rcc_hse_16mhz_3v3[RCC_CLOCK_3V3_84MHZ]);

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_OTGFS);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_GPIOD);
	rcc_periph_clock_enable(RCC_CRC);

	/* Set up USB Pins and alternate function*/
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO11 | GPIO12);

	gpio_mode_setup(JTAG_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
			TMS_PIN | TCK_PIN | TDI_PIN);
	gpio_set_output_options(JTAG_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
			TMS_PIN | TCK_PIN | TDI_PIN);
        gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE,
			TDO_PIN);

	gpio_mode_setup(TRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TRST_PIN);
	gpio_mode_setup(NRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, NRST_PIN);

	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_IDLE_RUN);
	gpio_mode_setup(LED_PORT_UART, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_UART);
	gpio_mode_setup(LED_PORT_ERROR, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_ERROR);

	platform_timing_init();
	usbuart_init();
	cdcacm_init();
}

void platform_nrst_set_val(bool assert)
{
	if (assert)
		gpio_clear(NRST_PORT, NRST_PIN);
	else
		gpio_set(NRST_PORT, NRST_PIN);
}

bool platform_nrst_get_val(void)
{
	return gpio_get(NRST_PORT, NRST_PIN);
}

const char *platform_target_voltage(void)
{
	return "ABSENT!";
}

void platform_request_boot(void)
{
	/* Disconnect USB cable */
	usbd_disconnect(usbdev, 1);
	nvic_disable_irq(USB_IRQ);

	/* Jump to the built in bootloader by mapping System flash */
	rcc_periph_clock_enable(RCC_SYSCFG);
	SYSCFG_MEMRM &= ~3;
	SYSCFG_MEMRM |=  1;
}
