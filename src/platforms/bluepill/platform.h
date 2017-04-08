/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Patching for Bluepill board by Dave Marples <dave@marples.net>
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

#include <libopencm3/cm3/common.h>
#include <libopencm3/stm32/f1/memorymap.h>
#include <libopencm3/usb/usbd.h>

#define BOARD_IDENT       "Black Magic Probe (Bluepill), (Firmware " FIRMWARE_VERSION ")"
#define BOARD_IDENT_DFU   "Black Magic (Upgrade) for Bluepill, (Firmware " FIRMWARE_VERSION ")"
#define BOARD_IDENT_UPD   "Black Magic (DFU Upgrade) for Bluepill, (Firmware " FIRMWARE_VERSION ")"
#define DFU_IDENT         "Black Magic Firmware Upgrade (Bluepill)"
#define DFU_IFACE_STRING  "@Internal Flash   /0x08000000/8*001Ka,56*001Kg"
#define UPD_IFACE_STRING  "@Internal Flash   /0x08000000/8*001Kg"

/* Important pin mappings for STM32 Bluepill implementation:
 *
 *
 * LED0 = 	PC13	(Yellow LED : Running)
 *
 * SRST_OUT = 	PB5
 * TDI = 	PB6
 * TMS = 	PB9  (==SWDIO)
 * TCK = 	PB8  (==SWCLK)
 * TDO = 	PB7  (==SDO)
 * nSRST = 	PB5 
 * TRST =       PA10
 * VSense =     PB4

 * Serial port;
 * PA2
 * PA3
 */

#define PLATFORM_HAS_TRACESWO 1

/* Hardware definitions... */
#define TRST_PORT       GPIOA
#define TRST_PIN        GPIO10

#define TMS_PORT	    GPIOB
#define TMS_PIN		    GPIO9
#define SWDIO_PORT 	    TMS_PORT
#define SWDIO_PIN	    TMS_PIN

#define TCK_PORT	    GPIOB
#define TCK_PIN		    GPIO8
#define SWCLK_PORT 	    TCK_PORT
#define SWCLK_PIN	    TCK_PIN

#define TDO_PORT	    GPIOB
#define TDO_PIN		    GPIO7
#define SWO_PORT        TDO_PORT
#define SWO_PIN         TDO_PIN

#define TDI_PORT	    GPIOB
#define TDI_PIN		    GPIO6

#define SRST_PORT	    GPIOB
#define SRST_PIN	    GPIO5

#define VSENSE_PORT     GPIOB
#define VSENSE_PIN      GPIO4

/* These are fixed by the board design */
#define LED_PORT	    GPIOC
#define LED_IDLE_RUN    GPIO13

/* Use PC14 for a "dummy" uart led. So we can observere at least with scope*/
#define LED_PORT_UART	GPIOC
#define LED_UART	    GPIO14

#define TMS_SET_MODE() \
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_50_MHZ, \
	              GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN);
#define SWDIO_MODE_FLOAT() \
	gpio_set_mode(SWDIO_PORT, GPIO_MODE_INPUT, \
	              GPIO_CNF_INPUT_FLOAT, SWDIO_PIN);
#define SWDIO_MODE_DRIVE() \
	gpio_set_mode(SWDIO_PORT, GPIO_MODE_OUTPUT_50_MHZ, \
	              GPIO_CNF_OUTPUT_PUSHPULL, SWDIO_PIN);

#define UART_PIN_SETUP() \
       gpio_set_mode(USBUSART_PORT, GPIO_MODE_OUTPUT_2_MHZ, \
                     GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, USBUSART_TX_PIN);


#define SWO_PIN_SETUP()						     \
        AFIO_MAPR |= AFIO_MAPR_USART1_REMAP;     \
        gpio_set_mode(SWO_PORT, GPIO_MODE_INPUT, \
        GPIO_CNF_INPUT_FLOAT, SWO_PIN);


#define USB_DRIVER              stm32f103_usb_driver
#define USB_IRQ	                NVIC_USB_LP_CAN_RX0_IRQ
#define USB_ISR	                usb_lp_can_rx0_isr
/* Interrupt priorities.  Low numbers are high priority.
 * For now USARTq preempts USB which may spin while buffer is drained.
 */
#define IRQ_PRI_USB		        (3 << 4)
#define IRQ_PRI_USBUSART	    (2 << 4)
#define IRQ_PRI_USBUSART_TIM	(4 << 4)
#define IRQ_PRI_USB_VBUS	    (14 << 4)
#define IRQ_PRI_SWODMA          (1 << 4)

/* Note that SWO needs to be on USART1 RX to get maximum speed */
#define SWOUSART                USART1
#define SWOUSARTDR              USART1_DR
#define SWOUSART_CR1            USART1_CR1
#define SWOUSART_IRQ            NVIC_USART1_IRQ
#define SWOUSART_CLK            RCC_USART1
#define SWOUSART_PORT           GPIOB
#define SWOUSART_TX_PIN         GPIO7
#define SWOUSART_ISR            usart1_isr

/* This DMA channel is set by the USART in use */
#define SWODMABUS               DMA1
#define SWDDMACHAN              DMA_CHANNEL5
#define SWODMAIRQ               NVIC_DMA1_CHANNEL5_IRQ

#define USBUSART                USART2
#define USBUSART_CR1            USART2_CR1
#define USBUSART_IRQ            NVIC_USART2_IRQ
#define USBUSART_CLK            RCC_USART2
#define USBUSART_PORT           GPIOA
#define USBUSART_TX_PIN         GPIO2
#define USBUSART_ISR            usart2_isr
#define USBUSART_TIM            TIM3
#define USBUSART_TIM_CLK_EN()   rcc_periph_clock_enable(RCC_TIM3)
#define USBUSART_TIM_IRQ        NVIC_TIM3_IRQ
#define USBUSART_TIM_ISR        tim3_isr

//#define TRACE_TIM_CLK_EN()      rcc_periph_clock_enable(RCC_TIM4)
//#define TRACE_TIM               TIM4
//#define TRACE_IRQ   NVIC_TIM4_IRQ
//#define TRACE_ISR   tim4_isr

#define DEBUG(...)

#define SET_RUN_STATE(state)	{running_status = (state);}
#define SET_IDLE_STATE(state)	{gpio_set_val(LED_PORT, LED_IDLE_RUN, state);}
#define SET_ERROR_STATE(x)

/* Use newlib provided integer only stdio functions */
#define sscanf                  siscanf
#define sprintf                 siprintf
#define vasprintf               vasiprintf
#define snprintf                sniprintf

#endif
