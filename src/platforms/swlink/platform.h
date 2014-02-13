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

#include <libopencm3/stm32/f1/gpio.h>
#include <libopencm3/usb/usbd.h>

#include <setjmp.h>
#include <alloca.h>

#include "gdb_packet.h"

#define INLINE_GPIO
#define CDCACM_PACKET_SIZE 	64
#define BOARD_IDENT             "Black Magic Probe (SWLINK), (Firmware 1.5" VERSION_SUFFIX ", build " BUILDDATE ")"
#define BOARD_IDENT_DFU		"Black Magic (Upgrade), STM8S Discovery, (Firmware 1.5" VERSION_SUFFIX ", build " BUILDDATE ")"
#define BOARD_IDENT_UPD		"Black Magic (DFU Upgrade), STM8S Discovery, (Firmware 1.5" VERSION_SUFFIX ", build " BUILDDATE ")"
#define DFU_IDENT               "Black Magic Firmware Upgrade (SWLINK)"
#define DFU_IFACE_STRING	"@Internal Flash   /0x08000000/8*001Ka,56*001Kg"
#define UPD_IFACE_STRING	"@Internal Flash   /0x08000000/8*001Kg"

extern usbd_device *usbdev;
#define CDCACM_GDB_ENDPOINT	1
#define CDCACM_UART_ENDPOINT	3

/* Pin mappings:
 *
 * nTRST = 	PB1
 * TDI = 	PA3
 * TMS = 	PA4 (input for SWDP)
 * TCK = 	PA5
 * TDO = 	PA6 (input)
 * nSRST = 	PA7 (input)
 *
 * USB cable pull-up: PA8
 * USB VBUS detect:  PB13 -- New on mini design.
 *                           Enable pull up for compatibility.
 * Force DFU mode button: PB9 Low
 */

/* Hardware definitions... */
#define TMS_PORT	GPIOA
#define TCK_PORT	GPIOA
#define TDI_PORT	GPIOA
#define TDO_PORT	GPIOB
#define NRST_PORT	GPIOB
#define TMS_PIN		GPIO13
#define TCK_PIN		GPIO14
#define TDI_PIN		GPIO15
#define TDO_PIN		GPIO3
#define NRST_PIN	GPIO4

#define SWDIO_PORT 	TMS_PORT
#define SWCLK_PORT 	TCK_PORT
#define SWDIO_PIN	TMS_PIN
#define SWCLK_PIN	TCK_PIN

#define LED_PORT	GPIOA
/* Use PC14 for a "dummy" uart led. So we can observere at least with scope*/
#define LED_PORT_UART	GPIOC
#define LED_UART	GPIO14

#define TMS_SET_MODE()                                          \
    gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_50_MHZ,            \
                  GPIO_CNF_OUTPUT_PUSHPULL, TMS_PIN);
#define SWDIO_MODE_FLOAT()                              \
    gpio_set_mode(SWDIO_PORT, GPIO_MODE_INPUT,          \
                  GPIO_CNF_INPUT_FLOAT, SWDIO_PIN);
#define SWDIO_MODE_DRIVE()                                              \
    gpio_set_mode(SWDIO_PORT, GPIO_MODE_OUTPUT_50_MHZ,                  \
                  GPIO_CNF_OUTPUT_PUSHPULL, SWDIO_PIN);

#define UART_PIN_SETUP()     do                                         \
    {                                                                   \
        AFIO_MAPR |= AFIO_MAPR_USART1_REMAP;                            \
        gpio_set_mode(USBUSART_PORT, GPIO_MODE_OUTPUT_2_MHZ,            \
                      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, USBUSART_TX_PIN); \
    } while (0)

#define USB_DRIVER      stm32f103_usb_driver
#define USB_IRQ         NVIC_USB_LP_CAN_RX0_IRQ
#define USB_ISR          usb_lp_can_rx0_isr
/* Interrupt priorities.  Low numbers are high priority.
 * For now USART1 preempts USB which may spin while buffer is drained.
 * TIM2 is used for traceswo capture and must be highest priority.
 */
#define IRQ_PRI_USB		(2 << 4)
#define IRQ_PRI_USBUSART	(1 << 4)
#define IRQ_PRI_USBUSART_TIM	(3 << 4)
#define IRQ_PRI_USB_VBUS	(14 << 4)
#define IRQ_PRI_TRACE		(0 << 4)

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

#define led_idle_run GPIO8
#define SET_RUN_STATE(state)	{running_status = (state);}
#define SET_IDLE_STATE(state)	{gpio_set_val(LED_PORT, led_idle_run, state);}

#define PLATFORM_SET_FATAL_ERROR_RECOVERY()	{setjmp(fatal_error_jmpbuf);}
#define PLATFORM_FATAL_ERROR(error)	{ 		\
	if(running_status) gdb_putpacketz("X1D");	\
		else gdb_putpacketz("EFF");		\
	running_status = 0;				\
	target_list_free();				\
	longjmp(fatal_error_jmpbuf, (error));		\
}

int platform_init(void);
void morse(const char *msg, char repeat);
const char *platform_target_voltage(void);
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
static inline void _gpio_set(uint32_t gpioport, uint16_t gpios)
{
	GPIO_BSRR(gpioport) = gpios;
}
#define gpio_set _gpio_set

static inline void _gpio_clear(uint32_t gpioport, uint16_t gpios)
{
	GPIO_BRR(gpioport) = gpios;
}
#define gpio_clear _gpio_clear

static inline uint16_t _gpio_get(uint32_t gpioport, uint16_t gpios)
{
	return (uint16_t)GPIO_IDR(gpioport) & gpios;
}
#define gpio_get _gpio_get
#endif

#endif

void disconnect_usb(void);
void assert_boot_pin(void);
