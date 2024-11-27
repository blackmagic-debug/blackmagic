/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * Implementation for ctxLink by Sid Price sid@sidprice.com
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

/* This file implements the platform specific functions for the F4 Discovery implementation. */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "aux_serial.h"
#include "morse.h"
#include "exception.h"
#include <string.h>
#include <stdatomic.h>

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/syscfg.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/cortex.h>
#include <libopencm3/usb/dwc/otg_fs.h>
#include <libopencm3/stm32/f4/flash.h>
#include "WiFi_Server.h"
#include "winc1500_api.h"

//
// With a 3V3 reference voltage and using a 12 bit ADC each bit represents 0.8mV
//  Note the battery voltage is divided by 2 with resistor divider
//
// No battery voltage 1 == 2.0v
// No battery voltage 2 == 4.268v
// Battery present (report voltage) < 4.268v
// Low batter voltage == 3.6v
//
#define BATTERY_VOLTAGE_1 2000U
#define BATTERY_VOLTAGE_2 4268U
#define BATTERY_LOW       3600U

#define CTXLINK_BATTERY_INPUT        0 // ADC Channel for battery input
#define CTXLINK_TARGET_VOLTAGE_INPUT 8 // ADC Chanmel for target voltage

#define CTXLINK_ADC_BATTERY 0
#define CTXLINK_ADC_TARGET  1

bool last_battery_state = true;
bool battery_present = false;

#define READ_ADC_PERIOD 100 //< ms count
static uint32_t read_adc_period = READ_ADC_PERIOD;

#define MAX_ADC_PHASE 4
static uint8_t adc_phase;

_Atomic uint32_t input_voltages[2] = {0};
static uint8_t adc_channels[] = {CTXLINK_BATTERY_INPUT, CTXLINK_TARGET_VOLTAGE_INPUT}; /// ADC channels used by ctxLink

typedef void (*irq_function_t)(void);

static void adc_init(void)
{
	rcc_periph_clock_enable(RCC_ADC1);
	gpio_mode_setup(TPWR_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, TPWR_PIN); // Target voltage monitor input
	gpio_mode_setup(VBAT_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, VBAT_PIN); // Battery voltage monitor input

	adc_power_off(ADC1);
	adc_disable_scan_mode(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_480CYC);

	adc_power_on(ADC1);
	/* Wait for ADC starting up. */
	for (volatile size_t i = 0; i < 800000; i++) /* Wait a bit. */
		__asm__("nop");
}

//
// Use the passed string to configure the USB UART
//
// e.g. 38400,8,N,1
bool platform_configure_uart(char *configuration_string)
{
	if (strlen(configuration_string) > 5) {
		uint32_t baudRate;
		uint32_t bits;
		uint32_t stopBits;
		char parity;
		int count = sscanf(
			configuration_string, "%" SCNd32 ",%" SCNd32 ",%c,%" SCNd32 "", &baudRate, &bits, &parity, &stopBits);
		if (count == 4) {
			uint32_t parityValue;
			usart_set_baudrate(USBUSART, baudRate);
			usart_set_databits(USBUSART, bits);
			usart_set_stopbits(USBUSART, stopBits);
			switch (parity) {
			default:
			case 'N': {
				parityValue = USART_PARITY_NONE;
				break;
			}

			case 'O': {
				parityValue = USART_PARITY_ODD;
				break;
			}

			case 'E': {
				parityValue = USART_PARITY_EVEN;
				break;
			}
			}
			usart_set_parity(USBUSART, parityValue);
			return true;
		}
	}
	return false;
}

void wifi_init(void)
{
	//
	// Initialize the WiFi server app
	//
	m2m_wifi_init();
	app_initialize();
}

uint32_t platform_target_voltage_sense(void)
{
	return atomic_load_explicit(&input_voltages[CTXLINK_ADC_TARGET], memory_order_relaxed);
}

int platform_hwversion(void)
{
	return 0;
}

