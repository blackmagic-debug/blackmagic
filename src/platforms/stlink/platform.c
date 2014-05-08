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

#include "platform.h"
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/adc.h>

#include "jtag_scan.h"
#include <usbuart.h>

#include <ctype.h>

uint8_t running_status;
volatile uint32_t timeout_counter;

jmp_buf fatal_error_jmpbuf;

uint16_t led_idle_run;
/* Pins PC[14:13] are used to detect hardware revision. Read
 * 11 for STLink V1 e.g. on VL Discovery, tag as hwversion 0
 * 10 for STLink V2 e.g. on F4 Discovery, tag as hwversion 1
 */
int platform_hwversion(void)
{
	static int hwversion = -1;
        int i;
	if (hwversion == -1) {
		gpio_set_mode(GPIOC, GPIO_MODE_INPUT,
				GPIO_CNF_INPUT_PULL_UPDOWN,
				GPIO14 | GPIO13);
		gpio_set(GPIOC, GPIO14 | GPIO13);
                for (i = 0; i<10; i++)
                    hwversion = ~(gpio_get(GPIOC, GPIO14 | GPIO13) >> 13) & 3;
                switch (hwversion)
                {
                case 0:
                    led_idle_run = GPIO8;
                    break;
                default:
                    led_idle_run = GPIO9;
                }
	}
	return hwversion;
}

int platform_init(void)
{
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRC);

	/* On Rev 1 unconditionally activate MCO on PORTA8 with HSE
         * platform_hwversion() also needed to initialize led_idle_run!
         */
        if (platform_hwversion() == 1)
        {
            RCC_CFGR &= ~(                 0xf<< 24);
            RCC_CFGR |=  (RCC_CFGR_MCO_HSECLK << 24);
            gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
                          GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);
        }
	/* Setup GPIO ports */
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_50_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN);
	gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_50_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
	gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_50_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);
	uint16_t srst_pin = platform_hwversion() == 0 ?
		SRST_PIN_V1 : SRST_PIN_V2;
	gpio_set(SRST_PORT, srst_pin);
	gpio_set_mode(SRST_PORT, GPIO_MODE_OUTPUT_50_MHZ,
			GPIO_CNF_OUTPUT_OPENDRAIN, srst_pin);

	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, led_idle_run);

	/* Setup heartbeat timer */
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(900000);	/* Interrupt us at 10 Hz */
	SCB_SHPR(11) &= ~((15 << 4) & 0xff);
	SCB_SHPR(11) |= ((14 << 4) & 0xff);
	systick_interrupt_enable();
	systick_counter_enable();

	usbuart_init();

	SCB_VTOR = 0x2000;	// Relocate interrupt vector table here

	cdcacm_init();

	jtag_scan(NULL);

	return 0;
}

void platform_delay(uint32_t delay)
{
	timeout_counter = delay;
	while(timeout_counter);
}

void platform_srst_set_val(bool assert)
{
	uint16_t pin;
	pin = platform_hwversion() == 0 ? SRST_PIN_V1 : SRST_PIN_V2;
	if (assert)
		gpio_clear(SRST_PORT, pin);
	else
		gpio_set(SRST_PORT, pin);
}

void sys_tick_handler(void)
{
	if(running_status)
		gpio_toggle(LED_PORT, led_idle_run);

	if(timeout_counter)
		timeout_counter--;
}

const char *morse_msg;

void morse(const char *msg, char repeat)
{
	(void)repeat;
	morse_msg = msg;
}

const char *platform_target_voltage(void)
{
	return "unknown";
}

void disconnect_usb(void)
{
	/* Disconnect USB cable by resetting USB Device and pulling USB_DP low*/
	rcc_periph_reset_pulse(RST_USB);
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
}

void assert_boot_pin(void)
{
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
void setup_vbus_irq(void){};
