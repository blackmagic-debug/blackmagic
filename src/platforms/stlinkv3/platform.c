/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022-2023 1BitSquared  <info@1bitsquared.com>
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

/* This file provides the platform specific functions for the STLINK-V3
 * implementation.
 */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "aux_serial.h"
#include "gdb_if.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/syscfg.h>

uint16_t srst_pin;
static uint32_t hw_version;

#define SCB_CCR_IC_SHIFT               17U                       /*!< SCB CCR: Instruction cache enable bit Position */
#define SCB_CCR_IC_MASK                (1UL << SCB_CCR_IC_SHIFT) /*!< SCB CCR: Instruction cache enable bit Mask */
#define SCB_CCR_DC_SHIFT               16U                       /*!< SCB CCR: Cache enable bit Position */
#define SCB_CCR_DC_MASK                (1UL << SCB_CCR_DC_SHIFT) /*!< SCB CCR: DC Mask */
#define SCB_CCSIDR_NUMSETS_MASK        (0x7fffUL << SCB_CCSIDR_NUMSETS_SHIFT) /*!< SCB CCSIDR: NumSets Mask */
#define SCB_CCSIDR_NUMSETS_SHIFT       13U                                    /*!< SCB CCSIDR: NumSets Position */
#define SCB_CCSIDR_ASSOCIATIVITY_SHIFT 3U                                     /*!< SCB CCSIDR: Associativity Position */
#define SCB_CCSIDR_ASSOCIATIVITY_MASK  (0x3ffUL << SCB_CCSIDR_ASSOCIATIVITY_SHIFT) /*!< SCB CCSIDR: Associativity Mask */
#define CCSIDR_WAYS(x)                 (((x)&SCB_CCSIDR_ASSOCIATIVITY_MASK) >> SCB_CCSIDR_ASSOCIATIVITY_SHIFT)
#define CCSIDR_SETS(x)                 (((x)&SCB_CCSIDR_NUMSETS_MASK) >> SCB_CCSIDR_NUMSETS_SHIFT)
#define SCB_DCISW_SET_SHIFT            5U                               /*!< SCB DCISW: Set Position */
#define SCB_DCISW_SET_MASK             (0x1ffUL << SCB_DCISW_SET_SHIFT) /*!< SCB DCISW: Set Mask */
#define SCB_DCISW_WAY_SHIFT            30U                              /*!< SCB DCISW: Way Position */
#define SCB_DCISW_WAY_MASK             (3UL << SCB_DCISW_WAY_SHIFT)     /*!< SCB DCISW: Way Mask */

/* DSB - Data Synchronization Barrier
 * forces memory access needs to have completed before program execution progresses */
static void cm_dsb(void)
{
	__asm__ volatile("dsb 0xf" ::: "memory");
}

/* ISB - Instruction Synchronization Barrier
 * forces instruction fetches to explicitly take place after a certain point in the program */
static void cm_isb(void)
{
	__asm__ volatile("isb 0xf" ::: "memory");
}

static void scb_enable_i_cache(void)
{
	cm_dsb();
	cm_isb();
	SCB_ICIALLU = 0UL;          /* invalidate I-Cache */
	cm_dsb();                   /* ensure completion of the invalidation */
	cm_isb();                   /* ensure instruction fetch path sees new I-Cache state */
	SCB_CCR |= SCB_CCR_IC_MASK; /* enable I-Cache */
	cm_dsb();                   /* ensure completion of enable I-Cache */
	cm_isb();                   /* ensure instruction fetch path sees new I-Cache state */
}

static void scb_enable_d_cache(void)
{
	SCB_CCSELR = 0U; /*(0U << 1U) | 0U;*/ /* Level 1 data cache */
	cm_dsb();

	uint32_t ccsidr = SCB_CCSIDR;

	const uint32_t sets = CCSIDR_SETS(ccsidr);
	const uint32_t ways = CCSIDR_WAYS(ccsidr);
	for (uint32_t set = 0; set <= sets; ++set) {
		for (uint32_t way = 0; way <= ways; ++way) {
			SCB_DCISW = ((set << SCB_DCISW_SET_SHIFT) & SCB_DCISW_SET_MASK) |
				((way << SCB_DCISW_WAY_SHIFT) & SCB_DCISW_WAY_MASK);
		}
	}
	cm_dsb();

	SCB_CCR |= SCB_CCR_DC_MASK; /* enable D-Cache */

	cm_dsb(); /* ensure completion of enable I-Cache */
	cm_isb(); /* ensure instruction fetch path sees new D-Cache state */
}

int platform_hwversion(void)
{
	return hw_version;
}

void platform_nrst_set_val(bool assert)
{
	gpio_set_val(SRST_PORT, SRST_PIN, !assert);
	if (assert) {
		for (volatile size_t i = 0; i < 10000; i++)
			continue;
	}
}

bool platform_nrst_get_val()
{
	return gpio_get(SRST_PORT, SRST_PIN) == 0;
}

