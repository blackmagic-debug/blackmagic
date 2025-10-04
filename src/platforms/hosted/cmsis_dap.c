/*
 * Copyright (c) 2013-2019, Alex Taradov <alex@taradov.com>
 * Copyright (C) 2023-2025 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019-2021 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
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
/* Modified from edbg.c
 *   Links between bmp and edbg
 *
 * https://arm-software.github.io/CMSIS_5/DAP/html/index.html
*/

#include "general.h"
#include "adiv5.h"

#include <string.h>
#ifndef _MSC_VER
#include <unistd.h>
#include <sys/time.h>
#endif
#include <stdlib.h>
#include <hidapi.h>
#include <wchar.h>
#include <sys/stat.h>
#include <assert.h>

#include "bmp_hosted.h"
#include "dap.h"
#include "dap_command.h"
#include "cmsis_dap.h"

#include "target.h"

#define TRANSFER_TIMEOUT_MS (100)

typedef enum cmsis_type {
	CMSIS_TYPE_NONE = 0,
	CMSIS_TYPE_HID,
	CMSIS_TYPE_BULK
} cmsis_type_e;

#ifdef __linux__
typedef struct hid_device_info hid_device_info_s;
#endif

typedef struct dap_version {
	uint16_t major;
	uint16_t minor;
	uint16_t revision;
} dap_version_s;

uint8_t dap_caps;
dap_cap_e dap_mode;
uint8_t dap_quirks;

static cmsis_type_e type;
static libusb_device_handle *usb_handle = NULL;
static uint8_t in_ep;
static uint8_t out_ep;
static hid_device *handle = NULL;
/* Provide enough space for up to a HS USB HID payload + the HID report ID byte */
static uint8_t buffer[1025U];
/*
 * Start by defaulting this to the typical size of `DAP_PACKET_SIZE` for FS USB. This value is pulled from here:
 * https://arm-software.github.io/CMSIS-DAP/latest/group__DAP__Config__Debug__gr.html#gaa28bb1da2661291634c4a8fb3e227404
 */
static size_t dap_packet_size = 64U;

dap_version_s dap_adaptor_version(dap_info_e version_kind);

static size_t mbslen(const char *str)
{
	const char *const end = str + strlen(str);
	size_t result = 0;
	// Reset conversion state
	mblen(NULL, 0);
	while (str < end) {
		const int next = mblen(str, end - str);
		// If an error occurs, bail out with whatever we got so far.
		if (next == -1)
			break;
		str += next;
		++result;
	}
	return result;
}

static inline bool dap_version_compare_ge(const dap_version_s lhs, const dap_version_s rhs)
{
	/*
	 * Try to first check if the major on the left is more than the one on the right.
	 * If it is not, check that they're equal and that the minor on the left is more than the right.
	 * If that is still not true, check that the majors are equal, the minors are equal and that the
	 * revision on the left is more than or equal the one on the right.
	 */
	return lhs.major > rhs.major || (lhs.major == rhs.major && lhs.minor > rhs.minor) ||
		(lhs.major == rhs.major && lhs.minor == rhs.minor && lhs.revision >= rhs.revision);
}

static inline bool dap_version_compare_le(const dap_version_s lhs, const dap_version_s rhs)
{
	/*
	 * Try to first check if the major on the left is less than the one on the right.
	 * If it is not, check that they're equal and that the minor on the left is less than the right.
	 * If that is still not true, check that the majors are equal, the minors are equal and that the
	 * revision on the left is less than or equal the one on the right.
	 */
	return lhs.major < rhs.major || (lhs.major == rhs.major && lhs.minor < rhs.minor) ||
		(lhs.major == rhs.major && lhs.minor == rhs.minor && lhs.revision <= rhs.revision);
}

/*
 * Return maximum length in bytes that can be sent in the 'data' payload of a
 * DAP transfer, given the interface type and (provided) DAP command header size.
 */
static inline size_t dap_max_transfer_data(size_t command_header_len)
{
	const size_t result = dap_packet_size - command_header_len;

	/* Allow for an additional byte of payload overhead when sending data in HID Report payloads */
	if (type == CMSIS_TYPE_HID)
		return result - 1U;

	return result;
}

