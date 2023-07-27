/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
 * Modified by Rafael Silva <perigoso@riseup.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	 If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Originally based on code from:
 * https://gitlab.zapb.de/libjaylink/libjaylink and
 * https://github.com/afaerber/jlink
 */

#include "general.h"
#include "gdb_if.h"
#include "adiv5.h"
#include "jlink.h"
#include "jlink_protocol.h"
#include "exception.h"
#include "buffer_utils.h"

#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/time.h>
#include <libusb.h>

#include "cli.h"

typedef struct jlink {
	char fw_version[256U];         /* Firmware version string */
	uint32_t hw_version;           /* Hardware version */
	uint32_t capabilities;         /* Bitfield of supported capabilities */
	uint32_t available_interfaces; /* Bitfield of available interfaces */
	uint32_t base_frequency;       /* Base frequency of the interface */
	uint16_t min_divisor;          /* Minimum divisor for the interface */
	uint16_t current_divisor;      /* Current divisor for the interface */
} jlink_s;

jlink_s jlink;

/* J-Link USB protocol functions */

bool jlink_simple_query(const uint8_t command, void *const rx_buffer, const size_t rx_len)
{
	return bmda_usb_transfer(info.usb_link, &command, sizeof(command), rx_buffer, rx_len, JLINK_USB_TIMEOUT) >= 0;
}

bool jlink_simple_request_8(const uint8_t command, const uint8_t operation, void *const rx_buffer, const size_t rx_len)
{
	const uint8_t request[2U] = {command, operation};
	return bmda_usb_transfer(info.usb_link, request, sizeof(request), rx_buffer, rx_len, JLINK_USB_TIMEOUT) >= 0;
}

bool jlink_simple_request_16(
	const uint8_t command, const uint16_t operation, void *const rx_buffer, const size_t rx_len)
{
	uint8_t request[3U] = {command};
	write_le2(request, 1U, operation);
	return bmda_usb_transfer(info.usb_link, request, sizeof(request), rx_buffer, rx_len, JLINK_USB_TIMEOUT) >= 0;
}

bool jlink_simple_request_32(
	const uint8_t command, const uint32_t operation, void *const rx_buffer, const size_t rx_len)
{
	uint8_t request[5U] = {command};
	write_le4(request, 1U, operation);
	return bmda_usb_transfer(info.usb_link, request, sizeof(request), rx_buffer, rx_len, JLINK_USB_TIMEOUT) >= 0;
}

/*
 * This runs JLINK_CMD_IO_TRANSACTION transactions, these have the following format:
 * ┌─────────┬─────────┬───────────────┬───────┐
 * │    0    │    1    │       2       │   3   │
 * ├╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┴╌╌╌╌╌╌╌┤
 * │ Command │  Align  │      Cycle count      │
 * ├─────────┼─────────┼───────────────┬───────┤
 * │    4    │   ...   │ 4 + tms_bytes │  ...  │
 * ├╌╌╌╌╌╌╌╌╌┴╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┴╌╌╌╌╌╌╌┤
 * │ TMS data bytes... │   TDI data bytes...   │
 * └───────────────────┴───────────────────────┘
 * where the byte counts for each of TDI and TMS are defined by:
 * count = ⌈cycle_count / 8⌉
 *
 * ⌈⌉ is defined as the ceiling function.
 *
 * In SWD mode, the `tms` buffer represents direction states and
 * the `tdi` buffer represents SWDIO data to send to the device
 *
 * RM08001 Reference manual for J-Link USB Protocol 
 * §5.5.12 EMU_CMD_HW_JTAG3 
 */
