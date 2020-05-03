/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Francesco Valla <valla.francesco@gmail.com>
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
#include "general.h"
#include "gdb_if.h"
#include "cdcacm.h"
#include "usbuart.h"

#include <libopencm3/lm4f/rcc.h>
#include <libopencm3/lm4f/nvic.h>
#include <libopencm3/lm4f/uart.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/lm4f/usb.h>

#define SYSTICKHZ	100
#define SYSTICKMS	(1000 / SYSTICKHZ)

#define PLL_DIV_80MHZ	5
#define PLL_DIV_25MHZ	16

extern void trace_tick(void);

volatile platform_timeout * volatile head_timeout;
uint8_t running_status;
static volatile uint32_t time_ms;

void sys_tick_handler(void)
{
	trace_tick();
	time_ms += 10;
}

uint32_t platform_time_ms(void)
{
	return time_ms;
}

void
platform_init(void)
{
	int i;
	for(i=0; i<1000000; i++);

	rcc_sysclk_config(OSCSRC_MOSC, XTAL_16M, PLL_DIV_80MHZ);

	// Enable JTAG port and set pins to output
	periph_clock_enable(JTAG_PORT_CLOCK);

	gpio_enable_ahb_aperture();

	gpio_mode_setup(TMS_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TMS_PIN);
	gpio_mode_setup(TCK_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TCK_PIN);
	gpio_mode_setup(TDI_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TDI_PIN);
	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TDO_PIN);
	gpio_mode_setup(SRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SRST_PIN);
	gpio_set_output_config(SRST_PORT, GPIO_OTYPE_OD, GPIO_DRIVE_2MA, SRST_PIN);
	gpio_set(SRST_PORT, SRST_PIN);

#ifdef LED_PORT
	// Enable all LED ports and set pins to output
	periph_clock_enable(LED_PORT_CLOCK);

	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLDOWN, LED_ERROR);
	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLDOWN, LED_IDLE);
	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLDOWN, LED_RUN);
#endif

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(rcc_get_system_clock_frequency() / (SYSTICKHZ * 8));

	systick_interrupt_enable();
	systick_counter_enable();

	nvic_enable_irq(NVIC_SYSTICK_IRQ);
	nvic_enable_irq(USBUART_IRQ);

	// Enable and setup USB pins
	periph_clock_enable(USB_PORT_CLOCK);
	__asm__("nop"); __asm__("nop"); __asm__("nop");
	gpio_mode_setup(USB_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, USB_DN|USB_DP);

	usbuart_init();
	cdcacm_init();

#ifdef ENABLE_DEBUG
	debuguart_init();
#endif

	usb_enable_interrupts(USB_INT_RESET | USB_INT_DISCON |
						  USB_INT_RESUME | USB_INT_SUSPEND,
						  0xff, 0xff);
}

void platform_srst_set_val(bool assert)
{
	volatile int i;
	if (assert) {
		gpio_clear(SRST_PORT, SRST_PIN);
		for(i = 0; i < 10000; i++) asm("nop");
	} else {
		gpio_set(SRST_PORT, SRST_PIN);
	}
}

bool platform_srst_get_val(void)
{
	return gpio_get(SRST_PORT, SRST_PIN) == 0;
}

void platform_delay(uint32_t ms)
{
	platform_timeout timeout;
	platform_timeout_set(&timeout, ms);
	while (!platform_timeout_is_expired(&timeout));
}

const char *platform_target_voltage(void)
{
	return "not supported";
}

char *serialno_read(char *s)
{
	/* No unique ID on Tiva microcontrollers */
	int i;

	for(i = 0; i < 8; i++) {
		s[i] = '0';
	}

	s[8] = 0;

	return s;
}

void platform_request_boot(void)
{
}