#ifdef __linux__
static void dap_hid_print_permissions_for(const hid_device_info_s *const dev)
{
	const char *const path = dev->path;
	DEBUG_ERROR("Tried device '%s'", path);
	struct stat dev_stat;
	if (stat(path, &dev_stat) == 0) {
		DEBUG_ERROR(", permissions = %04o, owner = %u, group = %u", dev_stat.st_mode & ACCESSPERMS, dev_stat.st_uid,
			dev_stat.st_gid);
	}
	DEBUG_ERROR("\n");
}

static void dap_hid_print_permissions(const uint16_t vid, const uint16_t pid, const wchar_t *const serial)
{
	hid_device_info_s *const devs = hid_enumerate(vid, pid);
	if (!devs)
		return;
	for (const hid_device_info_s *dev = devs; dev; dev = dev->next) {
		if (serial) {
			if (wcscmp(serial, dev->serial_number) == 0) {
				dap_hid_print_permissions_for(dev);
				break;
			}
		} else
			dap_hid_print_permissions_for(dev);
	}
	hid_free_enumeration(devs);
}
#endif

static bool dap_init_hid(void)
{
	/* Initialise HIDAPI */
	DEBUG_INFO("Using HID transfer\n");
	if (hid_init())
		return false;

	/* Extract the serial number information */
	const size_t size = mbslen(bmda_probe_info.serial);
	if (size > 64U) {
		DEBUG_ERROR("Serial number invalid, aborting\n");
		hid_exit();
		return false;
	}
	wchar_t serial[65] = {0};
	if (mbstowcs(serial, bmda_probe_info.serial, size) != size) {
		DEBUG_ERROR("Serial number conversion failed, aborting\n");
		hid_exit();
		return false;
	}
	serial[size] = 0;

	/*
	 * Base the report length information for the device on the max packet length from its descriptors.
	 * Add 1 to account for HIDAPI's need to prefix with a report type byte. Limit to at most 513 bytes.
	 */
	dap_packet_size = MIN(bmda_probe_info.max_packet_length + 1U, 513U);

	/* Handle the NXP LPC11U3x CMSIS-DAP v1.0.7 implementation needing a 64 byte report length */
	if (bmda_probe_info.vid == 0x1fc9U && bmda_probe_info.pid == 0x0132U)
		dap_packet_size = 64U + 1U;

	/* Now open the device with HIDAPI so we can talk with it */
	handle = hid_open(bmda_probe_info.vid, bmda_probe_info.pid, serial[0] ? serial : NULL);
	if (!handle) {
		DEBUG_ERROR("hid_open failed: %ls\n", hid_error(NULL));
#ifdef __linux__
		dap_hid_print_permissions(bmda_probe_info.vid, bmda_probe_info.pid, serial[0] ? serial : NULL);
#endif
		hid_exit();
		return false;
	}
	return true;
}

static bool dap_init_bulk(void)
{
	DEBUG_INFO("Using bulk transfer\n");
	int res = libusb_open(bmda_probe_info.libusb_dev, &usb_handle);
	if (res != LIBUSB_SUCCESS) {
		DEBUG_ERROR("libusb_open() failed (%d): %s\n", res, libusb_error_name(res));
		return false;
	}
	if (libusb_claim_interface(usb_handle, bmda_probe_info.interface_num) < 0) {
		DEBUG_ERROR("libusb_claim_interface() failed\n");
		return false;
	}
	/* Base the packet size on the one retrieved from the device descriptors */
	dap_packet_size = bmda_probe_info.max_packet_length;
	in_ep = bmda_probe_info.in_ep;
	out_ep = bmda_probe_info.out_ep;
	return true;
}

