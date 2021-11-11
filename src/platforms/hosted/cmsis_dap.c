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

#include "bmp_hosted.h"
#include "dap.h"
#include "cmsis_dap.h"

#include "cl_utils.h"
#include "target.h"
#include "target_internal.h"

uint8_t dap_caps;
uint8_t mode;

typedef enum cmsis_type_s {
	CMSIS_TYPE_NONE = 0,
	CMSIS_TYPE_HID,
	CMSIS_TYPE_BULK
} cmsis_type_t;
/*- Variables ---------------------------------------------------------------*/
static cmsis_type_t type;
static libusb_device_handle *usb_handle = NULL;
static uint8_t in_ep;
static uint8_t out_ep;
static hid_device *handle = NULL;
static uint8_t buffer[1024 + 1];
static int report_size = 64 + 1; // TODO: read actual report size
static bool has_swd_sequence = false;

/* LPC845 Breakout Board Rev. 0 report invalid response with > 65 bytes */
int dap_init(bmp_info_t *info)
{
	type = (info->in_ep && info->out_ep) ? CMSIS_TYPE_BULK : CMSIS_TYPE_HID;
	int size;

	if (type == CMSIS_TYPE_HID) {
		DEBUG_INFO("Using hid transfer\n");
		if (hid_init())
			return -1;
		size = strlen(info->serial);
		wchar_t serial[64] = {0}, *wc = serial;
		for (int i = 0; i < size; i++)
			*wc++ = info->serial[i];
		*wc = 0;
		/* Blacklist devices that do not work with 513 byte report length
		* FIXME: Find a solution to decipher from the device.
		*/
		if ((info->vid == 0x1fc9) && (info->pid == 0x0132)) {
			DEBUG_WARN("Blacklist\n");
			report_size = 64 + 1;
		}
		handle = hid_open(info->vid, info->pid,  (serial[0]) ? serial : NULL);
		if (!handle) {
			DEBUG_WARN("hid_open failed\n");
			return -1;
		}
	} else if (type == CMSIS_TYPE_BULK) {
		DEBUG_INFO("Using bulk transfer\n");
		usb_handle = libusb_open_device_with_vid_pid(info->libusb_ctx, info->vid, info->pid);
		if (!usb_handle) {
			DEBUG_WARN("WARN: libusb_open_device_with_vid_pid() failed\n");
			return -1;
		}
		if (libusb_claim_interface(usb_handle, info->interface_num) < 0) {
			DEBUG_WARN("WARN: libusb_claim_interface() failed\n");
			return -1;
		}
		in_ep = info->in_ep;
		out_ep = info->out_ep;
	}
	dap_disconnect();
	size = dap_info(DAP_INFO_FW_VER, buffer, sizeof(buffer));
	if (size) {
		DEBUG_INFO("Ver %s, ", buffer);
		int major = -1, minor = -1, sub = -1;
		if (sscanf((const char *)buffer, "%d.%d.%d",
				   &major, &minor, &sub)) {
			if (sub == -1) {
				if (minor >= 10) {
					minor /= 10;
					sub = 0;
				}
			}
			has_swd_sequence = ((major > 1 ) || ((major > 0 ) && (minor > 1)));
		}
	}
	size = dap_info(DAP_INFO_CAPABILITIES, buffer, sizeof(buffer));
	dap_caps = buffer[0];
	DEBUG_INFO("Cap (0x%2x): %s%s%s", dap_caps,
		   (dap_caps & 1)? "SWD" : "",
		   ((dap_caps & 3) == 3) ? "/" : "",
		   (dap_caps & 2)? "JTAG" : "");
	if (dap_caps & 4)
		DEBUG_INFO(", SWO_UART");
	if (dap_caps & 8)
		DEBUG_INFO(", SWO_MANCHESTER");
	if (dap_caps & 0x10)
		DEBUG_INFO(", Atomic Cmds");
	if (has_swd_sequence)
		DEBUG_INFO(", DAP_SWD_Sequence");
	DEBUG_INFO("\n");
	return 0;
}

void dap_srst_set_val(bool assert)
{
	dap_reset_pin(!assert);
}

