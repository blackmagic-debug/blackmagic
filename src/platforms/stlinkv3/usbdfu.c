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

uint32_t app_address = APP_START;

void dfu_detach(void)
{
	scb_reset_system();
}

int main(void)
{
	/* Use Top of ITCM Flash as magic marker */
	volatile uint32_t *magic = (volatile uint32_t *) 0x3ff8;
	rcc_periph_clock_enable(RCC_GPIOA);
	/* On the Mini, NRST is on the footprint for the 1.27 mm Jumper
	 * to the side of th USB connector */
	bool force_bootloader;
	force_bootloader = ((RCC_CSR &  RCC_CSR_RESET_FLAGS) == RCC_CSR_PINRSTF);
	RCC_CSR |= RCC_CSR_RMVF;
	RCC_CSR &= ~RCC_CSR_RMVF;
	if (force_bootloader ||
	   ((magic[0] == BOOTMAGIC0) && (magic[1] == BOOTMAGIC1))) {
		magic[0] = 0;
		magic[1] = 0;
	} else {
		dfu_jump_app_if_valid();
	}
	rcc_periph_clock_enable(RCC_APB2ENR_SYSCFGEN);
	rcc_clock_setup_hse(rcc_3v3 + RCC_CLOCK_3V3_216MHZ, 25);

	/* Set up green/red led to blink green to indicate bootloader active*/
	gpio_mode_setup(LED_RG_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_RG_PIN);
	gpio_set_output_options(LED_RG_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, LED_RG_PIN);

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(216*1000*1000/(8 * 10));
	systick_interrupt_enable();
	systick_counter_enable();

	dfu_protect(false);
	dfu_init(&USB_DRIVER);
	dfu_main();

}


void dfu_event(void)
{
}

void sys_tick_handler(void)
{
	gpio_toggle(LED_RG_PORT, LED_RG_PIN);
}
