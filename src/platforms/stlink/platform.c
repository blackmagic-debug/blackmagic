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
#include <libopencm3/cm3/scs.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/adc.h>

uint8_t running_status;

uint16_t led_idle_run;
uint16_t srst_pin;
static uint32_t rev;

uint32_t USBUSART;
uint32_t USBUSART_IRQ;
uint32_t USBUSART_CLK;
uint32_t USBUSART_PORT;
uint32_t USBUSART_TX_PIN;

int platform_hwversion(void)
{
	return rev;
}

void platform_init(void)
{
	rev = detect_rev();
	SCS_DEMCR |= SCS_DEMCR_VC_MON_EN;
#ifdef ENABLE_DEBUG
	void initialise_monitor_handles(void);
	initialise_monitor_handles();
#endif
	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	if (rev == 0) {
		led_idle_run = GPIO8;
		srst_pin = SRST_PIN_V1;
	} else {
		led_idle_run = GPIO9;
		srst_pin = SRST_PIN_V2;
	}
	/* Setup GPIO ports */
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_50_MHZ,
	              GPIO_CNF_INPUT_FLOAT, TMS_PIN);
	gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_50_MHZ,
	              GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
	gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_50_MHZ,
	              GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);

	platform_srst_set_val(false);

	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ,
	              GPIO_CNF_OUTPUT_PUSHPULL, led_idle_run);

	/* Relocate interrupt vector table here */
	extern int vector_table;
	SCB_VTOR = (uint32_t)&vector_table;

	platform_timing_init();
	if (rev > 1) /* Reconnect USB */
		gpio_set(GPIOA, GPIO15);
	cdcacm_init();
	/* Check for SWIM pins and use then as UART if present
	 * SWIM is connected as follows (from STM8S-DISCOVERY ST-LINK):
	 * - PB5: SWIM_RST_IN
	 * - PB6: SWIM_RST
	 * - PB7: SWIM_IN
	 * - PB8: SWIM
	 * - PB9: SWIM_IN
	 * - PB10: SWIM_IN
	 * - PB11: SWIM
	 *
	 * SWIM_RST_IN is connected through 220R to SWIM_RST
	 * SWIM_IN is connected through 220R to SWIM
	 * SWIM is pulled up by 680R
	 */
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
	              GPIO_CNF_INPUT_PULL_UPDOWN, GPIO11);
	gpio_clear(GPIOB, GPIO11);
	if (!gpio_get(GPIOB, GPIO11)) { /* SWIM pins are absent */
		/* Use USART2 as UART, as used on some embedded ST-LINK/V2-1,
		such as the Nucleo 64 development board */
		USBUSART = USART2;
		USBUSART_IRQ = NVIC_USART2_IRQ;
		USBUSART_CLK = RCC_USART2;
		USBUSART_PORT = GPIOA;
		USBUSART_TX_PIN = GPIO2;
	} else { /* SWIM pins are present */
		/* Use USART1 REMAP as UART */
		USBUSART = USART1;
		USBUSART_IRQ = NVIC_USART1_IRQ;
		USBUSART_CLK = RCC_USART1;
		USBUSART_PORT = GPIOB;
		USBUSART_TX_PIN = GPIO6; /* SWIM_RST */
		/* SWIM/PB7 is already an input */
		AFIO_MAPR |= AFIO_MAPR_USART1_REMAP; /* use USART1 REMAP */
	}
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
	              GPIO_CNF_INPUT_FLOAT, GPIO11);
	/* Don't enable UART if we're being debugged. */
	if (!(SCS_DEMCR & SCS_DEMCR_TRCENA))
		usbuart_init();
}

void platform_srst_set_val(bool assert)
{
	if (assert) {
		gpio_set_mode(SRST_PORT, GPIO_MODE_OUTPUT_50_MHZ,
		              GPIO_CNF_OUTPUT_OPENDRAIN, srst_pin);
		gpio_clear(SRST_PORT, srst_pin);
		while (gpio_get(SRST_PORT, srst_pin)) {};
	} else {
		gpio_set_mode(SRST_PORT, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_PULL_UPDOWN, srst_pin);
		gpio_set(SRST_PORT, srst_pin);
		while (!gpio_get(SRST_PORT, srst_pin)) {};
	}
}

bool platform_srst_get_val()
{
	return gpio_get(SRST_PORT, srst_pin) == 0;
}

const char *platform_target_voltage(void)
{
	return "unknown";
}

/* Be sure USART1 (from SWIM) ISR also calls the default USB UART ISR */
void usart1_isr(void)
{
	USBUSART_ISR();
}
