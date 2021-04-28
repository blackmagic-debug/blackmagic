/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011-2021 Black Sphere Technologies Ltd.
 * Portions (C) 2020-2021 Stoyan Shopov <stoyan.shopov@gmail.com>
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

#include <libopencm3/cm3/common.h>
#include <libopencm3/stm32/f1/memorymap.h>
#include <libopencm3/usb/usbd.h>

#ifdef ENABLE_DEBUG
# define PLATFORM_HAS_DEBUG
# define USBUART_DEBUG
extern bool debug_bmp;
int usbuart_debug_write(const char *buf, size_t len);
#endif

#define PLATFORM_IDENT    "STLINK-V3 "

#define BOOTMAGIC0 0xb007da7a
#define BOOTMAGIC1 0xbaadfeed

#define DESIG_UNIQUE_ID_BASE DESIG_UNIQUE_ID_BASE_452

/* Hardware definitions... */
#define TDI_PORT	GPIOA
#define TMS_PORT	GPIOF
#define TCK_PORT	GPIOH
#define TDO_PORT	GPIOD
#define TDI_PIN		GPIO1
#define TMS_PIN		GPIO9
#define TCK_PIN		GPIO6
#define TDO_PIN		GPIO2

#define SWDIO_PORT 	TMS_PORT
#define SWCLK_PORT 	TCK_PORT
#define SWDIO_PIN	TMS_PIN
#define SWCLK_PIN	TCK_PIN

#define SRST_PORT	GPIOA
#define SRST_PIN	GPIO6

#define PLATFORM_HAS_TRACESWO		1
#define NUM_TRACE_PACKETS		(16)
#define TRACESWO_PROTOCOL		2			/* 1 = Manchester, 2 = NRZ / async */

#define PLATFORM_HAS_SLCAN		1

#define SWDIO_MODER   GPIO_MODER(TMS_PORT)
#define SWDIO_MODER_MULT (1 << (9 << 1))

#define TMS_SET_MODE()\
	gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN);	\
	gpio_set_output_options(TMS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TMS_PIN);

#define SWDIO_MODE_FLOAT()	do {				\
		uint32_t moder = SWDIO_MODER;			\
		moder &= ~(0x3 * SWDIO_MODER_MULT);		\
		SWDIO_MODER = moder;					\
	} while(0)

#define SWDIO_MODE_DRIVE()   do {				\
		uint32_t moder = SWDIO_MODER;			\
		moder |= (1 * SWDIO_MODER_MULT);		\
		SWDIO_MODER = moder;					\
	} while(0)

#define PIN_MODE_FAST()  do {											\
		gpio_set_output_options(TMS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, TMS_PIN); \
		gpio_set_output_options(TCK_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, TCK_PIN); \
		gpio_set_output_options(TDO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, TDO_PIN); \
	} while(0)

#define PIN_MODE_NORMAL() do {											\
		gpio_set_output_options(TMS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TMS_PIN); \
		gpio_set_output_options(TCK_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TCK_PIN); \
		gpio_set_output_options(TDO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TDO_PIN);	\
	} while(0)

extern const struct _usbd_driver stm32f723_usb_driver;
#define USB_DRIVER      stm32f723_usb_driver
#define USB_IRQ	        NVIC_OTG_HS_IRQ
#define USB_ISR	        otg_hs_isr
#define MAX_BINTERVAL   11
//#define CDCACM_PACKET_SIZE 512 /* Fixme: Needs more consideratiobs*/
#define TRACE_ENDPOINT_SIZE 512

/* Interrupt priorities.  Low numbers are high priority.
 * For now USART2 preempts USB which may spin while buffer is drained.
 */
#define IRQ_PRI_USB		     (1 << 4)
#define IRQ_PRI_USBUSART	 (2 << 4)
#define IRQ_PRI_USBUSART_DMA (2 << 4)
#define IRQ_PRI_USB_VBUS	(14 << 4)
#define IRQ_PRI_SWO_DMA			(0 << 4)

