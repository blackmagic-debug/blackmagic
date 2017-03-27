/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

/* This file implements the platform specific functions for the ST-Link
 * implementation.
 */

#include "general.h"
#include "cdcacm.h"
#include "usbuart.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/adc.h>

uint8_t running_status;
volatile uint32_t timeout_counter;

void platform_init(void)
{
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRC);

	// FIXME
    gpio_clear(GPIOA, GPIO5);
    gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, GPIO15);
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, GPIO5);
    // FIXME

	/* Setup GPIO ports */
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_50_MHZ,
	              GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN);
	gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_50_MHZ,
	              GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
	gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_50_MHZ,
	              GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);
	gpio_set(SRST_PORT, SRST_PIN);
	gpio_set_mode(SRST_PORT, GPIO_MODE_OUTPUT_50_MHZ,
	              GPIO_CNF_OUTPUT_OPENDRAIN, SRST_PIN);

	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ,
	              GPIO_CNF_OUTPUT_PUSHPULL, LED_IDLE_RUN);

	/* Deal with special case that PB4 needs remapping */
	if ((VSENSE_PORT==GPIOB) && (VSENSE_PIN==GPIO4))
	  {
	    AFIO_MAPR |= AFIO_MAPR_SWJ_CFG_FULL_SWJ_NO_JNTRST;
	  }

        gpio_set_mode(VSENSE_PORT, GPIO_MODE_INPUT,
		      GPIO_CNF_INPUT_PULL_UPDOWN, VSENSE_PIN);
	gpio_clear(VSENSE_PORT,VSENSE_PIN);

	SCB_VTOR = 0x2000; /* Relocate interrupt vector table here */

	platform_timing_init();
	cdcacm_init();
	usbuart_init();
}

int platform_hwversion(void)

{
  return 0;
}

void platform_srst_set_val(bool assert)
{
	if (assert)
		gpio_clear(SRST_PORT, SRST_PIN);
	else
		gpio_set(SRST_PORT, SRST_PIN);
}

bool platform_srst_get_val()
{
	return gpio_get(SRST_PORT, SRST_PIN) == 0;
}

const char *platform_target_voltage(void)
{
  return (gpio_get(VSENSE_PORT,VSENSE_PIN))?"Present":"Not Detected";
}

void platform_request_boot(void)
{
	/* Disconnect USB cable by resetting USB Device and pulling USB_DP low*/
	rcc_periph_reset_pulse(RST_USB);
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_clock_enable(RCC_GPIOA);
    gpio_clear(GPIOA, GPIO12);
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);

	/* Assert bootloader pin */
	uint32_t crl = GPIOA_CRL;
	rcc_periph_clock_enable(RCC_GPIOA);
	/* Enable Pull on GPIOA1. We don't rely on the external pin
	 * really pulled, but only on the value of the CNF register
	 * changed from the reset value
	 */
	crl &= 0xffffff0f;
	crl |= 0x80;
	GPIOA_CRL = crl;
}

