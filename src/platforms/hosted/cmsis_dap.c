/*
 * Copyright (c) 2013-2019, Alex Taradov <alex@taradov.com>
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
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
#include "gdb_if.h"
#include "adiv5.h"

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <sys/time.h>
#include <hidapi.h>
#include <wchar.h>
#include <sys/stat.h>

#include "bmp_hosted.h"
#include "dap.h"
#include "dap_command.h"
#include "cmsis_dap.h"
#include "buffer_utils.h"

#include "cli.h"
#include "target.h"
#include "target_internal.h"

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
static uint8_t buffer[1024U];
static size_t report_size = 64U + 1U; // TODO: read actual report size
bool dap_has_swd_sequence = false;

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
	DEBUG_INFO("Using hid transfer\n");
	if (hid_init())
		return false;

	const size_t size = mbslen(info.serial);
	if (size > 64U) {
		DEBUG_ERROR("Serial number invalid, aborting\n");
		hid_exit();
		return false;
	}
	wchar_t serial[65] = {0};
	if (mbstowcs(serial, info.serial, size) != size) {
		DEBUG_ERROR("Serial number conversion failed, aborting\n");
		hid_exit();
		return false;
	}
	serial[size] = 0;
	/*
	 * Special-case devices that do not work with 513 byte report length
	 * FIXME: Find a solution to decipher from the device.
	 */
	if (info.vid == 0x1fc9U && info.pid == 0x0132U) {
		DEBUG_WARN("Device does not work with the normal report length, activating quirk\n");
		report_size = 64U + 1U;
	}
	handle = hid_open(info.vid, info.pid, serial[0] ? serial : NULL);
	if (!handle) {
		DEBUG_ERROR("hid_open failed: %ls\n", hid_error(NULL));
#ifdef __linux__
		dap_hid_print_permissions(info.vid, info.pid, serial[0] ? serial : NULL);
#endif
		hid_exit();
		return false;
	}
	return true;
}

static bool dap_init_bulk(void)
{
	DEBUG_INFO("Using bulk transfer\n");
	int res = libusb_open(info.libusb_dev, &usb_handle);
	if (res != LIBUSB_SUCCESS) {
		DEBUG_ERROR("libusb_open() failed (%d): %s\n", res, libusb_error_name(res));
		return false;
	}
	if (libusb_claim_interface(usb_handle, info.interface_num) < 0) {
		DEBUG_ERROR("libusb_claim_interface() failed\n");
		return false;
	}
	in_ep = info.in_ep;
	out_ep = info.out_ep;
	return true;
}

/* LPC845 Breakout Board Rev. 0 reports an invalid response with > 65 bytes */
bool dap_init(void)
{
	/* Initialise the adaptor via a suitable protocol */
	if (info.in_ep && info.out_ep) {
		type = CMSIS_TYPE_BULK;
		if (!dap_init_bulk())
			return false;
	} else {
		type = CMSIS_TYPE_HID;
		if (!dap_init_hid())
			return false;
	}

	/* Ensure the adaptor is idle and not prepared for any protocol in particular */
	dap_disconnect();
	/* Get the adaptor version information so we can set quirks as-needed */
	const dap_version_s adaptor_version = dap_adaptor_version(DAP_INFO_ADAPTOR_VERSION);
	const dap_version_s cmsis_version = dap_adaptor_version(DAP_INFO_CMSIS_DAP_VERSION);
	/* Look for CMSIS-DAP v1.2+ */
	dap_has_swd_sequence = cmsis_version.major > 1 || (cmsis_version.major == 1 && cmsis_version.minor > 1);

	/* Try to get the device's capabilities */
	const size_t size = dap_info(DAP_INFO_CAPABILITIES, &dap_caps, sizeof(dap_caps));
	if (size != 1U) {
		/* Report the failure */
		DEBUG_ERROR("Failed to get adaptor capabilities, aborting\n");
		/* Close any open connections and return failure so we don't go further */
		dap_exit_function();
		return false;
	}

	/* Having got the capabilities, decode and print an informitive string about them */
	const bool supportsJTAG = dap_caps & DAP_CAP_JTAG;
	const bool supportsSWD = dap_caps & DAP_CAP_SWD;
	DEBUG_INFO("Capabilities: %02x (", dap_caps);
	if (supportsJTAG)
		DEBUG_INFO("JTAG%s", supportsSWD ? "/" : "");
	if (supportsSWD)
		DEBUG_INFO("SWD");
	if (dap_caps & DAP_CAP_SWO_ASYNC)
		DEBUG_INFO(", Async SWO");
	if (dap_caps & DAP_CAP_SWO_MANCHESTER)
		DEBUG_INFO(", Manchester SWO");
	if (dap_caps & DAP_CAP_ATOMIC_CMDS)
		DEBUG_INFO(", Atomic commands");
	DEBUG_INFO(")\n");

	DEBUG_INFO("Adaptor %s DAP SWD sequences\n", dap_has_swd_sequence ? "supports" : "does not support");

	dap_quirks = 0;
	/* Handle multi-TAP JTAG on older ORBTrace gateware being broken */
	if (strcmp(info.product, "Orbtrace") == 0 &&
		(adaptor_version.major < 1 || (adaptor_version.major == 1 && adaptor_version.minor <= 2))) {
		dap_quirks |= DAP_QUIRK_NO_JTAG_MUTLI_TAP;
	}

	return true;
}

