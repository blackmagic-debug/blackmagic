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

/* This file implements the platform specific functions for the Blackpillv2 implementation. */

#include "general.h"
#include "usb.h"
#include "aux_serial.h"
#include "morse.h"
#include "exception.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/usb/dwc/otg_fs.h>

#define PLATFORM_PRINTF printf

jmp_buf fatal_error_jmpbuf;
extern uint32_t _ebss; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
void debug_serial_init(void);

void platform_init(void)
{
	volatile uint32_t *magic = (uint32_t *)&_ebss;
	/* Enable GPIO peripherals */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_GPIOB);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
	/* Check the USER button */
	if (gpio_get(GPIOA, GPIO0) || (magic[0] == BOOTMAGIC0 && magic[1] == BOOTMAGIC1)) {
		magic[0] = 0;
		magic[1] = 0;
		/* Assert blue LED as indicator we are in the bootloader */
		gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_BOOTLOADER);
		gpio_set(LED_PORT, LED_BOOTLOADER);
		/*
		 * Jump to the built in bootloader by mapping System flash.
		 * As we just come out of reset, no other deinit is needed!
		 */
		rcc_periph_clock_enable(RCC_SYSCFG);
		SYSCFG_MEMRM &= ~3U;
		SYSCFG_MEMRM |= 1U;
		scb_reset_core();
	}
#pragma GCC diagnostic pop
	rcc_clock_setup_pll(&rcc_hse_25mhz_3v3[RCC_CLOCK_3V3_84MHZ]);

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_OTGFS);
	rcc_periph_clock_enable(RCC_CRC);

	/* Set up USB Pins and alternate function */
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO9 | GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO9 | GPIO10 | GPIO11 | GPIO12);

	GPIOA_OSPEEDR &= 0x3c00000cU;
	GPIOA_OSPEEDR |= 0x28000008U;

	gpio_mode_setup(JTAG_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TCK_PIN | TDI_PIN);
	gpio_mode_setup(JTAG_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TMS_PIN);
	gpio_set_output_options(JTAG_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TCK_PIN | TDI_PIN | TMS_PIN);
	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TDO_PIN);
	gpio_set_output_options(TDO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TDO_PIN | TMS_PIN);

	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_IDLE_RUN | LED_ERROR | LED_BOOTLOADER);

	gpio_mode_setup(LED_PORT_UART, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_UART);

#ifdef PLATFORM_HAS_POWER_SWITCH
	gpio_set(PWR_BR_PORT, PWR_BR_PIN);
	gpio_mode_setup(PWR_BR_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PWR_BR_PIN);
#endif

	platform_timing_init();
	blackmagic_usb_init();
	aux_serial_init();
#ifdef PLATFORM_EXP_DEBUG
	debug_serial_init();
#endif

	/* https://github.com/libopencm3/libopencm3/pull/1256#issuecomment-779424001 */
	OTG_FS_GCCFG |= OTG_GCCFG_NOVBUSSENS | OTG_GCCFG_PWRDWN;
	OTG_FS_GCCFG &= ~(OTG_GCCFG_VBUSBSEN | OTG_GCCFG_VBUSASEN);
}

