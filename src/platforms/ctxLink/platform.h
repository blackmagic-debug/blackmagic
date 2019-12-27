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

/* This file implements the platform specific functions for the STM32
 * implementation.
 */
#ifndef __PLATFORM_H
#define __PLATFORM_H

#include "gpio.h"
#include "timing.h"
#include "timing_stm32.h"
#include "version.h"

#include "ctxLink_mode_led.h"

#include <setjmp.h>

//
// Define the following symbol to disable the Mode LED
// and allow it to be used for instrumentation
//
//#define	INSTRUMENT	1

#define PLATFORM_HAS_TRACESWO
#define PLATFORM_HAS_POWER_SWITCH
#define PLATFORM_HAS_BATTERY
#ifdef ENABLE_DEBUG
#define PLATFORM_HAS_DEBUG
#define USBUART_DEBUG
#endif

#define BOARD_IDENT       "CtxLink - Wireless Debug Probe, (Firmware " FIRMWARE_VERSION ")"
// #define BOARD_IDENT_DFU   "ctxLink (Upgrade), (Firmware " FIRMWARE_VERSION ")"
#define DFU_IDENT         "ctxLink Firmware Upgrade ("
// #define DFU_IFACE_STRING  "@Internal Flash   /0x08000000/1*016Ka,3*016Kg,1*064Kg,7*128Kg"

bool platform_check_battery_voltage (void) ;
//
// Define the network name for the probe
//
//	TODO, use part or all of the MAC address to make this unique.
//

#define ctxLink_NetName	"ctxLink_0001"

/* Important pin mappings for ctxLink implementation:
 *
 * LED0 = 	PB2				:	(Blue  LED	: LED_UART)
 * LED1 = 	PC7				:	(Green LED	: Idle)
 * LED2 = 	PC8				:	(Red LED	: Error)
 * LED3 =	PC9				:	(Green LED	: ctxLink Mode)
 *
 * VTGT		= 	PB0 (analog)	ADC1_8 input
 *
 * TMS_DIR	=	PA1			: iTMS_DIR
 * SRST_OUT	= 	PA2			: iRST
 * TDI		= 	PA3			: iTDI
 * TMS		= 	PA4			: iTMS (input/output for SWDIO)
 * TCK		= 	PA5			: iTCK (output SWCLK)
 * TDO		= 	PA6			: iTDO (input for TRACESWO)
 * nSRST	=	PA7			: iRST_SENSE (target reset line sensing)
 *
 * USB cable pull-up: PA8	TODO
 * USB VBUS detect:  PB13 -- New on mini design.  TODO
 *                           Enable pull up for compatibility.
 * Force DFU mode button: PC8
 */

/* Hardware definitions... */
#define JTAG_PORT 	GPIOA
#define TDI_PORT	JTAG_PORT
#define TMS_DIR_PORT	JTAG_PORT
#define TMS_PORT	JTAG_PORT
#define TCK_PORT	JTAG_PORT
#define TDO_PORT	JTAG_PORT
#define TDI_PIN		GPIO3
#define TMS_DIR_PIN	GPIO1
#define TMS_PIN		GPIO4
#define TCK_PIN		GPIO5
#define TDO_PIN		GPIO6

#define SWDIO_DIR_PORT	JTAG_PORT
#define SWDIO_PORT 	JTAG_PORT
#define SWCLK_PORT 	JTAG_PORT
#define SWDIO_DIR_PIN	TMS_DIR_PIN
#define SWDIO_PIN	TMS_PIN
#define SWCLK_PIN	TCK_PIN
//#define TRST_PORT	GPIOA
//#define TRST_PIN	GPIO10
#define PWR_BR_PORT		GPIOB
#define PWR_BR_PIN		GPIO1
#define SRST_PORT		GPIOA
#define SRST_PIN		GPIO2
#define SRST_SENSE_PORT	GPIOA
#define SRST_SENSE_PIN	GPIO7

#define USB_PU_PORT	GPIOA
#define USB_PU_PIN	GPIO8

#define USB_VBUS_PORT	GPIOB
#define USB_VBUS_PIN	GPIO13
#define USB_VBUS_IRQ	NVIC_EXTI15_10_IRQ

#define LED_PORT		GPIOC
#define LED_PORT_UART	GPIOB
#define LED_0		GPIO2
#define LED_1		GPIO7
#define LED_2		GPIO8
#define	LED_3		GPIO9
#define LED_UART	LED_0
#define LED_IDLE_RUN	LED_1
#define LED_ERROR	LED_2
#define LED_MODE	LED_3
//
// SJP added definitions for the bootloader switch input port and pin
//
// platform.c also updated to use these definitions
//
#define SWITCH_PORT	GPIOB
#define SW_BOOTLOADER_PIN	GPIO12
//
// Use the UART Led as a probe foe debug
// 
#define PROBE_PIN gpio_toggle (LED_PORT_UART, LED_UART)

//
// Target voltage input
//
#define VTGT_PORT	GPIOB	
#define VTGT_PIN	GPIO0
//
// Battery monitor input
//
#define VBAT_PORT	GPIOA
#define	VBAT_PIN	GPIO0

#define TMS_SET_MODE() \
	gpio_set(TMS_DIR_PORT, TMS_DIR_PIN); \
    gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, \
                    GPIO_PUPD_NONE, TMS_PIN); 

