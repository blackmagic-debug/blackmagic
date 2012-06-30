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

#include <libopencm3/stm32/f1/rcc.h>
#include <libopencm3/stm32/systick.h>
#include <libopencm3/stm32/f1/scb.h>
#include <libopencm3/stm32/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/f1/adc.h>

#include "platform.h"
#include "jtag_scan.h"

#include <ctype.h>

uint8_t running_status;
volatile uint32_t timeout_counter;

jmp_buf fatal_error_jmpbuf;

int platform_init(void)
{
	rcc_clock_setup_in_hse_8mhz_out_72mhz();

	/* Enable peripherals */
	rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_USBEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPAEN);
	rcc_peripheral_enable_clock(&RCC_APB2ENR, RCC_APB2ENR_IOPBEN);

	/* Setup GPIO ports */
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_50_MHZ, 
			GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN);
	gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_50_MHZ, 
			GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
	gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_50_MHZ, 
			GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);
	
	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ, 
			GPIO_CNF_OUTPUT_PUSHPULL, LED_IDLE_RUN);

	/* Setup heartbeat timer */
	systick_set_clocksource(STK_CTRL_CLKSOURCE_AHB_DIV8); 
	systick_set_reload(900000);	/* Interrupt us at 10 Hz */
	systick_interrupt_enable();
	systick_counter_enable();

	cdcacm_init();

	jtag_scan(NULL);
	
	return 0;
}

void sys_tick_handler(void)
{
	if(running_status) 
		gpio_toggle(LED_PORT, LED_IDLE_RUN);

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
