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

#include "usbdfu.h"
#include "platform.h"

uint32_t app_address = 0x08002000;

void dfu_detach(void)
{
	/* Disconnect USB cable by resetting USB Device
	   and pulling USB_DP low*/
	rcc_periph_reset_pulse(RST_USB);
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
	scb_reset_system();
}

int main(void)
{
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOA);

    gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ,
            GPIO_CNF_OUTPUT_PUSHPULL, LED_IDLE_RUN);
    gpio_clear(LED_PORT,LED_IDLE_RUN);

    /* Check DFUFORCE....it will be pulled one way or the other by the jumper */
    /* If the jumper isn't present then what happens here is indeterminate, but we can't */
    /* put a pull on here 'cos the pin is driven via 100K :-( */
    gpio_set_mode(DFUFORCE_PORT, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_FLOAT, DFUFORCE_PIN);
    gpio_clear(DFUFORCE_PORT,DFUFORCE_PIN);

    /* Jump to the app if it's valid, weren't told to stay in DFU and the jumper isn't set */
	if (((GPIOA_CRL & 0x40) == 0x40) && (!gpio_get(DFUFORCE_PORT,DFUFORCE_PIN)))
		dfu_jump_app_if_valid();

	dfu_protect(DFU_MODE);

	rcc_clock_setup_in_hse_8mhz_out_72mhz();
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(900000);

	/* Handle USB disconnect/connect */
	/* Just in case: Disconnect USB cable by resetting USB Device
	 * and pulling USB_DP low
	 * Device will reconnect automatically */
	rcc_periph_reset_pulse(RST_USB);
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);

	systick_interrupt_enable();
	systick_counter_enable();

	dfu_init(&stm32f103_usb_driver, DFU_MODE);

	dfu_main();
}

void dfu_event(void)
{
}

void sys_tick_handler(void)
{
    gpio_toggle(LED_PORT, LED_IDLE_RUN);
}

