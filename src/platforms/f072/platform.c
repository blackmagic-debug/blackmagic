/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2017 Uwe Bonnes bon@elektron,ikp,physik.tu-darmstadt.de
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

/* This file implements the platform specific functions for the STM32F072-IF
 * implementation.
 */

#include "general.h"
#include "cdcacm.h"
#include "usbuart.h"
#include "morse.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/flash.h>

extern uint32_t _ebss[];

#define SYSCFG_MEMRM                  MMIO32(0x40010000)
#define SYSMEM_RESET_VECTOR            0x1fffC804

void platform_init(void)
{
	volatile uint32_t *magic = (uint32_t *) &_ebss;
	/* If RCC_CFGR is not at it's reset value, the bootloader was executed
	 * and SET_ADDRESS got us to this place. On F3 ???, without further efforts,
	 * DFU does not start in that case.
	 * So issue an reset to allow a clean start!
	 */
	if (RCC_CFGR)
		scb_reset_system();
	SYSCFG_MEMRM &= ~3;
	/* Buttom is BOOT0, so buttom is already evaluated!*/
	if (((magic[0] == BOOTMAGIC0) && (magic[1] == BOOTMAGIC1))) {
		magic[0] = 0;
		magic[1] = 0;
		/* Jump to the built in bootloader by mapping System flash.
		   As we just come out of reset, no other deinit is needed!*/
		SYSCFG_MEMRM |=  1;
		void (*bootloader)(void) =
			(void (*)(void)) (*((uint32_t *) SYSMEM_RESET_VECTOR));
		/* We come out of reset, so MSP is already set*/
		bootloader();
		while (1);
	}
	rcc_clock_setup_in_hse_8mhz_out_48mhz();

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_CRC);
	rcc_set_usbclk_source(RCC_PLL);

	GPIOA_OSPEEDR &= ~0xF00C;
	GPIOA_OSPEEDR |= 0x5004; /* Set medium speed on PA1, PA6,PA7*/
	gpio_mode_setup(JTAG_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
					TMS_PIN | TCK_PIN |TDI_PIN);
	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TDO_PIN);
	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT,
					GPIO_PUPD_NONE,
					LED_UART | LED_IDLE_RUN | LED_ERROR | LED_BOOTLOADER);
	gpio_mode_setup(SRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SRST_PIN);
	gpio_set(SRST_PORT, SRST_PIN);
	gpio_set_output_options(SRST_PORT, GPIO_OTYPE_OD,
							GPIO_OSPEED_2MHZ, SRST_PIN);
	platform_timing_init();
	cdcacm_init();
	usbuart_init();
}

void platform_srst_set_val(bool assert)
{
	gpio_set_val(SRST_PORT, SRST_PIN, !assert);
}

bool platform_srst_get_val(void)
{
	return (gpio_get(SRST_PORT, SRST_PIN)) ? false : true;
}

const char *platform_target_voltage(void)
{
	return "ABSENT!";
}

void platform_request_boot(void)
{
	uint32_t *magic = (uint32_t *) &_ebss;
	magic[0] = BOOTMAGIC0;
	magic[1] = BOOTMAGIC1;
	scb_reset_system();
}
