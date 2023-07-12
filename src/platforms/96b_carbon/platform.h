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

/* This file provides the platform specific declarations for the 96Boards Carbon implementation. */

#ifndef PLATFORMS_96B_CARBON_PLATFORM_H
#define PLATFORMS_96B_CARBON_PLATFORM_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"
#include "version.h"

#define PLATFORM_HAS_TRACESWO
#define PLATFORM_IDENT "(Carbon)"

/*
 * Important pin mappings for Carbon implementation:
 *
 * LED0 = 	   PA15 (Green USR2 : Idle))
 * LED1 = 	   PD5  (Green USR1 : UART)
 * LED2 = 	   PB5  (Blue BT    : Error)
 *
 * TDO = 	   PB12 (LS-02)
 * TDI = 	   PB15 (LS-04)
 * TMS/SWDIO = PB14 (LS-06)  The pinout for the programmer allows a Carbon to
 * TCK/SWCLK = PB13 (LS-08)  program another Carbon (either the STM32 or the
 * GND              (LS-10)  nRF51) with adjacent pins from LS-06 to LS-12.
 * VCC              (LS-12)  The order matches the SWD pins for easy hook up.
 * nTRST =     PC3  (LS-14)
 * nRST =      PC5  (LS-16)
 */

/* Hardware definitions... */
#define JTAG_PORT GPIOB
#define TDO_PORT  JTAG_PORT
#define TDI_PORT  JTAG_PORT
#define TMS_PORT  JTAG_PORT
#define TCK_PORT  JTAG_PORT
#define TDO_PIN   GPIO12
#define TDI_PIN   GPIO15
#define TMS_PIN   GPIO14
#define TCK_PIN   GPIO13

#define SWDIO_PORT JTAG_PORT
#define SWCLK_PORT JTAG_PORT
#define SWDIO_PIN  TMS_PIN
#define SWCLK_PIN  TCK_PIN

#define TRST_PORT GPIOC
#define TRST_PIN  GPIO3
#define NRST_PORT GPIOC
#define NRST_PIN  GPIO5

#define LED_PORT       GPIOA
#define LED_IDLE_RUN   GPIO15
#define LED_PORT_UART  GPIOD
#define LED_UART       GPIO2
#define LED_PORT_ERROR GPIOB
#define LED_ERROR      GPIO5

#define TMS_SET_MODE()     gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN);
#define SWDIO_MODE_FLOAT() gpio_mode_setup(SWDIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SWDIO_PIN);

#define SWDIO_MODE_DRIVE() gpio_mode_setup(SWDIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SWDIO_PIN);

#define USB_DRIVER otgfs_usb_driver
#define USB_IRQ    NVIC_OTG_FS_IRQ
#define USB_ISR    otg_fs_isr
/*
 * Interrupt priorities. Low numbers are high priority.
 * TIM3 is used for traceswo capture and must be highest priority.
 * USBUSART can be lowest priority as it is using DMA to transfer
 * data to the buffer and thus is less critical than USB.
 */
#define IRQ_PRI_USB          (1U << 4U)
#define IRQ_PRI_USBUSART     (2U << 4U)
#define IRQ_PRI_USBUSART_DMA (2U << 4U)
#define IRQ_PRI_TRACE        (0U << 4U)

#define USBUSART               USART2
#define USBUSART_CR1           USART2_CR1
#define USBUSART_IRQ           NVIC_USART2_IRQ
#define USBUSART_CLK           RCC_USART2
#define USBUSART_TX_PORT       GPIOA
#define USBUSART_TX_PIN        GPIO2
#define USBUSART_RX_PORT       GPIOA
#define USBUSART_RX_PIN        GPIO3
#define USBUSART_ISR(x)        usart2_isr(x)
#define USBUSART_DMA_BUS       DMA1
#define USBUSART_DMA_CLK       RCC_DMA1
#define USBUSART_DMA_TX_CHAN   DMA_STREAM6
#define USBUSART_DMA_TX_IRQ    NVIC_DMA1_STREAM6_IRQ
#define USBUSART_DMA_TX_ISR(x) dma1_stream6_isr(x)
#define USBUSART_DMA_RX_CHAN   DMA_STREAM5
#define USBUSART_DMA_RX_IRQ    NVIC_DMA1_STREAM5_IRQ
#define USBUSART_DMA_RX_ISR(x) dma1_stream5_isr(x)
/* For STM32F4 DMA trigger source must be specified */
#define USBUSART_DMA_TRG DMA_SxCR_CHSEL_4

#define UART_PIN_SETUP()                                                                  \
	do {                                                                                  \
		gpio_mode_setup(USBUSART_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, USBUSART_TX_PIN); \
		gpio_mode_setup(USBUSART_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, USBUSART_RX_PIN); \
		gpio_set_af(USBUSART_TX_PORT, GPIO_AF7, USBUSART_TX_PIN);                         \
		gpio_set_af(USBUSART_RX_PORT, GPIO_AF7, USBUSART_RX_PIN);                         \
	} while (0)

#define TRACE_TIM          TIM3
#define TRACE_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM3)
#define TRACE_IRQ          NVIC_TIM3_IRQ
#define TRACE_ISR(x)       tim3_isr(x)

#define DEBUG(...) \
	do {           \
	} while (false)

#define SET_RUN_STATE(state)      \
	{                             \
		running_status = (state); \
	}
#define SET_IDLE_STATE(state)                        \
	{                                                \
		gpio_set_val(LED_PORT, LED_IDLE_RUN, state); \
	}
#define SET_ERROR_STATE(state)                          \
	{                                                   \
		gpio_set_val(LED_PORT_ERROR, LED_ERROR, state); \
	}

#endif /* PLATFORMS_96B_CARBON_PLATFORM_H */