dap_version_s dap_adaptor_version(const dap_info_e version_kind)
{
	char version_str[256U] = {};
	/* Try to retrieve the version string, and if we fail, report back an obvious bad one */
	const size_t version_length = dap_info(version_kind, version_str, ARRAY_LENGTH(version_str));
	if (!version_length)
		return (dap_version_s){UINT16_MAX, UINT16_MAX, UINT16_MAX};

	/* Display the version string */
	if (version_kind == DAP_INFO_ADAPTOR_VERSION)
		DEBUG_INFO("Adaptor version %s, ", version_str);
	else if (version_kind == DAP_INFO_CMSIS_DAP_VERSION)
		DEBUG_INFO("CMSIS-DAP v%s\n", version_str);
	const char *begin = version_str;
	char *end = NULL;
	dap_version_s version = {};
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

	/* Now skip the delimeter and try to parse out the next component */
	begin = end + 1U;
	const uint16_t minor = strtoul(begin, &end, 10);
	/* If that failed, return just the major */
	if (!end)
		return version;
	version.minor = minor;
	/* Check if it's worth trying to convert anything more */
	if ((size_t)(end - version_str) >= version_length || end[0] != '.')
		return version;

	/* Finally skip the delimeter and try to parse out the final component */
	begin = end + 1U;
	const uint16_t revision = strtoul(begin, &end, 10);
	/* If that failed, return just the major + minor */
	if (!end)
		return version;
	version.revision = revision;
	/* We got a complete version, discard anything more and return the 3 parts we care about. */
	return version;
}

void dap_nrst_set_val(bool assert)
{
	dap_set_reset_state(assert);
}

void dap_dp_abort(adiv5_debug_port_s *const target_dp, const uint32_t abort)
{
	/* DP Write to Reg 0.*/
	dap_write_reg(target_dp, ADIV5_DP_ABORT, abort);
}

uint32_t dap_dp_low_access(
	adiv5_debug_port_s *const target_dp, const uint8_t rnw, const uint16_t addr, const uint32_t value)
{
	const bool APnDP = addr & ADIV5_APnDP;
	uint32_t res = 0;
	const uint8_t reg = (addr & 0xcU) | (APnDP ? 1 : 0);
	if (rnw)
		res = dap_read_reg(target_dp, reg);
	else
		dap_write_reg(target_dp, reg, value);
	return res;
}

