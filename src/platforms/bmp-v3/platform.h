/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* This file provides the platform specific declarations for the BMPv3 implementation. */

#ifndef PLATFORMS_BMP_V3_PLATFORM_H
#define PLATFORMS_BMP_V3_PLATFORM_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"

#define PLATFORM_HAS_TRACESWO
#define PLATFORM_MULTI_UART

#define PLATFORM_IDENT "v3 "

/*
 * Important pin mappings for BMPv3:
 *
 * State Indication LEDs:
 * LED0      = PB5  (Yellow LED: Running)
 * LED1      = PB4  (Orange LED: Idle)
 * LED2      = PA10 (Red LED   : Error)
 * LED3      = PA8  (Green LED : Power/Connection state)
 *
 * Host Interface & Misc:
 * USB_VBUS  = PA9
 * USB_D+    = PA12
 * USB_D-    = PA11
 * BTN1      = PA15
 *
 * Target Debug Interface:
 * TPWR_SNS  = PB2
 * TPWR_EN   = PA5
 * nRST      = PH1
 * nRST_SNS  = PH0
 * TCK       = PB13
 * TMS       = PB12
 * TDI       = PB15
 * TDO       = PB14
 * SWCLK     = PB13
 * SWDIO     = PB12
 * SWO       = PA1
 * TCKTDI_EN = PC15
 * TMS_DIR   = PC14
 * SWCLK_DIR = PC15
 * SWDIO_DIR = PC14
 *
 * Target Comms Interface:
 * TXD1      = PA2
 * RXD1      = PA3
 * TXD2      = PB6
 * RXD2      = PB7
 * UART2_DIR = PC13
 *
 * On-Board Flash:
 * FLASH_nCS = PA4
 * FLASH_CLK = PB10
 * FLASH_IO0 = PB1
 * FLASH_IO1 = PB0
 * FLASH_IO2 = PA7
 * FLASH_IO3 = PA6
 *
 * AUX Interface:
 * AUX_SCL   = PB8
 * AUX_SDA   = PB9
 */

/* Hardware definitions... */
#define TCK_PORT     GPIOB
#define TCK_PIN      GPIO13
#define TMS_PORT     GPIOB
#define TMS_PIN      GPIO12
#define TDI_PORT     GPIOB
#define TDI_PIN      GPIO15
#define TDO_PORT     GPIOB
#define TDO_PIN      GPIO14
#define TCK_DIR_PORT GPIOC
#define TCK_DIR_PIN  GPIO15
#define TMS_DIR_PORT GPIOC
#define TMS_DIR_PIN  GPIO14

#define SWCLK_PORT     GPIOB
#define SWDIO_PORT     GPIOB
#define SWCLK_PIN      GPIO13
#define SWDIO_PIN      GPIO12
#define SWCLK_DIR_PORT GPIOC
#define SWCLK_DIR_PIN  GPIO15
#define SWDIO_DIR_PORT GPIOC
#define SWDIO_DIR_PIN  GPIO14

#define EXT_SPI           SPI2
#define EXT_SPI_SCLK_PORT GPIOB
#define EXT_SPI_SCLK_PIN  GPIO13
#define EXT_SPI_CS_PORT   GPIOB
#define EXT_SPI_CS_PIN    GPIO12
#define EXT_SPI_POCI_PORT GPIOB
#define EXT_SPI_POCI_PIN  GPIO14
#define EXT_SPI_PICO_PORT GPIOB
#define EXT_SPI_PICO_PIN  GPIO15

#define NRST_PORT       GPIOH
#define NRST_PIN        GPIO1
#define NRST_SENSE_PORT GPIOH
#define NRST_SENSE_PIN  GPIO0

#define SWO_PORT GPIOA
#define SWO_PIN  GPIO1

#define TPWR_EN_PORT    GPIOA
#define TPWR_EN_PIN     GPIO5
#define TPWR_SENSE_PORT GPIOB
#define TPWR_SENSE_PIN  GPIO2

#define USB_PORT   GPIOA
#define USB_DP_PIN GPIO12
#define USB_DM_PIN GPIO11

#define USB_VBUS_PORT GPIOA
#define USB_VBUS_PIN  GPIO9

#define LED0_PORT         GPIOB
#define LED0_PIN          GPIO5
#define LED1_PORT         GPIOB
#define LED1_PIN          GPIO4
#define LED2_PORT         GPIOA
#define LED2_PIN          GPIO10
#define LED3_PORT         GPIOA
#define LED3_PIN          GPIO8
#define LED_UART_PORT     LED0_PORT
#define LED_UART_PIN      LED0_PIN
#define LED_IDLE_RUN_PORT LED1_PORT
#define LED_IDLE_RUN_PIN  LED1_PIN
#define LED_ERROR_PORT    LED2_PORT
#define LED_ERROR_PIN     LED2_PIN

#define TMS_SET_MODE()                                                        \
	do {                                                                      \
		gpio_set(TMS_DIR_PORT, TMS_DIR_PIN);                                  \
		gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN); \
	} while (0)

#define SWDIO_MODE_FLOAT()                                                       \
	do {                                                                         \
		gpio_clear(SWDIO_DIR_PORT, SWDIO_DIR_PIN);                               \
		gpio_mode_setup(SWDIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SWDIO_PIN); \
	} while (0)

