/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2017 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

/* return 0 for stlink V1, 1 for stlink V2 and 2 for stlink V2.1 */
uint32_t detect_rev(void)
{
	uint32_t rev;
	int res;

	while (RCC_CFGR & 0xf) /* Switch back to HSI. */
		RCC_CFGR &= ~3;
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_reset_pulse(RST_USB);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRC);
	/* First, get Board revision by pulling PC13/14 up. Read
	 *  11 for ST-Link V1, e.g. on VL Discovery, tag as rev 0
	 *  00 for ST-Link V2, e.g. on F4 Discovery, tag as rev 1
	 *  01 for ST-Link V2, else,                 tag as rev 1
	 */
	gpio_set_mode(GPIOC, GPIO_MODE_INPUT,
				  GPIO_CNF_INPUT_PULL_UPDOWN, GPIO14 | GPIO13);
	gpio_set(GPIOC, GPIO14 | GPIO13);
	for (int i = 0; i < 100; i ++)
		res = gpio_get(GPIOC, GPIO13);
	if (res)
		rev = 0;
	else {
		/* Check for V2.1 boards.
		 * PA15/TDI is USE_RENUM, pulled with 10 k to U5V on V2.1,
		 * Otherwise unconnected. Enable pull low. If still high.
		 * it is V2.1.*/
		rcc_periph_clock_enable(RCC_AFIO);
		AFIO_MAPR |= 0x02000000; /* Release from TDI.*/
		gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
                                 GPIO_CNF_INPUT_PULL_UPDOWN, GPIO15);
		gpio_clear(GPIOA, GPIO15);
		for (int i = 0; i < 100; i++)
			res =  gpio_get(GPIOA, GPIO15);
		if (res) {
			rev = 2;
			/* Pull PWR_ENn low.*/
			gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
						  GPIO_CNF_OUTPUT_OPENDRAIN, GPIO15);
			gpio_clear(GPIOB, GPIO15);
			/* Pull USB_RENUM low!*/
			gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
						  GPIO_CNF_OUTPUT_OPENDRAIN, GPIO15);
			gpio_clear(GPIOA, GPIO15);
		} else
			/* Catch F4 Disco board with both resistors fitted.*/
			rev = 1;
		/* On Rev > 0 unconditionally activate MCO on PORTA8 with HSE! */
		RCC_CFGR &= ~(0xf << 24);
		RCC_CFGR |= (RCC_CFGR_MCO_HSECLK << 24);
		gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
		GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);
	}
	if (rev < 2) {
		gpio_clear(GPIOA, GPIO12);
		gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
					  GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
	}
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
