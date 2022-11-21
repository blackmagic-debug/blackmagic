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
#include "general.h"
#include "platform.h"

static uint32_t rev;
static uint16_t led_bootloader;
static uint16_t pin_nrst;
static uint32_t led2_state = 0;

uintptr_t app_address = 0x08002000U;

static bool stlink_test_nrst(void)
{
	uint16_t nrst;
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, pin_nrst);
	gpio_set(GPIOB, pin_nrst);
	for (size_t i = 0; i < 10000U; ++i)
		nrst = gpio_get(GPIOB, pin_nrst);
	return nrst;
}

void dfu_detach(void)
{
	scb_reset_system();
}

int main(void)
{
	rev = detect_rev();
	if (rev == 0) {
		led_bootloader = GPIO8;
		pin_nrst = GPIO1;
	} else {
		led_bootloader = GPIO9;
		pin_nrst = GPIO0;
	}

	if ((GPIOA_CRL & 0x40U) == 0x40U && stlink_test_nrst())
		dfu_jump_app_if_valid();
	dfu_protect(false);

	rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(900000);

	systick_interrupt_enable();
	systick_counter_enable();

	if (rev > 1U)
		gpio_set(GPIOA, GPIO15);
	dfu_init(&st_usbfs_v1_usb_driver);

	dfu_main();
}

void dfu_event(void)
{
}

void sys_tick_handler(void)
{
	if (rev == 0) {
		gpio_toggle(GPIOA, led_bootloader);
	} else {
		if (led2_state & 1U) {
			gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, led_bootloader);
			gpio_clear(GPIOA, led_bootloader);
		} else
			gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, led_bootloader);

		++led2_state;
	}
}
