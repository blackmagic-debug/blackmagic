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

uint32_t app_address = 0x08002000;
int dfu_activity_counter = 0;

void dfu_detach(void)
{
	/* USB device must detach, we just reset... */
	scb_reset_system();
}

int main(void)
{
	/* Check the force bootloader pin*/
	rcc_periph_clock_enable(RCC_GPIOB);
	if(gpio_get(GPIOB, GPIO12))
		dfu_jump_app_if_valid();

	dfu_protect(DFU_MODE);

	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(900000);

	/* Configure USB related clocks and pins. */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_USB);
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, 0, GPIO8);

	systick_interrupt_enable();
	systick_counter_enable();

	/* Configure the LED pins. */
	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, LED_0 | LED_1 | LED_2);

	dfu_init(&stm32f103_usb_driver, DFU_MODE);

	/* Configure the USB pull up pin. */
	gpio_set(GPIOA, GPIO8);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO8);

	dfu_main();
}

void dfu_event(void)
{
	/* If the counter was at 0 before we should reset LED status. */
	if (dfu_activity_counter == 0) {
		gpio_clear(LED_PORT, LED_0 | LED_1 | LED_2);
	}

	/* Prevent the sys_tick_handler from blinking leds for a bit. */
	dfu_activity_counter = 10;

	/* Toggle the DFU activity LED. */
	gpio_toggle(LED_PORT, LED_1);
}

void sys_tick_handler(void)
{
	static int count = 0;
	static bool reset = true;

	/* Run the LED show only if there is no DFU activity. */
	if (dfu_activity_counter != 0) {
		dfu_activity_counter--;
		reset = true;
	} else {
		if (reset) {
			gpio_clear(LED_PORT, LED_0 | LED_1 | LED_2);
			count = 0;
			reset = false;
		}

		switch (count) {
		case 0:
			gpio_toggle(LED_PORT, LED_2); /* LED2 on/off */
			count++;
			break;
		case 1:
			gpio_toggle(LED_PORT, LED_1); /* LED1 on/off */
			count++;
			break;
		case 2:
			gpio_toggle(LED_PORT, LED_0); /* LED0 on/off */
			count=0;
			break;
		}
	}
}