static void dap_dp_abort(ADIv5_DP_t *dp, uint32_t abort)
{
	/* DP Write to Reg 0.*/
	dap_write_reg(dp, ADIV5_DP_ABORT, abort);
}

static uint32_t dap_dp_error(ADIv5_DP_t *dp)
{
	/* Not used for SWD debugging, so no TARGETID switch needed!*/
	uint32_t ctrlstat = dap_read_reg(dp, ADIV5_DP_CTRLSTAT);
	uint32_t err = ctrlstat &
		(ADIV5_DP_CTRLSTAT_STICKYORUN | ADIV5_DP_CTRLSTAT_STICKYCMP |
		ADIV5_DP_CTRLSTAT_STICKYERR | ADIV5_DP_CTRLSTAT_WDATAERR);
	uint32_t clr = 0;
	if(err & ADIV5_DP_CTRLSTAT_STICKYORUN)
		clr |= ADIV5_DP_ABORT_ORUNERRCLR;
	if(err & ADIV5_DP_CTRLSTAT_STICKYCMP)
		clr |= ADIV5_DP_ABORT_STKCMPCLR;
	if(err & ADIV5_DP_CTRLSTAT_STICKYERR)
		clr |= ADIV5_DP_ABORT_STKERRCLR;
	if(err & ADIV5_DP_CTRLSTAT_WDATAERR)
		clr |= ADIV5_DP_ABORT_WDERRCLR;
	dap_write_reg(dp, ADIV5_DP_ABORT, clr);
	dp->fault = 0;
	return err;
}

static uint32_t dap_dp_low_access(struct ADIv5_DP_s *dp, uint8_t RnW,
                               uint16_t addr, uint32_t value)
{
	bool APnDP = addr & ADIV5_APnDP;
	uint32_t res = 0;
	uint8_t reg = (addr & 0xc) | ((APnDP)? 1 : 0);
	if (RnW) {
		res = dap_read_reg(dp, reg);
	}
	else {
		dap_write_reg(dp, reg, value);
	}
	return res;
}

static uint32_t dap_dp_read_reg(ADIv5_DP_t *dp, uint16_t addr)
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

int dbg_dap_cmd(uint8_t *data, int size, int rsize)

{
	char cmd = data[0];
	int res = -1;

	memset(buffer, 0xff, report_size + 1);

	buffer[0] = 0x00; // Report ID??
	memcpy(&buffer[1], data, rsize);

	DEBUG_WIRE("cmd :   ");
	for(int i = 0; (i < 32) && (i < rsize + 1); i++)
		DEBUG_WIRE("%02x.",	buffer[i]);
	DEBUG_WIRE("\n");
	if (type == CMSIS_TYPE_HID) {
		res = hid_write(handle, buffer, 65);
		if (res < 0) {
			DEBUG_WARN( "Error: %ls\n", hid_error(handle));
			exit(-1);
		}
		res = hid_read_timeout(handle, buffer, 65, 1000);
		if (res < 0) {
			DEBUG_WARN( "debugger read(): %ls\n", hid_error(handle));
			exit(-1);
		} else if (res == 0) {
			DEBUG_WARN( "timeout\n");
			exit(-1);
		}
	} else if (type == CMSIS_TYPE_BULK) {
		int transferred = 0;

		res = libusb_bulk_transfer(usb_handle, out_ep, data, rsize, &transferred, 500);
		if (res < 0) {
			DEBUG_WARN("OUT error: %d\n", res);
			return res;
		}
		res = libusb_bulk_transfer(usb_handle, in_ep, buffer, report_size, &transferred, 500);
		if (res < 0) {
			DEBUG_WARN("IN error: %d\n", res);
			return res;
		}
		res = transferred;
	}
	DEBUG_WIRE("cmd res:");
	for(int i = 0; (i < 16) && (i < size + 1); i++)
		DEBUG_WIRE("%02x.",	buffer[i]);
	DEBUG_WIRE("\n");
	if (buffer[0] != cmd) {
		DEBUG_WARN("cmd %02x not implemented\n", cmd);
		buffer[1] = 0xff /*DAP_ERROR*/;
	}
	if (size)
		memcpy(data, &buffer[1], (size < res) ? size : res);
	return res;
}
#define ALIGNOF(x) (((x) & 3) == 0 ? ALIGN_WORD :					\
                    (((x) & 1) == 0 ? ALIGN_HALFWORD : ALIGN_BYTE))