void platform_init(void)
{
	/* Enable GPIO peripherals */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_GPIOD);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
	//
	// Initialize the "Bootloader" input, it is used in
	// normal running mode as the WPS selector switch
	// The switch is active low and therefore needs
	// a pullup
	//
	gpio_mode_setup(SWITCH_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, SW_BOOTLOADER_PIN);
	/*
		Check the Bootloader button
	*/
	if (!gpio_get(SWITCH_PORT, SW_BOOTLOADER_PIN))
		platform_request_boot(); // Does not return from this call

#pragma GCC diagnostic pop
	rcc_clock_setup_pll(&rcc_hse_8mhz_3v3[RCC_CLOCK_3V3_84MHZ]);

	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_OTGFS);
	rcc_periph_clock_enable(RCC_CRC);
	/*
		toggle the PWR_BR and SRST pins

		this is what the native BMP does, don't really know why
	*/
	gpio_port_write(GPIOA, 0xa102);
	gpio_port_write(GPIOB, 0x0000);

	gpio_port_write(GPIOA, 0xa182);
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
	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_IDLE_RUN | LED_ERROR | LED_MODE);
	gpio_mode_setup(LED_PORT_UART, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_UART);
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
	gpio_set_output_options(GPIOB, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, PWR_BR_PIN);

	platform_timing_init();
	adc_init();
	wifi_init(); //	Setup the Wifi channel
#ifdef WINC_1500_FIRMWARE_UPDATE
	//
	// ONLY for firmware update
	//
	// Perform WINC1500 reset sequence
	//
	m2mStub_PinSet_CE(M2M_WIFI_PIN_LOW);
	m2mStub_PinSet_RESET(M2M_WIFI_PIN_LOW);
	platform_delay(100);
	m2mStub_PinSet_CE(M2M_WIFI_PIN_HIGH);
	platform_delay(10);
	m2mStub_PinSet_RESET(M2M_WIFI_PIN_HIGH);
	platform_delay(10);
	while (true)
		continue;
#endif

	blackmagic_usb_init();
	aux_serial_init();

	/* https://github.com/libopencm3/libopencm3/pull/1256#issuecomment-779424001 */
	OTG_FS_GCCFG |= OTG_GCCFG_NOVBUSSENS | OTG_GCCFG_PWRDWN;
	OTG_FS_GCCFG &= ~(OTG_GCCFG_VBUSBSEN | OTG_GCCFG_VBUSASEN);

	/* By default, do not drive the SWD bus too fast. */
}

//
// The following method is called in the main gdb loop in order to run
// the app and wifi tasks
//
// It also checks for GDB packets from a connected WiFi client
//
//	Return "0" if no WiFi client or no data from client
//	Return number of bytes available from the WiFi client
//

static bool startup = true; ///< True to startup

////////////////////////////////////////////////////////////////////////////////////////////////////
/// <summary> Platform tasks.</summary>
///
/// <remarks> Sid Price, 3/15/2018.</remarks>
////////////////////////////////////////////////////////////////////////////////////////////////////

void platform_tasks(void)
{
	app_task(); // WiFi Server app tasks
	if (startup == true) {
		startup = false;
		platform_delay(1000);
	}
	m2m_wifi_task();   // WINC1500 tasks
	gdb_tcp_server();  // Run the TCP sever state machine
	data_tcp_server(); // Run the Uart/Debug TCP server
}

void platform_nrst_set_val(bool assert)
{
	(void)assert;
}

bool platform_nrst_get_val(void)
{
	return false;
}

