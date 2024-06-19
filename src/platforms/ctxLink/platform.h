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

#if ENABLE_DEBUG == 1
#define PLATFORM_HAS_DEBUG
extern bool debug_bmp;
#endif

#define PLATFORM_IDENT   ""
#define UPD_IFACE_STRING "@Internal Flash   /0x08000000/8*001Kg"

/*
 * Important pin mappings for ctxLink implementation:
 *
 * LED0     = PB2   (Yellow LED : Running)
 * LED1     = PC6   (Orange LED : Idle)
 * LED2     = PC8   (Red LED    : Error)
 * LED3		= PC9	(Gree LED   : ctxLink Mode)	
 *
 * TPWR     = PB0  (input)  -- analogue on mini design ADC1, CH8
 * PWR_BR   = PB1  (output) [blackmagic_mini] -- supply power to the target, active low
 * TMS_DIR  = PA1  (output) [blackmagic_mini v2.1] -- choose direction of the TCK pin, input low, output high
 * nRST     = PA2  (output)
 * TDI      = PA3  (output)
 * TMS      = PA4  (input/output for SWDIO)
 * TCK      = PA5  (output SWCLK)
 * TDO      = PA6  (input)
 * TRACESWO = PB3  (input)  -- To allow trace decoding using USART1
 * nRST_SNS = PA7  (input)
 *
 * USB_PU   = PA8  (output)
 * USB_VBUS = PA9  (input)
 * 
 * BTN1     = PB12 (input)  -- Force ST System Bootloader when pressed during powerup.
 *
 * UART_TX  = PB6  (output)
 * UART_RX  = PB7 (input)
 *
 * VBAT       = PA0  (input)  -- Battery voltage sense ADC2, CH0
 *
 * nRST_SNS is the nRST sense line
 */

/* Hardware definitions... */
#define JTAG_PORT    GPIOA
#define TDI_PORT     JTAG_PORT
#define TMS_PORT     JTAG_PORT
#define TMS_DIR_PORT JTAG_PORT
#define TCK_PORT     JTAG_PORT
#define TDO_PORT     JTAG_PORT
#define TDI_PIN      GPIO3
#define TMS_DIR_PIN  GPIO1
#define TMS_PIN      GPIO4
#define TCK_PIN      GPIO5
#define TDO_PIN      GPIO6

#define SWDIO_DIR_PORT JTAG_PORT
#define SWDIO_PORT     JTAG_PORT
#define SWCLK_PORT     JTAG_PORT
#define SWDIO_DIR_PIN  TMS_DIR_PIN
#define SWDIO_PIN      TMS_PIN
#define SWCLK_PIN      TCK_PIN

#define TRST_PORT       GPIOA // TODO What is the difference between this and NRST? Seems it may not be used
#define TRST_PIN        GPIO2
#define NRST_PORT       GPIOA
#define NRST_PIN        GPIO2
#define NRST_SENSE_PORT GPIOA
#define NRST_SENSE_PIN  GPIO7

/*
 * These are the control output pin definitions for TPWR.
 * TPWR is sensed via PB0 by sampling ADC1's channel 8.
 */
#define PWR_BR_PORT GPIOB
#define PWR_BR_PIN  GPIO1
#define TPWR_PORT   GPIOB
#define TPWR_PIN    GPIO0

/* USB pin definitions */
#define USB_PU_PORT GPIOA
#define USB_PORT    GPIOA
#define USB_PU_PIN  GPIO8
#define USB_DP_PIN  GPIO12
#define USB_DM_PIN  GPIO11

/* IRQ stays the same for all hw revisions. */
#define USB_VBUS_IRQ NVIC_EXTI9_5_IRQ // TODO This seems new, may need attention

/* For HW Rev 5 and newer */
#define USB_VBUS5_PORT GPIOA
#define USB_VBUS5_PIN  GPIO9

#define LED_PORT       GPIOB
#define LED_PORT_OTHER GPIOC
#define LED_0          GPIO2
#define LED_1          GPIO7
#define LED_2          GPIO8
#define LED_3          GPIO9
#define LED_UART       LED_0
#define LED_IDLE_RUN   LED_1
#define LED_ERROR      LED_2
#define LED_CTX_MODE   LED_3

/* AUX Port HW Rev 5 and newer */
#define AUX_PORT GPIOB

#define AUX_BTN1_PORT     AUX_PORT
#define SWITCH_PORT       AUX_BTN1_PORT
#define AUX_BTN1          GPIO12
#define SW_BOOTLOADER_PIN AUX_BTN1

/* Note that VBat is on PA0, not PB. */
#define AUX_VBAT_PORT GPIOA
#define AUX_VBAT      GPIO0

#define SWD_CR       GPIO_CRL(SWDIO_PORT)
#define SWD_CR_SHIFT (4U << 2U)

#define TMS_SET_MODE()                                                                       \
	do {                                                                                     \
		gpio_set(TMS_DIR_PORT, TMS_DIR_PIN);                                                 \
		gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN); \
	} while (0)

#define SWDIO_MODE_FLOAT()                        \
	do {                                          \
		uint32_t cr = SWD_CR;                     \
		cr &= ~(0xfU << SWD_CR_SHIFT);            \
		cr |= (0x4U << SWD_CR_SHIFT);             \
		GPIO_BRR(SWDIO_DIR_PORT) = SWDIO_DIR_PIN; \
		SWD_CR = cr;                              \
	} while (0)

#define SWDIO_MODE_DRIVE()                         \
	do {                                           \
		uint32_t cr = SWD_CR;                      \
		cr &= ~(0xfU << SWD_CR_SHIFT);             \
		cr |= (0x1U << SWD_CR_SHIFT);              \
		GPIO_BSRR(SWDIO_DIR_PORT) = SWDIO_DIR_PIN; \
		SWD_CR = cr;                               \
	} while (0)