#ifdef PLATFORM_EXP_DEBUG
void debug_serial_init(void)
{
	/* Enable clocks */
	rcc_periph_clock_enable(DEBUGUSART_CLK);
	rcc_periph_clock_enable(DEBUGUSART_DMA_CLK);

	/* Setup UART parameters */
	DEBUGUART_PIN_SETUP();
	usart_set_baudrate(DEBUGUSART, 115200);
	usart_set_databits(DEBUGUSART, 8);
	usart_set_stopbits(DEBUGUSART, USART_STOPBITS_1);
	usart_set_mode(DEBUGUSART, USART_MODE_TX);
	usart_set_parity(DEBUGUSART, USART_PARITY_NONE);
	usart_set_flow_control(DEBUGUSART, USART_FLOWCONTROL_NONE);

	/* Setup USART TX DMA */
#if !defined(DEBUGUSART_TDR) && defined(DEBUGUSART_DR)
#define DEBUGUSART_TDR DEBUGUSART_DR
#elif !defined(DEBUGUSART_TDR)
#define DEBUGUSART_TDR USART_DR(DEBUGUSART)
#endif
#if !defined(DEBUGUSART_RDR) && defined(DEBUGUSART_DR)
#define DEBUGUSART_RDR DEBUGUSART_DR
#elif !defined(DEBUGUSART_RDR)
#define DEBUGUSART_RDR USART_DR(DEBUGUSART)
#endif
	dma_channel_reset(DEBUGUSART_DMA_BUS, DEBUGUSART_DMA_TX_CHAN);
	dma_set_peripheral_address(DEBUGUSART_DMA_BUS, DEBUGUSART_DMA_TX_CHAN, (uintptr_t)&DEBUGUSART_TDR);
	dma_enable_memory_increment_mode(DEBUGUSART_DMA_BUS, DEBUGUSART_DMA_TX_CHAN);
	dma_set_peripheral_size(DEBUGUSART_DMA_BUS, DEBUGUSART_DMA_TX_CHAN, DMA_PSIZE_8BIT);
	dma_set_memory_size(DEBUGUSART_DMA_BUS, DEBUGUSART_DMA_TX_CHAN, DMA_MSIZE_8BIT);
	dma_set_priority(DEBUGUSART_DMA_BUS, DEBUGUSART_DMA_TX_CHAN, DMA_PL_HIGH);
	dma_enable_transfer_complete_interrupt(DEBUGUSART_DMA_BUS, DEBUGUSART_DMA_TX_CHAN);
#ifdef DMA_STREAM0
	dma_set_transfer_mode(DEBUGUSART_DMA_BUS, DEBUGUSART_DMA_TX_CHAN, DMA_SxCR_DIR_MEM_TO_PERIPHERAL);
	dma_channel_select(DEBUGUSART_DMA_BUS, DEBUGUSART_DMA_TX_CHAN, DEBUGUSART_DMA_TRG);
	dma_set_dma_flow_control(DEBUGUSART_DMA_BUS, DEBUGUSART_DMA_TX_CHAN);
	dma_enable_direct_mode(DEBUGUSART_DMA_BUS, DEBUGUSART_DMA_TX_CHAN);
#else
	dma_set_read_from_memory(DEBUGUSART_DMA_BUS, DEBUGUSART_DMA_TX_CHAN);
#endif
	/* Enable interrupts */
	nvic_set_priority(DEBUGUSART_IRQ, IRQ_PRI_DEBUGUSART);
#if defined(DEBUGUSART_DMA_RXTX_IRQ)
	nvic_set_priority(DEBUGUSART_DMA_RXTX_IRQ, IRQ_PRI_DEBUGUSART_DMA);
#else
	nvic_set_priority(DEBUGUSART_DMA_TX_IRQ, IRQ_PRI_DEBUGUSART_DMA);
#endif
	nvic_enable_irq(DEBUGUSART_IRQ);
#if defined(DEBUGUSART_DMA_RXTX_IRQ)
	nvic_enable_irq(DEBUGUSART_DMA_RXTX_IRQ);
#else
	nvic_enable_irq(DEBUGUSART_DMA_TX_IRQ);
#endif

	/* Finally enable the USART */
	usart_enable(DEBUGUSART);
	usart_enable_tx_dma(DEBUGUSART);
}

size_t platform_debug_usart_send(const char *buf, const size_t len)
{
	for (size_t i = 0; i < len; i++)
		usart_send_blocking(DEBUGUSART, buf[i]);
	return len;
}
#endif

void platform_nrst_set_val(bool assert)
{
	if (assert) {
		gpio_mode_setup(NRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, NRST_PIN);
		gpio_set_output_options(NRST_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, NRST_PIN);
		gpio_clear(NRST_PORT, NRST_PIN);
	} else {
		gpio_mode_setup(NRST_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, NRST_PIN);
		gpio_set(NRST_PORT, NRST_PIN);
	}
}

bool platform_nrst_get_val(void)
{
	return gpio_get(NRST_PORT, NRST_PIN) == 0;
}

const char *platform_target_voltage(void)
{
	return NULL;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"

void platform_request_boot(void)
{
	uint32_t *magic = (uint32_t *)&_ebss;
	magic[0] = BOOTMAGIC0;
	magic[1] = BOOTMAGIC1;
	scb_reset_system();
}

#pragma GCC diagnostic pop

#ifdef PLATFORM_HAS_POWER_SWITCH
bool platform_target_get_power(void)
{
	return !gpio_get(PWR_BR_PORT, PWR_BR_PIN);
}

void platform_target_set_power(const bool power)
{
	gpio_set_val(PWR_BR_PORT, PWR_BR_PIN, !power);
}

/*
 * A dummy implementation of platform_target_voltage_sense as the
 * blackpillv2 has no ability to sense the voltage on the power pin.
 * This function is only needed for implementations that allow the target
 * to be powered from the debug probe.
 */
uint32_t platform_target_voltage_sense(void)
{
	return 0;
}
#endif

void platform_target_clk_output_enable(bool enable)
{
	(void)enable;
}