static void dap_mem_read(ADIv5_AP_t *ap, void *dest, uint32_t src, size_t len)
{
	if (len == 0)
		return;
	enum align align = MIN(ALIGNOF(src), ALIGNOF(len));
	DEBUG_WIRE("memread @ %" PRIx32 " len %ld, align %d , start: \n",
		   src, len, align);
	if (((unsigned)(1 << align)) == len)
		return dap_read_single(ap, dest, src, align);
	/* One word transfer for every byte/halfword/word
	 * Total number of bytes in transfer*/
	unsigned int max_size = ((dbg_get_report_size() - 6) >> (2 - align)) & ~3;
	while (len) {
		dap_ap_mem_access_setup(ap, src, align);
		/* Calculate length until next access setup is needed */
		unsigned int blocksize = (src | 0x3ff) - src + 1;
		if (blocksize > len)
			blocksize = len;
		while (blocksize) {
			unsigned int transfersize = blocksize;
			if (transfersize > max_size)
				transfersize = max_size;
			unsigned int res = dap_read_block(ap, dest, src, transfersize,
											  align);
			if (res) {
			    DEBUG_WIRE("mem_read failed %02x\n", res);
				ap->dp->fault = 1;
				return;
			}
			blocksize -= transfersize;
			len       -= transfersize;
			dest      += transfersize;
			src       += transfersize;
		}
	}
    DEBUG_WIRE("memread res last data %08" PRIx32 "\n", ((uint32_t*)dest)[-1]);
}

static void dap_mem_write_sized(
	ADIv5_AP_t *ap, uint32_t dest, const void *src,
							size_t len, enum align align)
{
	if (len == 0)
		return;
	DEBUG_WIRE("memwrite @ %" PRIx32 " len %ld, align %d , %08x start: \n",
		dest, len, align, *(uint32_t *)src);
	if (((unsigned)(1 << align)) == len)
		return dap_write_single(ap, dest, src, align);
	unsigned int max_size = ((dbg_get_report_size() - 6) >> (2 - align) & ~3);
	while (len) {
		dap_ap_mem_access_setup(ap, dest, align);
		unsigned int blocksize = (dest | 0x3ff) - dest + 1;
		if (blocksize > len)
			blocksize = len;
		while (blocksize) {
			unsigned int transfersize = blocksize;
			if (transfersize > max_size)
				transfersize = max_size;
			unsigned int res = dap_write_block(ap, dest, src, transfersize,
											   align);
			if (res) {
				DEBUG_WARN("mem_write failed %02x\n", res);
				ap->dp->fault = 1;
				return;
			}
			blocksize -= transfersize;
			len       -= transfersize;
			dest      += transfersize;
			src       += transfersize;
		}
	}
	DEBUG_WIRE("memwrite done\n");
}

void dap_adiv5_dp_defaults(ADIv5_DP_t *dp)
{
	if ((mode == DAP_CAP_JTAG) && dap_jtag_configure())
		return;
	dp->ap_read  = dap_ap_read;
	dp->ap_write = dap_ap_write;
	dp->mem_read = dap_mem_read;
	dp->mem_write_sized =  dap_mem_write_sized;
}

static void cmsis_dap_jtagtap_reset(void)
{
	jtagtap_soft_reset();
	/* Is there a way to know if TRST is available?*/
}

static void cmsis_dap_jtagtap_tms_seq(uint32_t MS, int ticks)
{
	uint8_t TMS[4] = {MS & 0xff, (MS >> 8) & 0xff, (MS >> 16) & 0xff,
					  (MS >> 24) & 0xff};
	dap_jtagtap_tdi_tdo_seq(NULL, false, TMS, NULL, ticks);
	DEBUG_PROBE("tms_seq DI %08x %d\n", MS, ticks);
}

static void cmsis_dap_jtagtap_tdi_tdo_seq(uint8_t *DO, const uint8_t final_tms,
										  const uint8_t *DI, int ticks)
{
	dap_jtagtap_tdi_tdo_seq(DO, (final_tms), NULL, DI, ticks);
	DEBUG_PROBE("jtagtap_tdi_tdo_seq %d, %02x-> %02x\n", ticks, DI[0], (DO)? DO[0] : 0);
}

