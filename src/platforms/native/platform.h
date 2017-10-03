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

/* This file implements the platform specific functions for the STM32
 * implementation.
 */
#ifndef __PLATFORM_H
#define __PLATFORM_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"

#define PLATFORM_HAS_TRACESWO
#define PLATFORM_HAS_POWER_SWITCH
#ifdef ENABLE_DEBUG
#define PLATFORM_HAS_DEBUG
#define USBUART_DEBUG
#endif
#define BOARD_IDENT             "Black Magic Probe"
#define BOARD_IDENT_DFU	        "Black Magic Probe (Upgrade)"
#define BOARD_IDENT_UPD	        "Black Magic Probe (DFU Upgrade)"
#define DFU_IDENT               "Black Magic Firmware Upgrade"
#define UPD_IFACE_STRING        "@Internal Flash   /0x08000000/8*001Kg"

/* Important pin mappings for STM32 implementation:
 *
 * LED0 = 	PB2	(Yellow LED : Running)
 * LED1 = 	PB10	(Yellow LED : Idle)
 * LED2 = 	PB11	(Red LED    : Error)
 *
 * TPWR = 	RB0 (input) -- analogue on mini design ADC1, ch8
 * nTRST = 	PB1 (output) [blackmagic]
 * PWR_BR = 	PB1 (output) [blackmagic_mini] -- supply power to the target, active low
 * TMS_DIR =    PA1 (output) [blackmagic_mini v2.1] -- choose direction of the TCK pin, input low, output high
 * SRST_OUT = 	PA2 (output)
 * TDI = 	PA3 (output)
 * TMS = 	PA4 (input/output for SWDIO)
 * TCK = 	PA5 (output SWCLK)
 * TDO = 	PA6 (input)
 * nSRST = 	PA7 (input)
 *
 * USB cable pull-up: PA8
 * USB VBUS detect:  PB13 -- New on mini design.
 *                           Enable pull up for compatibility.
 * Force DFU mode button: PB12
 */

/* Hardware definitions... */
#define JTAG_PORT 	GPIOA
#define TDI_PORT	JTAG_PORT
#define TMS_DIR_PORT	JTAG_PORT
#define TMS_PORT	JTAG_PORT
#define TCK_PORT	JTAG_PORT
#define TDO_PORT	JTAG_PORT
#define TDI_PIN		GPIO3
#define TMS_DIR_PIN	GPIO1
#define TMS_PIN		GPIO4
#define TCK_PIN		GPIO5
#define TDO_PIN		GPIO6

#define SWDIO_DIR_PORT	JTAG_PORT
#define SWDIO_PORT 	JTAG_PORT
#define SWCLK_PORT 	JTAG_PORT
#define SWDIO_DIR_PIN	TMS_DIR_PIN
#define SWDIO_PIN	TMS_PIN
#define SWCLK_PIN	TCK_PIN

#define TRST_PORT	GPIOB
#define TRST_PIN	GPIO1
#define PWR_BR_PORT	GPIOB
#define PWR_BR_PIN	GPIO1
#define SRST_PORT	GPIOA
#define SRST_PIN	GPIO2
#define SRST_SENSE_PORT	GPIOA
#define SRST_SENSE_PIN	GPIO7

#define USB_PU_PORT	GPIOA
#define USB_PU_PIN	GPIO8

#define USB_VBUS_PORT	GPIOB
#define USB_VBUS_PIN	GPIO13
#define USB_VBUS_IRQ	NVIC_EXTI15_10_IRQ

#define LED_PORT	GPIOB
#define LED_PORT_UART	GPIOB
#define LED_0		GPIO2
#define LED_1		GPIO10
#define LED_2		GPIO11
#define LED_UART	LED_0
#define LED_IDLE_RUN	LED_1
#define LED_ERROR	LED_2

#define TMS_SET_MODE() do { \
	gpio_set(TMS_DIR_PORT, TMS_DIR_PIN); \
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_50_MHZ, \
	              GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN); \
} while(0)
#define SWDIO_MODE_FLOAT() do { \
	gpio_set_mode(SWDIO_PORT, GPIO_MODE_INPUT, \
	              GPIO_CNF_INPUT_FLOAT, SWDIO_PIN); \
	gpio_clear(SWDIO_DIR_PORT, SWDIO_DIR_PIN); \
} while(0)
#define SWDIO_MODE_DRIVE() do { \
	gpio_set(SWDIO_DIR_PORT, SWDIO_DIR_PIN); \
	gpio_set_mode(SWDIO_PORT, GPIO_MODE_OUTPUT_50_MHZ, \
	              GPIO_CNF_OUTPUT_PUSHPULL, SWDIO_PIN); \
} while(0)
#define UART_PIN_SETUP() do { \
	gpio_set_mode(USBUSART_PORT, GPIO_MODE_OUTPUT_2_MHZ, \
	              GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, USBUSART_TX_PIN); \
} while(0)

#define USB_DRIVER st_usbfs_v1_usb_driver
#define USB_IRQ    NVIC_USB_LP_CAN_RX0_IRQ
#define USB_ISR    usb_lp_can_rx0_isr
/* Interrupt priorities.  Low numbers are high priority.
 * For now USART1 preempts USB which may spin while buffer is drained.
 * TIM3 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB             (2 << 4)
#define IRQ_PRI_USBUSART        (1 << 4)
#define IRQ_PRI_USBUSART_TIM    (3 << 4)
#define IRQ_PRI_USB_VBUS        (14 << 4)
#define IRQ_PRI_TRACE           (0 << 4)

#define USBUSART USART1
#define USBUSART_CR1 USART1_CR1
#define USBUSART_IRQ NVIC_USART1_IRQ
#define USBUSART_CLK RCC_USART1
#define USBUSART_PORT GPIOA
#define USBUSART_TX_PIN GPIO9
#define USBUSART_ISR usart1_isr
#define USBUSART_TIM TIM4
#define USBUSART_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM4)
#define USBUSART_TIM_IRQ NVIC_TIM4_IRQ
#define USBUSART_TIM_ISR tim4_isr

#define TRACE_TIM TIM3
#define TRACE_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM3)
#define TRACE_IRQ   NVIC_TIM3_IRQ
#define TRACE_ISR   tim3_isr

#ifdef ENABLE_DEBUG
extern bool debug_bmp;
int usbuart_debug_write(const char *buf, size_t len);

#define DEBUG printf
#else
#define DEBUG(...)
#endif

#define SET_RUN_STATE(state)	{running_status = (state);}
#define SET_IDLE_STATE(state)	{gpio_set_val(LED_PORT, LED_IDLE_RUN, state);}
#define SET_ERROR_STATE(state)	{gpio_set_val(LED_PORT, LED_ERROR, state);}

/* Use newlib provided integer only stdio functions */
#define sscanf siscanf
#define sprintf siprintf
#define snprintf sniprintf
#define vasprintf vasiprintf

#endif