bool jlink_transfer(const uint16_t clock_cycles, const uint8_t *const tms, const uint8_t *const tdi, uint8_t *const tdo)
{
	if (!clock_cycles)
		return true;
	/*
	 * The max number of bits to transfer is one shy of 64kib, meaning byte_count tops out at 8kiB.
	 * However, we impose an "artificial" limit that it's not allowed to exceed 512B (4096b)
	 * as we shouldn't be generating anything larger anyway.
	 */
	const size_t byte_count = (clock_cycles + 7U) >> 3U;
	if (byte_count > 512U)
		return false;
	/* Allocate a stack buffer for the transfer */
	uint8_t buffer[1028U] = {0};
	/* The first 4 bytes define the parameters of the transaction, so map the transfer structure there */
	jlink_io_transact_s *header = (jlink_io_transact_s *)buffer;
	header->command = JLINK_CMD_IO_TRANSACTION;
	write_le2(header->clock_cycles, 0, clock_cycles);
	/* Copy in the TMS state values to transmit (if present) */
	if (tms)
		memcpy(buffer + 4U, tms, byte_count);
	/* Copy in the TDI values to transmit (if present) */
	if (tdi)
		memcpy(buffer + 4U + byte_count, tdi, byte_count);
	/* Send the resulting transaction and try to read back the response data */
	if (bmda_usb_transfer(
			info.usb_link, buffer, sizeof(*header) + (byte_count * 2U), buffer, byte_count, JLINK_USB_TIMEOUT) < 0 ||
		/* Try to read back the transaction return code */
		bmda_usb_transfer(info.usb_link, NULL, 0, buffer + byte_count, 1U, JLINK_USB_TIMEOUT) < 0)
		return false;
	/* Copy out the response into the TDO buffer (if present) */
	if (tdo)
		memcpy(tdo, buffer, byte_count);
	/* Check that the response code is 0 ('OK') */
	return buffer[byte_count] == 0U;
}

bool jlink_transfer_fixed_tms(
	const uint16_t clock_cycles, const bool final_tms, const uint8_t *const tdi, uint8_t *const tdo)
{
	if (!clock_cycles)
		return true;
	/*
	 * The max number of bits to transfer is one shy of 64kib, meaning byte_count tops out at 8kiB.
	 * However, we impose an "artificial" limit that it's not allowed to exceed 512B (4096b)
	 * as we shouldn't be generating anything larger anyway.
	 */
	const size_t byte_count = (clock_cycles + 7U) >> 3U;
	if (byte_count > 512U)
		return false;
	/* Set up the buffer for TMS */
	uint8_t tms[512] = {0};
	/* Figure out the position of the final bit in the sequence */
	const size_t cycles = clock_cycles - 1U;
	const size_t final_byte = cycles >> 3U;
	const uint8_t final_bit = cycles & 7U;
	/* Mark the appropriate bit in the buffer with the value of final_tms */
	tms[final_byte] |= (final_tms ? 1U : 0U) << final_bit;
	/* Run the transfer */
	return jlink_transfer(clock_cycles, tms, tdi, tdo);
}

bool jlink_transfer_swd(
	const uint16_t clock_cycles, const jlink_swd_dir_e direction, const uint8_t *const data_in, uint8_t *const data_out)
{
	/* Define a buffer to hold the request direction information */
	uint8_t dir[8] = {0};
	/* Fill the direction buffer appropriately for the requested transfer direction */
	memset(dir, direction == JLINK_SWD_IN ? 0x00U : 0xffU, sizeof(dir));
	/* Run the resulting transfer */
	/* NOLINTNEXTLINE(readability-suspicious-call-argument) */
	return jlink_transfer(clock_cycles, dir, data_in, data_out);
}

/*
 * Try to claim the debugging interface of a J-Link adaptor.
 * On success this copies the endpoint addresses identified into the
 * usb_link_s sub-structure of bmp_info_s (info.usb_link) for later use.
 * Returns true for success, false for failure.
 *
 * Note: Newer J-Links use 2 bulk endpoints, one for "IN" (EP1) and one
 * for "OUT" (EP2) communication whereas old J-Links (V3, V4) only use
 * one endpoint (EP1) for "IN" and "OUT" communication.
 * Presently we only support the newer J-Links with 2 bulk endpoints.
 */
