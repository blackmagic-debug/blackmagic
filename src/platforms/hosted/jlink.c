/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
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

#define USB_VID_SEGGER 0x1366U

#define USB_PID_SEGGER_0101 0x0101U
#define USB_PID_SEGGER_0105 0x0105U
#define USB_PID_SEGGER_1015 0x1015U
#define USB_PID_SEGGER_1020 0x1020U

static uint32_t jlink_caps;
static uint32_t jlink_freq_khz;
static uint16_t jlink_min_divisor;
static uint16_t jlink_current_divisor;

int jlink_simple_query(const uint8_t command, void *const rx_buffer, const size_t rx_len)
{
	return bmda_usb_transfer(info.usb_link, &command, sizeof(command), rx_buffer, rx_len);
}

int jlink_simple_request(const uint8_t command, const uint8_t operation, void *const rx_buffer, const size_t rx_len)
{
	const uint8_t request[2] = {command, operation};
	return bmda_usb_transfer(info.usb_link, request, sizeof(request), rx_buffer, rx_len);
}

/*
 * This runs JLINK_CMD_IO_TRANSACT transactions, these have the following format:
 * ╭─────────┬─────────┬───────────────┬─────────╮
 * │    0    │    1    │       2       │    3    │
 * ├╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌┤
 * │ Command │  Align  │       Cycle count       │
 * ├─────────┼─────────┼───────────────┼─────────┤
 * │    4    │    …    │ 4 + tms_bytes │    …    │
 * ├╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌┤
 * │  TMS data bytes…  │     TDI data bytes…     │
 * ╰─────────┴─────────┴───────────────┴─────────╯
 * where the byte counts for each of TDI and TMS are defined by:
 * count = ⌈cycle_count / 8⌉
 *
 * ⌈⌉ is defined as the ceiling function.
 *
 * In SWD mode, the `tms` buffer represents direction states and
 * the `tdi` buffer represents SWDIO data to send to the device
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
	header->command = JLINK_CMD_IO_TRANSACT;
	write_le2(header->clock_cycles, 0, clock_cycles);
	/* Copy in the TMS state values to transmit (if present) */
	if (tms)
		memcpy(buffer + 4U, tms, byte_count);
	/* Copy in the TDI values to transmit (if present) */
	if (tdi)
		memcpy(buffer + 4U + byte_count, tdi, byte_count);
	/* Send the resulting transaction and try to read back the response data */
	if (bmda_usb_transfer(info.usb_link, buffer, sizeof(jlink_io_transact_s) + (byte_count * 2U), buffer, byte_count) <
			0 ||
		/* Try to read back the transaction return code */
		bmda_usb_transfer(info.usb_link, NULL, 0, buffer + byte_count, 1U) < 0)
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

static bool jlink_print_version(void)
{
	uint8_t len_str[2];
	if (jlink_simple_query(JLINK_CMD_GET_VERSION, len_str, sizeof(len_str)) < 0)
		return false;
	uint8_t version[0x70];
	bmda_usb_transfer(info.usb_link, NULL, 0, version, sizeof(version));
	version[0x6f] = '\0';
	DEBUG_INFO("%s\n", version);
	return true;
}

static bool jlink_query_caps(void)
{
	uint8_t caps[4];
	if (jlink_simple_query(JLINK_CMD_GET_CAPABILITIES, caps, sizeof(caps)) < 0)
		return false;
	jlink_caps = read_le4(caps, 0);
	DEBUG_INFO("Caps %" PRIx32 "\n", jlink_caps);

	if (jlink_caps & JLINK_CAP_GET_HW_VERSION) {
		uint8_t version[4];
		if (jlink_simple_query(JLINK_CMD_GET_ADAPTOR_VERSION, version, sizeof(version)) < 0)
			return false;
		DEBUG_INFO("HW: Type %u, Major %u, Minor %u, Rev %u\n", version[3], version[2], version[1], version[0]);
	}
	return true;
}

