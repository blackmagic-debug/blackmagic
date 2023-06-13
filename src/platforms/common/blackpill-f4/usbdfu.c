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
#include <libopencm3/usb/dwc/otg_fs.h>

#include "usbdfu.h"
#include "general.h"
#include "platform.h"

uintptr_t app_address = 0x08004000U;
volatile uint32_t magic[2] __attribute__((section(".noinit")));

void dfu_detach(void)
{
	scb_reset_system();
}

int main(void)
{
	rcc_periph_clock_enable(RCC_GPIOA);

	/* Blackpill board has a floating button on PA0. Pull it up and use as active-low. */
	gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO0);

	if (!gpio_get(GPIOA, GPIO0) || (magic[0] == BOOTMAGIC0 && magic[1] == BOOTMAGIC1)) {
		magic[0] = 0;
		magic[1] = 0;
	} else
		dfu_jump_app_if_valid();

	rcc_clock_setup_pll(&rcc_hse_25mhz_3v3[PLATFORM_CLOCK_FREQ]);

	/* Assert blue LED as indicator we are in the bootloader */
	rcc_periph_clock_enable(RCC_GPIOC);
	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_BOOTLOADER);
	gpio_set(LED_PORT, LED_BOOTLOADER);

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_OTGFS);

	/* Set up USB Pins and alternate function*/
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO11 | GPIO12);

	dfu_protect(false);
	dfu_init(&USB_DRIVER);

	/* https://github.com/libopencm3/libopencm3/pull/1256#issuecomment-779424001 */
	OTG_FS_GCCFG |= OTG_GCCFG_NOVBUSSENS | OTG_GCCFG_PWRDWN;
	OTG_FS_GCCFG &= ~(OTG_GCCFG_VBUSBSEN | OTG_GCCFG_VBUSASEN);

	dfu_main();
}

void dfu_event(void)
{
}
