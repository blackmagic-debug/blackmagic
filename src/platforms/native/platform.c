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

/* This file implements the platform specific functions for the native implementation. */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "aux_serial.h"
#include "morse.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/flash.h>

static void adc_init(void);
static void setup_vbus_irq(void);

/* This is defined by the linker script */
extern char vector_table;

/*
 * Starting with hardware version 4 we are storing the hardware version in the
 * flash option user Data1 byte.
 * The hardware version 4 was the transition version that had it's hardware
 * pins strapped to 3 but contains version 4 in the Data1 byte.
 * The hardware 4 is backward compatible with V3 but provides the new jumper
 * connecting STRACE target pin to the UART1 pin.
 * Hardware version 5 does not have the physically strapped version encoding
 * any more and the hardware version has to be read out of the option bytes.
 * This means that older firmware versions that don't do the detection won't
 * work on the newer hardware.
 */
#define BMP_HWVERSION_BYTE FLASH_OPTION_BYTE_2

/*
 * Pins PB[7:5] are used to detect hardware revision.
 * User option byte Data1 is used starting with hardware revision 4.
 * Pin -  OByte - Rev - Description
 * 000 - 0xffff -   0 - Original production build.
 * 001 - 0xffff -   1 - Mini production build.
 * 010 - 0xffff -   2 - Mini V2.0e and later.
 * 011 - 0xffff -   3 - Mini V2.1a and later.
 * 011 - 0xfb04 -   4 - Mini V2.1d and later.
 * xxx - 0xfb05 -   5 - Mini V2.2a and later.
 * xxx - 0xfb06 -   6 - Mini V2.3a and later.
 *
 * This function will return -2 if the version number does not make sense.
 * This can happen when the Data1 byte contains "garbage". For example a
 * hardware revision that is <4 or the high byte is not the binary inverse of
 * the lower byte.
 * Note: The high byte of the Data1 option byte should always be the binary
 * inverse of the lower byte unless the byte is not set, then all bits in both
 * high and low byte are 0xff.
 */
int platform_hwversion(void)
{
	static int hwversion = -1;
	uint16_t hwversion_pins = GPIO7 | GPIO6 | GPIO5;
	uint16_t unused_pins = hwversion_pins ^ 0xffffU;

	/* Check if the hwversion is set in the user option byte. */
	if (hwversion == -1) {
		if (BMP_HWVERSION_BYTE != 0xffffU && BMP_HWVERSION_BYTE != 0x00ffU) {
			/* Check if the data is valid. When valid it should only have values 4 and higher. */
			if ((BMP_HWVERSION_BYTE >> 8U) != (~BMP_HWVERSION_BYTE & 0xffU) || (BMP_HWVERSION_BYTE & 0xffU) < 4)
				return -2;
			hwversion = BMP_HWVERSION_BYTE & 0xffU;
		}
	}

	/* If the hwversion is not set in option bytes check the hw pin strapping. */
	if (hwversion == -1) {
		/* Configure the hardware version pins as input pull-up/down */
		gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, hwversion_pins);

		/* Enable the weak pull up. */
		gpio_set(GPIOB, hwversion_pins);

		/* Wait a little to make sure the pull up is in effect... */
		for (volatile size_t i = 0; i < 100U; ++i)
			continue;

		/*
		 * Get all pins that are pulled low in hardware.
		 * This also sets all the "unused" pins to 1.
		 */
		uint16_t pins_negative = gpio_get(GPIOB, hwversion_pins) | unused_pins;

		/* Enable the weak pull down. */
		gpio_clear(GPIOB, hwversion_pins);

		/* Wait a little to make sure the pull down is in effect... */
		for (volatile size_t i = 0; i < 100U; ++i)
			continue;

		/* Get all the pins that are pulled high in hardware. */
		uint16_t pins_positive = gpio_get(GPIOB, hwversion_pins);

		/*
		 * The hardware version is the ID defined by the pins that are
		 * asserted low or high by the hardware. This means that pins
		 * that are left floating are 0 and those that are either
		 * pulled high or low are 1.
		 *
		 * XXX: This currently converts `uint16_t`'s to `int`. It should not do this,
		 * it should remain unsigned at all times, but this requires changing how the invalid
		 * hardware version should be returned.
		 */
		hwversion = (((pins_positive ^ pins_negative) ^ 0xffffU) & hwversion_pins) >> 5U;
	}

	return hwversion;
}

