/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/crs.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/scb.h>

#include "usbdfu.h"
#include "platform.h"
#include "rcc_clocking.h"

uintptr_t app_address = 0x08004000U;
uint8_t dfu_activity_counter = 0U;

void dfu_detach(void)
{
	/* USB device must detach, we just reset... */
	scb_reset_system();
}

int main(void)
{
	/* Check the force bootloader pin */
	rcc_periph_clock_enable(RCC_GPIOA);
	if (gpio_get(BNT_BOOT_REQ_PORT, BTN_BOOT_REQ_PIN))
		dfu_jump_app_if_valid();

	dfu_protect(false);

	/* Bring up the clocks for operation, setting SysTick to 160MHz / 8 (20MHz) */
	rcc_clock_setup_pll(&rcc_hsi_config);
	rcc_clock_setup_hsi48();
	crs_autotrim_usb_enable();
	rcc_set_iclk_clksel(RCC_CCIPR1_ICLKSEL_HSI48);
	rcc_set_peripheral_clk_sel(SYS_TICK_BASE, RCC_CCIPR1_SYSTICKSEL_HCLK_DIV8);
	systick_set_clocksource(STK_CSR_CLKSOURCE_EXT);
	/* Reload every 100ms */
	systick_set_reload(2000000U);
	/* Power up USB controller */
	pwr_enable_vddusb();

	/* Configure USB related clocks and pins. */
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_OTGFS);

	/* Finish setting up and enabling SysTick */
	systick_interrupt_enable();
	systick_counter_enable();

	/* Configure the LED pins. */
	gpio_set_output_options(LED0_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, LED0_PIN);
	gpio_mode_setup(LED0_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED0_PIN);
	gpio_set_output_options(LED1_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, LED1_PIN);
	gpio_mode_setup(LED1_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED1_PIN);
	gpio_set_output_options(LED2_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, LED2_PIN);
	gpio_mode_setup(LED2_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED2_PIN);
	gpio_set_output_options(LED3_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, LED3_PIN);
	gpio_mode_setup(LED3_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED3_PIN);

	dfu_init(&otgfs_usb_driver);

	dfu_main();
}

void dfu_event(void)
{
	/* If the counter was at 0 before we should reset LED status. */
	if (dfu_activity_counter == 0) {
		gpio_clear(LED0_PORT, LED0_PIN);
		gpio_clear(LED1_PORT, LED1_PIN);
		gpio_clear(LED2_PORT, LED2_PIN);
		gpio_clear(LED3_PORT, LED3_PIN);
	}

	/* Prevent the sys_tick_handler from blinking leds for a bit. */
	dfu_activity_counter = 10;

	/* Toggle the DFU activity LED. */
	gpio_toggle(LED1_PORT, LED1_PIN);
}

void sys_tick_handler(void)
{
	static int count = 0;
	static bool reset = true;

	/* Run the LED show only if there is no DFU activity. */
	if (dfu_activity_counter != 0) {
		--dfu_activity_counter;
		reset = true;
	} else {
		if (reset) {
			gpio_clear(LED0_PORT, LED0_PIN);
			gpio_clear(LED1_PORT, LED1_PIN);
			gpio_clear(LED2_PORT, LED2_PIN);
			gpio_clear(LED3_PORT, LED3_PIN);
			count = 0;
			reset = false;
		}

		switch (count) {
		case 0:
			gpio_toggle(LED3_PORT, LED3_PIN); /* LED3 on/off */
			break;
		case 1:
			gpio_toggle(LED2_PORT, LED2_PIN); /* LED2 on/off */
			break;
		case 2:
			gpio_toggle(LED1_PORT, LED1_PIN); /* LED1 on/off */
			break;
		case 3:
			gpio_toggle(LED0_PORT, LED0_PIN); /* LED0 on/off */
			break;
		default:
			break;
		}
		++count;
		count &= 3U;
	}
}
