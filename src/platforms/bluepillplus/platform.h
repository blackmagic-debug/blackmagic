/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
 * Written by ALTracer <11005378+ALTracer@users.noreply.github.com>
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

/* This file provides the platform specific declarations for the WeActStudio.BluePill-Plus implementation. */

#ifndef PLATFORMS_BLUEPILLPLUS_PLATFORM_H
#define PLATFORMS_BLUEPILLPLUS_PLATFORM_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"

#if ENABLE_DEBUG == 1
#define PLATFORM_HAS_DEBUG
extern bool debug_bmp;
#endif

#define PLATFORM_IDENT "(BluePill-Plus) "
#define PLATFORM_HAS_TRACESWO

/*
 * Important pin mappings for STM32 implementation:
 *   * JTAG/SWD
 *     * PB6: TDI
 *     * PB7: TDO/SWO
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

#define TDI_PORT GPIOB
#define TDI_PIN  GPIO7

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

#define SWD_CR      GPIO_CRH(SWDIO_PORT)
#define SWD_CR_MULT (1U << ((9U - 8U) << 2U))

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

#define TMS_SET_MODE() gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN);

#define NRST_PORT GPIOA
#define NRST_PIN  GPIO6
#define TRST_PORT GPIOA
#define TRST_PIN  GPIO5

/* USB FS device and interrupt priorities */
#define USB_DRIVER st_usbfs_v1_usb_driver
#define USB_IRQ    NVIC_USB_LP_CAN_RX0_IRQ
#define USB_ISR(x) usb_lp_can_rx0_isr(x)

#define IRQ_PRI_USB          (1U << 4U)
#define IRQ_PRI_USBUSART     (2U << 4U)
#define IRQ_PRI_USBUSART_DMA (2U << 4U)
#define IRQ_PRI_SWO_DMA      (0U << 4U)
#define IRQ_PRI_SWO_TIM      (0U << 4U)

/* USART selection: dedicate the faster USART1/PA10 for SWO(NRZ), leaving USART2 for Aux serial */
#define USBUSART        USART2
#define USBUSART_CR1    USART2_CR1
#define USBUSART_DR     USART2_DR
#define USBUSART_IRQ    NVIC_USART2_IRQ
#define USBUSART_CLK    RCC_USART2
#define USBUSART_ISR(x) usart2_isr(x)
#define USBUSART_PORT   GPIOA
#define USBUSART_TX_PIN GPIO2
#define USBUSART_RX_PIN GPIO3

#define UART_PIN_SETUP()                                                                                        \
	do {                                                                                                        \
		gpio_set_mode(USBUSART_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, USBUSART_TX_PIN); \
		gpio_set_mode(USBUSART_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, USBUSART_RX_PIN);             \
		gpio_set(USBUSART_PORT, USBUSART_RX_PIN);                                                               \
	} while (0)

#define USBUSART_DMA_TX_CHAN   DMA_CHANNEL7
#define USBUSART_DMA_TX_IRQ    NVIC_DMA1_CHANNEL7_IRQ
#define USBUSART_DMA_TX_ISR(x) dma1_channel7_isr(x)
#define USBUSART_DMA_RX_CHAN   DMA_CHANNEL6
#define USBUSART_DMA_RX_IRQ    NVIC_DMA1_CHANNEL6_IRQ
#define USBUSART_DMA_RX_ISR(x) dma1_channel6_isr(x)
#define USBUSART_DMA_BUS       DMA1
#define USBUSART_DMA_CLK       RCC_DMA1

#ifdef PLATFORM_HAS_TRACESWO
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

/* Use TIM4 Input 2 (from PB7/TDO) */
#define SWO_TIM_CLK_EN()
#define SWO_TIM             TIM4
#define SWO_TIM_CLK         RCC_TIM4
#define SWO_TIM_IRQ         NVIC_TIM4_IRQ
#define SWO_TIM_ISR(x)      tim4_isr(x)
#define SWO_IC_IN           TIM_IC_IN_TI2
#define SWO_IC_RISING       TIM_IC1
#define SWO_CC_RISING       TIM4_CCR1
#define SWO_ITR_RISING      TIM_DIER_CC1IE
#define SWO_STATUS_RISING   TIM_SR_CC1IF
#define SWO_IC_FALLING      TIM_IC2
#define SWO_CC_FALLING      TIM4_CCR2
#define SWO_STATUS_FALLING  TIM_SR_CC2IF
#define SWO_STATUS_OVERFLOW (TIM_SR_CC1OF | TIM_SR_CC2OF)
#define SWO_TRIG_IN         TIM_SMCR_TS_TI2FP2

#define SWO_PORT GPIOB
#define SWO_PIN  GPIO7
#endif /* PLATFORM_HAS_TRACESWO */

/* SPI1: PA4/5/6/7 to onboard w25q64 */
#define OB_SPI         SPI1
#define OB_SPI_PORT    GPIOA
#define OB_SPI_SCLK    GPIO5
#define OB_SPI_MISO    GPIO6
#define OB_SPI_MOSI    GPIO7
#define OB_SPI_CS_PORT GPIOA
#define OB_SPI_CS      GPIO4

/* One active-low button labeled "KEY" */
#define USER_BUTTON_PORT GPIOA
#define USER_BUTTON_PIN  GPIO0

/* PB2/BOOT1 has an active-high LED (blue) */
#define LED_PORT       GPIOB
#define LED_IDLE_RUN   GPIO2
#define LED_PORT_ERROR GPIOB
#define LED_ERROR      GPIO10
#define LED_PORT_UART  GPIOB
#define LED_UART       GPIO11

#define SET_RUN_STATE(state)   running_status = (state);
#define SET_IDLE_STATE(state)  gpio_set_val(LED_PORT, LED_IDLE_RUN, state);
#define SET_ERROR_STATE(state) gpio_set_val(LED_PORT_ERROR, LED_ERROR, state);

#define BOOTMAGIC0 UINT32_C(0xb007da7a)
#define BOOTMAGIC1 UINT32_C(0xbaadfeed)

#endif /* PLATFORMS_BLUEPILLPLUS_PLATFORM_H */