static bool jlink_claim_interface(void)
{
	libusb_config_descriptor_s *config;
	const int result = libusb_get_active_config_descriptor(info.libusb_dev, &config);
	if (result != LIBUSB_SUCCESS) {
		DEBUG_ERROR("Failed to get configuration descriptor: %s\n", libusb_error_name(result));
		return false;
	}
	const libusb_interface_descriptor_s *descriptor = NULL;
	for (size_t idx = 0; idx < config->bNumInterfaces; ++idx) {
		const libusb_interface_s *const interface = &config->interface[idx];
		// XXX: This fails to handle multiple alt-modes being present correctly.
		const libusb_interface_descriptor_s *const interface_desc = &interface->altsetting[0];
		if (interface_desc->bInterfaceClass == LIBUSB_CLASS_VENDOR_SPEC &&
			interface_desc->bInterfaceSubClass == LIBUSB_CLASS_VENDOR_SPEC && interface_desc->bNumEndpoints > 1U) {
			const int result = libusb_claim_interface(info.usb_link->device_handle, (int)idx);
			if (result) {
				DEBUG_ERROR("Can not claim handle: %s\n", libusb_error_name(result));
				break;
			}
			info.usb_link->interface = idx;
			descriptor = interface_desc;
		}
	}
	if (!descriptor) {
		DEBUG_ERROR("No suitable interface found\n");
		libusb_free_config_descriptor(config);
		return false;
	}
	for (size_t i = 0; i < descriptor->bNumEndpoints; i++) {
		const libusb_endpoint_descriptor_s *endpoint = &descriptor->endpoint[i];
		if (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN)
			info.usb_link->ep_rx = endpoint->bEndpointAddress;
		else
			info.usb_link->ep_tx = endpoint->bEndpointAddress;
	}
	libusb_free_config_descriptor(config);
	return true;
}

/* J-Link command functions and utils */

static char *jlink_hw_type_to_string(const uint8_t hw_type)
{
	switch (hw_type) {
	case JLINK_HARDWARE_VERSION_TYPE_JLINK:
		return "J-Link";
	case JLINK_HARDWARE_VERSION_TYPE_JTRACE:
		return "J-Trace";
	case JLINK_HARDWARE_VERSION_TYPE_FLASHER:
		return "Flasher";
	case JLINK_HARDWARE_VERSION_TYPE_JLINKPRO:
		return "J-Link Pro";
	default:
		return "Unknown";
	}
}

static char *jlink_interface_to_string(const uint8_t interface)
{
	switch (interface) {
	case JLINK_INTERFACE_JTAG:
		return "JTAG";
	case JLINK_INTERFACE_SWD:
		return "SWD";
	default:
		return "Unknown";
	}
}

static bool jlink_get_version(void)
{
	uint8_t buffer[4U];

	/* Read the firmware version, replies with a 2 byte length packet followed by the version string packet */
	if (!jlink_simple_query(JLINK_CMD_INFO_GET_FIRMWARE_VERSION, buffer, 2U))
		return false;

	/* Verify the version string fits in the buffer (expected value is 0x70) */
	const uint16_t version_length = read_le2(buffer, 0);
	if (version_length > sizeof(jlink.fw_version))
		return false;

	/* Read vesion string directly into jlink.version */
	bmda_usb_transfer(info.usb_link, NULL, 0, jlink.fw_version, version_length, JLINK_USB_TIMEOUT);
	jlink.fw_version[version_length - 1U] = '\0'; /* Ensure null termination */

	DEBUG_INFO("Firmware version: %s\n", jlink.fw_version);

	/* Read the hardware version if supported */
	if (jlink.capabilities & JLINK_CAPABILITY_HARDWARE_VERSION) {
		if (!jlink_simple_query(JLINK_CMD_INFO_GET_HARDWARE_VERSION, buffer, 4U))
			return false;

		jlink.hw_version = read_le4(buffer, 0);

		DEBUG_INFO("Hardware Version: %s V%u.%u.%u\n",
			jlink_hw_type_to_string(JLINK_HARDWARE_VERSION_TYPE(jlink.hw_version)),
			JLINK_HARDWARE_VERSION_MAJOR(jlink.hw_version), JLINK_HARDWARE_VERSION_MINOR(jlink.hw_version),
			JLINK_HARDWARE_VERSION_REVISION(jlink.hw_version));
	}

	return true;
}

