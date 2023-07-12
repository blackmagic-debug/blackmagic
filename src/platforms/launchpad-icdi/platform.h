/*
 * This file is part of the Black Magic Debug project.
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

#ifndef PLATFORMS_LAUNCHPAD_ICDI_PLATFORM_H
#define PLATFORMS_LAUNCHPAD_ICDI_PLATFORM_H

#include <libopencm3/lm4f/gpio.h>
#include <libopencm3/usb/usbd.h>

#include "timing.h"

#define PLATFORM_IDENT "(Launchpad ICDI) "

extern uint8_t running_status;

#define TMS_PORT GPIOA_BASE
#define TMS_PIN  GPIO3

#define TCK_PORT GPIOA_BASE
#define TCK_PIN  GPIO2

#define TDI_PORT GPIOA_BASE
#define TDI_PIN  GPIO5

#define TDO_PORT GPIOA_BASE
#define TDO_PIN  GPIO4

#define SWO_PORT GPIOD_BASE
#define SWO_PIN  GPIO6

#define SWDIO_PORT TMS_PORT
#define SWDIO_PIN  TMS_PIN

#define SWCLK_PORT TCK_PORT
#define SWCLK_PIN  TCK_PIN

#define NRST_PORT GPIOA_BASE
#define NRST_PIN  GPIO6

#define TMS_SET_MODE()                                                            \
	{                                                                             \
		gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN);     \
		gpio_set_output_config(TMS_PORT, GPIO_OTYPE_PP, GPIO_DRIVE_2MA, TMS_PIN); \
	}

#define SWDIO_MODE_FLOAT()                                                       \
	{                                                                            \
		gpio_mode_setup(SWDIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, SWDIO_PIN); \
	}

#define SWDIO_MODE_DRIVE()                                                            \
	{                                                                                 \
		gpio_mode_setup(SWDIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SWDIO_PIN);     \
		gpio_set_output_config(SWDIO_PORT, GPIO_OTYPE_PP, GPIO_DRIVE_2MA, SWDIO_PIN); \
	}

extern const usbd_driver lm4f_usb_driver;
#define USB_DRIVER lm4f_usb_driver
#define USB_IRQ    NVIC_USB0_IRQ
#define USB_ISR    usb0_isr

#define IRQ_PRI_USB (2 << 4)

#define USBUART     UART0
#define USBUART_CLK RCC_UART0
#define USBUART_IRQ NVIC_UART0_IRQ
#define USBUART_ISR uart0_isr
#define UART_PIN_SETUP()                                                      \
	do {                                                                      \
		periph_clock_enable(RCC_GPIOA);                                       \
		__asm__("nop");                                                       \
		__asm__("nop");                                                       \
		__asm__("nop");                                                       \
		gpio_set_af(GPIOA_BASE, 0x1, GPIO0 | GPIO1);                          \
		gpio_mode_setup(GPIOA_BASE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0); \
		gpio_mode_setup(GPIOA_BASE, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO1);  \
	} while (0)

#define TRACEUART     UART2
#define TRACEUART_CLK RCC_UART2
#define TRACEUART_IRQ NVIC_UART2_IRQ
#define TRACEUART_ISR uart2_isr

#define SET_RUN_STATE(state)      \
	{                             \
		running_status = (state); \
	}
#define SET_IDLE_STATE(state) \
	{                         \
	}
#define SET_ERROR_STATE(state) SET_IDLE_STATE(state)

#define PLATFORM_HAS_TRACESWO

inline static void gpio_set_val(uint32_t port, uint8_t pin, uint8_t val)
{
	gpio_write(port, pin, val == 0 ? 0 : 0xff);
}

inline static uint8_t gpio_get(uint32_t port, uint8_t pin)
{
	return !(gpio_read(port, pin) == 0);
}

#define disconnect_usb()            \
	do {                            \
		usbd_disconnect(usbdev, 1); \
		nvic_disable_irq(USB_IRQ);  \
	} while (0)

#endif /* PLATFORMS_LAUNCHPAD_ICDI_PLATFORM_H */
