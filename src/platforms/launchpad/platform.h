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
#ifndef __PLATFORM_H
#define __PLATFORM_H

#include <libopencm3/lm4f/gpio.h>
#include <libopencm3/usb/usbd.h>

#include "timing.h"
#include "version.h"

#if defined(LAUNCHPAD_TARGET)
#define LAUNCHPAD_VARIANT       "Target"
#elif defined(LAUNCHPAD_ICDI)
#define LAUNCHPAD_VARIANT       "ICDI"
#else
#error "Launchpad variant not defined."
#endif

#define BOARD_IDENT             "Black Magic Probe (Launchpad " LAUNCHPAD_VARIANT "), (Firmware " FIRMWARE_VERSION ")"
#define BOARD_IDENT_DFU		"Black Magic (Upgrade) for Launchpad, (Firmware " FIRMWARE_VERSION ")"
#define DFU_IDENT               "Black Magic Firmware Upgrade (Launchpad " LAUNCHPAD_VARIANT ")"
#define DFU_IFACE_STRING	"lolwut"

extern uint8_t running_status;

#define JTAG_PORT_CLOCK	RCC_GPIOA

#define TMS_PORT	GPIOA_BASE
#define TMS_PIN		GPIO3

#define TCK_PORT	GPIOA_BASE
#define TCK_PIN		GPIO2

#define TDI_PORT	GPIOA_BASE
#define TDI_PIN		GPIO5

#define TDO_PORT	GPIOA_BASE
#define TDO_PIN		GPIO4

#if defined(LAUNCHPAD_TARGET)
#define SWO_PORT_CLOCK	RCC_GPIOE
#define SWO_PORT	GPIOE_BASE
#define SWO_PIN		GPIO0
#elif defined(LAUNCHPAD_ICDI)
#define SWO_PORT_CLOCK	RCC_GPIOD
#define SWO_PORT	GPIOD_BASE
#define SWO_PIN		GPIO6
#endif

#define SWDIO_PORT	TMS_PORT
#define SWDIO_PIN	TMS_PIN

#define SWCLK_PORT	TCK_PORT
#define SWCLK_PIN	TCK_PIN

#define SRST_PORT	GPIOA_BASE
#define SRST_PIN	GPIO6

#define USB_PORT_CLOCK	RCC_GPIOD
#define USB_PORT	GPIOD_BASE
#define USB_DN		GPIO4
#define USB_DP		GPIO5

#if defined(LAUNCHPAD_TARGET)
#define LED_PORT_CLOCK	RCC_GPIOF
#define LED_PORT	GPIOF_BASE
#define LED_ERROR	GPIO1
#define LED_IDLE	GPIO2
#define LED_RUN		GPIO3
#endif

#define TMS_SET_MODE()	{								\
	gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN);		\
	gpio_set_output_config(TMS_PORT, GPIO_OTYPE_PP, GPIO_DRIVE_2MA, TMS_PIN);	\
}

#define SWDIO_MODE_FLOAT() {								\
	gpio_mode_setup(SWDIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SWDIO_PIN);	\
}

#define SWDIO_MODE_DRIVE() {									\
	gpio_mode_setup(SWDIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SWDIO_PIN);		\
	gpio_set_output_config(SWDIO_PORT, GPIO_OTYPE_PP, GPIO_DRIVE_2MA, SWDIO_PIN);		\
}

extern const usbd_driver lm4f_usb_driver;
#define USB_DRIVER	lm4f_usb_driver
#define USB_IRQ		NVIC_USB0_IRQ
#define USB_ISR		usb0_isr

#define IRQ_PRI_USB	(2 << 4)

#if defined(LAUNCHPAD_TARGET)

#define USBUART		UART5
#define USBUART_CLK	RCC_UART5
#define USBUART_IRQ	NVIC_UART5_IRQ
#define USBUART_ISR	uart5_isr
#define UART_PIN_SETUP() do {								\
	periph_clock_enable(RCC_GPIOE);							\
	__asm__("nop"); __asm__("nop"); __asm__("nop");					\
	gpio_set_af(GPIOE_BASE, 0x1, GPIO4 | GPIO5);				\
	gpio_mode_setup(GPIOE_BASE, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO4);	\
	gpio_mode_setup(GPIOE_BASE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO5);	\
	} while (0)

#define TRACEUART	UART7
#define TRACEUART_CLK	RCC_UART7
#define TRACEUART_PORT_CLK	RCC_GPIOE
#define TRACEUART_IRQ	NVIC_UART7_IRQ
#define TRACEUART_ISR	uart7_isr

#define SET_RUN_STATE(state)	{gpio_set_val(LED_PORT, LED_RUN, state);running_status = (state);}
#define SET_IDLE_STATE(state)	{gpio_set_val(LED_PORT, LED_IDLE, state);}
#define SET_ERROR_STATE(state)	{gpio_set_val(LED_PORT, LED_ERROR, state);}

#ifdef ENABLE_DEBUG

#define DEBUGUART	UART0
#define DEBUGUART_CLK	RCC_UART0
#define DEBUGUART_IRQ	NVIC_UART0_IRQ
#define DEBUGUART_ISR	uart0_isr
#define DEBUGUART_PIN_SETUP() do {						\
	periph_clock_enable(RCC_GPIOA);						\
	__asm__("nop"); __asm__("nop"); __asm__("nop");				\
	gpio_set_af(GPIOA_BASE, 0x1, GPIO0 | GPIO1);				\
	gpio_mode_setup(GPIOA_BASE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0);	\
	gpio_mode_setup(GPIOA_BASE, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO1);	\
	} while (0)

#endif

#elif defined(LAUNCHPAD_ICDI)

#define USBUART		UART0
#define USBUART_CLK	RCC_UART0
#define USBUART_IRQ	NVIC_UART0_IRQ
#define USBUART_ISR	uart0_isr
#define UART_PIN_SETUP() do {								\
	periph_clock_enable(RCC_GPIOA);							\
	__asm__("nop"); __asm__("nop"); __asm__("nop");					\
	gpio_set_af(GPIOA_BASE, 0x1, GPIO0 | GPIO1);				\
	gpio_mode_setup(GPIOA_BASE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0);	\
	gpio_mode_setup(GPIOA_BASE, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO1);	\
	} while (0)

#define TRACEUART	UART2
#define TRACEUART_CLK	RCC_UART2
#define TRACEUART_PORT_CLK	RCC_GPIOD
#define TRACEUART_IRQ	NVIC_UART2_IRQ
#define TRACEUART_ISR	uart2_isr

#define SET_RUN_STATE(state)	{running_status = (state);}
#define SET_IDLE_STATE(state)	{}
#define SET_ERROR_STATE(state)	SET_IDLE_STATE(state)

#endif

/* Use newlib provided integer only stdio functions */
#define sscanf siscanf
#define sprintf siprintf
#define vasprintf vasiprintf
#define snprintf sniprintf

#ifdef ENABLE_DEBUG

#define PLATFORM_HAS_DEBUG
#define DEBUG printf

void debuguart_init(void);
void debuguart_test(void);

#else
#define DEBUG(...)
#endif

#define PLATFORM_HAS_TRACESWO

inline static void gpio_set_val(uint32_t port, uint8_t pin, uint8_t val) {
	gpio_write(port, pin, val == 0 ? 0 : 0xff);
}

inline static uint8_t gpio_get(uint32_t port, uint8_t pin) {
	return !(gpio_read(port, pin) == 0);
}

static inline int platform_hwversion(void)
{
	return 0;
}

#endif