static void  cmsis_dap_jtagtap_tdi_seq(const uint8_t final_tms,
									   const uint8_t *DI, int ticks)
{
	dap_jtagtap_tdi_tdo_seq(NULL, (final_tms), NULL, DI, ticks);
	DEBUG_PROBE("jtagtap_tdi_seq %d, %02x\n", ticks, DI[0]);
}

static uint8_t cmsis_dap_jtagtap_next(uint8_t dTMS, uint8_t dTDI)
{
	uint8_t tdo[1];
	dap_jtagtap_tdi_tdo_seq(tdo, false, &dTMS, &dTDI, 1);
	DEBUG_PROBE("next tms %02x tdi %02x tdo %02x\n", dTMS, dTDI, tdo[0]);
	return (tdo[0] & 1);
}

int cmsis_dap_jtagtap_init(jtag_proc_t *jtag_proc)
{
	DEBUG_PROBE("jtap_init\n");
	if (!(dap_caps & DAP_CAP_JTAG))
		return -1;
	mode =  DAP_CAP_JTAG;
	dap_disconnect();
	dap_connect(true);
	dap_reset_link(true);
	jtag_proc->jtagtap_reset       = cmsis_dap_jtagtap_reset;
	jtag_proc->jtagtap_next        = cmsis_dap_jtagtap_next;
	jtag_proc->jtagtap_tms_seq     = cmsis_dap_jtagtap_tms_seq;
	jtag_proc->jtagtap_tdi_tdo_seq = cmsis_dap_jtagtap_tdi_tdo_seq;
	jtag_proc->jtagtap_tdi_seq     = cmsis_dap_jtagtap_tdi_seq;
	return 0;
}

int dap_jtag_dp_init(ADIv5_DP_t *dp)
{
	dp->dp_read = dap_dp_read_reg;
	dp->error = dap_dp_error;
	dp->low_access = dap_dp_low_access;
	dp->abort = dap_dp_abort;

	return true;
}

#define SWD_SEQUENCE_IN 0x80
#define DAP_SWD_SEQUENCE 0x1d
static bool dap_dp_low_write(ADIv5_DP_t *dp, uint16_t addr, const uint32_t data)
{
	DEBUG_PROBE("dap_dp_low_write %08" PRIx32 "\n", data);
	(void)dp;
	unsigned int paket_request = make_packet_request(ADIV5_LOW_WRITE, addr);
	uint8_t buf[32] = {
		DAP_SWD_SEQUENCE,
		5,
		8,
		paket_request,
		4 + SWD_SEQUENCE_IN,  /* one turn-around + read 3 bit ACK */
		1,                    /* one bit turn around to drive SWDIO */
		0,
		32,                   /* write 32 bit data */
		(data >>  0) & 0xff,
		(data >>  8) & 0xff,
		(data >> 16) & 0xff,
		(data >> 24) & 0xff,
		1,                    /* write parity biT */
		__builtin_parity(data)
	};
	dbg_dap_cmd(buf, sizeof(buf), 14);
	if (buf[0])
		DEBUG_WARN("dap_dp_low_write failed\n");
	uint32_t ack = (buf[1] >> 1) & 7;
	return (ack != SWDP_ACK_OK);
}

int dap_swdptap_init(ADIv5_DP_t *dp)
{
	if (!(dap_caps & DAP_CAP_SWD))
		return 1;
	mode =  DAP_CAP_SWD;
	dap_transfer_configure(2, 128, 128);
	dap_swd_configure(0);
	dap_connect(false);
	dap_led(0, 1);
	dap_reset_link(false);
	if ((has_swd_sequence)  && dap_sequence_test()) {
		/* DAP_SWD_SEQUENCE does not do auto turnaround, use own!*/
		dp->dp_low_write = dap_dp_low_write;
	} else {
		dp->dp_low_write = NULL;
	}
	dp->seq_out = dap_swdptap_seq_out;
	dp->dp_read = dap_dp_read_reg;
	/* For error() use the TARGETID switching firmware_swdp_error */
	dp->low_access = dap_dp_low_access;
	dp->abort = dap_dp_abort;
	return 0;
}