/* LPC845 Breakout Board Rev. 0 reports an invalid response with > 65 bytes */
bool dap_init(bool allow_fallback)
{
	/* Initialise the adaptor via a suitable protocol */
	if (bmda_probe_info.in_ep && bmda_probe_info.out_ep)
		type = CMSIS_TYPE_BULK;
	else
		type = CMSIS_TYPE_HID;

	/* Windows hosts may not have the winusb driver associated with v2, handle that by degrading to v1 */
	if (type == CMSIS_TYPE_BULK && !dap_init_bulk()) {
		if (allow_fallback) {
			DEBUG_WARN("Could not setup a CMSIS-DAP v2 device in Bulk mode (no drivers?), retrying HID mode\n");
			type = CMSIS_TYPE_HID;
		} else {
			DEBUG_ERROR("Could not setup a CMSIS-DAP device over Bulk interface, failing. Hint: pass %s to retry "
						"HID interface\n",
				"--allow-fallback");
			return false;
		}
	}

	if (type == CMSIS_TYPE_HID && !dap_init_hid())
		return false;

	/* Ensure the adaptor is idle and not prepared for any protocol in particular */
	dap_disconnect();
	/* Get the adaptor version information so we can set quirks as-needed */
	const dap_version_s cmsis_version = dap_adaptor_version(DAP_INFO_CMSIS_DAP_VERSION);
	dap_version_s adaptor_version = {UINT16_MAX, UINT16_MAX, UINT16_MAX};
	/*
	 * If the adaptor implements CMSIS-DAP < 1.3.0 (in the 1.x series) or
	 * CMSIS-DAP < 2.1.0 (in the 2.x series) it won't have this command
	 */
	if ((cmsis_version.major == 1 && cmsis_version.minor >= 3) ||
		(cmsis_version.major == 2 && cmsis_version.minor >= 1) || cmsis_version.major > 2)
		adaptor_version = dap_adaptor_version(DAP_INFO_ADAPTOR_VERSION);

	/* Try to get the actual packet size information from the adaptor */
	uint16_t dap_packet_size;
	if (dap_info(DAP_INFO_PACKET_SIZE, &dap_packet_size, sizeof(dap_packet_size)) != sizeof(dap_packet_size))
		/* Report the failure */
		DEBUG_WARN("Failed to get adaptor packet size, assuming descriptor provided size\n");
	else
		dap_packet_size = dap_packet_size + (type == CMSIS_TYPE_HID ? 1U : 0U);

	/* Try to get the device's capabilities */
	const size_t size = dap_info(DAP_INFO_CAPABILITIES, &dap_caps, sizeof(dap_caps));
	if (size != sizeof(dap_caps)) {
		/* Report the failure */
		DEBUG_ERROR("Failed to get adaptor capabilities, aborting\n");
		/* Close any open connections and return failure so we don't go further */
		dap_exit_function();
		return false;
	}

	/* Having got the capabilities, decode and print an informitive string about them */
	const bool supports_jtag = dap_caps & DAP_CAP_JTAG;
	const bool supports_swd = dap_caps & DAP_CAP_SWD;
	DEBUG_INFO("Capabilities: %02x (", dap_caps);
	if (supports_jtag)
		DEBUG_INFO("JTAG%s", supports_swd ? "/" : "");
	if (supports_swd)
		DEBUG_INFO("SWD");
	if (dap_caps & DAP_CAP_SWO_ASYNC)
		DEBUG_INFO(", Async SWO");
	if (dap_caps & DAP_CAP_SWO_MANCHESTER)
		DEBUG_INFO(", Manchester SWO");
	if (dap_caps & DAP_CAP_ATOMIC_CMDS)
		DEBUG_INFO(", Atomic commands");
	DEBUG_INFO(")\n");

	dap_quirks = 0;
	/* Handle multi-TAP JTAG on older (pre-v1.3) ORBTrace gateware being broken */
	if (strcmp(bmda_probe_info.product, "Orbtrace") == 0 &&
		dap_version_compare_le(adaptor_version, (dap_version_s){1, 2, UINT16_MAX}))
		dap_quirks |= DAP_QUIRK_NO_JTAG_MUTLI_TAP;

	/* Handle SWD no-response turnarounds on older (pre-v1.3.2) ORBTrace gateware being broken */
	if (strcmp(bmda_probe_info.product, "Orbtrace") == 0 &&
		dap_version_compare_le(adaptor_version, (dap_version_s){1, 3, 1}))
		dap_quirks |= DAP_QUIRK_BAD_SWD_NO_RESP_DATA_PHASE;

	/* ORBTrace needs an extra ZLP read done on full packet reception */
	if (strcmp(bmda_probe_info.product, "Orbtrace") == 0)
		dap_quirks |= DAP_QUIRK_NEEDS_EXTRA_ZLP_READ;

	/* Pre-CMSIS-DAP v1.2.0 adaptors do not have DAP_SWD_Sequence and must use alternate means to do the same thing */
	if (!dap_version_compare_ge(cmsis_version, (dap_version_s){1, 2, 0})) {
		DEBUG_INFO("Adaptor does not support DAP_SWD_Sequence, using fallbacks\n");
		dap_quirks |= DAP_QUIRK_NO_SWD_SEQUENCE;
	}

	return true;
}

