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

/* This file implements the platform specific functions for the ctxLink implementation. */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "aux_serial.h"
#include "morse.h"

#include <libopencm3/cm3/vector.h>
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
#include <libopencm3/stm32/timer.h>
#include <libopencm3/cm3/systick.h>

static void adc_init(void);
static void setup_vbus_irq(void);

#define TPWR_SOFT_START_STEPS 64U

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
	ctxLink has no hardware option GPIOs and does not
	use the BMP Native approach of using a user option byte
 */
int platform_hwversion(void)
{
	return 1;
}

void platform_init(void)
{
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	//
	// Initialize the "Bootloader" input, it is used in
	// normal running mode as the WPS selector switch
	// The switch is active low and therefore needs
	// a pullup
	//
	gpio_mode_setup(SWITCH_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, SW_BOOTLOADER_PIN);
	/*
	Check the Bootloader button, not sure this is needed fro the native-derived hardware, need to check if
	this switch is looked at in the DFU bootloader
	*/
	if (!(gpio_get(SWITCH_PORT,
			SW_BOOTLOADER_PIN))) // SJP - 0118_2016, changed to use defs in platform.h and the switch is active low!
	{
		platform_request_boot(); // Does not return from this call
	}
	//
	// Normal running ... set up clocks and peripherals
	//
	//rcc_clock_setup_hse_3v3( &rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_84MHZ] );
	rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_84MHZ]);

	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_OTGFS);

	rcc_peripheral_enable_clock(&RCC_AHB1ENR, RCC_AHB1ENR_CRCEN);
	/*
		toggle the PWR_BR and SRST pins
		
		this is what the native BMP does, don't really know why
	*/
	gpio_port_write(GPIOA, 0xA102);
	gpio_port_write(GPIOB, 0x0000);

	gpio_port_write(GPIOA, 0xA182);
	gpio_port_write(GPIOB, 0x0002);

	/*
	 * Set up USB Pins and alternate function
	 * 
	 * Setup REN output
	 * 
	 */
	gpio_clear(USB_PU_PORT, USB_PU_PIN);
	gpio_mode_setup(USB_PU_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, USB_PU_PIN);
	/*
	 * USB DM & DP pins
	 */
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO9);
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
	gpio_set_af(GPIOA, GPIO_AF10, GPIO9 | GPIO11 | GPIO12);
	//
	// SJP - 0119_2016
	//
	// The following sets the register speed for the JTAG/SWD bits
	//
	// See the spreadsheet "SWD Port speed bits - OneNote"
	//
	GPIOA_OSPEEDR &= ~(TCK_PIN | TMS_PIN | TDI_PIN); // Clear the speed bits for TCK, TMS, & TDI
	GPIOA_OSPEEDR |= (TCK_PIN | TMS_PIN | TDI_PIN);  // Set TCK, TMS,& TDI to "Fast speed"
	gpio_mode_setup(JTAG_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, TMS_DIR_PIN | TMS_PIN | TCK_PIN | TDI_PIN);

	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, TDO_PIN);
	//
	// Initialize the LED ports
	//
	gpio_mode_setup(LED_PORT_OTHER, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_IDLE_RUN | LED_ERROR | LED_CTX_MODE);
	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_UART);
	//
	// Setup RST_SENSE as input
	//
	//	Give it a pullup (NOT reset) just in case similar issue to
	// native firmware.
	//
	gpio_mode_setup(NRST_SENSE_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, NRST_SENSE_PIN);
	/* Enable nRST output. Original uses a NPN to pull down, so setting the
	 * output HIGH asserts. Mini is directly connected so use open drain output
	 * and set LOW to assert.
	 */
	platform_nrst_set_val(false);
	//
	// setup the iRSTR pin
	//
	gpio_mode_setup(NRST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, NRST_PIN);
	/* 
	 * Enable internal pull-up on PWR_BR so that we don't drive
	 * TPWR locally or inadvertently supply power to the target. 
	 */
	gpio_set(PWR_BR_PORT, PWR_BR_PIN);
	gpio_mode_setup(PWR_BR_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PWR_BR_PIN);
	gpio_set_output_options(PWR_BR_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, PWR_BR_PIN);

	adc_init();
	platform_timing_init();
	blackmagic_usb_init();
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