void platform_init(void)
{
	const int hwversion = platform_hwversion();
	SCS_DEMCR |= SCS_DEMCR_VC_MON_EN;

	rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	if (hwversion >= 6)
		rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRC);

	/* Setup GPIO ports */
	gpio_clear(USB_PU_PORT, USB_PU_PIN);
	gpio_set_mode(USB_PU_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, USB_PU_PIN);

	gpio_set_mode(JTAG_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TMS_DIR_PIN | TCK_PIN | TDI_PIN);
	gpio_set_mode(JTAG_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_INPUT_FLOAT, TMS_PIN);

	/* This needs some fixing... */
	/* Toggle required to sort out line drivers... */
	gpio_port_write(GPIOA, 0x8102);
	gpio_port_write(GPIOB, 0x2000);

	gpio_port_write(GPIOA, 0x8182);
	gpio_port_write(GPIOB, 0x2002);

	if (hwversion >= 6) {
		gpio_set_mode(TCK_DIR_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TCK_DIR_PIN);
		gpio_set_mode(TCK_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, TCK_PIN);
		gpio_clear(TCK_DIR_PORT, TCK_DIR_PIN);
	}

	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, LED_UART | LED_IDLE_RUN | LED_ERROR);

	/*
	 * Enable nRST output. Original uses a NPN to pull down, so setting the
	 * output HIGH asserts. Mini is directly connected so use open drain output
	 * and set LOW to assert.
	 */
	platform_nrst_set_val(false);
	gpio_set_mode(NRST_PORT, GPIO_MODE_OUTPUT_50_MHZ,
		hwversion == 0 || hwversion >= 3 ? GPIO_CNF_OUTPUT_PUSHPULL : GPIO_CNF_OUTPUT_OPENDRAIN, NRST_PIN);
	/* FIXME: Gareth, Esden, what versions need this fix? */
	if (hwversion < 3)
		/*
		 * FIXME: This pin in intended to be input, but the TXS0108 fails
		 * to release the device from reset if this floats.
		 */
		gpio_set_mode(NRST_SENSE_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, NRST_SENSE_PIN);
	else {
		gpio_set(NRST_SENSE_PORT, NRST_SENSE_PIN);
		gpio_set_mode(NRST_SENSE_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, NRST_SENSE_PIN);
	}
	/*
	 * Enable internal pull-up on PWR_BR so that we don't drive
	 * TPWR locally or inadvertently supply power to the target.
	 */
	if (hwversion == 1) {
		gpio_set(PWR_BR_PORT, PWR_BR_PIN);
		gpio_set_mode(PWR_BR_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, PWR_BR_PIN);
	} else if (hwversion > 1) {
		gpio_set(PWR_BR_PORT, PWR_BR_PIN);
		gpio_set_mode(PWR_BR_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, PWR_BR_PIN);
	}

	if (hwversion >= 5) {
		gpio_set_mode(AUX_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, AUX_SCLK | AUX_COPI);
		gpio_set_mode(AUX_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, AUX_FCS | AUX_SDCS);
		gpio_set_mode(AUX_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, AUX_CIPO);
		gpio_set(AUX_PORT, AUX_FCS | AUX_SDCS);
		/* hw6 introduced an SD Card chip select on PB6, moving the display select to PB7 */
		if (hwversion >= 6) {
			gpio_set_mode(AUX_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, AUX_DCS6);
			gpio_set(AUX_PORT, AUX_DCS6);
		}
	}

	if (hwversion > 0)
		adc_init();
	else {
		gpio_clear(GPIOB, GPIO0);
		gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, GPIO0);
	}
	/* Set up the NVIC vector table for the firmware */
	SCB_VTOR = (uint32_t)&vector_table; // NOLINT(clang-diagnostic-pointer-to-int-cast)

	platform_timing_init();
	blackmagic_usb_init();

	/*
	 * On hardware version 1 and 2, UART and SWD share connector pins.
	 * Don't enable UART if we're being debugged.
	 */
	if (hwversion == 0 || hwversion >= 3 || !(SCS_DEMCR & SCS_DEMCR_TRCENA))
		aux_serial_init();

	setup_vbus_irq();
}

