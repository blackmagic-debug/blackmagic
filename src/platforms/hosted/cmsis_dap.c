/*
 * Copyright (c) 2013-2019, Alex Taradov <alex@taradov.com>
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
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/time.h>
#include <hidapi.h>
#include <wchar.h>
#include <sys/stat.h>

#include "bmp_hosted.h"
#include "dap.h"
#include "cmsis_dap.h"

#include "cli.h"
#include "target.h"
#include "target_internal.h"

uint8_t dap_caps;
uint8_t mode;

#define TRANSFER_TIMEOUT_MS (100)

typedef enum cmsis_type {
	CMSIS_TYPE_NONE = 0,
	CMSIS_TYPE_HID,
	CMSIS_TYPE_BULK
} cmsis_type_e;

#ifdef __linux__
typedef struct hid_device_info hid_device_info_s;
#endif

/*- Variables ---------------------------------------------------------------*/
static cmsis_type_e type;
static libusb_device_handle *usb_handle = NULL;
static uint8_t in_ep;
static uint8_t out_ep;
static hid_device *handle = NULL;
static uint8_t buffer[1024U];
static size_t report_size = 64U + 1U; // TODO: read actual report size
static bool has_swd_sequence = false;

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
	PRINT_INFO("Tried device '%s'", path);
	struct stat dev_stat;
	if (stat(path, &dev_stat) == 0) {
		PRINT_INFO(", permissions = %04o, owner = %u, group = %u", dev_stat.st_mode & ACCESSPERMS, dev_stat.st_uid,
			dev_stat.st_gid);
	}
	PRINT_INFO("\n");
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

static bool dap_init_hid(const bmp_info_s *const info)
{
	DEBUG_INFO("Using hid transfer\n");
	if (hid_init())
		return false;

	const size_t size = mbslen(info->serial);
	if (size > 64U) {
		PRINT_INFO("Serial number invalid, aborting\n");
		hid_exit();
		return false;
	}
	wchar_t serial[65] = {0};
	if (mbstowcs(serial, info->serial, size) != size) {
		PRINT_INFO("Serial number conversion failed, aborting\n");
		hid_exit();
		return false;
	}
	serial[size] = 0;
	/* Blacklist devices that do not work with 513 byte report length
	* FIXME: Find a solution to decipher from the device.
	*/
	if (info->vid == 0x1fc9U && info->pid == 0x0132U) {
		DEBUG_WARN("Blacklist\n");
		report_size = 64U + 1U;
	}
	handle = hid_open(info->vid, info->pid, serial[0] ? serial : NULL);
	if (!handle) {
		PRINT_INFO("hid_open failed: %ls\n", hid_error(NULL));
#ifdef __linux__
		dap_hid_print_permissions(info->vid, info->pid, serial[0] ? serial : NULL);
#endif
		hid_exit();
		return false;
	}
	return true;
}

static bool dap_init_bulk(const bmp_info_s *const info)
{
	DEBUG_INFO("Using bulk transfer\n");
	usb_handle = libusb_open_device_with_vid_pid(info->libusb_ctx, info->vid, info->pid);
	if (!usb_handle) {
		DEBUG_WARN("WARN: libusb_open_device_with_vid_pid() failed\n");
		return false;
	}
	if (libusb_claim_interface(usb_handle, info->interface_num) < 0) {
		DEBUG_WARN("WARN: libusb_claim_interface() failed\n");
		return false;
	}
	in_ep = info->in_ep;
	out_ep = info->out_ep;
	return true;
}

