/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * Updates for ctxLink Copyright (C) 2024 Sid Price - sid@sidprice.com
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
#define PLATFORM_HAS_WIFI

#define PLATFORM_IDENT "(ctxLink) "

#define PLATFORM_HAS_BATTERY

#if ENABLE_DEBUG == 1
#define PLATFORM_HAS_DEBUG
extern bool debug_bmp;
#endif

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
 * USB_PU   = PA8   (output)
 * TDI =      PA3	(output)
 * TMS =      PA4	(input/output for SWDIO)
 * TCK =      PA5	(output SWCLK)
 * TDO =      PC7	(input SWO)
 * TMS_DIR = PA1	(output) controls target buffer direction
 * nRST_SNS = PA7   (input)

 * TPWR =	 PB0	(analog input)
 * VBAT =	 PA0	(analog input)
 *
 * SW_BOOTLOADER	PB12	(input) System Bootloader button
 */

//
// Define the network name for the probe
//
//	TODO, use part or all of the MAC address to make this unique.
//

#define CTXLINK_NETWORK_NAME "ctxLink_0001"

// Port definitions for WINC1500 wireless module
//
//		The WINC1500 is attached to SPI_2
//
#define WINC1500_SPI_CHANNEL SPI2
#define WINC1500_RCC_SPI     RCC_SPI2

#define WINC1500_PORT    GPIOB  // Port for CS and IRQ
#define WINC1500_SPI_NCS GPIO15 // Chip select
#define WINC1500_IRQ     GPIO9  // IRQ input
//
// Reset port and pin
//
#define WINC1500_RESET_PORT GPIOB
#define WINC1500_RESET      GPIO14 // Reset output

//
// Chip enable port and pin
//
#define WINC1500_CHIP_EN_PORT GPIOB
#define WINC1500_CHIP_EN      GPIO13

//
// SPI clock port
//
#define WINC1500_SPI_CLK_PORT GPIOB
#define WINC1500_SPI_CLK      GPIO10
//
// SPI Data port
//
#define WINC1500_SPI_DATA_PORT GPIOC
#define WINC1500_SPI_MISO      GPIO2
#define WINC1500_SPI_MOSI      GPIO3

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

#define SWO_PORT GPIOC
#define SWO_PIN  GPIO7

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

//
// SWO UART definitions
//
#define SWO_UART        USART6
#define SWO_UART_CR1    USART6_CR1
#define SWO_UART_DR     USART6_DR
#define SWO_UART_CLK    RCC_USART6
#define SWO_UART_PORT   GPIOC
#define SWO_UART_RX_PIN GPIO7
#define SWO_UART_ISR    usart6_isr
#define SWO_UART_IRQ    NVIC_USART6_IRQ

/* USB pin definitions */
#define USB_PU_PORT GPIOA
#define USB_PORT    GPIOA
#define USB_PU_PIN  GPIO8
#define USB_DP_PIN  GPIO12
#define USB_DM_PIN  GPIO11

/*
 * To use USART1 as USBUSART, DMA2 is selected from RM0368, page 170, table 29.
 * This table defines USART1_TX as stream 7, channel 4, and USART1_RX as stream 2, channel 4.
 * Because USART1 is on APB2 with max Pclk of 84 MHz,
 * reachable baudrates are up to 10.5M with OVER8 or 5.25M with default OVER16 (per DocID025644 Rev3, page 30, table 6)
 */
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
#define IRQ_PRI_SWO_TIM      (3U << 4U)
#define IRQ_PRI_SWO_DMA      (1U << 4U)

/* Use TIM3 Input 2 from PC7/TDO, AF2, trigger on rising edge */
#define SWO_TIM             TIM3
#define SWO_TIM_CLK_EN()    rcc_periph_clock_enable(RCC_TIM3)
#define SWO_TIM_IRQ         NVIC_TIM3_IRQ
#define SWO_TIM_ISR(x)      tim3_isr(x)
#define SWO_IC_IN           TIM_IC_IN_TI2
#define SWO_IC_RISING       TIM_IC2
#define SWO_CC_RISING       TIM3_CCR2
#define SWO_ITR_RISING      TIM_DIER_CC2IE
#define SWO_STATUS_RISING   TIM_SR_CC2IF
#define SWO_IC_FALLING      TIM_IC1
#define SWO_CC_FALLING      TIM3_CCR1
#define SWO_STATUS_FALLING  TIM_SR_CC1IF
#define SWO_STATUS_OVERFLOW (TIM_SR_CC1OF | TIM_SR_CC2OF)
#define SWO_TRIG_IN         TIM_SMCR_TS_TI2FP2
#define SWO_TIM_PIN_AF      GPIO_AF2

/* On ctxLink use USART6 RX mapped on PC7 for async capture */
#define SWO_UART        USART6
#define SWO_UART_CLK    RCC_USART6
#define SWO_UART_DR     USART6_DR
#define SWO_UART_PORT   GPIOC
#define SWO_UART_RX_PIN GPIO7
#define SWO_UART_PIN_AF GPIO_AF8

/* Bind to the same DMA Rx channel */
#define SWO_DMA_BUS  DMA2
#define SWO_DMA_CLK  RCC_DMA2
#define SWO_DMA_CHAN DMA_STREAM1
#define SWO_DMA_IRQ  NVIC_DMA2_STREAM1_IRQ
#define SWO_DMA_ISR  dma2_stream1_isr
#define SWO_DMA_TRG  DMA_SxCR_CHSEL_5

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

void platform_tasks(void);
const char *platform_battery_voltage(void);
bool platform_check_battery_voltage(void);
bool platform_configure_uart(char *configuration_string);
void platform_read_adc(void);
const char *platform_wifi_state(int argc, const char **argv);
#endif /* PLATFORMS_CTXLINK_PLATFORM_H */
