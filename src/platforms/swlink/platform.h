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
#include "version.h"

#define BOARD_IDENT            "Black Magic Probe (SWLINK), (Firmware " FIRMWARE_VERSION ")"
#define BOARD_IDENT_DFU	       "Black Magic (Upgrade), STM8S Discovery, (Firmware " FIRMWARE_VERSION ")"
#define BOARD_IDENT_UPD	       "Black Magic (DFU Upgrade), STM8S Discovery, (Firmware " FIRMWARE_VERSION ")"
#define DFU_IDENT              "Black Magic Firmware Upgrade (SWLINK)"
#define UPD_IFACE_STRING       "@Internal Flash   /0x08000000/8*001Kg"

/* Hardware definitions... */
#define TMS_PORT	GPIOA
#define TCK_PORT	GPIOA
#define TDI_PORT	GPIOA
#define TDO_PORT	GPIOB
#define NRST_PORT	GPIOB
#define TMS_PIN		GPIO13
#define TCK_PIN		GPIO14
#define TDI_PIN		GPIO15
#define TDO_PIN		GPIO3
#define NRST_PIN	GPIO4

#define SWDIO_PORT 	TMS_PORT
#define SWCLK_PORT 	TCK_PORT
#define SWDIO_PIN	TMS_PIN
#define SWCLK_PIN	TCK_PIN

#define LED_PORT	GPIOA
#define LED_IDLE_RUN    GPIO8
/* Use PC14 for a "dummy" uart led. So we can observere at least with scope*/
#define LED_PORT_UART	GPIOC
#define LED_UART	GPIO14

#define TMS_SET_MODE() \
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_50_MHZ, \
	              GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN);
#define SWDIO_MODE_FLOAT() \
	gpio_set_mode(SWDIO_PORT, GPIO_MODE_INPUT, \
	              GPIO_CNF_INPUT_FLOAT, SWDIO_PIN);
#define SWDIO_MODE_DRIVE() \
	gpio_set_mode(SWDIO_PORT, GPIO_MODE_OUTPUT_50_MHZ, \
	              GPIO_CNF_OUTPUT_PUSHPULL, SWDIO_PIN);

#define UART_PIN_SETUP() do { \
	AFIO_MAPR |= AFIO_MAPR_USART1_REMAP; \
	gpio_set_mode(USBUSART_PORT, GPIO_MODE_OUTPUT_2_MHZ, \
	              GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, USBUSART_TX_PIN); \
} while (0)

#define USB_DRIVER      stm32f103_usb_driver
#define USB_IRQ         NVIC_USB_LP_CAN_RX0_IRQ
#define USB_ISR         usb_lp_can_rx0_isr
/* Interrupt priorities.  Low numbers are high priority.
 * For now USART1 preempts USB which may spin while buffer is drained.
 * TIM2 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB		(2 << 4)
#define IRQ_PRI_USBUSART	(1 << 4)
#define IRQ_PRI_USBUSART_TIM	(3 << 4)
#define IRQ_PRI_USB_VBUS	(14 << 4)
#define IRQ_PRI_TRACE		(0 << 4)

#define USBUSART USART1
#define USBUSART_CR1 USART1_CR1
#define USBUSART_IRQ NVIC_USART1_IRQ
#define USBUSART_CLK RCC_USART1
#define USBUSART_PORT GPIOB
#define USBUSART_TX_PIN GPIO6
#define USBUSART_ISR usart1_isr
#define USBUSART_TIM TIM4
#define USBUSART_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM4)
#define USBUSART_TIM_IRQ NVIC_TIM4_IRQ
#define USBUSART_TIM_ISR tim4_isr

#define TRACE_TIM TIM2
#define TRACE_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM2)
#define TRACE_IRQ   NVIC_TIM2_IRQ
#define TRACE_ISR   tim2_isr
#define TRACE_IC_IN TIM_IC_IN_TI2
#define TRACE_TRIG_IN TIM_SMCR_TS_IT1FP2

#ifdef ENABLE_DEBUG
# define PLATFORM_HAS_DEBUG
# define USBUART_DEBUG
extern bool debug_bmp;
int usbuart_debug_write(const char *buf, size_t len);
# define DEBUG printf
#else
# define DEBUG(...)
#endif

#define SET_RUN_STATE(state)	{running_status = (state);}
#define SET_IDLE_STATE(state)	{gpio_set_val(LED_PORT, LED_IDLE_RUN, state);}
#define SET_ERROR_STATE(x)

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

