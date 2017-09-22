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

uint32_t app_address = 0x08000000;
static uint16_t led_upgrade;
static uint32_t led2_state = 0;
extern uint32_t _stack;
static uint32_t rev;

void dfu_detach(void)
{
	platform_request_boot();
	scb_reset_core();
}

int main(void)
{
	rev = detect_rev();
	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	if (rev == 0)
		led_upgrade = GPIO8;
	else
		led_upgrade = GPIO9;

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(900000);

	dfu_protect(UPD_MODE);

	systick_interrupt_enable();
	systick_counter_enable();

	if (rev > 1) /* Reconnect USB */
		gpio_set(GPIOA, GPIO15);
	dfu_init(&stm32f103_usb_driver, UPD_MODE);

	dfu_main();
}

void dfu_event(void)
{
}

void sys_tick_handler(void)
{
	if (rev == 0) {
		gpio_toggle(GPIOA, led_upgrade);
	} else {
		if (led2_state & 1) {
			gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
				GPIO_CNF_OUTPUT_PUSHPULL, led_upgrade);
			gpio_set(GPIOA, led_upgrade);
		} else {
			gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
				GPIO_CNF_INPUT_ANALOG, led_upgrade);
		}
		led2_state++;
	}
}
