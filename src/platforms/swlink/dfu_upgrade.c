/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2018 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(900000);

	dfu_protect(UPD_MODE);

	systick_interrupt_enable();
	systick_counter_enable();

	dfu_init(&st_usbfs_v1_usb_driver, UPD_MODE);

	dfu_main();
}

void dfu_event(void)
{
}

void sys_tick_handler(void)
{
	if (rev == 0) {
		gpio_toggle(GPIOA, GPIO8);
	} else {
		gpio_toggle(GPIOC, GPIO13);
	}
}