dap_version_s dap_adaptor_version(const dap_info_e version_kind)
{
	char version_str[256U] = {0};
	/* Try to retrieve the version string, and if we fail, report back an obvious bad one */
	const size_t version_length = dap_info(version_kind, version_str, ARRAY_LENGTH(version_str));
	if (!version_length)
		return (dap_version_s){UINT16_MAX, UINT16_MAX, UINT16_MAX};

	/* Display the version string */
	if (version_kind == DAP_INFO_ADAPTOR_VERSION)
		DEBUG_INFO("Adaptor version %s\n", version_str);
	else if (version_kind == DAP_INFO_CMSIS_DAP_VERSION)
		DEBUG_INFO("CMSIS-DAP v%s, ", version_str);
	const char *begin = version_str;
	char *end = NULL;
	dap_version_s version = {0};
	/* If the string starts with a 'v' or 'V', skip over that */
	if (begin[0] == 'v' || begin[0] == 'V')
		++begin;
	/* Now try to parse out the individual parts of the version string */
	const uint16_t major = strtoul(begin, &end, 10);
	/* If we fail on the first hurdle, return the bad version */
	if (!end)
		return (dap_version_s){UINT16_MAX, UINT16_MAX, UINT16_MAX};
	version.major = major;
	/* Otherwise see if it's worth converting anything more */
	if ((size_t)(end - version_str) >= version_length || end[0] != '.')
		return version;

	/* Now skip the delimiter and try to parse out the next component */
	begin = end + 1U;
	const uint16_t minor = strtoul(begin, &end, 10);
	/* If that failed, return just the major */
	if (!end)
		return version;

	/* Special-case the MCU-Link firmware to correct some version numbering mistakes they've made */
	if (strncmp(bmda_probe_info.product, "MCU-Link", 8U) == 0) {
		/* If this is a v1.10+ MCU-Link */
		if (minor > 9U) {
			/* Then unpack the version number - CMSIS-DAP v1.1.0 is (wrongly) encoded as v1.10 on these adaptors */
			version.minor = minor / 10U;
			version.revision = minor % 10U;
			/* Now return early as we're now done */
			return version;
		}
	}

	version.minor = minor;
	/* Check if it's worth trying to convert anything more */
	if ((size_t)(end - version_str) >= version_length || end[0] != '.')
		return version;

	/* Finally skip the delimiter and try to parse out the final component */
	begin = end + 1U;
	const uint16_t revision = strtoul(begin, &end, 10);
	/* If that failed, return just the major + minor */
	if (!end)
		return version;
	version.revision = revision;
	/* We got a complete version, discard anything more and return the 3 parts we care about. */
	return version;
}

void dap_dp_abort(adiv5_debug_port_s *const target_dp, const uint32_t abort)
{
	/* DP Write to Reg 0.*/
	dap_write_reg(target_dp, ADIV5_DP_ABORT, abort);
}

uint32_t dap_dp_raw_access(
	adiv5_debug_port_s *const target_dp, const uint8_t rnw, const uint16_t addr, const uint32_t value)
{
	uint32_t res = 0;
	const uint8_t reg = (addr & 0xcU) | (addr & ADIV5_APnDP ? 1U : 0U);
	if (rnw)
		res = dap_read_reg(target_dp, reg);
	else
		dap_write_reg(target_dp, reg, value);
	return res;
}

uint32_t dap_dp_read_reg(adiv5_debug_port_s *const target_dp, const uint16_t addr)
{
	uint32_t result = dap_dp_raw_access(target_dp, ADIV5_LOW_READ, addr, 0);
	if (target_dp->fault == DAP_TRANSFER_NO_RESPONSE) {
		DEBUG_WARN("Recovering and re-trying access\n");
		target_dp->error(target_dp, true);
		result = dap_dp_raw_access(target_dp, ADIV5_LOW_READ, addr, 0);
	}
	DEBUG_PROBE("dp_read %04x %08" PRIx32 "\n", addr, result);
	return result;
}

