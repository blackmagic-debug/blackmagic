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

#include "general.h"
#include "cdcacm.h"
#include "usbuart.h"
#include "morse.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/flash.h>

static void adc_init(void);
static void setup_vbus_irq(void);

/* Starting with hardware version 4 we are storing the hardware version in the
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

/* Pins PB[7:5] are used to detect hardware revision.
 * User option byte Data1 is used starting with hardware revision 4.
 * Pin -  OByte - Rev - Description
 * 000 - 0xFFFF -   0 - Original production build.
 * 001 - 0xFFFF -   1 - Mini production build.
 * 010 - 0xFFFF -   2 - Mini V2.0e and later.
 * 011 - 0xFFFF -   3 - Mini V2.1a and later.
 * 011 - 0xFB04 -   4 - Mini V2.1d and later.
 * xxx - 0xFB05 -   5 - Mini V2.2a and later.
 *
 * This function will return -2 if the version number does not make sense.
 * This can happen when the Data1 byte contains "garbage". For example a
 * hardware revision that is <4 or the high byte is not the binary inverse of
 * the lower byte.
 * Note: The high byte of the Data1 option byte should always be the binary
 * inverse of the lower byte unless the byte is not set, then all bits in both
 * high and low byte are 0xFF.
 */
int platform_hwversion(void)
{
	static int hwversion = -1;
	uint16_t hwversion_pins = GPIO7 | GPIO6 | GPIO5;
	uint16_t unused_pins = hwversion_pins ^ 0xFFFF;

	/* Check if the hwversion is set in the user option byte. */
	if (hwversion == -1) {
		if (BMP_HWVERSION_BYTE != 0xFFFF) {
			/* Check if the data is valid.
			 * When valid it should only have values 4 and higher.
			 */
			if ((BMP_HWVERSION_BYTE >> 8) !=
			    (~BMP_HWVERSION_BYTE & 0xFF) ||
			    ((BMP_HWVERSION_BYTE & 0xFF) < 4)) {
				return -2;
			} else {
				hwversion = BMP_HWVERSION_BYTE & 0xFF;
			}
		}
	}

	/* If the hwversion is not set in option bytes check
	 * the hw pin strapping.
	 */
	if (hwversion == -1) {
		/* Configure the hardware version pins as input pull-up/down */
		gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
				GPIO_CNF_INPUT_PULL_UPDOWN,
				hwversion_pins);

		/* Enable the weak pull up. */
		gpio_set(GPIOB, hwversion_pins);

		/* Wait a little to make sure the pull up is in effect... */
		for(int i = 0; i < 100; i++) asm("nop");

		/* Get all pins that are pulled low in hardware.
		 * This also sets all the "unused" pins to 1.
		 */
		uint16_t pins_negative = gpio_get(GPIOB, hwversion_pins) | unused_pins;

		/* Enable the weak pull down. */
		gpio_clear(GPIOB, hwversion_pins);

		/* Wait a little to make sure the pull down is in effect... */
		for(int i = 0; i < 100; i++) asm("nop");

		/* Get all the pins that are pulled high in hardware. */
		uint16_t pins_positive = gpio_get(GPIOB, hwversion_pins);

		/* Hardware version is the id defined by the pins that are
		 * asserted low or high by the hardware. This means that pins
		 * that are left floating are 0 and those that are either
		 * pulled high or low are 1.
		 */
		hwversion = (((pins_positive ^ pins_negative) ^ 0xFFFF) & hwversion_pins) >> 5;
	}

	return hwversion;
}

