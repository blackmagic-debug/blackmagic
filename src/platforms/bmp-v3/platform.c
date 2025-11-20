/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "general.h"
#include "platform.h"
#include "usb.h"

#include <libopencm3/cm3/vector.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/spi.h>

static void adc_init(void);

void platform_init(void)
{
	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_OTGFS);
	rcc_periph_clock_enable(RCC_CRS);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_GPIOH);
	/* Make sure to power up the timer used for trace */
	rcc_periph_clock_enable(RCC_TIM5);
	rcc_periph_clock_enable(RCC_CRC);

	/* Bring up the ADC */
	adc_init();

	/* Set up the NVIC vector table for the firmware */
	SCB_VTOR = (uintptr_t)&vector_table; // NOLINT(clang-diagnostic-pointer-to-int-cast, performance-no-int-to-ptr)

	/* Bring up timing and USB */
	platform_timing_init();
	blackmagic_usb_init();
}

static void adc_init(void)
{
	rcc_periph_clock_enable(RCC_ADC1_2);

	gpio_mode_setup(TPWR_SENSE_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, TPWR_SENSE_PIN);

	adc_power_off(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_814DOT5CYC);
	adc_enable_temperature_sensor();
	adc_power_on(ADC1);

	/* Wait for the ADC to finish starting up */
	for (volatile size_t i = 0; i < 800000U; ++i)
		continue;

	adc_calibrate(ADC1);
}

void platform_nrst_set_val(bool assert)
{
	gpio_set(TMS_PORT, TMS_PIN);
	gpio_set_val(NRST_PORT, NRST_PIN, assert);

	if (assert) {
		for (volatile size_t i = 0; i < 10000U; ++i)
			continue;
	}
}

bool platform_nrst_get_val(void)
{
	return gpio_get(NRST_SENSE_PORT, NRST_SENSE_PIN) != 0;
}

bool platform_target_get_power(void)
{
	return !gpio_get(TPWR_EN_PORT, TPWR_EN_PIN);
}

uint32_t platform_target_voltage_sense(void)
{
	/*
	 * Returns the voltage in tenths of a volt (so 33 means 3.3V).
	 * BMPv3 uses ADC1_IN17 for target power sense
	 */
	const uint8_t channel = 17U;
	adc_set_regular_sequence(ADC1, 1, &channel);

	adc_start_conversion_regular(ADC1);

	/* Wait for end of conversion */
	while (!adc_eoc(ADC1))
		continue;

	uint32_t val = adc_read_regular(ADC1); /* 0-4095 */
	/* Clear EOC bit to acknowledge and rest for the next time */
	adc_clear_eos(ADC1);
	return (val * 99U) / 8191U;
}

const char *platform_target_voltage(void)
{
	static char ret[] = "0.0V";
	uint32_t val = platform_target_voltage_sense();
	ret[0] = '0' + val / 10U;
	ret[2] = '0' + val % 10U;

	return ret;
}

void platform_target_clk_output_enable(bool enable)
{
	/* If we're switching to tristate mode, first convert the processor pin to an input */
	if (!enable)
		gpio_mode_setup(TCK_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TCK_PIN);
	/* Reconfigure the logic levelt translator */
	gpio_set_val(TCK_DIR_PORT, TCK_DIR_PIN, enable);
	/* If we're switching back out of tristate mode, we're now safe to make the processor pin an output again */
	if (enable)
		gpio_mode_setup(TCK_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TCK_PIN);
}

bool platform_spi_init(const spi_bus_e bus)
{
	/* Test to see which bus we're supposed to be initialising */
	if (bus == SPI_BUS_EXTERNAL) {
		rcc_periph_clock_enable(RCC_SPI2);
		rcc_periph_reset_pulse(RST_SPI2);
		gpio_set_af(EXT_SPI_SCLK_PORT, GPIO_AF5, EXT_SPI_SCLK_PIN);
		gpio_mode_setup(EXT_SPI_SCLK_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, EXT_SPI_SCLK_PIN);
		gpio_set_af(EXT_SPI_POCI_PORT, GPIO_AF5, EXT_SPI_POCI_PIN);
		gpio_mode_setup(EXT_SPI_POCI_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, EXT_SPI_POCI_PIN);
		gpio_set_af(EXT_SPI_PICO_PORT, GPIO_AF5, EXT_SPI_PICO_PIN);
		gpio_mode_setup(EXT_SPI_PICO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, EXT_SPI_PICO_PIN);
		gpio_set(TCK_DIR_PORT, TCK_DIR_PIN);
		gpio_set(TMS_DIR_PORT, TMS_DIR_PIN);
	} else
		/* For now, we only support the external SPI bus */
		return false;

	const uintptr_t controller = EXT_SPI;
	spi_init_master(controller, SPI_CFG1_MBR_DIV8, SPI_CFG2_CPOL_CLK_TO_0_WHEN_IDLE, SPI_CFG2_CPHA_CLK_TRANSITION_1,
		SPI_CFG1_DSIZE_8BIT, SPI_CFG2_MSBFIRST, SPI_CFG2_SP_MOTOROLA);
	spi_enable(controller);
	return true;
}

bool platform_spi_deinit(spi_bus_e bus)
{
	if (bus != SPI_BUS_EXTERNAL)
		return false;

	spi_disable(EXT_SPI);

	if (bus == SPI_BUS_EXTERNAL) {
		rcc_periph_clock_disable(RCC_SPI2);
		gpio_mode_setup(TCK_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TCK_PIN);
		gpio_mode_setup(TDI_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TDI_PIN);
		platform_target_clk_output_enable(false);
	}
	return true;
}

bool platform_spi_chip_select(const uint8_t device_select)
{
	const uint8_t device = device_select & 0x7fU;
	const bool select = !(device_select & 0x80U);
	uint32_t port;
	uint16_t pin;
	switch (device) {
	/*
	case SPI_DEVICE_INT_FLASH:
		port = AUX_PORT;
		pin = AUX_FCS;
		break;
	*/
	case SPI_DEVICE_EXT_FLASH:
		port = EXT_SPI_CS_PORT;
		pin = EXT_SPI_CS_PIN;
		break;
	default:
		return false;
	}
	gpio_set_val(port, pin, select);
	return true;
}

uint8_t platform_spi_xfer(const spi_bus_e bus, const uint8_t value)
{
	if (bus != SPI_BUS_EXTERNAL)
		return 0xffU;
	return spi_xfer(EXT_SPI, value);
}