static bool jlink_get_capabilities(void)
{
	uint8_t buffer[4U];
	if (!jlink_simple_query(JLINK_CMD_INFO_GET_PROBE_CAPABILITIES, buffer, sizeof(buffer)))
		return false;

	jlink.capabilities = read_le4(buffer, 0);
	DEBUG_INFO("Capabilities: 0x%08" PRIx32 "\n", jlink.capabilities);

	return true;
}

static inline bool jlink_interface_available(const uint8_t interface)
{
	return jlink.available_interfaces & (1U << interface);
}

static uint8_t jlink_selected_interface(void)
{
	uint8_t buffer[4U];
	if (!jlink_simple_request_8(JLINK_CMD_INTERFACE_GET, JLINK_INTERFACE_GET_CURRENT, buffer, sizeof(buffer)))
		return UINT8_MAX; /* Invalid interface, max value is 31 */

	/* The max value of interface is 31, so we can use the first byte of the response directly */
	return buffer[0];
}

bool jlink_select_interface(const uint8_t interface)
{
	if (!jlink_interface_available(interface))
		return false;

	uint8_t buffer[4U];
	if (!jlink_simple_request_8(JLINK_CMD_INTERFACE_SET_SELECTED, interface, buffer, sizeof(buffer)))
		return false;

	return true;
}

static bool jlink_get_interfaces(void)
{
	uint8_t buffer[4U];
	if (!jlink_simple_request_8(JLINK_CMD_INTERFACE_GET, JLINK_INTERFACE_GET_AVAILABLE, buffer, sizeof(buffer)))
		return false;

	/* available_interfaces is a 32bit bitfield/mask */
	jlink.available_interfaces = read_le4(buffer, 0);

	/* Print the available interfaces, marking the selected one, and unsuported ones */
	const uint8_t selected_interface = jlink_selected_interface();
	DEBUG_INFO("Available interfaces: \n");
	for (size_t i = 0; i < JLINK_INTERFACE_MAX; i++) {
		if (jlink_interface_available(i)) {
			const bool is_current = i == selected_interface;
			const bool is_bmda_supported = i == JLINK_INTERFACE_SWD || i == JLINK_INTERFACE_JTAG;

			DEBUG_INFO("\t%zu: %s%c %s\n", i, jlink_interface_to_string(i), is_current ? '*' : ' ',
				is_bmda_supported ? "" : "(Not supported)");
		}
	}

	return true;
}

static bool jlink_get_interface_frequency(void)
{
	/* 
	 * Fixme: this is not strictly correct, the reported base frequency and min divisor depend on the
	 * interface selected, but we don't know which one is selected at this point, we are assuming JTAG
	 * and that JTAG is the only interface that supports frequency info/set.
	 * Unfortunately, this does not look like it's documented on the docs we have access to.
	*/

	if (jlink.capabilities & JLINK_CAPABILITY_INTERFACE_FREQUENCY) {
		uint8_t buffer[6U];
		if (!jlink_simple_query(JLINK_CMD_INTERFACE_GET_BASE_FREQUENCY, buffer, sizeof(buffer)))
			return false;

		jlink.base_frequency = read_le4(buffer, JLINK_INTERFACE_BASE_FREQUENCY_OFFSET);
		jlink.min_divisor = read_le2(buffer, JLINK_INTERFACE_MIN_DIV_OFFSET);
		jlink.current_divisor = jlink.min_divisor;

		DEBUG_INFO("Base frequency: %uHz\n\tMinimum divisor: %u\n", jlink.base_frequency, jlink.min_divisor);
	} else
		DEBUG_WARN("J-Link does not support frequency info command\n");

	return true;
}

