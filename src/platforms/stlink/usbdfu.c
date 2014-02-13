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

static uint8_t rev;
static uint16_t led_idle_run;
static uint32_t led2_state = 0;

uint32_t app_address = 0x08002000;

static int stlink_test_nrst(void)
{
	/* Test if JRST/NRST is pulled down*/
	uint16_t nrst;
	uint16_t pin;
	uint32_t systick_value;

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(0xffffff); /* no underflow for about 16.7 seconds*/
	systick_counter_enable();
	/* systick ist now running with 1 MHz, systick counts down */

	/* First, get Board revision by pulling PC13/14 up. Read
	 *  11 for ST-Link V1, e.g. on VL Discovery, tag as rev 0
	 *  10 for ST-Link V2, e.g. on F4 Discovery, tag as rev 1
	 */
	rcc_periph_clock_enable(RCC_GPIOC);
	gpio_set_mode(GPIOC, GPIO_MODE_INPUT,
				  GPIO_CNF_INPUT_PULL_UPDOWN, GPIO14 | GPIO13);
	gpio_set(GPIOC, GPIO14 | GPIO13);
	systick_value = systick_get_value();
	while (systick_get_value() > (systick_value - 1000)); /* Wait 1 msec*/
	rev = (~(gpio_get(GPIOC, GPIO14 | GPIO13)) >> 13) & 3;
	switch (rev) {
	case 0:
		pin = GPIO1;
		led_idle_run = GPIO8;
		break;
	default:
		pin = GPIO0;
		led_idle_run = GPIO9;
	}
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, led_idle_run);
	rcc_periph_clock_enable(RCC_GPIOB);
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_PULL_UPDOWN, pin);
	gpio_set(GPIOB, pin);
	systick_value = systick_get_value();
	while (systick_get_value() > (systick_value - 20000)); /* Wait 20 msec*/
	nrst = gpio_get(GPIOB, pin);
	systick_counter_disable();
	return (nrst) ? 1 : 0;
}

void dfu_detach(void)
{
	/* Disconnect USB cable by resetting USB Device
	   and pulling USB_DP low*/
	rcc_periph_reset_pulse(RST_USB);
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
	scb_reset_system();
}

int main(void)
{
	/* Check the force bootloader pin*/
	rcc_periph_clock_enable(RCC_GPIOA);
	/* Check value of GPIOA1 configuration. This pin is unconnected on
	 * STLink V1 and V2. If we have a value other than the reset value (0x4),
	 * we have a warm start and request Bootloader entry
	 */
	if(((GPIOA_CRL & 0x40) == 0x40) && stlink_test_nrst())
		dfu_jump_app_if_valid();

	dfu_protect(DFU_MODE);

	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(900000);

        /* Handle USB disconnect/connect */
	/* Just in case: Disconnect USB cable by resetting USB Device
         * and pulling USB_DP low
         * Device will reconnect automatically as Pull-Up is hard wired*/
	rcc_periph_reset_pulse(RST_USB);
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);

	systick_interrupt_enable();
	systick_counter_enable();

	dfu_init(&stm32f103_usb_driver, DFU_MODE);

	dfu_main();
}

void sys_tick_handler(void)
{
	if (rev == 0) {
		gpio_toggle(GPIOA, led_idle_run);
	} else {
		if (led2_state & 1) {
			gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
				GPIO_CNF_OUTPUT_PUSHPULL, led_idle_run);
		} else {
			gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
				GPIO_CNF_INPUT_ANALOG, led_idle_run);
		}
		led2_state++;
	}
}