uint32_t dap_dp_read_reg(adiv5_debug_port_s *const target_dp, const uint16_t addr)
{
	uint32_t result = dap_dp_low_access(target_dp, ADIV5_LOW_READ, addr, 0);
	if (target_dp->fault == DAP_TRANSFER_NO_RESPONSE) {
		DEBUG_WARN("Recovering and re-trying access\n");
		target_dp->error(target_dp, true);
		result = dap_dp_low_access(target_dp, ADIV5_LOW_READ, addr, 0);
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

ssize_t dbg_dap_cmd_hid(const uint8_t *const request_data, const size_t request_length, uint8_t *const response_data,
	const size_t response_length)
{
	if (request_length + 1U > report_size) {
		DEBUG_ERROR(
			"Attempted to make over-long request of %zu bytes, max length is %zu\n", request_length + 1U, report_size);
		exit(-1);
	}

	memset(buffer + request_length + 1U, 0xff, report_size - (request_length + 1U));
	buffer[0] = 0x00; // Report ID??
	memcpy(buffer + 1, request_data, request_length);

	const int result = hid_write(handle, buffer, report_size);
	if (result < 0) {
		DEBUG_ERROR("CMSIS-DAP write error: %ls\n", hid_error(handle));
		exit(-1);
	}

	int response = 0;
	do {
		response = hid_read_timeout(handle, response_data, response_length, 1000);
		if (response < 0) {
			DEBUG_ERROR("CMSIS-DAP read error: %ls\n", hid_error(handle));
			exit(-1);
		} else if (response == 0) {
			DEBUG_ERROR("CMSIS-DAP read timeout\n");
			exit(-1);
		}
	} while (response_data[0] != request_data[0]);
	return response;
}

ssize_t dbg_dap_cmd_bulk(const uint8_t *const request_data, const size_t request_length, uint8_t *const response_data,
	const size_t response_length)
{
	int transferred = 0;
	const int result = libusb_bulk_transfer(
		usb_handle, out_ep, (uint8_t *)request_data, (int)request_length, &transferred, TRANSFER_TIMEOUT_MS);
	if (result < 0) {
		DEBUG_ERROR("CMSIS-DAP write error: %s (%d)\n", libusb_strerror(result), result);
		return result;
	}

	/* We repeat the read in case we're out of step with the transmitter */
	do {
		const int result = libusb_bulk_transfer(
			usb_handle, in_ep, response_data, (int)response_length, &transferred, TRANSFER_TIMEOUT_MS);
		if (result < 0) {
			DEBUG_ERROR("CMSIS-DAP read error: %s (%d)\n", libusb_strerror(result), result);
			return result;
		}
	} while (response_data[0] != request_data[0]);
	return transferred;
}

static ssize_t dap_run_cmd_raw(const uint8_t *const request_data, const size_t request_length,
	uint8_t *const response_data, const size_t response_length)
{
	DEBUG_WIRE(" command: ");
	for (size_t i = 0; i < request_length; ++i)
		DEBUG_WIRE("%02x ", request_data[i]);
	DEBUG_WIRE("\n");

	uint8_t data[65];

	ssize_t response = -1;
	if (type == CMSIS_TYPE_HID)
		response = dbg_dap_cmd_hid(request_data, request_length, data, report_size);
	else if (type == CMSIS_TYPE_BULK)
		response = dbg_dap_cmd_bulk(request_data, request_length, data, report_size);
	if (response < 0)
		return response;
	const size_t result = (size_t)response;

	DEBUG_WIRE("response: ");
	for (size_t i = 0; i < result; i++)
		DEBUG_WIRE("%02x ", data[i]);
	DEBUG_WIRE("\n");

	if (response_length)
		memcpy(response_data, data + 1, MIN(response_length, result));
	return response;
}

bool dap_run_cmd(const void *const request_data, const size_t request_length, void *const response_data,
	const size_t response_length)
{
	/* This subtracts one off the result to account for the command byte that gets stripped above */
	const ssize_t result =
		dap_run_cmd_raw((const uint8_t *)request_data, request_length, (uint8_t *)response_data, response_length) - 1U;
	return (size_t)result >= response_length;
}

#define ALIGNOF(x) (((x)&3) == 0 ? ALIGN_WORD : (((x)&1) == 0 ? ALIGN_HALFWORD : ALIGN_BYTE))

static void dap_mem_read(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len)
{
	if (len == 0)
		return;
	align_e align = MIN(ALIGNOF(src), ALIGNOF(len));
	DEBUG_WIRE("dap_mem_read @ %" PRIx32 " len %zu, align %d\n", src, len, align);
	/* If the read can be done in a single transaction, use the dap_read_single() fast-path */
	if ((1U << align) == len)
		return dap_read_single(ap, dest, src, align);
	/* Otherwise proceed blockwise */
	const size_t blocks_per_transfer = (report_size - 4U) >> 2U;
	uint8_t *const data = (uint8_t *)dest;
	for (size_t offset = 0; offset < len;) {
		/* Setup AP_TAR every loop as failing to do so results in it wrapping */
		dap_ap_mem_access_setup(ap, src + offset, align);
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
			if (!dap_read_block(ap, data + offset, src + offset, transfer_length, align)) {
				DEBUG_WIRE("mem_read failed: %u\n", ap->dp->fault);
				return;
			}
			offset += transfer_length;
		}
	}
	DEBUG_WIRE("dap_mem_read transferred %zu blocks\n", len >> align);
}

static void dap_mem_write(adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align)
{
	if (len == 0)
		return;
	DEBUG_WIRE("memwrite @ %" PRIx32 " len %zu, align %d\n", dest, len, align);
	/* If the write can be done in a single transaction, use the dap_write_single() fast-path */
	if ((1U << align) == len)
		return dap_write_single(ap, dest, src, align);
	/* Otherwise proceed blockwise */
	const size_t blocks_per_transfer = (report_size - 4U) >> 2U;
	const uint8_t *const data = (const uint8_t *)src;
	for (size_t offset = 0; offset < len;) {
		/* Setup AP_TAR every loop as failing to do so results in it wrapping */
		dap_ap_mem_access_setup(ap, dest + offset, align);
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
			if (!dap_write_block(ap, dest + offset, data + offset, transfer_length, align)) {
				DEBUG_WIRE("mem_write failed: %u\n", ap->dp->fault);
				return;
			}
			offset += transfer_length;
		}
	}
	DEBUG_WIRE("dap_mem_write_sized transferred %zu blocks\n", len >> align);

	/* Make sure this write is complete by doing a dummy read */
	adiv5_dp_read(ap->dp, ADIV5_DP_RDBUFF);
}

void dap_adiv5_dp_init(adiv5_debug_port_s *target_dp)
{
	/* Setup the access functions for this adaptor */
	target_dp->ap_read = dap_ap_read;
	target_dp->ap_write = dap_ap_write;
	target_dp->mem_read = dap_mem_read;
	target_dp->mem_write = dap_mem_write;
}
