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

#include <libopencm3/stm32/f4/gpio.h>
#include <libopencm3/usb/usbd.h>

#include <setjmp.h>
#include <alloca.h>

#include "gdb_packet.h"

#define INLINE_GPIO
#define CDCACM_PACKET_SIZE 	64
#define PLATFORM_HAS_TRACESWO
#define BOARD_IDENT             "Black Magic Probe (USPS_F407), (Firmware 1.5" VERSION_SUFFIX ", build " BUILDDATE ")"
#define DFU_IDENT               "Black Magic Firmware Upgrade (USPS_F407)"

extern usbd_device *usbdev;
#define CDCACM_GDB_ENDPOINT	1
#define CDCACM_UART_ENDPOINT	3

/* Important pin mappings for STM32 implementation:
 *
 * LED0 = 	PB2

 * TPWR = 	XXX (input) -- analogue on mini design ADC1, ch8
 * nTRST = 	PD0
 * SRST_OUT = 	PD1
 * TDI = 	PA1
 * TMS = 	PA3 (input for SWDP)
 * TCK = 	PA8
 * TDO = 	PB4 (input)
 * nSRST = 	PD2 (input)
 *
 * USB cable pull-up: PA8
 * USB VBUS detect:  PB13 -- New on mini design.
 *                           Enable pull up for compatibility.
 * Force DFU mode button: PB1
 */

/* Hardware definitions... */
#define JTAG_PORT 	GPIOA
#define TDI_PORT	JTAG_PORT
#define TMS_PORT	JTAG_PORT
#define TCK_PORT	JTAG_PORT
#define TDO_PORT	GPIOB
#define TMS_PIN		GPIO13
#define TCK_PIN		GPIO14
#define TDI_PIN		GPIO15
#define TDO_PIN		GPIO3

#define SWDIO_PORT 	JTAG_PORT
#define SWCLK_PORT 	JTAG_PORT
#define SWDIO_PIN	TMS_PIN
#define SWCLK_PIN	TCK_PIN

#define TRST_PORT	TDO_PORT
#define TRST_PIN	GPIO4
#define SRST_PORT	GPIOB
#define SRST_PIN	GPIO4

#define LED_PORT	GPIOB
#define LED_PORT_UART	GPIOB
#define LED_UART	GPIO2

#define TMS_SET_MODE() gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, \
                                       GPIO_PUPD_NONE, TMS_PIN);
#define SWDIO_MODE_FLOAT() gpio_mode_setup(SWDIO_PORT, GPIO_MODE_INPUT, \
                                           GPIO_PUPD_NONE, SWDIO_PIN);

#define SWDIO_MODE_DRIVE() gpio_mode_setup(SWDIO_PORT, GPIO_MODE_OUTPUT, \
                                           GPIO_PUPD_NONE, SWDIO_PIN);


#define USB_DRIVER      stm32f107_usb_driver
#define USB_IRQ         NVIC_OTG_FS_IRQ
#define USB_ISR         otg_fs_isr
/* Interrupt priorities.  Low numbers are high priority.
 * For now USART1 preempts USB which may spin while buffer is drained.
 * TIM3 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB		(2 << 4)
#define IRQ_PRI_USBUSART	(1 << 4)
#define IRQ_PRI_TRACE		(0 << 4)

#define USBUSART USART3
#define USBUSART_CR1 USART3_CR1
#define USBUSART_IRQ NVIC_USART3_IRQ
#define USBUSART_APB_ENR RCC_APB1ENR
#define USBUSART_CLK_ENABLE  RCC_APB1ENR_USART3EN
#define USBUSART_TX_PORT GPIOC
#define USBUSART_TX_PIN  GPIO10
#define USBUSART_RX_PORT GPIOC
#define USBUSART_RX_PIN  GPIO11
#define USBUSART_ISR usart3_isr

#define UART_PIN_SETUP() do {                                           \
        gpio_mode_setup(USBUSART_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, \
                        USBUSART_TX_PIN);                               \
        gpio_mode_setup(USBUSART_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, \
                        USBUSART_RX_PIN);                               \
        gpio_set_af(USBUSART_TX_PORT, GPIO_AF7, USBUSART_TX_PIN);       \
        gpio_set_af(USBUSART_RX_PORT, GPIO_AF7, USBUSART_RX_PIN);       \
    } while(0)

#define TRACE_TIM TIM3
#define TRACE_TIM_CLK_EN() rcc_peripheral_enable_clock(&RCC_APB1ENR, RCC_APB1ENR_TIM3EN)
#define TRACE_IRQ   NVIC_TIM3_IRQ
#define TRACE_ISR   tim3_isr

#define DEBUG(...)

extern uint8_t running_status;
extern volatile uint32_t timeout_counter;

extern jmp_buf fatal_error_jmpbuf;

extern const char *morse_msg;

#define gpio_set_val(port, pin, val) do {	\
	if(val)					\
		gpio_set((port), (pin));	\
	else					\
		gpio_clear((port), (pin));	\
} while(0)

#define SET_RUN_STATE(state)	{running_status = (state);}
#define SET_IDLE_STATE(state)
#define SET_ERROR_STATE(state)

#define PLATFORM_SET_FATAL_ERROR_RECOVERY()	{setjmp(fatal_error_jmpbuf);}
#define PLATFORM_FATAL_ERROR(error)	{ 		\
	if(running_status) gdb_putpacketz("X1D");	\
		else gdb_putpacketz("EFF");		\
	running_status = 0;				\
	target_list_free();				\
	morse("TARGET LOST.", 1);			\
	longjmp(fatal_error_jmpbuf, (error));		\
}

int platform_init(void);
void morse(const char *msg, char repeat);
const char *platform_target_voltage(void);
int platform_hwversion(void);
void platform_delay(uint32_t delay);

/* <cdcacm.c> */
void cdcacm_init(void);
/* Returns current usb configuration, or 0 if not configured. */
int cdcacm_get_config(void);
int cdcacm_get_dtr(void);

/* <platform.h> */
void uart_usb_buf_drain(uint8_t ep);

/* Use newlib provided integer only stdio functions */
#define sscanf siscanf
#define sprintf siprintf
#define vasprintf vasiprintf

#ifdef INLINE_GPIO
static inline void _gpio_set(u32 gpioport, u16 gpios)
{
	GPIO_BSRR(gpioport) = gpios;
}
#define gpio_set _gpio_set

static inline void _gpio_clear(u32 gpioport, u16 gpios)
{
	GPIO_BSRR(gpioport) = gpios<<16;
}
#define gpio_clear _gpio_clear

static inline u16 _gpio_get(u32 gpioport, u16 gpios)
{
	return (u16)GPIO_IDR(gpioport) & gpios;
}
#define gpio_get _gpio_get
#endif

#endif

#define disconnect_usb() do {usbd_disconnect(usbdev,1); nvic_disable_irq(USB_IRQ);} while(0)
void assert_boot_pin(void);
#define setup_vbus_irq()