#define SWDIO_MODE_FLOAT() \
    gpio_mode_setup(SWDIO_PORT, GPIO_MODE_INPUT, \
                    GPIO_PUPD_NONE, SWDIO_PIN); \
					gpio_clear(SWDIO_DIR_PORT, SWDIO_DIR_PIN);
 
#define SWDIO_MODE_DRIVE() \
	gpio_set(SWDIO_DIR_PORT, SWDIO_DIR_PIN); \
    gpio_mode_setup(SWDIO_PORT, GPIO_MODE_OUTPUT, \
                    GPIO_PUPD_NONE, SWDIO_PIN); //
#define UART_PIN_SETUP() do { \
	gpio_mode_setup(USBUSART_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, \
	                USBUSART_TX_PIN); \
	gpio_mode_setup(USBUSART_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, \
	                USBUSART_RX_PIN); \
	gpio_set_af(USBUSART_TX_PORT, GPIO_AF7, USBUSART_TX_PIN); \
	gpio_set_af(USBUSART_RX_PORT, GPIO_AF7, USBUSART_RX_PIN); \
    } while(0)

#define USB_DRIVER      stm32f107_usb_driver
#define USB_IRQ         NVIC_OTG_FS_IRQ
#define USB_ISR         otg_fs_isr
/* Interrupt priorities.  Low numbers are high priority.
 * For now USART1 preempts USB which may spin while buffer is drained.
 * TIM3 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB		(2 << 4)
#define IRQ_PRI_USBUSART	(1 << 4)
#define IRQ_PRI_USBUSART_TIM	(3 << 4)
#define IRQ_PRI_TRACE		(0 << 4)

#define USBUSART USART1
#define USBUSART_CR1 USART1_CR1
#define USBUSART_IRQ NVIC_USART1_IRQ
#define USBUSART_CLK RCC_USART1
#define USBUSART_TX_PORT GPIOB
#define USBUSART_TX_PIN  GPIO6

#define USBUSART_RX_PORT GPIOB
#define USBUSART_RX_PIN  GPIO7

#define USBUSART_ISR usart1_isr
#define USBUSART_TIM TIM4
#define USBUSART_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM4)
#define USBUSART_TIM_IRQ NVIC_TIM4_IRQ
#define USBUSART_TIM_ISR tim4_isr


#define TRACE_TIM TIM3
#define TRACE_TIM_CLK_EN() rcc_periph_clock_enable(RCC_TIM3)
#define TRACE_IRQ   NVIC_TIM3_IRQ
#define TRACE_ISR   tim3_isr

#ifdef ENABLE_DEBUG
extern bool debug_bmp;
int usbuart_debug_write(const char *buf, size_t len);

#define DEBUG printf

#define DEBUG_FUNC_ENTRY	DEBUG("Enter %s  [%ld]\n", __func__, platform_time_ms())
#define DEBUG_FUNC_EXIT		DEBUG("Exit %s   [%ld]\n", __func__, platform_time_ms())
#else
#define DEBUG(...)
#endif

#define gpio_set_val(port, pin, val) do {	\
	if(val)					\
		gpio_set((port), (pin));	\
	else					\
		gpio_clear((port), (pin));	\
} while(0)

#define SET_RUN_STATE(state)	{running_status = (state);}
#define SET_IDLE_STATE(state)	{gpio_set_val(LED_PORT, LED_IDLE_RUN, state);}

extern bool wpsActive;
#define SET_ERROR_STATE(state)	{ \
	if ( led_mode == MODE_LED_IDLE ) \
	{ \
			gpio_set_val(LED_PORT, LED_ERROR, state); \
	} \
}

static inline int platform_hwversion(void)
{
	return 3;		/// We are compatible with this version of BMP
}

// Port definitions for WINC1500 wireless module
//
//		The WINC1500 is attached to SPI_2
//
#define WINC1500_SPI_CHANNEL	SPI2
#define WINC1500_RCC_SPI		RCC_SPI2

#define WINC1500_PORT		GPIOB	// Port for CS and IRQ
#define WINC1500_SPI_NCS	GPIO15	// Chip select
#define WINC1500_IRQ		GPIO9	// IRQ input
//
// Reset port and pin
//
#define WINC1500_RESET_PORT		GPIOB
#define WINC1500_RESET			GPIO14	// Reset output
//
// PCB rev 1.4 does not use the WAKE pin of the WINC1500
////
//// Wake port and pin
////
//#define WINC1500_WAKE_PORT		GPIOB
//#define WINC1500_WAKE			GPIO3
//
// Chip enable port and pin
//
#define WINC1500_CHIP_EN_PORT	GPIOB
#define WINC1500_CHIP_EN		GPIO13

//
// SPI clock port
//
#define WINC1500_SPI_CLK_PORT	GPIOB
#define WINC1500_SPI_CLK		GPIO10
//
// SPI Data port
//
#define WINC1500_SPI_DATA_PORT	GPIOC
#define WINC1500_SPI_MISO		GPIO2
#define WINC1500_SPI_MOSI		GPIO3

void platform_tasks(void);  			// Must be called from GDB main loop
const char *platform_battery_voltage (void);
bool platform_has_network_client(uint8_t * lpBuf_rx, uint8_t * lpBuf_rx_in, uint8_t * lpBuf_rx_out, unsigned fifoSize) ;
bool platform_configure_uart (char * configurationString);

#endif

