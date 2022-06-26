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

/* This file implements the platform specific functions for the STM32
 * implementation.
 */
#ifndef __PLATFORM_H
#define __PLATFORM_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"

#include <setjmp.h>

#define PLATFORM_HAS_TRACESWO

#ifdef ENABLE_DEBUG
# define PLATFORM_HAS_DEBUG
# define USBUART_DEBUG
#endif

#define PLATFORM_IDENT   "(F3-IF) "

/* Important pin mappings for STM32 implementation:
 *
 * LED0 = 	PB5	(Green  LED : Running)
 * LED1 = 	PB6	(Orange LED : Idle)
 * LED2 = 	PB7	(Red LED    : Error)
 *
 * TDI = 	PA0
 * TMS = 	PA1 (input for SWDP)
 * TCK = 	PA7/SWCLK
 * TDO = 	PA6 (input for TRACESWO
 * nRST =	PA5
 *
 * Force DFU mode button: BOOT0
 */

/* Hardware definitions... */
#define JTAG_PORT 	GPIOA
#define TDI_PORT	JTAG_PORT
#define TMS_PORT	JTAG_PORT
#define TCK_PORT	JTAG_PORT
#define TDO_PORT	JTAG_PORT
#define TDI_PIN		GPIO0
#define TMS_PIN		GPIO1
#define TCK_PIN		GPIO7
#define TDO_PIN		GPIO6

#define SWDIO_PORT 	JTAG_PORT
#define SWCLK_PORT 	JTAG_PORT
#define SWDIO_PIN	TMS_PIN
#define SWCLK_PIN	TCK_PIN

#define NRST_PORT	GPIOA
#define NRST_PIN	GPIO5

#define LED_PORT	GPIOB
#define LED_PORT_UART	GPIOB
#define LED_UART	GPIO6
#define LED_IDLE_RUN	GPIO5
#define LED_ERROR	GPIO7
/* PORTB does not stay active in system bootloader!*/
#define LED_BOOTLOADER	GPIO6

#define BOOTMAGIC0 0xb007da7a
#define BOOTMAGIC1 0xbaadfeed

#define TMS_SET_MODE() \
	gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN);
#define SWDIO_MODE_FLOAT() \
	gpio_mode_setup(SWDIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SWDIO_PIN);
#define SWDIO_MODE_DRIVE() \
	gpio_mode_setup(SWDIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SWDIO_PIN);

#define USB_DRIVER      st_usbfs_v1_usb_driver
#define USB_IRQ         NVIC_USB_LP_CAN1_RX0_IRQ
#define USB_ISR(x)      usb_lp_can1_rx0_isr(x)

/* Interrupt priorities.  Low numbers are high priority.
 * TIM3 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB		     (1 << 4)
#define IRQ_PRI_USBUSART	 (2 << 4)
#define IRQ_PRI_USBUSART_DMA (2 << 4)
#define IRQ_PRI_TRACE		 (0 << 4)

#define USBUSART USART2
#define USBUSART_CR1 USART2_CR1
#define USBUSART_TDR USART2_TDR
#define USBUSART_RDR USART2_RDR
#define USBUSART_IRQ NVIC_USART2_EXTI26_IRQ
#define USBUSART_CLK RCC_USART2
#define USBUSART_PORT GPIOA
#define USBUSART_TX_PIN  GPIO3
#define USBUSART_RX_PIN  GPIO2
#define USBUSART_ISR(x) usart2_exti26_isr(x)

#define USBUSART_DMA_BUS DMA1
#define USBUSART_DMA_CLK RCC_DMA1
#define USBUSART_DMA_TX_CHAN DMA_CHANNEL7
#define USBUSART_DMA_TX_IRQ NVIC_DMA1_CHANNEL7_IRQ
#define USBUSART_DMA_TX_ISR(x) dma1_channel7_isr(x)
#define USBUSART_DMA_RX_CHAN DMA_CHANNEL6
#define USBUSART_DMA_RX_IRQ NVIC_DMA1_CHANNEL6_IRQ
#define USBUSART_DMA_RX_ISR(x) dma1_channel6_isr(x)

/* TX/RX on the REV 0/1 boards are swapped against ftdijtag.*/
#define UART_PIN_SETUP() do {											\
		gpio_mode_setup(USBUSART_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP,  \
						USBUSART_TX_PIN | USBUSART_RX_PIN);				\
		gpio_set_af(USBUSART_PORT, GPIO_AF7,							\
					USBUSART_TX_PIN | USBUSART_RX_PIN);					\
		USART2_CR2 |= USART_CR2_SWAP;									\
	} while(0)

#define TRACE_TIM TIM3
#define TRACE_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM3)
#define TRACE_IRQ   NVIC_TIM3_IRQ
#define TRACE_ISR   tim3_isr

#ifdef ENABLE_DEBUG
extern bool debug_bmp;
int usbuart_debug_write(const char *buf, size_t len);
# define DEBUG printf
#else
# define DEBUG(...)
#endif

#define SET_RUN_STATE(state)	{running_status = (state);}
#define SET_IDLE_STATE(state)	{gpio_set_val(LED_PORT, LED_IDLE_RUN, state);}
#define SET_ERROR_STATE(state)	{gpio_set_val(LED_PORT, LED_ERROR, state);}

static inline int platform_hwversion(void)
{
	return 0;
}

/* Use newlib provided integer only stdio functions */
#define sscanf siscanf
#define sprintf siprintf
#define vasprintf vasiprintf
#define snprintf sniprintf

#endif