/* LPC845 Breakout Board Rev. 0 report invalid response with > 65 bytes */
int dap_init(bmp_info_s *info)
{
	type = (info->in_ep && info->out_ep) ? CMSIS_TYPE_BULK : CMSIS_TYPE_HID;

	if (type == CMSIS_TYPE_HID) {
		if (!dap_init_hid(info))
			return -1;
	} else if (type == CMSIS_TYPE_BULK) {
		if (!dap_init_bulk(info))
			return -1;
	}
	dap_disconnect();
	size_t size = dap_info(DAP_INFO_FW_VER, buffer, sizeof(buffer));
	if (size) {
		DEBUG_INFO("Ver %s, ", buffer);
		int major = -1;
		int minor = -1;
		int sub = -1;
		if (sscanf((const char *)buffer, "%d.%d.%d", &major, &minor, &sub)) {
			if (sub == -1) {
				if (minor >= 10) {
					minor /= 10U;
					sub = 0;
				}
			}
			has_swd_sequence = ((major > 1) || ((major > 0) && (minor > 1)));
		}
	}
	size = dap_info(DAP_INFO_CAPABILITIES, buffer, sizeof(buffer));
	dap_caps = buffer[0];
	DEBUG_INFO("Cap (0x%2x): %s%s%s", dap_caps, (dap_caps & 1U) ? "SWD" : "", ((dap_caps & 3U) == 3U) ? "/" : "",
		(dap_caps & 0x2U) ? "JTAG" : "");
	if (dap_caps & 0x4U)
		DEBUG_INFO(", SWO_UART");
	if (dap_caps & 0x8U)
		DEBUG_INFO(", SWO_MANCHESTER");
	if (dap_caps & 0x10U)
		DEBUG_INFO(", Atomic Cmds");
	if (has_swd_sequence)
		DEBUG_INFO(", DAP_SWD_Sequence");
	DEBUG_INFO("\n");
	return 0;
}

void dap_nrst_set_val(bool assert)
{
	dap_reset_pin(!assert);
}

static void dap_dp_abort(adiv5_debug_port_s *dp, uint32_t abort)
{
	/* DP Write to Reg 0.*/
	dap_write_reg(dp, ADIV5_DP_ABORT, abort);
}

/* JTAG DP error recovery function */
static uint32_t dap_dp_error(adiv5_debug_port_s *dp, const bool protocol_recovery)
{
	(void)protocol_recovery;
	/* XXX: This seems entirely wrong considering adiv5_jtagdp.c adiv5_jtagdp_error */
	uint32_t ctrlstat = dap_read_reg(dp, ADIV5_DP_CTRLSTAT);
	uint32_t err = ctrlstat &
		(ADIV5_DP_CTRLSTAT_STICKYORUN | ADIV5_DP_CTRLSTAT_STICKYCMP | ADIV5_DP_CTRLSTAT_STICKYERR |
			ADIV5_DP_CTRLSTAT_WDATAERR);
	uint32_t clr = 0;
	if (err & ADIV5_DP_CTRLSTAT_STICKYORUN)
		clr |= ADIV5_DP_ABORT_ORUNERRCLR;
	if (err & ADIV5_DP_CTRLSTAT_STICKYCMP)
		clr |= ADIV5_DP_ABORT_STKCMPCLR;
	if (err & ADIV5_DP_CTRLSTAT_STICKYERR)
		clr |= ADIV5_DP_ABORT_STKERRCLR;
	if (err & ADIV5_DP_CTRLSTAT_WDATAERR)
		clr |= ADIV5_DP_ABORT_WDERRCLR;
	dap_write_reg(dp, ADIV5_DP_ABORT, clr);
	dp->fault = 0;
	return err;
}

static uint32_t dap_dp_low_access(adiv5_debug_port_s *dp, uint8_t RnW, uint16_t addr, uint32_t value)
{
	bool APnDP = addr & ADIV5_APnDP;
	uint32_t res = 0;
	uint8_t reg = (addr & 0xcU) | ((APnDP) ? 1 : 0);
	if (RnW) {
		res = dap_read_reg(dp, reg);
	} else {
		dap_write_reg(dp, reg, value);
	}
	return res;
}

