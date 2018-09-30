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

#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>

uint8_t detect_rev()
{
	/* Test connectivity between PB9 and PB10 needed for
	 * original ST software to implement SWIM.
	 *
	 * Return 0 for Stlink on STM8S Disco and 1 for Bluepill
	 */
	uint8_t rev = 0;
	/* Enable Peripherals used by both debugger and DFU.*/
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_USB);
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_PULL_UPDOWN, GPIO9);
	gpio_set(GPIOB, GPIO9);
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO10);
	while (!gpio_get(GPIOB, GPIO10))
		gpio_set(GPIOB, GPIO10);
	while (gpio_get(GPIOB, GPIO10))
		gpio_clear(GPIOB, GPIO10);
	/* Read PB9 as soon as we read PB10 low.*/
	if (gpio_get(GPIOB, GPIO9))
		rev = 1;
	/* Release PB9/10 */
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_FLOAT, GPIO9 | GPIO10);
	gpio_set(GPIOB, GPIO9);
	switch (rev) {
	case 0:
		gpio_clear(GPIOA, GPIO8);
		gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
					  GPIO_CNF_OUTPUT_PUSHPULL, GPIO8);
		break;
	case 1:
		rcc_periph_clock_enable(RCC_GPIOC);
		gpio_set(GPIOC, GPIO13); /* LED on Blupill is active low!*/
		gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ,
					  GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);
		break;
	}
	/* Disconnect USB after reset:
	 * Pull USB_DP low. Device will reconnect automatically
	 * when USB is set up later, as Pull-Up is hard wired*/
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
				  GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
	gpio_clear(GPIOA, GPIO12);
	rcc_periph_reset_pulse(RST_USB);
	rcc_periph_clock_enable(RCC_USB);

	return rev;
}

void platform_request_boot(void)
{
	uint32_t crl = GPIOA_CRL;
	/* Assert bootloader marker.
	 * Enable Pull on GPIOA1. We don't rely on the external pin
	 * really pulled, but only on the value of the CNF register
	 * changed from the reset value
	 */
	crl &= 0xffffff0f;
	crl |= 0x80;
	GPIOA_CRL = crl;
	SCB_VTOR = 0;
}
