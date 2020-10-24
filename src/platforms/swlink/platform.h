/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2018  Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
#include "version.h"

#ifdef ENABLE_DEBUG
# define PLATFORM_HAS_DEBUG
# define USBUART_DEBUG
extern bool debug_bmp;
int usbuart_debug_write(const char *buf, size_t len);
#endif

#define BOARD_IDENT			"Black Magic Probe (SWLINK), (Firmware " FIRMWARE_VERSION ")"
#define BOARD_IDENT_DFU		"Black Magic (Upgrade), SWLINK, (Firmware " FIRMWARE_VERSION ")"
#define BOARD_IDENT_UPD		"Black Magic (DFU Upgrade), SWLINK, (Firmware " FIRMWARE_VERSION ")"
#define DFU_IDENT			"Black Magic Firmware Upgrade (SWLINK)"
#define UPD_IFACE_STRING	"@Internal Flash   /0x08000000/8*001Kg"

/* Hardware definitions... */
#define TMS_PORT	GPIOA
#define TCK_PORT	GPIOA
#define TDI_PORT	GPIOA
#define TDO_PORT	GPIOB
#define JRST_PORT	GPIOB
#define TMS_PIN		GPIO13
#define TCK_PIN		GPIO14
#define TDI_PIN		GPIO15
#define TDO_PIN		GPIO3
#define JRST_PIN	GPIO4

#define SWDIO_PORT 	TMS_PORT
#define SWCLK_PORT 	TCK_PORT
#define SWDIO_PIN	TMS_PIN
#define SWCLK_PIN	TCK_PIN

/* Use PC14 for a "dummy" uart led. So we can observere at least with scope*/
#define LED_PORT_UART	GPIOC
#define LED_UART	GPIO14

#define PLATFORM_HAS_TRACESWO	1
#define NUM_TRACE_PACKETS		(128)		/* This is an 8K buffer */
#define TRACESWO_PROTOCOL		2			/* 1 = Manchester, 2 = NRZ / async */

# define SWD_CR   GPIO_CRH(SWDIO_PORT)
# define SWD_CR_MULT (1 << ((13 - 8) << 2))

#define TMS_SET_MODE() \
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_2_MHZ, \
	              GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN);
#define SWDIO_MODE_FLOAT() 	do { \
	uint32_t cr = SWD_CR; \
	cr  &= ~(0xf * SWD_CR_MULT); \
	cr  |=  (0x4 * SWD_CR_MULT); \
	SWD_CR = cr; \
} while(0)
#define SWDIO_MODE_DRIVE() 	do { \
	uint32_t cr = SWD_CR; \
	cr  &= ~(0xf * SWD_CR_MULT); \
	cr  |=  (0x1 * SWD_CR_MULT); \
	SWD_CR = cr; \
} while(0)
#define UART_PIN_SETUP() do { \
	AFIO_MAPR |= AFIO_MAPR_USART1_REMAP; \
	gpio_set_mode(USBUSART_PORT, GPIO_MODE_OUTPUT_2_MHZ, \
	              GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, USBUSART_TX_PIN); \
} while (0)

#define USB_DRIVER      st_usbfs_v1_usb_driver
#define USB_IRQ         NVIC_USB_LP_CAN_RX0_IRQ
#define USB_ISR         usb_lp_can_rx0_isr
/* Interrupt priorities.  Low numbers are high priority.
 * For now USART1 preempts USB which may spin while buffer is drained.
 * TIM2 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB		(2 << 4)
#define IRQ_PRI_USBUSART	(1 << 4)
#define IRQ_PRI_USBUSART_TIM	(3 << 4)
#define IRQ_PRI_USB_VBUS	(14 << 4)
#define IRQ_PRI_SWO_DMA		(0 << 4)

#define USBUSART USART1
#define USBUSART_CR1 USART1_CR1
#define USBUSART_IRQ NVIC_USART1_IRQ
#define USBUSART_CLK RCC_USART1
#define USBUSART_PORT GPIOB
#define USBUSART_TX_PIN GPIO6
#define USBUSART_ISR usart1_isr
#define USBUSART_TIM TIM4
#define USBUSART_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM4)
#define USBUSART_TIM_IRQ NVIC_TIM4_IRQ
#define USBUSART_TIM_ISR tim4_isr

#define TRACE_TIM TIM2
#define TRACE_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM2)
#define TRACE_IRQ   NVIC_TIM2_IRQ
#define TRACE_ISR   tim2_isr
#define TRACE_IC_IN TIM_IC_IN_TI2
#define TRACE_TRIG_IN TIM_SMCR_TS_IT1FP2

/* On F103, only USART1 is on AHB2 and can reach 4.5 MBaud at 72 MHz.
 * USART1 is already used. sp maximum speed is 2.25 MBaud. */
#define SWO_UART				USART2
#define SWO_UART_DR				USART2_DR
#define SWO_UART_CLK			RCC_USART2
#define SWO_UART_PORT			GPIOA
#define SWO_UART_RX_PIN			GPIO3

/* This DMA channel is set by the USART in use */
#define SWO_DMA_BUS				DMA1
#define SWO_DMA_CLK				RCC_DMA1
#define SWO_DMA_CHAN			DMA_CHANNEL6
#define SWO_DMA_IRQ				NVIC_DMA1_CHANNEL6_IRQ
#define SWO_DMA_ISR(x)			dma1_channel6_isr(x)

#define LED_PORT GPIOC
#define LED_IDLE_RUN GPIO15
#define SET_RUN_STATE(state)
#define SET_ERROR_STATE(state)
extern void set_idle_state(int state);
#define SET_IDLE_STATE(state) set_idle_state(state)

extern uint8_t detect_rev(void);

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