void dap_exit_function(void)
{
	if (type == CMSIS_TYPE_HID) {
		if (handle) {
			dap_disconnect();
			hid_close(handle);
		}
	} else if (type == CMSIS_TYPE_BULK) {
		if (usb_handle) {
			dap_disconnect();
			libusb_close(usb_handle);
		}
	}
}

ssize_t dbg_dap_cmd_hid_io(const uint8_t *const request_data, const size_t request_length, uint8_t *const response_data,
	const size_t response_length)
{
	/* Make the unused part of the request buffer all 0xff */
	memset(buffer + request_length + 1U, 0xff, dap_packet_size - (request_length + 1U));
	/* Then copy in the report ID and request data */
	buffer[0] = 0x00U;
	memcpy(buffer + 1U, request_data, request_length);

	/* Send the request to the adaptor, checking for errors */
	const int result = hid_write(handle, buffer, dap_packet_size);
	if (result < 0) {
		DEBUG_ERROR("CMSIS-DAP write error: %ls\n", hid_error(handle));
		return result;
	}

	/* Now try and read back the response */
	const int response = hid_read_timeout(handle, buffer, dap_packet_size - 1U, 1000);
	/* hid_read_timeout returns -1, 0, or the number of bytes read */
	if (response < 0) {
		DEBUG_ERROR("CMSIS-DAP read error: %ls\n", hid_error(handle));
		/* As the read failed, return -1 here */
		return result;
	}
	if (response == 0) {
		DEBUG_ERROR("CMSIS-DAP read timeout\n");
		/* Signal timeout with 0 */
		return response;
	}
	/* If we got a good response, copy the data for it to the response buffer */
	const size_t bytes_transferred = MIN((size_t)response, response_length);
	memcpy(response_data, buffer, bytes_transferred);
	return (int)bytes_transferred;
}

ssize_t dbg_dap_cmd_hid(const uint8_t *const request_data, const size_t request_length, uint8_t *const response_data,
	const size_t response_length)
{
	/* Need room to prepend HID Report ID byte */
	if (request_length + 1U > dap_packet_size) {
		DEBUG_ERROR("Attempted to make over-long request of %zu bytes, max length is %zu\n", request_length + 1U,
			dap_packet_size);
		exit(-1);
	}

	/* Ensure that the response data type byte is something valid */
	response_data[0] = ~request_data[0];

	size_t tries = 0U;
	ssize_t response = 0;
	/* Try up to 3 times to make the request and get the response */
	while (response == 0 && tries < 3U) {
		response = dbg_dap_cmd_hid_io(request_data, request_length, response_data, response_length);
		++tries;
		/* If this try succeeded, make sure the data read back was sane */
		while (response_data[0] != request_data[0]) {
			/* it was not, so try the read back again */
			response = hid_read_timeout(handle, buffer, dap_packet_size, 1000);
			/* hid_read_timeout returns -1, 0, or the number of bytes read */
			if (response < 0) {
				DEBUG_ERROR("CMSIS-DAP read error: %ls\n", hid_error(handle));
				/* As the read failed, return -1 here */
				return response;
			}
			if (response == 0) {
				DEBUG_ERROR("CMSIS-DAP read timeout\n");
				/* Signal timeout with -2 */
				return -2;
			}
			/* If we got a good response, copy the data for it to the response buffer */
			memcpy(response_data, buffer, response_length);
		}
	}
	if (response > 0)
		return MIN((size_t)response, response_length);
	return response;
}

