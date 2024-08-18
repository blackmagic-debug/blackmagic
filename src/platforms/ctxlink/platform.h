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

/* This file provides the platform specific declarations for the ctxLink implementation. */

#ifndef PLATFORMS_CTXLINK_PLATFORM_H
#define PLATFORMS_CTXLINK_PLATFORM_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"

#define PLATFORM_HAS_TRACESWO
#define PLATFORM_HAS_POWER_SWITCH

#define PLATFORM_IDENT "(ctxLink) "

/*
 * Important pin mappings for STM32 implementation:
 *
 * LED0 = 	PB2	(Green  LED : Running)
 * LED1 = 	PC6		(Orange LED : Idle)
 * LED2 = 	PC8		(Red LED    : Error)
 * LED3 = 	PC9		(Green LED   : ctxLink Mode)
 *
 * nRST		= A2	(output)
 * PWR_BR	= PB1	(output) - supply power to the target, active low
 *
 * TDI =      PA3	(output)
 * TMS =      PA4	(input/output for SWDIO)
 * TCK =      PA5	(output SWCLK)
 * TDO =      PC6	(input)
 * TMS_DIR = PA1	(output) controls target buffer direction
 * TPWR =	 PB0		(analog input)
 * VBAT =	 PA0		(analog input)
 *
 * SW_BOOTLOADER	PB12	(input) System Bootloader button
 */

/* Hardware definitions... */
#define JTAG_PORT    GPIOA
#define TDI_PORT     JTAG_PORT
#define TMS_PORT     JTAG_PORT
#define TCK_PORT     JTAG_PORT
#define TMS_DIR_PORT JTAG_PORT
#define TDO_PORT     GPIOC
#define TDI_PIN      GPIO3
#define TMS_PIN      GPIO4
#define TMS_DIR_PIN  GPIO1
#define TCK_PIN      GPIO5
#define TDO_PIN      GPIO7

#define SWDIO_PORT     JTAG_PORT
#define SWCLK_PORT     JTAG_PORT
#define SWDIO_DIR_PORT JTAG_PORT
#define SWDIO_PIN      TMS_PIN
#define SWCLK_PIN      TCK_PIN
#define SWDIO_DIR_PIN  TMS_DIR_PIN

#define TRST_PORT       GPIOA
#define TRST_PIN        GPIO2
#define NRST_PORT       GPIOA
#define NRST_PIN        GPIO2
#define NRST_SENSE_PORT GPIOA
#define NRST_SENSE_PIN  GPIO7

#define LED_PORT      GPIOC
#define LED_PORT_UART GPIOB
#define LED_UART      GPIO2
#define LED_IDLE_RUN  GPIO6
#define LED_ERROR     GPIO8
#define LED_MODE      GPIO9

#define SWITCH_PORT       GPIOB
#define SW_BOOTLOADER_PIN GPIO12

#define TPWR_PORT   GPIOB
#define TPWR_PIN    GPIO0
#define VBAT_PORT   GPIOA
#define VBAT_PIN    GPIO0
#define PWR_BR_PORT GPIOB
#define PWR_BR_PIN  GPIO1

/* USB pin definitions */
#define USB_PU_PORT GPIOA
#define USB_PORT    GPIOA
#define USB_PU_PIN  GPIO8
#define USB_DP_PIN  GPIO12
#define USB_DM_PIN  GPIO11

#define USBUSART               USART1
#define USBUSART_CR1           USART1_CR1
#define USBUSART_DR            USART1_DR
#define USBUSART_IRQ           NVIC_USART1_IRQ
#define USBUSART_CLK           RCC_USART1
#define USBUSART_PORT          GPIOB
#define USBUSART_TX_PIN        GPIO6
#define USBUSART_RX_PIN        GPIO7
#define USBUSART_ISR(x)        usart1_isr(x)
#define USBUSART_DMA_BUS       DMA2
#define USBUSART_DMA_CLK       RCC_DMA2
#define USBUSART_DMA_TX_CHAN   DMA_STREAM7
#define USBUSART_DMA_TX_IRQ    NVIC_DMA2_STREAM7_IRQ
#define USBUSART_DMA_TX_ISR(x) dma2_stream7_isr(x)
#define USBUSART_DMA_RX_CHAN   DMA_STREAM2
#define USBUSART_DMA_RX_IRQ    NVIC_DMA2_STREAM2_IRQ
#define USBUSART_DMA_RX_ISR(x) dma2_stream2_isr(x)
/* For STM32F4 DMA trigger source must be specified */
#define USBUSART_DMA_TRG DMA_SxCR_CHSEL_4

#define SWD_CR       GPIO_MODER(SWDIO_PORT)
#define SWD_CR_SHIFT (0x4U << 0x1U)

#define TMS_SET_MODE()                                                        \
	do {                                                                      \
		gpio_set(TMS_DIR_PORT, TMS_DIR_PIN);                                  \
		gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN); \
	} while (0)

#define SWDIO_MODE_FLOAT()                                \
	do {                                                  \
		uint32_t cr = SWD_CR;                             \
		cr &= ~(0x3U << SWD_CR_SHIFT);                    \
		GPIO_BSRR(SWDIO_DIR_PORT) = SWDIO_DIR_PIN << 16U; \
		SWD_CR = cr;                                      \
	} while (0)

#define SWDIO_MODE_DRIVE()                         \
	do {                                           \
		uint32_t cr = SWD_CR;                      \
		cr &= ~(0x3U << SWD_CR_SHIFT);             \
		cr |= (0x1U << SWD_CR_SHIFT);              \
		GPIO_BSRR(SWDIO_DIR_PORT) = SWDIO_DIR_PIN; \
		SWD_CR = cr;                               \
	} while (0)

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

/* Use TIM3 Input 2 (from PC7/TDO) AF2, trigger on Rising Edge */
#define TRACE_TIM          TIM3
#define TRACE_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM3)
#define TRACE_IRQ          NVIC_TIM3_IRQ
#define TRACE_ISR(x)       tim3_isr(x)
#define TRACE_IC_IN        TIM_IC_IN_TI2
#define TRACE_TRIG_IN      TIM_SMCR_TS_TI1FP1
#define TRACE_TIM_PIN_AF   GPIO_AF2

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

#endif /* PLATFORMS_CTXLINK_PLATFORM_H */
