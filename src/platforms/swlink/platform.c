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

#include <libopencm3/stm32/f1/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/f1/adc.h>

void platform_init(void)
{
	uint32_t data;
	SCS_DEMCR |= SCS_DEMCR_VC_MON_EN;
#ifdef ENABLE_DEBUG
	void initialise_monitor_handles(void);
	initialise_monitor_handles();
#endif
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRC);

	/* Unmap JTAG Pins so we can reuse as GPIO */
	data = AFIO_MAPR;
	data &= ~AFIO_MAPR_SWJ_MASK;
	data |= AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_OFF;
	AFIO_MAPR = data;
	/* Setup JTAG GPIO ports */
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_10_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN);
	gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_10_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
	gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_10_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);

	gpio_set_mode(TDO_PORT, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_FLOAT, TDO_PIN);

	gpio_set(NRST_PORT,NRST_PIN);
	gpio_set_mode(NRST_PORT, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_PULL_UPDOWN, NRST_PIN);

	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, LED_IDLE_RUN);

	/* Remap TIM2 TIM2_REMAP[1]
	 * TIM2_CH1_ETR -> PA15 (TDI, set as output above)
	 * TIM2_CH2     -> PB3  (TDO)
	 */
	data = AFIO_MAPR;
	data &= ~AFIO_MAPR_TIM2_REMAP_FULL_REMAP;
	data |=  AFIO_MAPR_TIM2_REMAP_PARTIAL_REMAP1;
	AFIO_MAPR = data;

	/* Relocate interrupt vector table here */
	extern int vector_table;
	SCB_VTOR = (uint32_t)&vector_table;

	platform_timing_init();
	cdcacm_init();
	/* Don't enable UART if we're being debugged. */
	if (!(SCS_DEMCR & SCS_DEMCR_TRCENA))
		usbuart_init();
	usbuart_init();
}

void platform_srst_set_val(bool assert) { (void)assert; }
bool platform_srst_get_val(void) { return false; }

const char *platform_target_voltage(void)
{
	return "unknown";
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