bool jlink_set_jtag_frequency(const uint32_t frequency)
{
	/* Fixme: see note on jlink_get_interface_frequency */

	if (!(jlink.capabilities & JLINK_CAPABILITY_INTERFACE_FREQUENCY))
		return false;

	/* Find the divisor that gets us closest to the requested frequency */
	uint16_t divisor = (jlink.base_frequency + frequency - 1U) / frequency;

	/* Bound the divisor to the min divisor */
	if (divisor < jlink.min_divisor)
		divisor = jlink.min_divisor;

	/* Get the approximate frequency we'll actually be running at, convert to kHz in the process */
	const uint16_t frequency_khz = (jlink.base_frequency / jlink.current_divisor) / 1000U;

	if (!jlink_simple_request_16(JLINK_CMD_INTERFACE_SET_FREQUENCY_KHZ, frequency_khz, NULL, 0))
		return false;

	/* Update the current divisor for frquency calculations */
	jlink.current_divisor = divisor;

	return true;
}

static uint16_t jlink_target_voltage(void)
{
	uint8_t buffer[8U];
	if (!jlink_simple_query(JLINK_CMD_SIGNAL_GET_STATE, buffer, sizeof(buffer)))
		return UINT16_MAX;

	return read_le2(buffer, JLINK_SIGNAL_STATE_VOLTAGE_OFFSET);
}

/* BMDA interface functions */

/*
 * Return true if single J-Link device connected or
 * serial given matches one of several J-Link devices.
 */
bool jlink_init(void)
{
	usb_link_s *link = calloc(1, sizeof(usb_link_s));
	if (!link)
		return false;
	info.usb_link = link;
	link->context = info.libusb_ctx;
	int result = libusb_open(info.libusb_dev, &link->device_handle);
	if (result != LIBUSB_SUCCESS) {
		DEBUG_ERROR("libusb_open() failed (%d): %s\n", result, libusb_error_name(result));
		return false;
	}
	if (!jlink_claim_interface()) {
		libusb_close(info.usb_link->device_handle);
		return false;
	}
	if (!link->ep_tx || !link->ep_rx) {
		DEBUG_ERROR("Device setup failed\n");
		libusb_release_interface(info.usb_link->device_handle, info.usb_link->interface);
		libusb_close(info.usb_link->device_handle);
		return false;
	}
	if (!jlink_get_capabilities() || !jlink_get_version() || !jlink_get_interfaces() ||
		!jlink_get_interface_frequency()) {
		DEBUG_ERROR("Failed to read J-Link information\n");
		libusb_release_interface(info.usb_link->device_handle, info.usb_link->interface);
		libusb_close(info.usb_link->device_handle);
		return false;
	}
	memcpy(info.version, jlink.fw_version, strlen(jlink.fw_version) + 1U);
	return true;
}

uint32_t jlink_target_voltage_sense(void)
{
	/* Convert from mV to dV (deci-Volt, i.e. tenths of a Volt) */
	return jlink_target_voltage() / 100U;
}

const char *jlink_target_voltage_string(void)
{
	static char result[8U] = {'\0'};

	const uint16_t millivolts = jlink_target_voltage();
	if (millivolts == UINT16_MAX)
		return "ERROR!";

	snprintf(result, sizeof(result), "%2u.%03uV", millivolts / 1000U, millivolts % 1000U);
	return result;
}

void jlink_nrst_set_val(const bool assert)
{
	jlink_simple_query(assert ? JLINK_CMD_SIGNAL_CLEAR_RESET : JLINK_CMD_SIGNAL_SET_RESET, NULL, 0);
	platform_delay(2U);
}

bool jlink_nrst_get_val(void)
{
	uint8_t result[8U];
	if (!jlink_simple_query(JLINK_CMD_SIGNAL_GET_STATE, result, sizeof(result)))
		return false;
	return result[JLINK_SIGNAL_STATE_TRES_OFFSET] == 0;
}

void jlink_max_frequency_set(const uint32_t frequency)
{
	/* Fixme: see note on jlink_get_interface_frequency */

	if (!(jlink.capabilities & JLINK_CAPABILITY_INTERFACE_FREQUENCY) || !info.is_jtag)
		return;

	jlink_set_jtag_frequency(frequency);
}

uint32_t jlink_max_frequency_get(void)
{
	/* Fixme: see note on jlink_get_interface_frequency */

	if (!(jlink.capabilities & JLINK_CAPABILITY_INTERFACE_FREQUENCY) || !info.is_jtag)
		return FREQ_FIXED;

	return jlink.base_frequency / jlink.current_divisor;
}