static uint32_t dap_dp_read_reg(adiv5_debug_port_s *dp, uint16_t addr)
{
	uint32_t res = dap_dp_low_access(dp, ADIV5_LOW_READ, addr, 0);
	DEBUG_PROBE("dp_read %04x %08" PRIx32 "\n", addr, res);
	return res;
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

int dbg_get_report_size(void)
{
	return report_size;
}

ssize_t dbg_dap_cmd_hid(const uint8_t *const data, const size_t request_length)
{
	if (request_length + 1U > report_size) {
		DEBUG_WARN(
			"Attempted to make over-long request of %zu bytes, max length is %zu\n", request_length + 1U, report_size);
		exit(-1);
	}

	memset(buffer + request_length + 1U, 0xff, report_size - (request_length + 1U));
	buffer[0] = 0x00; // Report ID??
	memcpy(buffer + 1, data, request_length);

	const int result = hid_write(handle, buffer, 65);
	if (result < 0) {
		DEBUG_WARN("CMSIS-DAP write error: %ls\n", hid_error(handle));
		exit(-1);
	}

	int response = 0;
	do {
		response = hid_read_timeout(handle, buffer, 65, 1000);
		if (response < 0) {
			DEBUG_WARN("CMSIS-DAP read error: %ls\n", hid_error(handle));
			exit(-1);
		} else if (response == 0) {
			DEBUG_WARN("CMSIS-DAP read timeout\n");
			exit(-1);
		}
	} while (buffer[0] != data[0]);
	return response;
}

ssize_t dbg_dap_cmd_bulk(uint8_t *const data, const size_t request_length)
{
	int transferred = 0;
	const int result =
		libusb_bulk_transfer(usb_handle, out_ep, data, request_length, &transferred, TRANSFER_TIMEOUT_MS);
	if (result < 0) {
		DEBUG_WARN("CMSIS-DAP write error: %d\n", result);
		return result;
	}

	/* We repeat the read in case we're out of step with the transmitter */
	do {
		const int result =
			libusb_bulk_transfer(usb_handle, in_ep, buffer, report_size, &transferred, TRANSFER_TIMEOUT_MS);
		if (result < 0) {
			DEBUG_WARN("CMSIS-DAP read error: %d\n", result);
			return result;
		}
	} while (buffer[0] != data[0]);
	return transferred;
}

int dbg_dap_cmd(uint8_t *data, int response_length, int request_length)
{
	char cmd = data[0];
	int res = -1;

	DEBUG_WIRE("cmd :   ");
	for (int i = 0; (i < request_length + 1); i++)
		DEBUG_WIRE("%02x.", data[i]);
	DEBUG_WIRE("\n");
	if (type == CMSIS_TYPE_HID)
		res = dbg_dap_cmd_hid(data, request_length);
	else if (type == CMSIS_TYPE_BULK)
		res = dbg_dap_cmd_bulk(data, request_length);
	if (res < 0)
		return res;

	DEBUG_WIRE("cmd res:");
	for (int i = 0; i < res; i++)
		DEBUG_WIRE("%02x.", buffer[i]);
	DEBUG_WIRE("\n");
	if (buffer[0] != cmd) {
		DEBUG_WARN("cmd %02x not implemented\n", cmd);
		buffer[1] = 0xff /*DAP_ERROR*/;
	}
	if (response_length)
		memcpy(data, &buffer[1], (response_length < res) ? response_length : res);
	return res;
}

#define ALIGNOF(x) (((x)&3) == 0 ? ALIGN_WORD : (((x)&1) == 0 ? ALIGN_HALFWORD : ALIGN_BYTE))

static void dap_mem_read(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len)
{
	if (len == 0)
		return;
	align_e align = MIN(ALIGNOF(src), ALIGNOF(len));
	DEBUG_WIRE("memread @ %" PRIx32 " len %ld, align %d , start: \n", src, len, align);
	if (((unsigned)(1 << align)) == len)
		return dap_read_single(ap, dest, src, align);
	/* One word transfer for every byte/halfword/word
	 * Total number of bytes in transfer*/
	unsigned int max_size = ((dbg_get_report_size() - 6U) >> (2U - align)) & ~3U;
	while (len) {
		dap_ap_mem_access_setup(ap, src, align);
		/* Calculate length until next access setup is needed */
		unsigned int blocksize = (src | 0x3ffU) - src + 1U;
		if (blocksize > len)
			blocksize = len;
		while (blocksize) {
			unsigned int transfersize = blocksize;
			if (transfersize > max_size)
				transfersize = max_size;
			unsigned int res = dap_read_block(ap, dest, src, transfersize, align);
			if (res) {
				DEBUG_WIRE("mem_read failed %02x\n", res);
				ap->dp->fault = 1;
				return;
			}
			blocksize -= transfersize;
			len -= transfersize;
			dest += transfersize;
			src += transfersize;
		}
	}
	DEBUG_WIRE("memread res last data %08" PRIx32 "\n", ((uint32_t *)dest)[-1]);
}

static void dap_mem_write_sized(adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align)
{
	if (len == 0)
		return;
	DEBUG_WIRE("memwrite @ %" PRIx32 " len %ld, align %d , %08x start: \n", dest, len, align, *(uint32_t *)src);
	if (((unsigned)(1 << align)) == len)
		return dap_write_single(ap, dest, src, align);
	unsigned int max_size = ((dbg_get_report_size() - 6U) >> (2U - align) & ~3U);
	while (len) {
		dap_ap_mem_access_setup(ap, dest, align);
		unsigned int blocksize = (dest | 0x3ffU) - dest + 1U;
		if (blocksize > len)
			blocksize = len;
		while (blocksize) {
			unsigned int transfersize = blocksize;
			if (transfersize > max_size)
				transfersize = max_size;
			unsigned int res = dap_write_block(ap, dest, src, transfersize, align);
			if (res) {
				DEBUG_WARN("mem_write failed %02x\n", res);
				ap->dp->fault = 1;
				return;
			}
			blocksize -= transfersize;
			len -= transfersize;
			dest += transfersize;
			src += transfersize;
		}
	}

	/* Make sure this write is complete by doing a dummy read */
	adiv5_dp_read(ap->dp, ADIV5_DP_RDBUFF);
	DEBUG_WIRE("memwrite done\n");
}

void dap_adiv5_dp_defaults(adiv5_debug_port_s *dp)
{
	if ((mode == DAP_CAP_JTAG) && dap_jtag_configure())
		return;
	dp->ap_read = dap_ap_read;
	dp->ap_write = dap_ap_write;
	dp->mem_read = dap_mem_read;
	dp->mem_write_sized = dap_mem_write_sized;
}

static void cmsis_dap_jtagtap_reset(void)
{
	jtagtap_soft_reset();
	/* Is there a way to know if TRST is available?*/
}

static void cmsis_dap_jtagtap_tms_seq(const uint32_t tms_states, const size_t clock_cycles)
{
	const uint8_t tms[4] = {
		(uint8_t)tms_states, (uint8_t)(tms_states >> 8U), (uint8_t)(tms_states >> 16U), (uint8_t)(tms_states >> 24U)};
	dap_jtagtap_tdi_tdo_seq(NULL, false, tms, NULL, clock_cycles);
	DEBUG_PROBE("tms_seq data_in %08x %zu\n", tms_states, clock_cycles);
}

static void cmsis_dap_jtagtap_tdi_tdo_seq(
	uint8_t *const data_out, const bool final_tms, const uint8_t *const data_in, const size_t clock_cycles)
{
	dap_jtagtap_tdi_tdo_seq(data_out, final_tms, NULL, data_in, clock_cycles);
	DEBUG_PROBE("jtagtap_tdi_tdo_seq %zu, %02x-> %02x\n", clock_cycles, data_in[0], data_out ? data_out[0] : 0);
}

static void cmsis_dap_jtagtap_tdi_seq(const bool final_tms, const uint8_t *const data_in, const size_t clock_cycles)
{
	dap_jtagtap_tdi_tdo_seq(NULL, final_tms, NULL, data_in, clock_cycles);
	DEBUG_PROBE("jtagtap_tdi_seq %zu, %02x\n", clock_cycles, data_in[0]);
}

static bool cmsis_dap_jtagtap_next(const bool tms, const bool tdi)
{
	const uint8_t tms_byte = tms ? 1 : 0;
	const uint8_t tdi_byte = tdi ? 1 : 0;
	uint8_t tdo = 0;
	dap_jtagtap_tdi_tdo_seq(&tdo, false, &tms_byte, &tdi_byte, 1U);
	DEBUG_PROBE("next tms %02x tdi %02x tdo %02x\n", tms, tdi, tdo);
	return tdo;
}

int cmsis_dap_jtagtap_init(jtag_proc_s *jtag_proc)
{
	DEBUG_PROBE("jtap_init\n");
	if (!(dap_caps & DAP_CAP_JTAG))
		return -1;
	mode = DAP_CAP_JTAG;
	dap_disconnect();
	dap_connect(true);
	dap_reset_link(true);
	jtag_proc->jtagtap_reset = cmsis_dap_jtagtap_reset;
	jtag_proc->jtagtap_next = cmsis_dap_jtagtap_next;
	jtag_proc->jtagtap_tms_seq = cmsis_dap_jtagtap_tms_seq;
	jtag_proc->jtagtap_tdi_tdo_seq = cmsis_dap_jtagtap_tdi_tdo_seq;
	jtag_proc->jtagtap_tdi_seq = cmsis_dap_jtagtap_tdi_seq;
	return 0;
}

int dap_jtag_dp_init(adiv5_debug_port_s *dp)
{
	dp->dp_read = dap_dp_read_reg;
	dp->error = dap_dp_error;
	dp->low_access = dap_dp_low_access;
	dp->abort = dap_dp_abort;

	return true;
}

#define SWD_SEQUENCE_IN  0x80U
#define DAP_SWD_SEQUENCE 0x1dU

static bool dap_dp_low_write(adiv5_debug_port_s *dp, uint16_t addr, const uint32_t data)
{
	DEBUG_PROBE("dap_dp_low_write %08" PRIx32 "\n", data);
	(void)dp;
	unsigned int paket_request = make_packet_request(ADIV5_LOW_WRITE, addr);
	uint8_t buf[32] = {DAP_SWD_SEQUENCE, 5, 8, paket_request,
		4U + SWD_SEQUENCE_IN, /* one turn-around + read 3 bit ACK */
		1,                    /* one bit turn around to drive SWDIO */
		0, 32,                /* write 32 bit data */
		data & 0xffU, (data >> 8U) & 0xffU, (data >> 16U) & 0xffU, (data >> 24U) & 0xffU, 1, /* write parity biT */
		__builtin_parity(data)};
	dbg_dap_cmd(buf, sizeof(buf), 14);
	if (buf[0])
		DEBUG_WARN("dap_dp_low_write failed\n");
	uint32_t ack = (buf[1] >> 1U) & 7U;
	return (ack != SWDP_ACK_OK);
}

int dap_swdptap_init(adiv5_debug_port_s *dp)
{
	if (!(dap_caps & DAP_CAP_SWD))
		return 1;
	mode = DAP_CAP_SWD;
	dap_transfer_configure(2, 128, 128);
	dap_swd_configure(0);
	dap_connect(false);
	dap_led(0, 1);
	dap_reset_link(false);
	if (has_swd_sequence)
		/* DAP_SWD_SEQUENCE does not do auto turnaround, use own!*/
		dp->dp_low_write = dap_dp_low_write;
	else
		dp->dp_low_write = NULL;
	dp->seq_out = dap_swdptap_seq_out;
	dp->dp_read = dap_dp_read_reg;
	/* For error() use the TARGETID switching firmware_swdp_error */
	dp->low_access = dap_dp_low_access;
	dp->abort = dap_dp_abort;
	return 0;
}
