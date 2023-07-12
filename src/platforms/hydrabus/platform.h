/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2016 Benjamin Vernoux <bvernoux@gmail.com>
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

/* This file provides the platform specific declarations for the Hydrabus implementation. */

#ifndef PLATFORMS_HYDRABUS_PLATFORM_H
#define PLATFORMS_HYDRABUS_PLATFORM_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"

#define PLATFORM_HAS_TRACESWO
#define PLATFORM_IDENT " (HydraBus))"

/*
 * Important pin mappings for STM32 implementation:
 *
 * LED0 = 	PA4	(Green LED : Running)
 * LED0 = 	PA4	(Green LED : Idle)
 * LED0 = 	PA4	(Green LED : Error)
 * LED0 = 	PA4	(Green LED : Bootloader active)
 *
 * TMS = 	PC0 (SWDIO)
 * TCK = 	PC1 (SWCLK)
 * TDO = 	PC2 (input for TRACESWO)
 * TDI = 	PC3
 * nRST =   PC4 (nRST / nRESET / "System Reset")
 * nTRST = 	PC5 (Test Reset optional)
 *
 * USB VBUS detect:  PB13
 */

/* Hardware definitions... */
#define JTAG_PORT GPIOC
#define TDI_PORT  JTAG_PORT
#define TMS_PORT  JTAG_PORT
#define TCK_PORT  JTAG_PORT
#define TDO_PORT  JTAG_PORT

#define TDI_PIN GPIO3
#define TMS_PIN GPIO0
#define TCK_PIN GPIO1
#define TDO_PIN GPIO2

#define SWDIO_PORT JTAG_PORT
#define SWCLK_PORT JTAG_PORT
#define SWDIO_PIN  TMS_PIN
#define SWCLK_PIN  TCK_PIN

#define TRST_PORT GPIOC
#define TRST_PIN  GPIO5
#define NRST_PORT GPIOC
#define NRST_PIN  GPIO4

#define LED_PORT       GPIOA
#define LED_PORT_UART  GPIOA
#define LED_UART       GPIO4
#define LED_IDLE_RUN   GPIO4
#define LED_ERROR      GPIO4
#define LED_BOOTLOADER GPIO4

#define TMS_SET_MODE()     gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN);
#define SWDIO_MODE_FLOAT() gpio_mode_setup(SWDIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SWDIO_PIN);

#define SWDIO_MODE_DRIVE() gpio_mode_setup(SWDIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SWDIO_PIN);
#define UART_PIN_SETUP()                                                                            \
	do {                                                                                            \
		gpio_mode_setup(USBUSART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, USBUSART_TX_PIN);              \
		gpio_set_output_options(USBUSART_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, USBUSART_TX_PIN); \
		gpio_set_af(USBUSART_PORT, GPIO_AF7, USBUSART_TX_PIN);                                      \
		gpio_mode_setup(USBUSART_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USBUSART_RX_PIN);            \
		gpio_set_output_options(USBUSART_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_100MHZ, USBUSART_RX_PIN); \
		gpio_set_af(USBUSART_PORT, GPIO_AF7, USBUSART_RX_PIN);                                      \
	} while (0)

#define USB_DRIVER stm32f107_usb_driver
#define USB_IRQ    NVIC_OTG_FS_IRQ
#define USB_ISR(x) otg_fs_isr(x)
/*
 * Interrupt priorities. Low numbers are high priority.
 * TIM3 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB          (1U << 4U)
#define IRQ_PRI_USBUSART     (2U << 4U)
#define IRQ_PRI_USBUSART_DMA (2U << 4U)
#define IRQ_PRI_TRACE        (0U << 4U)

#define USBUSART               USART1
#define USBUSART_CR1           USART1_CR1
#define USBUSART_DR            USART1_DR
#define USBUSART_IRQ           NVIC_USART1_IRQ
#define USBUSART_CLK           RCC_USART1
#define USBUSART_PORT          GPIOA
#define USBUSART_TX_PIN        GPIO9
#define USBUSART_RX_PIN        GPIO10
#define USBUSART_ISR(x)        usart1_isr(x)
#define USBUSART_DMA_BUS       DMA2
#define USBUSART_DMA_CLK       RCC_DMA2
#define USBUSART_DMA_TX_CHAN   DMA_STREAM7
#define USBUSART_DMA_TX_IRQ    NVIC_DMA2_STREAM7_IRQ
#define USBUSART_DMA_TX_ISR(x) dma2_stream7_isr(x)
#define USBUSART_DMA_RX_CHAN   DMA_STREAM5
#define USBUSART_DMA_RX_IRQ    NVIC_DMA2_STREAM5_IRQ
#define USBUSART_DMA_RX_ISR(x) dma2_stream5_isr(x)
/* For STM32F4 DMA trigger source must be specified */
#define USBUSART_DMA_TRG DMA_SxCR_CHSEL_4

#define TRACE_TIM          TIM3
#define TRACE_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM3)
#define TRACE_IRQ          NVIC_TIM3_IRQ
#define TRACE_ISR(x)       tim3_isr(x)

#define SET_RUN_STATE(state)      \
	{                             \
		running_status = (state); \
	}
#define SET_IDLE_STATE(state)                        \
	{                                                \
		gpio_set_val(LED_PORT, LED_IDLE_RUN, state); \
	}
#define SET_ERROR_STATE(state)                    \
	{                                             \
		gpio_set_val(LED_PORT, LED_ERROR, state); \
	}

#endif /* PLATFORMS_HYDRABUS_PLATFORM_H */