static inline void platform_wait_pwm_cycle()
{
	while (!timer_get_flag(TIM1, TIM_SR_UIF))
		continue;
	timer_clear_flag(TIM1, TIM_SR_UIF);
}

bool platform_target_set_power(const bool power)
{
	if (platform_hwversion() <= 0)
		return false;
	/* If we're on hw1 or newer, and are turning the power on */
	if (power) {
		/* Configure the pin to be driven by the timer */
		gpio_mode_setup(PWR_BR_PORT, GPIO_MODE_OUTPUT, GPIO_MODE_AF, PWR_BR_PIN);
		timer_clear_flag(TIM1, TIM_SR_UIF);
		/* Wait for one PWM cycle to have taken place */
		platform_wait_pwm_cycle();
		/* Soft start power on the target */
		for (size_t step = 1U; step < TPWR_SOFT_START_STEPS; ++step) {
			/* Set the new PWM value */
			timer_set_oc_value(TIM1, TIM_OC3, step);
			/* Wait for one PWM cycle to have taken place */
			platform_wait_pwm_cycle();
		}
	}
	/* Set the pin state */
	gpio_set_val(PWR_BR_PORT, PWR_BR_PIN, !power);
	/*
	 * If we're turning power on and running hw1+, now configure the pin back over to GPIO and
	 * reset state timer for the next request
	 */
	if (power) {

		gpio_mode_setup(PWR_BR_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, PWR_BR_PIN);
		timer_set_oc_value(TIM1, TIM_OC3, 0U);
	}
	return true;
}

static void adc_init(void)
{
	rcc_periph_clock_enable(RCC_ADC1);

	gpio_mode_setup(TPWR_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, TPWR_PIN);

	adc_power_off(ADC1);
	adc_disable_scan_mode(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_right_aligned(ADC1);

	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_56CYC);	// TODO Check if this is OK
	adc_enable_temperature_sensor();
	adc_power_on(ADC1);

	/* Wait for the ADC to finish starting up */
	for (volatile size_t i = 0; i < 800000U; ++i)
		continue;

	// TODO Attention for F4
	// adc_reset_calibration(ADC1);
	// adc_calibrate(ADC1);
}

uint32_t platform_target_voltage_sense(void)
{
	/*
	 * Returns the voltage in tenths of a volt (so 33 means 3.3V)
	 */

	uint8_t channel = 8;
	adc_set_regular_sequence(ADC1, 1, &channel);

	adc_start_conversion_regular(ADC1);

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
	gpio_mode_setup(USB_PU_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, USB_PU_PIN);
	gpio_mode_setup(USB_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, USB_DP_PIN | USB_DM_PIN);
	gpio_clear(USB_PORT, USB_DP_PIN | USB_DM_PIN);
	/* Make sure we drive the USB reset condition for at least 10ms */
	while (!(STK_CSR & STK_CSR_COUNTFLAG))
		continue;
	for (size_t count = 0U; count < 10U * SYSTICKMS; ++count) {
		while (!(STK_CSR & STK_CSR_COUNTFLAG))
			continue;
	}

	/* Drive boot request pin */
	gpio_mode_setup(SWITCH_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SW_BOOTLOADER_PIN);
	gpio_clear(GPIOB, GPIO12);
}

void platform_target_clk_output_enable(bool enable)
{
	(void)enable ;
	// TODO Can this be removed?
	// if (platform_hwversion() >= 6) {
	// 	/* If we're switching to tristate mode, first convert the processor pin to an input */
	// 	if (!enable)
	// 		gpio_set_mode(TCK_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, TCK_PIN);
	// 	/* Reconfigure the logic levelt translator */
	// 	gpio_set_val(TCK_DIR_PORT, TCK_DIR_PIN, enable);
	// 	/* If we're switching back out of tristate mode, we're now safe to make the processor pin an output again */
	// 	if (enable)
	// 		gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
	// }
}

