/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019-20 Uwe Bonnes <bon@elektron,ikp.physik.tu-darmstadt.de>
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

/*- Variables ---------------------------------------------------------------*/
static hid_device *handle = NULL;
static uint8_t hid_buffer[1024 + 1];
static int report_size = 64 + 1; // TODO: read actual report size
/* LPC845 Breakout Board Rev. 0 report invalid response with > 65 bytes */
int dap_init(bmp_info_t *info)
{
	DEBUG_INFO("dap_init\n");
	if (hid_init())
		return -1;
	int size = strlen(info->serial);
	wchar_t serial[size + 1], *wc = serial;
	for (int i = 0; i < size; i++)
		*wc++ = info->serial[i];
	*wc = 0;
	/* Blacklist devices that do not wirk with 513 byte report length
	 * FIXME: Find a solution to decipher from the device.
	 */
	if ((info->vid == 0x1fc9) && (info->pid == 0x0132)) {
		DEBUG_WARN("Blacklist\n");
		report_size = 64 + 1;
	}
	handle = hid_open(info->vid, info->pid, serial);
	if (!handle)
		return -1;
	dap_disconnect();
	size = dap_info(DAP_INFO_CAPABILITIES, hid_buffer, sizeof(hid_buffer));
	dap_caps = hid_buffer[0];
	DEBUG_INFO(" Cap (0x%2x): %s%s%s", hid_buffer[0],
		   (hid_buffer[0] & 1)? "SWD" : "",
		   ((hid_buffer[0] & 3) == 3) ? "/" : "",
		   (hid_buffer[0] & 2)? "JTAG" : "");
	if (hid_buffer[0] & 4)
		DEBUG_INFO(", SWO_UART");
	if (hid_buffer[0] & 8)
		DEBUG_INFO(", SWO_MANCHESTER");
	if (hid_buffer[0] & 0x10)
		DEBUG_INFO(", Atomic Cmds");
	DEBUG_INFO("\n");
	return 0;
}

static void dap_dp_abort(ADIv5_DP_t *dp, uint32_t abort)
{
	/* DP Write to Reg 0.*/
	dap_write_reg(dp, ADIV5_DP_ABORT, abort);
}

static uint32_t dap_dp_error(ADIv5_DP_t *dp)
{
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
	if (handle) {
		dap_disconnect();
		hid_close(handle);
	}
}

int dbg_get_report_size(void)
{
	return report_size;
}

int dbg_dap_cmd(uint8_t *data, int size, int rsize)

{
	char cmd = data[0];
	int res;

	memset(hid_buffer, 0xff, report_size + 1);

	hid_buffer[0] = 0x00; // Report ID??
	memcpy(&hid_buffer[1], data, rsize);

	DEBUG_WIRE("cmd :   ");
	for(int i = 0; (i < 16) && (i < rsize + 1); i++)
		DEBUG_WIRE("%02x.",	hid_buffer[i]);
	DEBUG_WIRE("\n");
	res = hid_write(handle, hid_buffer, rsize + 1);
	if (res < 0) {
		DEBUG_WARN( "Error: %ls\n", hid_error(handle));
		exit(-1);
	}
	if (size) {
		res = hid_read(handle, hid_buffer, report_size + 1);
		if (res < 0) {
			DEBUG_WARN( "debugger read(): %ls\n", hid_error(handle));
			exit(-1);
		}
		if (size && hid_buffer[0] != cmd) {
			DEBUG_WARN("cmd %02x invalid response received %02x\n",
				   cmd, hid_buffer[0]);
		}
		res--;
		memcpy(data, &hid_buffer[1], (size < res) ? size : res);
		DEBUG_WIRE("cmd res:");
		for(int i = 0; (i < 16) && (i < size + 4); i++)
			DEBUG_WIRE("%02x.",	hid_buffer[i]);
		DEBUG_WIRE("\n");
	}

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
	unsigned int max_size = (dbg_get_report_size() - 5) >> (2 - align);
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
	unsigned int max_size = (dbg_get_report_size() - 5) >> (2 - align);
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

int dap_enter_debug_swd(ADIv5_DP_t *dp)
{
	target_list_free();
	if (!(dap_caps & DAP_CAP_SWD))
		return -1;
	mode =  DAP_CAP_SWD;
	dap_swj_clock(2000000);
	dap_transfer_configure(2, 128, 128);
	dap_swd_configure(0);
	dap_connect(false);
	dap_led(0, 1);
	dap_reset_link(false);

	dp->idcode = dap_read_idcode(dp);
	dp->dp_read = dap_dp_read_reg;
	dp->error = dap_dp_error;
	dp->low_access =  dap_dp_low_access;
	dp->abort = dap_dp_abort; /* DP Write to Reg 0.*/
	return 0;
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
	DEBUG_PROBE("jtagtap_tdi_tdo_seq %d, %02x-> %02x\n", ticks, DI[0], DO[0]);
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
	dap_swj_clock(2000000);
	jtag_proc->jtagtap_reset       = cmsis_dap_jtagtap_reset;
	jtag_proc->jtagtap_next        = cmsis_dap_jtagtap_next;
	jtag_proc->jtagtap_tms_seq     = cmsis_dap_jtagtap_tms_seq;
	jtag_proc->jtagtap_tdi_tdo_seq = cmsis_dap_jtagtap_tdi_tdo_seq;
	jtag_proc->jtagtap_tdi_seq     = cmsis_dap_jtagtap_tdi_seq;
	dap_reset_link(true);
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
