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

/* This file provides the platform specific declarations for the Blackpillv2 implementation. */

#ifndef PLATFORMS_BLACKPILLV2_PLATFORM_H
#define PLATFORMS_BLACKPILLV2_PLATFORM_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"

#define PLATFORM_HAS_TRACESWO
#define PLATFORM_IDENT "(BlackPillV2) "
/*
 * Important pin mappings for STM32 implementation:
 *   * JTAG/SWD
 *     * PB6: TDI
 *     * PB7: TDO/TRACESWO
 *     * PB8: TCK/SWCLK
 *     * PB9: TMS/SWDIO
 *     * PA6: TRST
 *     * PA5: nRST
 *   * USB USART
 *     * PA2: USART TX
 *     * PA3: USART RX
 *   * +3V3
 *     * PA1: power pin
 *   * Force DFU mode button:
 *     * PA0: user button KEY
 */

/* Hardware definitions... */
#define TDI_PORT GPIOB
#define TDI_PIN  GPIO6

#define TDO_PORT GPIOB
#define TDO_PIN  GPIO7

#define TCK_PORT   GPIOB
#define TCK_PIN    GPIO8
#define SWCLK_PORT TCK_PORT
#define SWCLK_PIN  TCK_PIN

#define TMS_PORT   GPIOB
#define TMS_PIN    GPIO9
#define SWDIO_PORT TMS_PORT
#define SWDIO_PIN  TMS_PIN

#define TRST_PORT GPIOA
#define TRST_PIN  GPIO6

#define NRST_PORT GPIOA
#define NRST_PIN  GPIO5

#define PWR_BR_PORT GPIOA
#define PWR_BR_PIN  GPIO1

#define LED_PORT       GPIOC
#define LED_IDLE_RUN   GPIO13
#define LED_ERROR      GPIO14
#define LED_BOOTLOADER GPIO15

#define LED_PORT_UART GPIOA
#define LED_UART      GPIO4

/* for USART2, DMA1 is selected from https://www.st.com/resource/en/reference_manual/dm00119316-stm32f411xc-e-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf, page 170, table 27.
 * This table defines USART2_TX as stream 6, channel 4, and USART2_RX as stream 5, channel 4.
 */
#define USBUSART               USART2
#define USBUSART_CR1           USART2_CR1
#define USBUSART_DR            USART2_DR
#define USBUSART_IRQ           NVIC_USART2_IRQ
#define USBUSART_CLK           RCC_USART2
#define USBUSART_PORT          GPIOA
#define USBUSART_TX_PIN        GPIO2
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
/* For STM32F4 DMA trigger source must be specified. Channel 4 is selected, in line with the USART selected in the DMA table. */
#define USBUSART_DMA_TRG DMA_SxCR_CHSEL_4

#define BOOTMAGIC0 UINT32_C(0xb007da7a)
#define BOOTMAGIC1 UINT32_C(0xbaadfeed)

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
 * Interrupt priorities.  Low numbers are high priority.
 * TIM3 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB          (1U << 4U)
#define IRQ_PRI_USBUSART     (2U << 4U)
#define IRQ_PRI_USBUSART_DMA (2U << 4U)
#define IRQ_PRI_TRACE        (0U << 4U)

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

static inline int platform_hwversion(void)
{
	return 0;
}

/* Use newlib provided integer-only stdio functions */

#ifdef sscanf
#undef sscanf
#endif
#define sscanf siscanf

#ifdef sprintf
#undef sprintf
#endif
#define sprintf siprintf

#ifdef vasprintf
#undef vasprintf
#endif
#define vasprintf vasiprintf

#ifdef snprintf
#undef snprintf
#endif
#define snprintf sniprintf

#endif /* PLATFORMS_BLACKPILLV2_PLATFORM_H */
