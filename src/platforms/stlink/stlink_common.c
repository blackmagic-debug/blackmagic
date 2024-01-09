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

#include <stdint.h>
#include <stddef.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>

#include "platform.h"

/* If we force CLONE or V2_ISOL variant, we don't need to read GPIO levels */
#if !(defined(STLINK_FORCE_CLONE) || defined(STLINK_V2_ISOL))
static bool stlink_stable_read(const uint32_t gpio_port, const uint16_t gpio)
{
	bool result = false;
	for (volatile size_t i = 0; i < 100U; ++i)
		result = gpio_get(gpio_port, gpio);
	return result;
}
#endif

/* Return 0 for ST-Link v1, 1 for ST-Link v2 and 2 for ST-Link v2.1 */
uint32_t detect_rev(void)
{
	while (RCC_CFGR & 0xfU) /* Switch back to HSI. */
		RCC_CFGR &= ~3U;
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_reset_pulse(RST_USB);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRC);

#if defined(STLINK_FORCE_CLONE)
	/* PA12 is USB D+ pin */
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
	/* Override detection to use clone pinmap (i.e. PB6 as nRST). */
	return 0x101;
#elif defined(STLINK_V2_ISOL)
	/* Override detection to stlink v2 isol*/
	return 0x103;
#else
	/*
	 * First, get the board revision by pulling PC13/14 up then reading them.
	 * This gives us the following table of values for these pins:
	 *  11 for ST-Link v1, e.g. on VL Discovery, tag as rev 0
	 *  11 for "Baite" clones, PB11 pulled high, tag as rev 257 (0x101)
	 *  00 for ST-Link v2, e.g. on F4 Discovery, tag as rev 1
	 *  01 for ST-Link v2, else,                 tag as rev 1
	 */
	gpio_set_mode(GPIOC, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, GPIO14 | GPIO13);
	gpio_set(GPIOC, GPIO14 | GPIO13);
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, GPIO11);
	gpio_clear(GPIOB, GPIO11);

	uint32_t revision = 0;
	if (stlink_stable_read(GPIOC, GPIO13))
		revision = gpio_get(GPIOB, GPIO11) ? 0x101 : 0;
	else {
		/*
		 * Check for ST-Link v2.1 boards.
		 * PA15/TDI is USE_RENUM, pulled high with 10k to U5V on v2.1 boards.
		 * The pin is otherwise unconnected on other ST-Links.
		 * Enable internal pull-down on PA15 - if still high it is V2.1.
		 */
		rcc_periph_clock_enable(RCC_AFIO);
		AFIO_MAPR |= 0x02000000U; /* Release from TDI. */
		gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, GPIO15);
		gpio_clear(GPIOA, GPIO15);
		if (stlink_stable_read(GPIOA, GPIO15)) {
			revision = 2;
			/* Pull PWR_ENn low. */
			gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, GPIO15);
			gpio_clear(GPIOB, GPIO15);
			/* Pull USB_RENUM low. */
			gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, GPIO15);
			gpio_clear(GPIOA, GPIO15);
		} else
			/* Catch F4 Discovery board with both resistors fitted. */
			revision = 1;
		/* On boards identifying as anything other than ST-Link v1 unconditionally activate MCO on PA8 with HSE. */
		RCC_CFGR &= ~(0xfU << 24U);
		RCC_CFGR |= (RCC_CFGR_MCO_HSE << 24U);
		gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);
	}

	/* Clean up after ourself on boards that aren't identified as ST-Link v2.1's */
	if ((revision & 0xff) < 2U) {
		gpio_clear(GPIOA, GPIO12);
		gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
	}
	return revision;
#endif
}

void platform_request_boot(void)
{
#if defined(ST_BOOTLOADER)
	/* Disconnect USB cable by resetting USB Device */
	rcc_periph_reset_pulse(RST_USB);
	scb_reset_system();
#else
	/*
	 * Assert bootloader marker - enable internal pull-up/down on PA1.
	 * We don't rely on the external pin really being pulled, but rather
	 * on if the value of the CNF register is different from the reset value.
	 */
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, GPIO1);
	SCB_VTOR = 0;
#endif
}
