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
#include "rcc_clocking.h"
#include "aux_serial.h"

#include <libopencm3/cm3/vector.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/crs.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/assert.h>

#define BOOTLOADER_ADDRESS 0x08000000U

static void adc_init(void);

int hwversion = -1;

void platform_init(void)
{
	hwversion = 0;
	SCS_DEMCR |= SCS_DEMCR_VC_MON_EN;

	/* Enable the FPU as we're in hard float mode and defined it to the compiler */
	SCB_CPACR |= SCB_CPACR_CP10_FULL | SCB_CPACR_CP11_FULL;

	/* Set up the NVIC vector table for the firmware */
	SCB_VTOR = (uintptr_t)&vector_table; // NOLINT(clang-diagnostic-pointer-to-int-cast, performance-no-int-to-ptr)

	/* Bring up the PLLs, set up HSI48 for USB, and set up the clock recovery system for that */
	rcc_clock_setup_pll(&rcc_hsi_config);
	rcc_clock_setup_hsi48();
	crs_autotrim_usb_enable();
	/* Power up USB controller */
	pwr_enable_vddusb();
	/* Power up the analog domain */
	pwr_enable_vdda();
	/* Route HSI48 to the USB controller */
	rcc_set_iclk_clksel(RCC_CCIPR1_ICLKSEL_HSI48);

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

	/* Setup GPIO ports */
	gpio_mode_setup(TCK_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TCK_PIN);
	gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_PIN);
	gpio_mode_setup(TDI_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TDI_PIN);
	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TDO_PIN);

	gpio_set_output_options(LED0_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, LED0_PIN);
	gpio_mode_setup(LED0_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED0_PIN);
	gpio_set_output_options(LED1_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, LED1_PIN);
	gpio_mode_setup(LED1_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED1_PIN);
	gpio_set_output_options(LED2_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, LED2_PIN);
	gpio_mode_setup(LED2_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED2_PIN);
	gpio_set_output_options(LED3_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, LED3_PIN);
	gpio_mode_setup(LED3_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED3_PIN);

	gpio_set_af(AUX_UART1_PORT, GPIO_AF7, AUX_UART1_TX_PIN | AUX_UART1_RX_PIN);
	gpio_set_output_options(AUX_UART1_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, AUX_UART1_TX_PIN | AUX_UART1_RX_PIN);
	gpio_mode_setup(AUX_UART1_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, AUX_UART1_TX_PIN);
	gpio_mode_setup(AUX_UART1_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, AUX_UART1_RX_PIN);

	gpio_set_af(AUX_UART2_PORT, GPIO_AF7, AUX_UART2_TX_PIN | AUX_UART2_RX_PIN);
	gpio_set_output_options(AUX_UART2_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_100MHZ, AUX_UART2_TX_PIN | AUX_UART2_RX_PIN);
	gpio_mode_setup(AUX_UART2_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, AUX_UART2_TX_PIN);
	gpio_mode_setup(AUX_UART2_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, AUX_UART2_RX_PIN);

	/* Bring up the ADC */
	adc_init();

	/* Bring up timing and USB */
	platform_timing_init();
	blackmagic_usb_init();

	/* Bring up the aux serial interface */
	aux_serial_init();
}

static void adc_init(void)
{
	/*
	 * Configure the ADC/DAC mux and bring the peripheral clock up, knocking it down from 160MHz to 40MHz
	 * to bring it into range for the peripheral per the f(ADC) characteristic of 5MHz <= f(ADC) <= 55MHz
	 */
	rcc_set_peripheral_clk_sel(ADC1, RCC_CCIPR3_ADCDACSEL_SYSCLK);
	rcc_periph_clock_enable(RCC_ADC1_2);
	adc_ungate_power(ADC1);
	adc_set_common_prescaler(ADC12_CCR_PRESC_DIV4);

	gpio_mode_setup(TPWR_SENSE_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, TPWR_SENSE_PIN);

	adc_power_off(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_sample_time(ADC1, 17U, ADC12_SMPR_SMP_68CYC);
	adc_channel_preselect(ADC1, 17U);
	adc_enable_temperature_sensor();
	adc_calibrate_linearity(ADC1);
	adc_calibrate(ADC1);
	adc_power_on(ADC1);
}

int platform_hwversion(void)
{
	return hwversion;
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
	adc_set_regular_sequence(ADC1, 1U, &channel);
	adc_clear_eoc(ADC1);

	adc_start_conversion_regular(ADC1);

	/* Wait for end of conversion */
	while (!adc_eoc(ADC1))
		continue;

	const uint32_t voltage = adc_read_regular(ADC1); /* 0-16383 */
	return (voltage * 99U) / 32767U;
}

const char *platform_target_voltage(void)
{
	static char result[] = "0.0V";
	uint32_t voltage = platform_target_voltage_sense();
	result[0] = (char)('0' + (voltage / 10U));
	result[2] = (char)('0' + (voltage % 10U));

	return result;
}

void platform_request_boot(void)
{
	/* Disconnect USB cable */
	usbd_disconnect(usbdev, true);
	gpio_mode_setup(USB_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, USB_DP_PIN | USB_DM_PIN);
	/* Make sure we drive the USB reset condition for at least 10ms */
	while (!(STK_CSR & STK_CSR_COUNTFLAG))
		continue;
	for (size_t count = 0U; count < 10U * SYSTICKMS; ++count) {
		while (!(STK_CSR & STK_CSR_COUNTFLAG))
			continue;
	}

	/* Drive boot request pin */
	gpio_mode_setup(BNT_BOOT_REQ_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, BTN_BOOT_REQ_PIN);
	gpio_clear(BNT_BOOT_REQ_PORT, BTN_BOOT_REQ_PIN);

	/* Reset core to enter bootloader */
	/* Reload PC and SP with their POR values from the start of Flash */
	const uint32_t stack_pointer = *((uint32_t *)BOOTLOADER_ADDRESS);
	/* clang-format off */
	__asm__(
		"msr msp, %1\n"     /* Load the system stack register with the new stack pointer */
		"ldr pc, [%0, 4]\n" /* Jump to the bootloader */
		: : "l"(BOOTLOADER_ADDRESS), "l"(stack_pointer) : "r0"
	);
	/* clang-format on */
	cm3_assert_not_reached();
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
		rcc_set_peripheral_clk_sel(EXT_SPI, RCC_CCIPR_SPIxSEL_PCLKx);
		rcc_periph_clock_enable(RCC_SPI2);
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
	spi_init_master(controller, SPI_CFG1_MBR_DIV16, SPI_CFG2_CPOL_CLK_TO_0_WHEN_IDLE, SPI_CFG2_CPHA_CLK_TRANSITION_1,
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
	uintptr_t port;
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
	return spi_xfer8(EXT_SPI, value);
}