void platform_init(void)
{
	SCS_DEMCR |= SCS_DEMCR_VC_MON_EN;
#ifdef ENABLE_DEBUG
	void initialise_monitor_handles(void);
	initialise_monitor_handles();
#endif

	rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_USB);
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRC);

	/* Setup GPIO ports */
	gpio_clear(USB_PU_PORT, USB_PU_PIN);
	gpio_set_mode(USB_PU_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT,
			USB_PU_PIN);

	gpio_set_mode(JTAG_PORT, GPIO_MODE_OUTPUT_50_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL,
			TMS_DIR_PIN | TMS_PIN | TCK_PIN | TDI_PIN);
	gpio_set_mode(JTAG_PORT, GPIO_MODE_OUTPUT_50_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL,
			TMS_DIR_PIN | TCK_PIN | TDI_PIN);
	gpio_set_mode(JTAG_PORT, GPIO_MODE_OUTPUT_50_MHZ,
			GPIO_CNF_INPUT_FLOAT, TMS_PIN);
	/* This needs some fixing... */
	/* Toggle required to sort out line drivers... */
	gpio_port_write(GPIOA, 0x8102);
	gpio_port_write(GPIOB, 0x2000);

	gpio_port_write(GPIOA, 0x8182);
	gpio_port_write(GPIOB, 0x2002);

	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL,
			LED_UART | LED_IDLE_RUN | LED_ERROR);

	/* Enable SRST output. Original uses a NPN to pull down, so setting the
	 * output HIGH asserts. Mini is directly connected so use open drain output
	 * and set LOW to assert.
	 */
	platform_srst_set_val(false);
	gpio_set_mode(SRST_PORT, GPIO_MODE_OUTPUT_50_MHZ,
			(((platform_hwversion() == 0) ||
			  (platform_hwversion() >= 3))
			 ? GPIO_CNF_OUTPUT_PUSHPULL
			 : GPIO_CNF_OUTPUT_OPENDRAIN),
			SRST_PIN);
	/* FIXME: Gareth, Esden, what versions need this fix? */
	if (platform_hwversion() < 3) {
		/* FIXME: This pin in intended to be input, but the TXS0108 fails
		 * to release the device from reset if this floats. */
		gpio_set_mode(SRST_SENSE_PORT, GPIO_MODE_OUTPUT_2_MHZ,
					  GPIO_CNF_OUTPUT_PUSHPULL, SRST_SENSE_PIN);
	} else {
		gpio_set(SRST_SENSE_PORT, SRST_SENSE_PIN);
		gpio_set_mode(SRST_SENSE_PORT, GPIO_MODE_INPUT,
		              GPIO_CNF_INPUT_PULL_UPDOWN, SRST_SENSE_PIN);
	}
	/* Enable internal pull-up on PWR_BR so that we don't drive
	   TPWR locally or inadvertently supply power to the target. */
	if (platform_hwversion () == 1) {
		gpio_set(PWR_BR_PORT, PWR_BR_PIN);
		gpio_set_mode(PWR_BR_PORT, GPIO_MODE_INPUT,
		              GPIO_CNF_INPUT_PULL_UPDOWN, PWR_BR_PIN);
	} else if (platform_hwversion() > 1) {
		gpio_set(PWR_BR_PORT, PWR_BR_PIN);
		gpio_set_mode(PWR_BR_PORT, GPIO_MODE_OUTPUT_50_MHZ,
		              GPIO_CNF_OUTPUT_OPENDRAIN, PWR_BR_PIN);
	}

	if (platform_hwversion() > 0) {
		adc_init();
	} else {
		gpio_clear(GPIOB, GPIO0);
		gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
				GPIO_CNF_INPUT_PULL_UPDOWN, GPIO0);
	}
	/* Relocate interrupt vector table here */
	extern int vector_table;
	SCB_VTOR = (uint32_t)&vector_table;

	platform_timing_init();
	cdcacm_init();

	/* On mini hardware, UART and SWD share connector pins.
	 * Don't enable UART if we're being debugged. */
	if ((platform_hwversion() == 0) || !(SCS_DEMCR & SCS_DEMCR_TRCENA))
		usbuart_init();

	setup_vbus_irq();
}

void platform_srst_set_val(bool assert)
{
	gpio_set_val(TMS_PORT, TMS_PIN, 1);
	if ((platform_hwversion() == 0) ||
	    (platform_hwversion() >= 3)) {
		gpio_set_val(SRST_PORT, SRST_PIN, assert);
	} else {
		gpio_set_val(SRST_PORT, SRST_PIN, !assert);
	}
	if (assert) {
		for(int i = 0; i < 10000; i++) asm("nop");
	}
}