ssize_t dbg_dap_cmd_bulk(const uint8_t *const request_data, const size_t request_length, uint8_t *const response_data,
	const size_t response_length)
{
	int transferred = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
	const int request_result = libusb_bulk_transfer(
		usb_handle, out_ep, (uint8_t *)request_data, (int)request_length, &transferred, TRANSFER_TIMEOUT_MS);
#pragma GCC diagnostic push
	if (request_result < 0) {
		DEBUG_ERROR("CMSIS-DAP write error: %s (%d)\n", libusb_strerror(request_result), request_result);
		return request_result;
	}

	/* We repeat the read in case we're out of step with the transmitter */
	do {
		const int response_result = libusb_bulk_transfer(
			usb_handle, in_ep, response_data, (int)response_length, &transferred, TRANSFER_TIMEOUT_MS);
		if (response_result < 0) {
			DEBUG_ERROR("CMSIS-DAP read error: %s (%d)\n", libusb_strerror(response_result), response_result);
			return response_result;
		}
	} while (response_data[0] != request_data[0]);

	/* If the response requested is the size of the packet size for the adaptor, generate a ZLP read to clean state */
	if ((dap_quirks & DAP_QUIRK_NEEDS_EXTRA_ZLP_READ) && (size_t)transferred == dap_packet_size) {
		uint8_t zlp;
		int zlp_read = 0;
		libusb_bulk_transfer(usb_handle, in_ep, &zlp, sizeof(zlp), &zlp_read, TRANSFER_TIMEOUT_MS);
		assert(zlp_read == 0);
	}
	return transferred;
}

static ssize_t dap_run_cmd_raw(const uint8_t *const request_data, const size_t request_length,
	uint8_t *const response_data, const size_t response_length)
{
	DEBUG_WIRE(" command: ");
	for (size_t i = 0; i < request_length; ++i)
		DEBUG_WIRE("%02x ", request_data[i]);
	DEBUG_WIRE("\n");

	/* Provide enough space for up to a HS USB HID payload */
	uint8_t data[1024];
	/* Make sure that we're not about to blow this buffer when we request data back */
	if (sizeof(data) < dap_packet_size) {
		DEBUG_ERROR("CMSIS-DAP request would exceed response buffer\n");
		return -1;
	}

	ssize_t response = -1;
	if (type == CMSIS_TYPE_HID)
		response = dbg_dap_cmd_hid(request_data, request_length, data, MIN(response_length + 1U, dap_packet_size));
	else if (type == CMSIS_TYPE_BULK)
		response = dbg_dap_cmd_bulk(request_data, request_length, data, dap_packet_size);
	if (response < 0)
		return response;
	const size_t result = (size_t)response;

	DEBUG_WIRE("response: ");
	for (size_t i = 0; i < result; i++)
		DEBUG_WIRE("%02x ", data[i]);
	DEBUG_WIRE("\n");

	if (response_length)
		memcpy(response_data, data + 1, MIN(response_length, result - 1U));
	return response;
}

bool dap_run_cmd(const void *const request_data, const size_t request_length, void *const response_data,
	const size_t response_length)
{
	/* This subtracts one off the result to account for the command byte that gets stripped above */
	const ssize_t result =
		dap_run_cmd_raw((const uint8_t *)request_data, request_length, (uint8_t *)response_data, response_length) - 1U;
	if (result < 0)
		return false;
	return (size_t)result >= response_length;
}

bool dap_run_transfer(const void *const request_data, const size_t request_length, void *const response_data,
	const size_t response_length, size_t *const actual_length)
{
	/*
	 * This function works almost exactly the same as dap_run_cmd(), but captures and preserves the resulting
	 * response length if the result is not an outright failure. It sets the actual response length to 0 when it is.
	 */
	const ssize_t result =
		dap_run_cmd_raw((const uint8_t *)request_data, request_length, (uint8_t *)response_data, response_length) - 1U;
	if (result < 0) {
		*actual_length = 0U;
		return false;
	}
	*actual_length = (size_t)result;
	return *actual_length >= response_length;
}

