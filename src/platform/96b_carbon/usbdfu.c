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

#include "general.h"
#include "usbdfu.h"

#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/scb.h>

void dfu_detach(void)
{
	/* USB device must detach, we just reset... */
	scb_reset_system();
}

int main(void)
{
	/* Check the force bootloader pin*/
	rcc_periph_enable_clock(RCC_GPIOA);
	if (!gpio_get(GPIOA, GPIO0))
		dfu_jump_app_if_valid();

	dfu_protect_enable();

	/* Set up clock*/
	rcc_clock_setup_hse_3v3(&hse_8mhz_3v3[CLOCK_3V3_168MHZ]);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(2100000);

	systick_interrupt_enable();
	systick_counter_enable();

	/* Handle LEDs */
	rcc_periph_enable_clock(RCC_GPIOD);
	gpio_clear(GPIOD, GPIO12 | GPIO13 | GPIO14 | GPIO15);
	gpio_mode_setup(GPIOD, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO12 | GPIO13 | GPIO14 | GPIO15);

	/* Set up USB*/
	rcc_periph_enable_clock(RCC_OTGFS);

	/* Set up USB Pins and alternate function*/
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO10 | GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO10 | GPIO11 | GPIO12);
	dfu_init(&stm32f107_usb_driver);

	dfu_main();
}

void sys_tick_handler(void)
{
	gpio_toggle(GPIOD, GPIO12); /* Green LED on/off */
}