// bool platform_spi_init(const spi_bus_e bus)
// {
// 	if (bus == SPI_BUS_EXTERNAL) {
// 		rcc_periph_clock_enable(RCC_SPI1);
// 		rcc_periph_reset_pulse(RST_SPI1);
// 		platform_target_clk_output_enable(true);
// 		gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, TCK_PIN);
// 		gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, TDI_PIN);
// 		gpio_set(TMS_DIR_PORT, TMS_DIR_PIN);
// 	} else {
// 		rcc_periph_clock_enable(RCC_SPI2);
// 		rcc_periph_reset_pulse(RST_SPI2);
// 	}

// 	const uint32_t controller = bus == SPI_BUS_EXTERNAL ? EXT_SPI : AUX_SPI;
// 	spi_init_master(controller, SPI_CR1_BAUDRATE_FPCLK_DIV_8, SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE,
// 		SPI_CR1_CPHA_CLK_TRANSITION_1, SPI_CR1_DFF_8BIT, SPI_CR1_MSBFIRST);
// 	spi_enable(controller);
// 	return true;
// }

// bool platform_spi_deinit(spi_bus_e bus)
// {
// 	spi_disable(bus == SPI_BUS_EXTERNAL ? EXT_SPI : AUX_SPI);

// 	if (bus == SPI_BUS_EXTERNAL) {
// 		rcc_periph_clock_disable(RCC_SPI1);
// 		gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
// 		gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);
// 		platform_target_clk_output_enable(false);
// 	} else
// 		rcc_periph_clock_disable(RCC_SPI2);
// 	return true;
// }

// bool platform_spi_chip_select(const uint8_t device_select)
// {
// 	const uint8_t device = device_select & 0x7fU;
// 	const bool select = !(device_select & 0x80U);
// 	uint32_t port = AUX_PORT;
// 	uint16_t pin;
// 	switch (device) {
// 	case SPI_DEVICE_INT_FLASH:
// 		pin = AUX_FCS;
// 		break;
// 	case SPI_DEVICE_EXT_FLASH:
// 		port = EXT_SPI_CS_PORT;
// 		pin = EXT_SPI_CS;
// 		break;
// 	case SPI_DEVICE_SDCARD:
// 		pin = AUX_SDCS;
// 		break;
// 	case SPI_DEVICE_DISPLAY:
// 		pin = AUX_DCS;
// 		break;
// 	default:
// 		return false;
// 	}
// 	gpio_set_val(port, pin, select);
// 	return true;
// }

// uint8_t platform_spi_xfer(const spi_bus_e bus, const uint8_t value)
// {
// 	return spi_xfer(bus == SPI_BUS_EXTERNAL ? EXT_SPI : AUX_SPI, value);
//}

void exti9_5_isr(void)
{
	uint32_t usb_vbus_port;
	uint16_t usb_vbus_pin;

	usb_vbus_port = USB_VBUS5_PORT;
	usb_vbus_pin = USB_VBUS5_PIN;

	if (gpio_get(usb_vbus_port, usb_vbus_pin))
		/* Drive pull-up high if VBUS connected */
		gpio_mode_setup(USB_PU_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, USB_PU_PIN);
	else
		/* Allow pull-up to float if VBUS disconnected */
		gpio_mode_setup(USB_PU_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE, USB_PU_PIN);

	exti_reset_request(usb_vbus_pin);
}

static void setup_vbus_irq(void)
{
	uint32_t usb_vbus_port;
	uint16_t usb_vbus_pin;

	usb_vbus_port = USB_VBUS5_PORT;
	usb_vbus_pin = USB_VBUS5_PIN;

	nvic_set_priority(USB_VBUS_IRQ, IRQ_PRI_USB_VBUS);
	nvic_enable_irq(USB_VBUS_IRQ);

	gpio_set(usb_vbus_port, usb_vbus_pin);
	gpio_set(USB_PU_PORT, USB_PU_PIN);

	gpio_mode_setup(usb_vbus_port, GPIO_MODE_INPUT, GPIO_PUPD_NONE, usb_vbus_pin);

	/* Configure EXTI for USB VBUS monitor */
	exti_select_source(usb_vbus_pin, usb_vbus_port);
	exti_set_trigger(usb_vbus_pin, EXTI_TRIGGER_BOTH);
	exti_enable_request(usb_vbus_pin);

	exti9_5_isr();
}