#define SWDIO_MODE_DRIVE()                                                        \
	do {                                                                          \
		gpio_set(SWDIO_DIR_PORT, SWDIO_DIR_PIN);                                  \
		gpio_mode_setup(SWDIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SWDIO_PIN); \
	} while (0)

#define UART_PIN_SETUP()                                                                   \
	do {                                                                                   \
		gpio_set_af(AUX_UART1_PORT, GPIO_AF7, AUX_UART1_TX_PIN | AUX_UART1_RX_PIN);        \
		gpio_mode_setup(AUX_UART1_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, AUX_UART1_TX_PIN);   \
		gpio_mode_setup(AUX_UART1_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, AUX_UART1_RX_PIN); \
		gpio_set_af(AUX_UART2_PORT, GPIO_AF7, AUX_UART2_TX_PIN | AUX_UART2_RX_PIN);        \
		gpio_mode_setup(AUX_UART2_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, AUX_UART2_TX_PIN);   \
		gpio_mode_setup(AUX_UART2_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, AUX_UART2_RX_PIN); \
	} while (0)

#define USB_DRIVER otgfs_usb_driver
#define USB_IRQ    NVIC_USB_IRQ
#define USB_ISR(x) usb_isr(x)
/*
 * Interrupt priorities. Low numbers are high priority.
 * TIM5 is used for SWO capture and must be highest priority.
 */
#define IRQ_PRI_USB          (1U << 4U)
#define IRQ_PRI_AUX_UART     (2U << 4U)
#define IRQ_PRI_AUX_UART_DMA (2U << 4U)
#define IRQ_PRI_USB_VBUS     (14U << 4U)
#define IRQ_PRI_SWO_TIM      (0U << 4U)
#define IRQ_PRI_SWO_DMA      (0U << 4U)

/* PA2/3 as USART2 TX/RX */
#define AUX_UART1        USART2
#define AUX_UART1_CLK    RCC_USART2
#define AUX_UART1_IRQ    NVIC_USART2_IRQ
#define AUX_UART1_ISR(x) usart2_isr(x)
#define AUX_UART1_PORT   GPIOA
#define AUX_UART1_TX_PIN GPIO2
#define AUX_UART1_RX_PIN GPIO3

/* PB6/7 as USART1 TX/RX */
#define AUX_UART2        USART1
#define AUX_UART2_CLK    RCC_USART1
#define AUX_UART2_IRQ    NVIC_USART1_IRQ
#define AUX_UART2_ISR(x) usart1_isr(x)
#define AUX_UART2_PORT   GPIOB
#define AUX_UART2_TX_PIN GPIO6
#define AUX_UART2_RX_PIN GPIO7

#define USBUSART_DMA_BUS       GPDMA1
#define USBUSART_DMA_CLK       RCC_GPDMA1
#define USBUSART_DMA_TX_CHAN   DMA_CHANNEL0
#define USBUSART_DMA_RX_CHAN   DMA_CHANNEL1
#define AUX_UART_DMA_TX_IRQ    NVIC_GPDMA1_CH0_IRQ
#define USBUSART_DMA_TX_ISR(x) gpdma1_ch0_isr(x)
#define AUX_UART_DMA_RX_IRQ    NVIC_GPDMA1_CH1_IRQ
#define USBUSART_DMA_RX_ISR(x) gpdma1_ch1_isr(x)

/* Use TIM5 Input 2 (from PA1/SWO) for Manchester data recovery */
#define SWO_TIM TIM5
#define SWO_TIM_CLK_EN()
#define SWO_TIM_IRQ         NVIC_TIM5_IRQ
#define SWO_TIM_ISR(x)      tim5_isr(x)
#define SWO_IC_IN           TIM_IC_IN_TI2
#define SWO_IC_RISING       TIM_IC2
#define SWO_CC_RISING       TIM5_CCR2
#define SWO_ITR_RISING      TIM_DIER_CC2IE
#define SWO_STATUS_RISING   TIM_SR_CC2IF
#define SWO_IC_FALLING      TIM_IC1
#define SWO_CC_FALLING      TIM5_CCR1
#define SWO_STATUS_FALLING  TIM_SR_CC1IF
#define SWO_STATUS_OVERFLOW (TIM_SR_CC1OF | TIM_SR_CC2OF)
#define SWO_TRIG_IN         TIM_SMCR_TS_TI2FP2
#define SWO_TIM_PIN_AF      GPIO_AF2

/* Use PA1 (UART4) for UART/NRZ/Async data recovery */
#define SWO_UART        USART4
#define SWO_UART_CLK    RCC_UART4
#define SWO_UART_DR     USART4_RDR
#define SWO_UART_PORT   SWO_PORT
#define SWO_UART_RX_PIN SWO_PIN
#define SWO_UART_PIN_AF GPIO_AF8

#define SWO_DMA_BUS    GPDMA1
#define SWO_DMA_CLK    RCC_GPDMA1
#define SWO_DMA_CHAN   DMA_CHANNEL2
#define SWO_DMA_IRQ    NVIC_GPDMA1_CH2_IRQ
#define SWO_DMA_ISR(x) gpdma1_ch2_isr(x)

#define SET_RUN_STATE(state)   running_status = (state)
#define SET_IDLE_STATE(state)  gpio_set_val(LED_IDLE_RUN_PORT, LED_IDLE_RUN_PIN, state)
#define SET_ERROR_STATE(state) gpio_set_val(LED_ERROR_PORT, LED_ERROR_PIN, state)

#endif /* PLATFORMS_BMP_V3_PLATFORM_H */