/* USART6 is on DMA2 Channel 5 , Rx Stream 1/2, TX Stream 6/7*/
#define USBUSART USART6
#define USBUSART_BASE USART6_BASE

#define USBUSART_CR1 USART_CR1(USBUSART_BASE)
#define USBUSART_RDR USART_RDR(USBUSART_BASE)
#define USBUSART_TDR USART_TDR(USBUSART_BASE)
#define USBUSART_IRQ NVIC_USART6_IRQ
#define USBUSART_CLK RCC_USART6
#define USBUSART_PORT GPIOG
#define USBUSART_PIN_AF        GPIO_AF8
#define USBUSART_PORT_CLKEN RCC_GPIOG
#define USBUSART_TX_PIN GPIO14
#define USBUSART_RX_PIN GPIO9
#define USBUSART_ISR usart6_isr

#define USBUSART_DMA_BUS DMA2
#define USBUSART_DMA_CLK RCC_DMA2
#define USBUSART_DMA_TX_CHAN DMA_STREAM6
#define USBUSART_DMA_TX_IRQ NVIC_DMA2_STREAM6_IRQ
#define USBUSART_DMA_TX_ISR(x) dma2_stream6_isr(x)
#define USBUSART_DMA_RX_CHAN DMA_STREAM2
#define USBUSART_DMA_RX_IRQ NVIC_DMA2_STREAM2_IRQ
#define USBUSART_DMA_RX_ISR(x) dma2_stream2_isr(x)

/* For STM32F7 DMA trigger source must be specified */
#define USBUSART_DMA_TRG DMA_SxCR_CHSEL_5

#define UART_PIN_SETUP() do { \
	rcc_periph_clock_enable(USBUSART_PORT_CLKEN); \
	gpio_mode_setup(USBUSART_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, USBUSART_TX_PIN | USBUSART_RX_PIN);\
	gpio_set_output_options(USBUSART_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, USBUSART_TX_PIN);\
	gpio_set_af(USBUSART_PORT, USBUSART_PIN_AF, USBUSART_TX_PIN | USBUSART_RX_PIN);\
} while (0)


#define SWO_UART			UART5
#define SWO_UART_DR			UART5_RDR
#define SWO_UART_CLK			RCC_UART5
#define SWO_UART_PORT			GPIOD
#define SWO_UART_RX_PIN			GPIO2
#define SWO_UART_PIN_AF			GPIO_AF8

/* This DMA channel is set by the USART in use */
#define SWO_DMA_BUS			DMA1
#define SWO_DMA_CLK			RCC_DMA1
#define SWO_DMA_CHAN			DMA_CHANNEL4
#define SWO_DMA_STREAM			DMA_STREAM0
#define SWO_DMA_IRQ			NVIC_DMA1_STREAM0_IRQ
#define SWO_DMA_ISR(x)			dma1_stream0_isr(x)

extern uint16_t led_idle_run;
#define LED_RG_PORT	GPIOA
#define LED_RG_PIN  GPIO10
#define LED_PORT	GPIOA
#define LED_PORT_UART	GPIOA
#define LED_UART	GPIO10

#define LED_IDLE_RUN            led_idle_run
#define SET_RUN_STATE(state)
#define SET_IDLE_STATE(state)
#define SET_ERROR_STATE(x)

extern uint32_t detect_rev(void);

/*
 * Use newlib provided integer only stdio functions
 */

/* sscanf */
#ifdef sscanf
#undef sscanf
#define sscanf siscanf
#else
#define sscanf siscanf
#endif
/* sprintf */
#ifdef sprintf
#undef sprintf
#define sprintf siprintf
#else
#define sprintf siprintf
#endif
/* vasprintf */
#ifdef vasprintf
#undef vasprintf
#define vasprintf vasiprintf
#else
#define vasprintf vasiprintf
#endif
/* snprintf */
#ifdef snprintf
#undef snprintf
#define snprintf sniprintf
#else
#define snprintf sniprintf
#endif


#endif