const char *platform_target_voltage(void)
{
	/* On the stlinkv3, the target input voltage is divided by two.
	 * The ADC is sampling at 12 bit resolution.
	 * Vref+ input is assumed to be 3.3 volts. */
	static char ret[] = "0.0V";
	uint8_t channel = ADC_CHANNEL0;

	adc_set_regular_sequence(ADC1, 1, &channel);
	adc_start_conversion_regular(ADC1);
	while (!adc_eoc(ADC1))
		continue;
	uint32_t value = adc_read_regular(ADC1);

	value *= 3379;   /* 3.3 * 1024 == 3379.2 */
	value += 104858; /* round, 0.05V * 2 ^ 21 == 104857.6 */
	ret[0] = (value >> 21) + '0';
	value &= (1 << 21) - 1;
	value *= 10;
	ret[2] = (value >> 21) + '0';

	return ret;
}

void platform_request_boot(void)
{
	/* Use top of ITCM RAM as magic marker*/
	volatile uint32_t *magic = (volatile uint32_t *)0x3ff8;
	magic[0] = BOOTMAGIC0;
	magic[1] = BOOTMAGIC1;
	scb_reset_system();
}

void platform_init(void)
{
	rcc_periph_clock_enable(RCC_APB2ENR_SYSCFGEN);
	rcc_clock_setup_hse(rcc_3v3 + RCC_CLOCK_3V3_216MHZ, 25);
	scb_enable_i_cache();
	scb_enable_d_cache();
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOD);
	rcc_periph_clock_enable(RCC_GPIOH);
	rcc_periph_clock_enable(RCC_GPIOF);
	rcc_periph_clock_enable(RCC_GPIOG);

	/* Initialize ADC. */
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO0);
	rcc_periph_clock_enable(RCC_ADC1);
	adc_power_off(ADC1);
	adc_disable_scan_mode(ADC1);
	adc_set_sample_time(ADC1, ADC_CHANNEL0, ADC_SMPR_SMP_3CYC);
	adc_power_on(ADC1);

	/* Configure srst pin. */
	gpio_set_output_options(SRST_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, SRST_PIN);
	gpio_mode_setup(SRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, SRST_PIN);
	gpio_set(SRST_PORT, SRST_PIN);

	gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN);
	gpio_set_output_options(TMS_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TMS_PIN);
	gpio_mode_setup(SWDIO_IN_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, SWDIO_IN_PIN);

	/* Configure TDI pin. */
	gpio_mode_setup(TDI_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TDI_PIN);
	gpio_set_output_options(TDI_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TDI_PIN);

	/* Drive the tck/swck pin low. */
	gpio_mode_setup(TCK_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TCK_PIN);
	gpio_set_output_options(TCK_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TCK_PIN);

	/* Drive direction switch pin. */
	gpio_mode_setup(TMS_DRIVE_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_DRIVE_PIN);
	gpio_set_output_options(TMS_DRIVE_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, TMS_DRIVE_PIN);
	gpio_set(TMS_DRIVE_PORT, TMS_DRIVE_PIN);

	gpio_mode_setup(PWR_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PWR_EN_PIN);
	gpio_set_output_options(PWR_EN_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, PWR_EN_PIN);
	gpio_set(PWR_EN_PORT, PWR_EN_PIN);

	/* Set up MCO at 8 MHz on PA8 */
	gpio_set_af(MCO1_PORT, MCO1_AF, MCO1_PIN);
	gpio_mode_setup(MCO1_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, MCO1_PIN);
	gpio_set_output_options(MCO1_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, MCO1_PIN);
	RCC_CR |= RCC_CR_HSION;
	RCC_CFGR &= ~(0x3 << RCC_CFGR_MCO1_SHIFT);
	RCC_CFGR |= RCC_CFGR_MCO1_HSI << RCC_CFGR_MCO1_SHIFT;
	RCC_CFGR &= ~(0x7 << RCC_CFGR_MCO1PRE_SHIFT);
	RCC_CFGR |= RCC_CFGR_MCOPRE_DIV_2 << RCC_CFGR_MCO1PRE_SHIFT;

	/* Setup USB pins */
	rcc_periph_clock_enable(RCC_GPIOB);
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO14 | GPIO15);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, GPIO14 | GPIO15);
	gpio_set_af(GPIOB, GPIO_AF12, GPIO14 | GPIO15);

	/* Set up green/red led to steady green to indicate application active
	 * FIXME: Allow RED and yellow constant and blinking,
	 * e.g. by PWM onTIM1_CH3 (PA10)
	 */
	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_PIN);
	gpio_set_output_options(LED_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, LED_PIN);

	/* Relocate interrupt vector table here */
	extern int vector_table;
	SCB_VTOR = (uintptr_t)&vector_table;

	platform_timing_init();
	blackmagic_usb_init();
	aux_serial_init();
	/* By default, do not drive the swd bus too fast. */
	platform_max_frequency_set(6000000);
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