static void dap_adiv5_mem_read(adiv5_access_port_s *ap, void *dest, target_addr64_t src, size_t len)
{
	if (len == 0U)
		return;
	const align_e align = MIN_ALIGN(src, len);
	DEBUG_PROBE("%s @%08" PRIx64 "+%zu, alignment %u\n", __func__, src, len, align);
	/* If the read can be done in a single transaction, use the dap_adiv5_mem_read_single() fast-path */
	if ((1U << align) == len) {
		dap_adiv5_mem_read_single(ap, dest, src, align);
		return;
	}
	/* Otherwise proceed blockwise */
	const size_t blocks_per_transfer = dap_max_transfer_data(DAP_CMD_BLOCK_READ_HDR_LEN + 1U) >> 2U;
	uint8_t *const data = (uint8_t *)dest;
	for (size_t offset = 0; offset < len;) {
		/* Setup AP_TAR every loop as failing to do so results in it wrapping */
		if (!dap_adiv5_mem_access_setup(ap, src + offset, align))
			return;
		/*
		 * src can start out unaligned to a 1024 byte chunk size,
		 * so we have to calculate how much is left of the chunk.
		 * We also have to take into account how much of the chunk the caller
		 * has requested we fill.
		 */
		const size_t chunk_remaining = MIN(1024 - ((src + offset) & 0x3ffU), len - offset);
		const size_t blocks = chunk_remaining >> align;
		for (size_t i = 0; i < blocks; i += blocks_per_transfer) {
			/* blocks - i gives how many blocks are left to transfer in this 1024 byte chunk */
			const size_t transfer_length = MIN(blocks - i, blocks_per_transfer) << align;
			if (!dap_mem_read_block(ap, data + offset, src + offset, transfer_length, align)) {
				DEBUG_WIRE("%s failed: %u\n", __func__, ap->dp->fault);
				return;
			}
			offset += transfer_length;
		}
	}
	DEBUG_WIRE("%s transferred %zu blocks\n", __func__, len >> align);
}

static void dap_adiv5_mem_write(
	adiv5_access_port_s *ap, target_addr64_t dest, const void *src, size_t len, align_e align)
{
	if (len == 0U)
		return;
	DEBUG_PROBE("%s @%08" PRIx64 "+%zu, alignment %u\n", __func__, dest, len, align);
	/* If the write can be done in a single transaction, use the dap_adiv5_mem_write_single() fast-path */
	if ((1U << align) == len) {
		dap_adiv5_mem_write_single(ap, dest, src, align);
		return;
	}
	/* Otherwise proceed blockwise */
	const size_t blocks_per_transfer = dap_max_transfer_data(DAP_CMD_BLOCK_WRITE_HDR_LEN) >> 2U;
	const uint8_t *const data = (const uint8_t *)src;
	for (size_t offset = 0; offset < len;) {
		/* Setup AP_TAR every loop as failing to do so results in it wrapping */
		if (!dap_adiv5_mem_access_setup(ap, dest + offset, align))
			return;
		/*
		 * dest can start out unaligned to a 1024 byte chunk size,
		 * so we have to calculate how much is left of the chunk.
		 * We also have to take into account how much of the chunk the caller
		 * has requested we fill.
		 */
		const size_t chunk_remaining = MIN(1024 - ((dest + offset) & 0x3ffU), len - offset);
		const size_t blocks = chunk_remaining >> align;
		for (size_t i = 0; i < blocks; i += blocks_per_transfer) {
			/* blocks - i gives how many blocks are left to transfer in this 1024 byte chunk */
			const size_t transfer_length = MIN(blocks - i, blocks_per_transfer) << align;
			if (!dap_mem_write_block(ap, dest + offset, data + offset, transfer_length, align)) {
				DEBUG_WIRE("%s failed: %u\n", __func__, ap->dp->fault);
				return;
			}
			offset += transfer_length;
		}
	}
	DEBUG_WIRE("%s transferred %zu blocks\n", __func__, len >> align);

	/* Make sure this write is complete by doing a dummy read */
	adiv5_dp_read(ap->dp, ADIV5_DP_RDBUFF);
}