#define UART_PIN_SETUP()                                                                                        \
	do {                                                                                                        \
		gpio_mode_setup(USBUSART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, USBUSART_TX_PIN); \
		gpio_mode_setup(USBUSART_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, USBUSART_RX_PIN);             \
		gpio_set(USBUSART_PORT, USBUSART_RX_PIN);                                                               \
	} while (0)

#define USB_DRIVER st_usbfs_v1_usb_driver
#define USB_IRQ    NVIC_USB_LP_CAN_RX0_IRQ
#define USB_ISR(x) usb_lp_can_rx0_isr(x)
/*
 * Interrupt priorities. Low numbers are high priority.
 * TIM3 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB          (1U << 4U)
#define IRQ_PRI_USBUSART     (2U << 4U)
#define IRQ_PRI_USBUSART_DMA (2U << 4U)
#define IRQ_PRI_USB_VBUS     (14U << 4U)
#define IRQ_PRI_TRACE        (0U << 4U)

#define USBUSART        USBUSART1
#define USBUSART_IRQ    NVIC_USART1_IRQ
#define USBUSART_CLK    RCC_USART1
#define USBUSART_PORT   GPIOB
#define USBUSART_TX_PIN GPIO6
#define USBUSART_RX_PIN GPIO7

#define USBUSART_DMA_BUS     DMA1
#define USBUSART_DMA_CLK     RCC_DMA1
#define USBUSART_DMA_TX_CHAN USBUSART1_DMA_TX_CHAN
#define USBUSART_DMA_RX_CHAN USBUSART1_DMA_RX_CHAN
#define USBUSART_DMA_TX_IRQ  USBUSART1_DMA_TX_IRQ
#define USBUSART_DMA_RX_IRQ  USBUSART1_DMA_RX_IRQ

#define USBUSART1               USART1
#define USBUSART1_IRQ           NVIC_USART1_IRQ
#define USBUSART1_ISR(x)        usart1_isr(x)
#define USBUSART1_DMA_TX_CHAN   DMA_CHANNEL4
#define USBUSART1_DMA_TX_IRQ    NVIC_DMA1_CHANNEL4_IRQ
#define USBUSART1_DMA_TX_ISR(x) dma1_channel4_isr(x)
#define USBUSART1_DMA_RX_CHAN   DMA_CHANNEL5
#define USBUSART1_DMA_RX_IRQ    NVIC_DMA1_CHANNEL5_IRQ
#define USBUSART1_DMA_RX_ISR(x) dma1_channel5_isr(x)

#define USBUSART2               USART2
#define USBUSART2_IRQ           NVIC_USART2_IRQ
#define USBUSART2_ISR(x)        usart2_isr(x)
#define USBUSART2_DMA_TX_CHAN   DMA_CHANNEL7
#define USBUSART2_DMA_TX_IRQ    NVIC_DMA1_CHANNEL7_IRQ
#define USBUSART2_DMA_TX_ISR(x) dma1_channel7_isr(x)
#define USBUSART2_DMA_RX_CHAN   DMA_CHANNEL6
#define USBUSART2_DMA_RX_IRQ    NVIC_DMA1_CHANNEL6_IRQ
#define USBUSART2_DMA_RX_ISR(x) dma1_channel6_isr(x)

#define TRACE_TIM          TIM3
#define TRACE_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM3)
#define TRACE_IRQ          NVIC_TIM3_IRQ
#define TRACE_ISR(x)       tim3_isr(x)

#define SET_RUN_STATE(state)   running_status = (state)
#define SET_IDLE_STATE(state)  gpio_set_val(LED_PORT, LED_IDLE_RUN, state)
#define SET_ERROR_STATE(state) gpio_set_val(LED_PORT, LED_ERROR, state)

/* Frequency constants (in Hz) for the bitbanging routines */
#define BITBANG_CALIBRATED_FREQS
/*
 * The 3 major JTAG bitbanging routines that get called result in these stats for
 * clock frequency being generated with the _no_delay routines:
 * jtag_proc.jtagtap_next(): 705.882kHz
 * jtag_proc.jtagtap_tms_seq(): 4.4MHz
 * jtag_proc.jtagtap_tdi_tdo_seq(): 750kHz
 * The result is an average 1.95MHz achieved.
 */
#define BITBANG_NO_DELAY_FREQ 1951961U
/*
 * On the _swd_delay routines with the delay loops inoperative, we then get:
 * jtag_proc.jtagtap_next(): 626.181kHz
 * jtag_proc.jtagtap_tms_seq(): 2.8MHz
 * jtag_proc.jtagtap_tdi_tdo_seq(): 727.27kHz
 * The result is an average 1.38MHz achieved.
 */
#define BITBANG_0_DELAY_FREQ 1384484U
/*
 * On the _swd_delay routines with the delay set to 1, we then get:
 * jtag_proc.jtagtap_next(): 521.739kHz
 * jtag_proc.jtagtap_tms_seq(): 1.378MHz
 * jtag_proc.jtagtap_tdi_tdo_seq(): 583.624kHz
 * The result is an average 827.788kHz achieved
 */

/*
 * After taking samples with the delay set to 2, 3, and 4 as well, then running
 * a linear regression on the results using the divider calculation tool, we arrive
 * at an offset of 52 for the ratio and a division factor of 30 to produce divider numbers
 */
#define BITBANG_DIVIDER_OFFSET 52U
#define BITBANG_DIVIDER_FACTOR 30U

#endif /* PLATFORMS_CTXLINK_PLATFORM_H */