bool platform_srst_get_val(void)
{
	if (platform_hwversion() == 0) {
		return gpio_get(SRST_SENSE_PORT, SRST_SENSE_PIN) == 0;
	} else if (platform_hwversion() >= 3) {
		return gpio_get(SRST_SENSE_PORT, SRST_SENSE_PIN) != 0;
	} else {
		return gpio_get(SRST_PORT, SRST_PIN) == 0;
	}
}

bool platform_target_get_power(void)
{
	if (platform_hwversion() > 0) {
		return !gpio_get(PWR_BR_PORT, PWR_BR_PIN);
  	}
	return 0;
}

void platform_target_set_power(bool power)
{
	if (platform_hwversion() > 0) {
		gpio_set_val(PWR_BR_PORT, PWR_BR_PIN, !power);
	}
}

static void adc_init(void)
{
	rcc_periph_clock_enable(RCC_ADC1);

	gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_ANALOG, GPIO0);

	adc_power_off(ADC1);
	adc_disable_scan_mode(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_28DOT5CYC);

	adc_power_on(ADC1);

	/* Wait for ADC starting up. */
	for (int i = 0; i < 800000; i++)    /* Wait a bit. */
		__asm__("nop");

	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);
}

uint32_t platform_target_voltage_sense(void)
{
	/* returns the voltage in volt scaled by 10 (so 33 means 3.3V), except
	 * for hardware version 1
	 * this function is only needed for implementations that allow the
	 * target to be powered from the debug probe
	 */
	if (platform_hwversion() == 0)
		return 0;

	const uint8_t channel = 8;
	adc_set_regular_sequence(ADC1, 1, (uint8_t*)&channel);

	adc_start_conversion_direct(ADC1);

	/* Wait for end of conversion. */
	while (!adc_eoc(ADC1));

	uint32_t val = adc_read_regular(ADC1); /* 0-4095 */
	return (val * 99) / 8191;
}

const char *platform_target_voltage(void)
{
	if (platform_hwversion() == 0)
		return gpio_get(GPIOB, GPIO0) ? "OK" : "ABSENT!";

	static char ret[] = "0.0V";
	uint32_t val = platform_target_voltage_sense();
	ret[0] = '0' + val / 10;
	ret[2] = '0' + val % 10;

	return ret;
}

void platform_request_boot(void)
{
	/* Disconnect USB cable */
	gpio_set_mode(USB_PU_PORT, GPIO_MODE_INPUT, 0, USB_PU_PIN);

	/* Drive boot request pin */
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
			GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);
	gpio_clear(GPIOB, GPIO12);
}

void exti15_10_isr(void)
{
	if (gpio_get(USB_VBUS_PORT, USB_VBUS_PIN)) {
		/* Drive pull-up high if VBUS connected */
		gpio_set_mode(USB_PU_PORT, GPIO_MODE_OUTPUT_10_MHZ,
				GPIO_CNF_OUTPUT_PUSHPULL, USB_PU_PIN);
	} else {
		/* Allow pull-up to float if VBUS disconnected */
		gpio_set_mode(USB_PU_PORT, GPIO_MODE_INPUT,
				GPIO_CNF_INPUT_FLOAT, USB_PU_PIN);
	}

	exti_reset_request(USB_VBUS_PIN);
}

static void setup_vbus_irq(void)
{
	nvic_set_priority(USB_VBUS_IRQ, IRQ_PRI_USB_VBUS);
	nvic_enable_irq(USB_VBUS_IRQ);

	gpio_set(USB_VBUS_PORT, USB_VBUS_PIN);
	gpio_set(USB_PU_PORT, USB_PU_PIN);

	gpio_set_mode(USB_VBUS_PORT, GPIO_MODE_INPUT,
			GPIO_CNF_INPUT_PULL_UPDOWN, USB_VBUS_PIN);

	/* Configure EXTI for USB VBUS monitor */
	exti_select_source(USB_VBUS_PIN, USB_VBUS_PORT);
	exti_set_trigger(USB_VBUS_PIN, EXTI_TRIGGER_BOTH);
	exti_enable_request(USB_VBUS_PIN);

	exti15_10_isr();
}
