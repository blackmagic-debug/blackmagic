/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2013 Gareth McMullin <gareth@blacksphere.co.nz>
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

#include <string.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/scb.h>

#include "usbdfu.h"
#include "platform.h"

uintptr_t app_address = 0x08002000U;
uint32_t rev;

void dfu_detach(void)
{
	/* Disconnect USB cable by resetting USB Device
	   and pulling USB_DP low*/
	rcc_periph_reset_pulse(RST_USB);
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
	scb_reset_system();
}

int main(void)
{
	/* Check the force bootloader pin*/
	bool normal_boot = 0;
	rev = detect_rev();
	switch (rev) {
	case 0:
		/* For Stlink on  STM8S check that CN7 PIN 4 RESET# is
		 * forced to GND, Jumper CN7 PIN3/4 is plugged).
		 * Switch PB5 high. Read PB6 low means jumper plugged.
		 */
		gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, GPIO6);
		gpio_set(GPIOB, GPIO6);
		gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO5);
		while (gpio_get(GPIOB, GPIO5))
			gpio_clear(GPIOB, GPIO5);
		while (!gpio_get(GPIOB, GPIO5))
			gpio_set(GPIOB, GPIO5);
		normal_boot = (gpio_get(GPIOB, GPIO6));
		break;
	case 1:
		/* Boot0/1 pins have 100k between Jumper and MCU
		 * and are jumperd to low by default.
		 * If we read PB2 high, force bootloader entry.*/
		gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO2);
		normal_boot = !(gpio_get(GPIOB, GPIO2));
	}
	if ((GPIOA_CRL & 0x40U) == 0x40U && normal_boot)
		dfu_jump_app_if_valid();

	dfu_protect(false);

	rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(900000);

	systick_interrupt_enable();
	systick_counter_enable();

	dfu_init(&st_usbfs_v1_usb_driver);

	dfu_main();
}

void dfu_event(void)
{
}

void sys_tick_handler(void)
{
	switch (rev) {
	case 0:
		gpio_toggle(GPIOA, GPIO8);
		break;
	case 1:
		gpio_toggle(GPIOC, GPIO13);
		break;
	}
}
