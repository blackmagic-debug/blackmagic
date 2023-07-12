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

/* This file provides the platform specific declarations for the ST-Link implementation. */

#ifndef PLATFORMS_STLINK_PLATFORM_H
#define PLATFORMS_STLINK_PLATFORM_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"

#include <libopencm3/cm3/common.h>
#include <libopencm3/stm32/memorymap.h>
#include <libopencm3/usb/usbd.h>

#ifdef ENABLE_DEBUG
#define PLATFORM_HAS_DEBUG
extern bool debug_bmp;
#endif

#define PLATFORM_IDENT "(ST-Link/v2) "

/* Hardware definitions... */
#define TDI_PORT GPIOA
#define TMS_PORT GPIOB
#define TCK_PORT GPIOA
#define TDO_PORT GPIOA
#define TDI_PIN  GPIO7
#define TMS_PIN  GPIO14
#define TCK_PIN  GPIO5
#define TDO_PIN  GPIO6

#define SWDIO_PORT TMS_PORT
#define SWCLK_PORT TCK_PORT
#define SWDIO_PIN  TMS_PIN
#define SWCLK_PIN  TCK_PIN

#define NRST_PORT      GPIOB
#define NRST_PIN_V1    GPIO1
#define NRST_PIN_V2    GPIO0
#define NRST_PIN_CLONE GPIO6

#ifdef BLUEPILL
#define LED_PORT GPIOC
#else
#define LED_PORT GPIOA
#endif
/* Use PC14 for a "dummy" UART LED so we can observere at least with scope */
#define LED_PORT_UART GPIOA
#define LED_UART      GPIO9

#ifndef SWIM_AS_UART
#define PLATFORM_HAS_TRACESWO 1
#endif

#define NUM_TRACE_PACKETS 128U /* This is an 8K buffer */
#define TRACESWO_PROTOCOL 2U   /* 1 = Manchester, 2 = NRZ / async */

#define SWD_CR      GPIO_CRH(SWDIO_PORT)
#define SWD_CR_MULT (1U << ((14U - 8U) << 2U))

#define TMS_SET_MODE() gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN);
#define SWDIO_MODE_FLOAT()           \
	do {                             \
		uint32_t cr = SWD_CR;        \
		cr &= ~(0xfU * SWD_CR_MULT); \
		cr |= (0x4U * SWD_CR_MULT);  \
		SWD_CR = cr;                 \
	} while (0)
#define SWDIO_MODE_DRIVE()           \
	do {                             \
		uint32_t cr = SWD_CR;        \
		cr &= ~(0xfU * SWD_CR_MULT); \
		cr |= (0x1U * SWD_CR_MULT);  \
		SWD_CR = cr;                 \
	} while (0)
#define UART_PIN_SETUP()                                                                                        \
	do {                                                                                                        \
		gpio_set_mode(USBUSART_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, USBUSART_TX_PIN); \
		gpio_set_mode(USBUSART_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, USBUSART_RX_PIN);             \
		gpio_set(USBUSART_PORT, USBUSART_RX_PIN);                                                               \
	} while (0)

#define USB_DRIVER st_usbfs_v1_usb_driver
#define USB_IRQ    NVIC_USB_LP_CAN_RX0_IRQ
#define USB_ISR(x) usb_lp_can_rx0_isr(x)
/* Interrupt priorities. Low numbers are high priority. */
#define IRQ_PRI_USB          (1U << 4U)
#define IRQ_PRI_USBUSART     (2U << 4U)
#define IRQ_PRI_USBUSART_DMA (2U << 4U)
#define IRQ_PRI_USB_VBUS     (14U << 4U)
#define IRQ_PRI_SWO_DMA      (0U << 4U)

#ifdef SWIM_AS_UART
#define USBUSART               USART1
#define USBUSART_CR1           USART1_CR1
#define USBUSART_DR            USART1_DR
#define USBUSART_IRQ           NVIC_USART1_IRQ
#define USBUSART_CLK           RCC_USART1
#define USBUSART_ISR(x)        usart1_isr(x)
#define USBUSART_PORT          GPIOB
#define USBUSART_TX_PIN        GPIO6
#define USBUSART_RX_PIN        GPIO7
#define USBUSART_DMA_TX_CHAN   DMA_CHANNEL4
#define USBUSART_DMA_TX_IRQ    NVIC_DMA1_CHANNEL4_IRQ
#define USBUSART_DMA_TX_ISR(x) dma1_channel4_isr(x)
#define USBUSART_DMA_RX_CHAN   DMA_CHANNEL5
#define USBUSART_DMA_RX_IRQ    NVIC_DMA1_CHANNEL5_IRQ
#define USBUSART_DMA_RX_ISR(x) dma1_channel5_isr(x)
#else
#define USBUSART               USART2
#define USBUSART_CR1           USART2_CR1
#define USBUSART_DR            USART2_DR
#define USBUSART_IRQ           NVIC_USART2_IRQ
#define USBUSART_CLK           RCC_USART2
#define USBUSART_ISR(x)        usart2_isr(x)
#define USBUSART_PORT          GPIOA
#define USBUSART_TX_PIN        GPIO2
#define USBUSART_RX_PIN        GPIO3
#define USBUSART_DMA_TX_CHAN   DMA_CHANNEL7
#define USBUSART_DMA_TX_IRQ    NVIC_DMA1_CHANNEL7_IRQ
#define USBUSART_DMA_TX_ISR(x) dma1_channel7_isr(x)
#define USBUSART_DMA_RX_CHAN   DMA_CHANNEL6
#define USBUSART_DMA_RX_IRQ    NVIC_DMA1_CHANNEL6_IRQ
#define USBUSART_DMA_RX_ISR(x) dma1_channel6_isr(x)
#endif

#define USBUSART_DMA_BUS DMA1
#define USBUSART_DMA_CLK RCC_DMA1

/* On F103, only USART1 is on AHB2 and can reach 4.5MBaud at 72 MHz. */
#define SWO_UART        USART1
#define SWO_UART_DR     USART1_DR
#define SWO_UART_CLK    RCC_USART1
#define SWO_UART_PORT   GPIOA
#define SWO_UART_RX_PIN GPIO10

/* This DMA channel is set by the USART in use */
#define SWO_DMA_BUS    DMA1
#define SWO_DMA_CLK    RCC_DMA1
#define SWO_DMA_CHAN   DMA_CHANNEL5
#define SWO_DMA_IRQ    NVIC_DMA1_CHANNEL5_IRQ
#define SWO_DMA_ISR(x) dma1_channel5_isr(x)

extern uint16_t led_idle_run;
#define LED_IDLE_RUN led_idle_run
#define SET_RUN_STATE(state)      \
	{                             \
		running_status = (state); \
	}
#define SET_IDLE_STATE(state)                        \
	{                                                \
		gpio_set_val(LED_PORT, led_idle_run, state); \
	}
#define SET_ERROR_STATE(x)

extern uint32_t detect_rev(void);

#endif /* PLATFORMS_STLINK_PLATFORM_H */
