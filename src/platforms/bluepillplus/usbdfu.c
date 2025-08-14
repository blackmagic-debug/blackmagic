/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
 * Written by ALTracer <11005378+ALTracer@users.noreply.github.com>
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

#include "platform.h"
#include "usbdfu.h"
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>

uintptr_t app_address = 0x08002000U;
volatile uint32_t magic[2] __attribute__((section(".noinit")));
static int dfu_activity_counter;

static void sys_tick_init(void);
static void platform_detach_usb(void);

void dfu_detach(void)
{
	platform_detach_usb();
	scb_reset_system();
}

int main(void)
{
	/* BluePill-Plus board has an active-high button on PA0. Pull it down */
	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_set_mode(USER_BUTTON_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, USER_BUTTON_PIN);
	gpio_clear(USER_BUTTON_PORT, USER_BUTTON_PIN);
	/* Force detach USB device, this also provides the delay to charge button through pullup */
	platform_detach_usb();

	bool force_bootloader = false;
	/* Reason 1: sticky flag variable matches magic */
	if (magic[0] == BOOTMAGIC0 && magic[1] == BOOTMAGIC1)
		force_bootloader = true;
	/* Reason 2: button is activated by user */
	if (gpio_get(USER_BUTTON_PORT, USER_BUTTON_PIN))
		force_bootloader = true;

	if (force_bootloader) {
		magic[0] = 0;
		magic[1] = 0;
	} else {
		dfu_jump_app_if_valid();
	}

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_USB);
	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, LED_IDLE_RUN);
	rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);

	/* Run heartbeat on blue LED */
	sys_tick_init();

	dfu_protect(false);
	dfu_init(&USB_DRIVER);
	dfu_main();
}

void platform_detach_usb(void)
{
	/*
	 * Disconnect USB after reset:
	 * Pull USB_DP low. Device will reconnect automatically
	 * when USB is set up later, as Pull-Up is hard wired
	 */
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_reset_pulse(RST_USB);

	rcc_periph_clock_enable(RCC_GPIOA);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, GPIO12);
	gpio_clear(GPIOA, GPIO12);
	for (volatile uint32_t counter = 10000; counter > 0; --counter)
		continue;
}

void dfu_event(void)
{
	static bool idle_state = false;
	/* Ask systick to pause blinking for 1 second */
	dfu_activity_counter = 10;
	/* Blink it ourselves */
	SET_IDLE_STATE(idle_state);
	idle_state = !idle_state;
}

static void sys_tick_init(void)
{
	/* Use SysTick at 10Hz to blink the blue LED */
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(rcc_ahb_frequency / 8U / 10U);
	/* SYSTICK_IRQ with low priority */
	nvic_set_priority(NVIC_SYSTICK_IRQ, 14U << 4U);
	systick_interrupt_enable();
	/* Start the heartbeat timer */
	systick_counter_enable();
}

void sys_tick_handler(void)
{
	static int count = 0;
	if (dfu_activity_counter > 0) {
		dfu_activity_counter--;
		return;
	}

	switch (count) {
	case 0:
		/* Reload downcounter */
		count = 10;
		SET_IDLE_STATE(false);
		break;
	case 1:
		count--;
		/* Blink like a very slow PWM */
		SET_IDLE_STATE(true);
		break;
	default:
		count--;
		break;
	}
}