static bool jlink_query_speed(void)
{
	uint8_t data[6];
	if (jlink_simple_query(JLINK_CMD_GET_ADAPTOR_FREQS, data, sizeof(data)) < 0)
		return false;
	jlink_freq_khz = read_le4(data, 0) / 1000U;
	jlink_min_divisor = read_le2(data, 4);
	DEBUG_INFO("Emulator speed %ukHz, minimum divisor %u%s\n", jlink_freq_khz, jlink_min_divisor,
		(jlink_caps & JLINK_CAP_GET_SPEEDS) ? "" : ", fixed");
	return true;
}

static bool jlink_print_interfaces(void)
{
	uint8_t active_if[4];
	uint8_t available_ifs[4];

	if (jlink_simple_request(JLINK_CMD_TARGET_IF, JLINK_IF_GET_ACTIVE, active_if, sizeof(active_if)) < 0 ||
		jlink_simple_request(JLINK_CMD_TARGET_IF, JLINK_IF_GET_AVAILABLE, available_ifs, sizeof(available_ifs)) < 0)
		return false;
	++active_if[0];

	if (active_if[0] == JLINK_IF_SWD)
		DEBUG_INFO("SWD active");
	else if (active_if[0] == JLINK_IF_JTAG)
		DEBUG_INFO("JTAG active");
	else
		DEBUG_INFO("No interfaces active");

	const uint8_t other_interface = available_ifs[0] - active_if[0];
	if (other_interface)
		DEBUG_INFO(", %s available\n", other_interface == JLINK_IF_SWD ? "SWD" : "JTAG");
	else
		DEBUG_INFO(", %s not available\n", active_if[0] + 1U == JLINK_IF_SWD ? "JTAG" : "SWD");
	return true;
}

static bool jlink_info(void)
{
	return jlink_print_version() && jlink_query_caps() && jlink_query_speed() && jlink_print_interfaces();
}

/*
 * Try to claim the debugging interface of a J-Link adaptor.
 * On success this copies the endpoint addresses identified into the
 * usb_link_s sub-structure of bmp_info_s (info.usb_link) for later use.
 * Returns true for success, false for failure.
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
	jlink_info();
	return true;
}

const char *jlink_target_voltage(void)
{
	static char result[7] = {'\0'};

	uint8_t data[8];
	if (jlink_simple_query(JLINK_CMD_GET_STATE, data, sizeof(data)) < 0)
		return NULL;

	const uint16_t millivolts = read_le2(data, 0);
	snprintf(result, sizeof(result), "%2u.%03u", millivolts / 1000U, millivolts % 1000U);
	return result;
}

void jlink_nrst_set_val(const bool assert)
{
	jlink_simple_query(assert ? JLINK_CMD_SET_RESET : JLINK_CMD_CLEAR_RESET, NULL, 0);
	platform_delay(2);
}

bool jlink_nrst_get_val(void)
{
	uint8_t result[8];
	if (jlink_simple_query(JLINK_CMD_GET_STATE, result, sizeof(result)) < 0)
		return false;
	return result[6] == 0;
}

bool jlink_set_frequency(const uint16_t frequency_khz)
{
	jlink_set_freq_s command = {JLINK_CMD_SET_FREQ};
	write_le2(command.frequency, 0, frequency_khz);
	DEBUG_INFO("%s: %ukHz\n", __func__, frequency_khz);
	return bmda_usb_transfer(info.usb_link, &command, sizeof(command), NULL, 0) >= 0;
}

void jlink_max_frequency_set(const uint32_t freq)
{
	if (!(jlink_caps & JLINK_CAP_GET_SPEEDS) && !info.is_jtag)
		return;
	const uint16_t freq_khz = freq / 1000U;
	const uint16_t divisor = (jlink_freq_khz + freq_khz - 1U) / freq_khz;
	if (divisor > jlink_min_divisor)
		jlink_current_divisor = divisor;
	else
		jlink_current_divisor = jlink_min_divisor;
	jlink_set_frequency(jlink_freq_khz / jlink_current_divisor);
}

uint32_t jlink_max_frequency_get(void)
{
	if ((jlink_caps & JLINK_CAP_GET_SPEEDS) && info.is_jtag)
		return (jlink_freq_khz * 1000U) / jlink_current_divisor;
	return FREQ_FIXED;
}
