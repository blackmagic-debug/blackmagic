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

#include "general.h"
#include "platform.h"
#include "gdb_if.h"
#include "usb.h"
#include "aux_serial.h"

#include <libopencm3/lm4f/rcc.h>
#include <libopencm3/lm4f/nvic.h>
#include <libopencm3/lm4f/uart.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/lm4f/usb.h>

#define PLL_DIV_80MHZ 5
#define PLL_DIV_25MHZ 16

extern void trace_tick(void);

char serial_no[DFU_SERIAL_LENGTH];
uint8_t running_status;
static volatile uint32_t time_ms;

uint32_t target_clk_divider = 0;

void sys_tick_handler(void)
{
	trace_tick();
	time_ms += 10U;
}

uint32_t platform_time_ms(void)
{
	return time_ms;
}

int platform_hwversion(void)
{
	return 0;
}

void platform_init(void)
{
	for (volatile size_t i = 0; i < 1000000U; ++i)
		continue;

	rcc_sysclk_config(OSCSRC_MOSC, XTAL_16M, PLL_DIV_80MHZ);

	/* Enable all JTAG ports and set pins to output */
	periph_clock_enable(RCC_GPIOA);
	periph_clock_enable(RCC_GPIOB);

	gpio_enable_ahb_aperture();

	gpio_mode_setup(TMS_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TMS_PIN);
	gpio_mode_setup(TCK_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TCK_PIN);
	gpio_mode_setup(TDI_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TDI_PIN);
	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TDO_PIN);
	gpio_mode_setup(NRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, NRST_PIN);
	gpio_set_output_config(NRST_PORT, GPIO_OTYPE_OD, GPIO_DRIVE_2MA, NRST_PIN);
	gpio_set(NRST_PORT, NRST_PIN);

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(rcc_get_system_clock_frequency() / (SYSTICKHZ * 8U));

	systick_interrupt_enable();
	systick_counter_enable();

	nvic_enable_irq(NVIC_SYSTICK_IRQ);
	nvic_enable_irq(NVIC_UART0_IRQ);

	periph_clock_enable(RCC_GPIOD);
	__asm__("nop");
	__asm__("nop");
	__asm__("nop");
	gpio_mode_setup(GPIOD_BASE, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO4 | GPIO5);
	blackmagic_usb_init();
	aux_serial_init();

	usb_enable_interrupts(USB_INT_RESET | USB_INT_DISCON | USB_INT_RESUME | USB_INT_SUSPEND, 0xff, 0xff);
}

void platform_nrst_set_val(bool assert)
{
	if (assert) {
		gpio_clear(NRST_PORT, NRST_PIN);
		for (volatile size_t i = 0; i < 10000U; ++i)
			continue;
	} else
		gpio_set(NRST_PORT, NRST_PIN);
}

bool platform_nrst_get_val(void)
{
	return gpio_get(NRST_PORT, NRST_PIN) == 0;
}

void platform_delay(uint32_t ms)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, ms);
	while (!platform_timeout_is_expired(&timeout))
		continue;
}

const char *platform_target_voltage(void)
{
	return NULL;
}

void read_serial_number(void)
{
	/* FIXME: Store a unique serial number somewhere and retrieve here */
	uint32_t unique_id = SERIAL_NO;

	/* Fetch serial number from chip's unique ID */
	for (size_t i = 0; i < DFU_SERIAL_LENGTH - 1U; ++i) {
		serial_no[7U - i] = ((unique_id >> (4U * i)) & 0xfU) + '0';
		if (serial_no[7U - i] > '9')
			serial_no[7U - i] += 7; /* 'A' - '9' = 8, less 1 gives 7. */
	}
	serial_no[DFU_SERIAL_LENGTH - 1U] = 0;
}

void platform_request_boot(void)
{
}

void platform_max_frequency_set(uint32_t freq)
{
	(void)freq;
}

uint32_t platform_max_frequency_get(void)
{
	return 0;
}

void platform_target_clk_output_enable(bool enable)
{
	(void)enable;
}

bool platform_spi_init(const spi_bus_e bus)
{
	(void)bus;
	return false;
}

bool platform_spi_deinit(const spi_bus_e bus)
{
	(void)bus;
	return false;
}

bool platform_spi_chip_select(const uint8_t device_select)
{
	(void)device_select;
	return false;
}

uint8_t platform_spi_xfer(const spi_bus_e bus, const uint8_t value)
{
	(void)bus;
	return value;
}