void platform_nrst_set_val(bool assert)
{
	gpio_set(TMS_PORT, TMS_PIN);
	if (platform_hwversion() == 0 || platform_hwversion() >= 3)
		gpio_set_val(NRST_PORT, NRST_PIN, assert);
	else
		gpio_set_val(NRST_PORT, NRST_PIN, !assert);

	if (assert) {
		for (volatile size_t i = 0; i < 10000U; ++i)
			continue;
	}
}

bool platform_nrst_get_val(void)
{
	if (platform_hwversion() == 0)
		return gpio_get(NRST_SENSE_PORT, NRST_SENSE_PIN) == 0;
	if (platform_hwversion() >= 3)
		return gpio_get(NRST_SENSE_PORT, NRST_SENSE_PIN) != 0;
	return gpio_get(NRST_PORT, NRST_PIN) == 0;
}

bool platform_target_get_power(void)
{
	if (platform_hwversion() > 0)
		return !gpio_get(PWR_BR_PORT, PWR_BR_PIN);
	return false;
}

bool platform_target_set_power(const bool power)
{
	if (platform_hwversion() <= 0)
		return false;

	gpio_set_val(PWR_BR_PORT, PWR_BR_PIN, !power);
	return true;
}

static void adc_init(void)
{
	rcc_periph_clock_enable(RCC_ADC1);

	gpio_set_mode(TPWR_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, TPWR_PIN);

	adc_power_off(ADC1);
	adc_disable_scan_mode(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_239DOT5CYC);
	adc_enable_temperature_sensor();
	adc_power_on(ADC1);

	/* Wait for the ADC to finish starting up */
	for (volatile size_t i = 0; i < 800000U; ++i)
		continue;

	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);
}

uint32_t platform_target_voltage_sense(void)
{
	/*
	 * Returns the voltage in tenths of a volt (so 33 means 3.3V),
	 * except for hardware version 1.
	 * This function is only needed for implementations that allow the
	 * target to be powered from the debug probe
	 */
	if (platform_hwversion() == 0)
		return 0;

	uint8_t channel = 8;
	adc_set_regular_sequence(ADC1, 1, &channel);

	adc_start_conversion_direct(ADC1);

	/* Wait for end of conversion. */
	while (!adc_eoc(ADC1))
		continue;

	uint32_t val = adc_read_regular(ADC1); /* 0-4095 */
	/* Clear EOC bit. The GD32F103 does not automatically reset it on ADC read. */
	ADC_SR(ADC1) &= ~ADC_SR_EOC;
	return (val * 99U) / 8191U;
}

const char *platform_target_voltage(void)
{
	if (platform_hwversion() == 0)
		return gpio_get(GPIOB, GPIO0) ? "OK" : "ABSENT!";

	static char ret[] = "0.0V";
	uint32_t val = platform_target_voltage_sense();
	ret[0] = '0' + val / 10U;
	ret[2] = '0' + val % 10U;

	return ret;
}

void platform_request_boot(void)
{
	/* Disconnect USB cable */
	gpio_set_mode(USB_PU_PORT, GPIO_MODE_INPUT, 0, USB_PU_PIN);

	/* Drive boot request pin */
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);
	gpio_clear(GPIOB, GPIO12);
}

void platform_target_clk_output_enable(bool enable)
{
	if (platform_hwversion() >= 6) {
		/* If we're switching to tristate mode, first convert the processor pin to an input */
		if (!enable)
			gpio_set_mode(TCK_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, TCK_PIN);
		/* Reconfigure the logic levelt translator */
		gpio_set_val(TCK_DIR_PORT, TCK_DIR_PIN, enable);
		/* If we're switching back out of tristate mode, we're now safe to make the processor pin an output again */
		if (enable)
			gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
	}
}