void platform_read_adc(void)
{
	if (--read_adc_period != 0)
		return;
	read_adc_period = READ_ADC_PERIOD;
	//
	// The ADC is read here; there are two channels read for the target
	// and battery. A 4-phase system is used:
	//	phase 0 -> start target voltage conversion
	//	phase 1 -> Read and update the cached target voltage
	//	phase 2 -> start the battery voltage conversion
	//	phase 3 -> read and update the cached battery voltage
	switch (adc_phase) {
	default: {
		//
		// Something went wrong, reset the phase and start again
		//
		adc_phase = 0;
		BMD_FALLTHROUGH;
	}
	case 0: {
		adc_set_regular_sequence(ADC1, 1, &adc_channels[CTXLINK_ADC_TARGET]);
		adc_start_conversion_regular(ADC1);
		break;
	}
	case 1: {
		atomic_store_explicit(
			&input_voltages[CTXLINK_ADC_TARGET], (adc_read_regular(ADC1) * 16U) / 10U, memory_order_relaxed);
		break;
	}
	case 2: {
		adc_set_regular_sequence(ADC1, 1, &adc_channels[CTXLINK_ADC_BATTERY]);
		adc_start_conversion_regular(ADC1);
		break;
	}
	case 3:
		atomic_store_explicit(
			&input_voltages[CTXLINK_ADC_BATTERY], (adc_read_regular(ADC1) * 16U) / 10U, memory_order_relaxed);
		break;
	}
	adc_phase = (adc_phase + 1) % MAX_ADC_PHASE;
}

const char *platform_target_voltage(void)
{
	static char target[64] = {0};
	memset(target, 0, sizeof(target));
	uint32_t val = platform_target_voltage_sense();
	target[0] = '0' + val / 1000U;
	target[1] = '.';
	val = val % 1000U;
	target[2] = '0' + val / 100U;
	val = val % 100U;
	target[3] = '0' + val / 10U;
	target[4] = 'V';
	strncat(target, platform_battery_voltage(), sizeof(target) - 1);
	return target;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"

void platform_request_boot(void)
{
	/* Switch mapping at 0x0 from internal Flash to MaskROM and reboot CM4 into it */
	rcc_periph_clock_enable(RCC_SYSCFG);
	SYSCFG_MEMRM = (SYSCFG_MEMRM & ~3U) | 1U;
	scb_reset_core();
}

#pragma GCC diagnostic pop

bool platform_target_get_power(void)
{
	return !gpio_get(PWR_BR_PORT, PWR_BR_PIN);
}

bool platform_target_set_power(const bool power)
{
	gpio_set_val(PWR_BR_PORT, PWR_BR_PIN, !power);
	return true;
}

void platform_target_clk_output_enable(bool enable)
{
	(void)enable;
}

const char *platform_battery_voltage(void)
{
	static char ret[64] = {0};
	if (battery_present == true) {
		uint32_t battery_voltage = input_voltages[CTXLINK_ADC_BATTERY];
		//
		// Format the return string, this will be appened to the target voltage string
		//
		strcpy(ret, "\n      Battery : ");
		uint32_t append_index = strlen(ret);
		ret[append_index++] = '0' + battery_voltage / 1000U;
		ret[append_index++] = '.';
		battery_voltage = battery_voltage % 1000;
		ret[append_index++] = '0' + battery_voltage / 100U;
		battery_voltage = battery_voltage % 100;
		ret[append_index++] = '0' + battery_voltage / 10U;
		ret[append_index++] = 'V';
		ret[append_index++] = '\n';
		ret[append_index] = 0x00;
	} else
		memcpy(ret, "\n      Battery : Not present", strlen("\n      Battery : Not present") + 1);
	return ret;
}

bool platform_check_battery_voltage(void)
{
	//
	// Is battery connected?
	//
	uint32_t voltage = atomic_load_explicit(&input_voltages[CTXLINK_ADC_BATTERY], memory_order_relaxed);
	if (voltage <= BATTERY_VOLTAGE_1 || voltage >= BATTERY_VOLTAGE_2) {
		battery_present = false;
		last_battery_state = true;
	} else {
		battery_present = true;
		//
		// Is the voltage good?
		//
		last_battery_state = voltage > BATTERY_LOW;
	}
	return last_battery_state;
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
