/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2013 Gareth McMullin <gareth@blacksphere.co.nz>
 * Portions (C) 2020-2021 Stoyan Shopov <stoyan.shopov@gmail.com>
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
#include <libopencm3/cm3/scs.h>

#include "usbdfu.h"
#include "general.h"
#include "platform.h"

uintptr_t app_address = APP_START;

void dfu_detach(void)
{
	scb_reset_system();
}

int main(void)
{
	/* Use Top of ITCM Flash as magic marker */
	volatile uint32_t *magic = (volatile uint32_t *)0x3ff8;
	rcc_periph_clock_enable(RCC_GPIOA);
	/* On the Mini, NRST is on the footprint for the 1.27 mm Jumper
	 * to the side of the USB connector.
	 * With debugger connected, ignore reset. Use debugger to enter!
	 */
	bool force_bootloader;
	force_bootloader = (!(SCS_DHCSR & SCS_DHCSR_C_DEBUGEN) && ((RCC_CSR & RCC_CSR_RESET_FLAGS) == RCC_CSR_PINRSTF));
	RCC_CSR |= RCC_CSR_RMVF;
	RCC_CSR &= ~RCC_CSR_RMVF;
	if (force_bootloader || (magic[0] == BOOTMAGIC0 && magic[1] == BOOTMAGIC1)) {
		magic[0] = 0;
		magic[1] = 0;
	} else {
		dfu_jump_app_if_valid();
	}
	rcc_periph_clock_enable(RCC_APB2ENR_SYSCFGEN);
	rcc_clock_setup_hse(rcc_3v3 + RCC_CLOCK_3V3_216MHZ, 25);

	/* Keep the target powered and supplied with clock when in bootloader */
	rcc_periph_clock_enable(RCC_GPIOB);
	gpio_mode_setup(PWR_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PWR_EN_PIN);
	gpio_set_output_options(PWR_EN_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, PWR_EN_PIN);
	gpio_set(PWR_EN_PORT, PWR_EN_PIN);

	/* Set up MCO at 8 MHz on PA8 */
	gpio_set_af(MCO1_PORT, MCO1_AF, MCO1_PIN);
	gpio_mode_setup(MCO1_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, MCO1_PIN);
	gpio_set_output_options(MCO1_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, MCO1_PIN);
	RCC_CR |= RCC_CR_HSION;
	RCC_CFGR &= ~(0x3 << 21); /* HSI */
	RCC_CFGR &= ~(0x7 << 24); /* no division */

	/* Set up green/red led to blink green to indicate bootloader active*/
	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_PIN);
	gpio_set_output_options(LED_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, LED_PIN);
	gpio_clear(LED_PORT, LED_PIN);

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(216 * 1000 * 1000 / (8 * 10));
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
	gpio_toggle(LED_PORT, LED_PIN);
}