bool platform_spi_init(const spi_bus_e bus)
{
	if (bus == SPI_BUS_EXTERNAL) {
		rcc_periph_clock_enable(RCC_SPI1);
		rcc_periph_reset_pulse(RST_SPI1);
		platform_target_clk_output_enable(true);
		gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, TCK_PIN);
		gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, TDI_PIN);
		gpio_set(TMS_DIR_PORT, TMS_DIR_PIN);
	} else {
		rcc_periph_clock_enable(RCC_SPI2);
		rcc_periph_reset_pulse(RST_SPI2);
	}

	const uint32_t controller = bus == SPI_BUS_EXTERNAL ? EXT_SPI : AUX_SPI;
	spi_init_master(controller, SPI_CR1_BAUDRATE_FPCLK_DIV_8, SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE,
		SPI_CR1_CPHA_CLK_TRANSITION_1, SPI_CR1_DFF_8BIT, SPI_CR1_MSBFIRST);
	spi_enable(controller);
	return true;
}

bool platform_spi_deinit(spi_bus_e bus)
{
	spi_disable(bus == SPI_BUS_EXTERNAL ? EXT_SPI : AUX_SPI);

	if (bus == SPI_BUS_EXTERNAL) {
		rcc_periph_clock_disable(RCC_SPI1);
		gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
		gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);
		platform_target_clk_output_enable(false);
	} else
		rcc_periph_clock_disable(RCC_SPI2);
	return true;
}

bool platform_spi_chip_select(const uint8_t device_select)
{
	const uint8_t device = device_select & 0x7fU;
	const bool select = !(device_select & 0x80U);
	uint32_t port = AUX_PORT;
	uint16_t pin;
	switch (device) {
	case SPI_DEVICE_INT_FLASH:
		pin = AUX_FCS;
		break;
	case SPI_DEVICE_EXT_FLASH:
		port = EXT_SPI_CS_PORT;
		pin = EXT_SPI_CS;
		break;
	case SPI_DEVICE_SDCARD:
		pin = AUX_SDCS;
		break;
	case SPI_DEVICE_DISPLAY:
		pin = AUX_DCS;
		break;
	default:
		return false;
	}
	gpio_set_val(port, pin, select);
	return true;
}

uint8_t platform_spi_xfer(const spi_bus_e bus, const uint8_t value)
{
	return spi_xfer(bus == SPI_BUS_EXTERNAL ? EXT_SPI : AUX_SPI, value);
}

void exti15_10_isr(void)
{
	uint32_t usb_vbus_port;
	uint16_t usb_vbus_pin;

	if (platform_hwversion() < 5) {
		usb_vbus_port = USB_VBUS_PORT;
		usb_vbus_pin = USB_VBUS_PIN;
	} else {
		usb_vbus_port = USB_VBUS5_PORT;
		usb_vbus_pin = USB_VBUS5_PIN;
	}

	if (gpio_get(usb_vbus_port, usb_vbus_pin))
		/* Drive pull-up high if VBUS connected */
		gpio_set_mode(USB_PU_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, USB_PU_PIN);
	else
		/* Allow pull-up to float if VBUS disconnected */
		gpio_set_mode(USB_PU_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, USB_PU_PIN);

	exti_reset_request(usb_vbus_pin);
}

static void setup_vbus_irq(void)
{
	uint32_t usb_vbus_port;
	uint16_t usb_vbus_pin;

	if (platform_hwversion() < 5) {
		usb_vbus_port = USB_VBUS_PORT;
		usb_vbus_pin = USB_VBUS_PIN;
	} else {
		usb_vbus_port = USB_VBUS5_PORT;
		usb_vbus_pin = USB_VBUS5_PIN;
	}

	nvic_set_priority(USB_VBUS_IRQ, IRQ_PRI_USB_VBUS);
	nvic_enable_irq(USB_VBUS_IRQ);

	gpio_set(usb_vbus_port, usb_vbus_pin);
	gpio_set(USB_PU_PORT, USB_PU_PIN);

	gpio_set_mode(usb_vbus_port, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, usb_vbus_pin);

	/* Configure EXTI for USB VBUS monitor */
	exti_select_source(usb_vbus_pin, usb_vbus_port);
	exti_set_trigger(usb_vbus_pin, EXTI_TRIGGER_BOTH);
	exti_enable_request(usb_vbus_pin);

	exti15_10_isr();
}