static void dap_adiv6_mem_read(
	adiv5_access_port_s *const base_ap, void *const dest, const target_addr64_t src, const size_t len)
{
	if (len == 0U)
		return;
	adiv6_access_port_s *const ap = (adiv6_access_port_s *)base_ap;
	const align_e align = MIN_ALIGN(src, len);
	DEBUG_PROBE("%s @%08" PRIx64 "+%zu, alignment %u\n", __func__, src, len, align);
	/* If the read can be done in a single transaction, use the dap_advi6_mem_read_single() fast-path */
	if ((1U << align) == len) {
		dap_adiv6_mem_read_single(ap, dest, src, align);
		return;
	}
	/* Otherwise proceed blockwise */
	const size_t blocks_per_transfer = dap_max_transfer_data(DAP_CMD_BLOCK_READ_HDR_LEN + 1U) >> 2U;
	uint8_t *const data = (uint8_t *)dest;
	for (size_t offset = 0; offset < len;) {
		/* Setup AP_TAR every loop as failing to do so results in it wrapping */
		if (!dap_adiv6_mem_access_setup(ap, src + offset, align))
			return;
		/*
		 * src can start out unaligned to a 1024 byte chunk size,
		 * so we have to calculate how much is left of the chunk.
		 * We also have to take into account how much of the chunk the caller
		 * has requested we fill.
		 */
		const size_t chunk_remaining = MIN(1024 - ((src + offset) & 0x3ffU), len - offset);
		const size_t blocks = chunk_remaining >> align;
		for (size_t i = 0; i < blocks; i += blocks_per_transfer) {
			/* blocks - i gives how many blocks are left to transfer in this 1024 byte chunk */
			const size_t transfer_length = MIN(blocks - i, blocks_per_transfer) << align;
			if (!dap_mem_read_block(&ap->base, data + offset, src + offset, transfer_length, align)) {
				DEBUG_WIRE("%s failed: %u\n", __func__, ap->base.dp->fault);
				return;
			}
			offset += transfer_length;
		}
	}
	DEBUG_WIRE("%s transferred %zu blocks\n", __func__, len >> align);
}

static void dap_adiv6_mem_write(
	adiv5_access_port_s *base_ap, target_addr64_t dest, const void *src, size_t len, align_e align)
{
	if (len == 0U)
		return;
	adiv6_access_port_s *const ap = (adiv6_access_port_s *)base_ap;
	DEBUG_PROBE("%s @%08" PRIx64 "+%zu, alignment %u\n", __func__, dest, len, align);
	/* If the write can be done in a single transaction, use the dap_adiv5_mem_write_single() fast-path */
	if ((1U << align) == len) {
		dap_adiv6_mem_write_single(ap, dest, src, align);
		return;
	}
	/* Otherwise proceed blockwise */
	const size_t blocks_per_transfer = dap_max_transfer_data(DAP_CMD_BLOCK_WRITE_HDR_LEN) >> 2U;
	const uint8_t *const data = (const uint8_t *)src;
	for (size_t offset = 0; offset < len;) {
		/* Setup AP_TAR every loop as failing to do so results in it wrapping */
		if (!dap_adiv6_mem_access_setup(ap, dest + offset, align))
			return;
		/*
		 * dest can start out unaligned to a 1024 byte chunk size,
		 * so we have to calculate how much is left of the chunk.
		 * We also have to take into account how much of the chunk the caller
		 * has requested we fill.
		 */
		const size_t chunk_remaining = MIN(1024 - ((dest + offset) & 0x3ffU), len - offset);
		const size_t blocks = chunk_remaining >> align;
		for (size_t i = 0; i < blocks; i += blocks_per_transfer) {
			/* blocks - i gives how many blocks are left to transfer in this 1024 byte chunk */
			const size_t transfer_length = MIN(blocks - i, blocks_per_transfer) << align;
			if (!dap_mem_write_block(&ap->base, dest + offset, data + offset, transfer_length, align)) {
				DEBUG_WIRE("%s failed: %u\n", __func__, ap->base.dp->fault);
				return;
			}
			offset += transfer_length;
		}
	}
	DEBUG_WIRE("%s transferred %zu blocks\n", __func__, len >> align);

	/* Make sure this write is complete by doing a dummy read */
	adiv5_dp_read(ap->base.dp, ADIV5_DP_RDBUFF);
}

void dap_adiv5_dp_init(adiv5_debug_port_s *target_dp)
{
	/* Setup the access functions for this adaptor */
	target_dp->ap_read = dap_adiv5_ap_read;
	target_dp->ap_write = dap_adiv5_ap_write;
	target_dp->mem_read = dap_adiv5_mem_read;
	target_dp->mem_write = dap_adiv5_mem_write;
}

void dap_adiv6_dp_init(adiv5_debug_port_s *target_dp)
{
	/* Setup the access functions for this adaptor */
	target_dp->ap_read = dap_adiv6_ap_read;
	target_dp->ap_write = dap_adiv6_ap_write;
	target_dp->mem_read = dap_adiv6_mem_read;
	target_dp->mem_write = dap_adiv6_mem_write;
}
