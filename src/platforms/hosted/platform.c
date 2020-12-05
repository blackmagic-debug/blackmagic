/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

/* Handle different BMP pc-hosted platforms/
 */

#include "general.h"
#include "swdptap.h"
#include "jtagtap.h"
#include "target.h"
#include "target_internal.h"
#include "adiv5.h"
#include "timing.h"
#include "cl_utils.h"
#include "gdb_if.h"
#include <signal.h>

#include "bmp_remote.h"
#include "bmp_hosted.h"
#include "stlinkv2.h"
#include "ftdi_bmp.h"
#include "jlink.h"
#include "cmsis_dap.h"
#include "version.h"
#include "cl_utils.h"

bmp_info_t info;

swd_proc_t swd_proc;
jtag_proc_t jtag_proc;

void gdb_ident(char *p, int count)
{
	snprintf(p, count, "\n for %s (%s), %s\n", info.manufacturer, info.product,
			 info.version);
}

static void exit_function(void)
{
	libusb_exit_function(&info);
	switch (info.bmp_type) {
	case BMP_TYPE_CMSIS_DAP:
		dap_exit_function();
		break;
	default:
		break;
	}
	fflush(stdout);
}

/* SIGTERM handler. */
static void sigterm_handler(int sig)
{
	(void)sig;
	exit(0);
}

static	BMP_CL_OPTIONS_t cl_opts;

void platform_init(int argc, char **argv)
{
	cl_opts.opt_idstring = "Blackmagic PC-Hosted";
	cl_init(&cl_opts, argc, argv);
	atexit(exit_function);
	signal(SIGTERM, sigterm_handler);
	signal(SIGINT, sigterm_handler);
	if (cl_opts.opt_device) {
		info.bmp_type = BMP_TYPE_BMP;
	} else if (find_debuggers(&cl_opts, &info)) {
		exit(-1);
	}
	DEBUG_INFO("BMP hosted %s\n for ST-Link V2/3, CMSIS_DAP, JLINK and "
			   "LIBFTDI/MPSSE\n", FIRMWARE_VERSION);
	DEBUG_INFO("Using %04x:%04x %s %s\n %s\n", info.vid, info.pid, info.serial,
		   info.manufacturer,
		   info.product);
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		if (serial_open(&cl_opts, info.serial))
			exit(-1);
		remote_init();
		break;
	case BMP_TYPE_STLINKV2:
		if (stlink_init( &info))
			exit(-1);
		break;
	case BMP_TYPE_CMSIS_DAP:
		if (dap_init( &info))
			exit(-1);
		break;
	case BMP_TYPE_LIBFTDI:
		if (ftdi_bmp_init(&cl_opts, &info))
			exit(-1);
		break;
	case BMP_TYPE_JLINK:
		if (jlink_init(&info))
			exit(-1);
		break;
	default:
		exit(-1);
	}
	int ret = -1;
	if (cl_opts.opt_mode != BMP_MODE_DEBUG) {
		ret = cl_execute(&cl_opts);
	} else {
		gdb_if_init();
		return;
	}
	exit(ret);
}

int platform_adiv5_swdp_scan(void)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
	case BMP_TYPE_LIBFTDI:
		return adiv5_swdp_scan();
		break;
	case BMP_TYPE_STLINKV2:
	{
		target_list_free();
		ADIv5_DP_t *dp = (void*)calloc(1, sizeof(*dp));
		if (stlink_enter_debug_swd(&info, dp)) {
			free(dp);
		} else {
			adiv5_dp_init(dp);
			if (target_list)
				return 1;
		}
		break;
	}
	case BMP_TYPE_CMSIS_DAP:
	{
		target_list_free();
		ADIv5_DP_t *dp = (void*)calloc(1, sizeof(*dp));
		if (dap_enter_debug_swd(dp)) {
			free(dp);
		} else {
			adiv5_dp_init(dp);
			if (target_list)
				return 1;
		}
		break;
	}
	case BMP_TYPE_JLINK:
		return jlink_swdp_scan(&info);
	default:
		return 0;
	}
	return 0;
}

int platform_swdptap_init(void)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		return remote_swdptap_init(&swd_proc);
	case BMP_TYPE_STLINKV2:
	case BMP_TYPE_CMSIS_DAP:
	case BMP_TYPE_JLINK:
		return 0;
	case BMP_TYPE_LIBFTDI:
		return libftdi_swdptap_init(&swd_proc);
	default:
		return -1;
	}
	return -1;
}

void platform_add_jtag_dev(int i, const jtag_dev_t *jtag_dev)
{
	if (info.bmp_type == BMP_TYPE_BMP)
		remote_add_jtag_dev(i, jtag_dev);
}

int platform_jtag_scan(const uint8_t *lrlens)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
	case BMP_TYPE_LIBFTDI:
	case BMP_TYPE_JLINK:
	case BMP_TYPE_CMSIS_DAP:
		return jtag_scan(lrlens);
	case BMP_TYPE_STLINKV2:
		return jtag_scan_stlinkv2(&info, lrlens);
	default:
		return -1;
	}
	return -1;
}

int platform_jtagtap_init(void)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		return remote_jtagtap_init(&jtag_proc);
	case BMP_TYPE_STLINKV2:
		return 0;
	case BMP_TYPE_LIBFTDI:
		return libftdi_jtagtap_init(&jtag_proc);
	case BMP_TYPE_JLINK:
		return jlink_jtagtap_init(&info, &jtag_proc);
	case BMP_TYPE_CMSIS_DAP:
		return cmsis_dap_jtagtap_init(&jtag_proc);
	default:
		return -1;
	}
	return -1;
}

void platform_adiv5_dp_defaults(ADIv5_DP_t *dp)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		if (cl_opts.opt_no_hl) {
			DEBUG_WARN("Not using HL commands\n");
			return;
		}
		return remote_adiv5_dp_defaults(dp);
	case BMP_TYPE_STLINKV2:
		return stlink_adiv5_dp_defaults(dp);
	case BMP_TYPE_CMSIS_DAP:
		return dap_adiv5_dp_defaults(dp);
	default:
		break;
	}
}

int platform_jtag_dp_init(ADIv5_DP_t *dp)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
	case BMP_TYPE_LIBFTDI:
	case BMP_TYPE_JLINK:
		return 0;
	case BMP_TYPE_STLINKV2:
		return stlink_jtag_dp_init(dp);
	case BMP_TYPE_CMSIS_DAP:
		return dap_jtag_dp_init(dp);
	default:
		return 0;
	}
	return 0;
}

char *platform_ident(void)
{
	switch (info.bmp_type) {
	  case BMP_TYPE_NONE:
		return "NONE";
	  case BMP_TYPE_BMP:
		return "BMP";
	  case BMP_TYPE_STLINKV2:
		return "STLINKV2";
	  case BMP_TYPE_LIBFTDI:
		return "LIBFTDI";
	  case BMP_TYPE_CMSIS_DAP:
		return "CMSIS_DAP";
	  case BMP_TYPE_JLINK:
		return "JLINK";
	}
	return NULL;
}

const char *platform_target_voltage(void)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		return remote_target_voltage();
	case BMP_TYPE_STLINKV2:
		return stlink_target_voltage(&info);
	case BMP_TYPE_LIBFTDI:
		return libftdi_target_voltage();
	case BMP_TYPE_JLINK:
		return jlink_target_voltage(&info);
	default:
		break;
	}
	return NULL;
}

void platform_srst_set_val(bool assert)
{
	switch (info.bmp_type) {
	case BMP_TYPE_STLINKV2:
		return stlink_srst_set_val(&info, assert);
	case BMP_TYPE_BMP:
		return remote_srst_set_val(assert);
	case BMP_TYPE_JLINK:
		return jlink_srst_set_val(&info, assert);
	default:
		break;
	}
}

bool platform_srst_get_val(void)
{
	switch (info.bmp_type) {
	case BMP_TYPE_BMP:
		return remote_srst_get_val();
	case BMP_TYPE_STLINKV2:
		return stlink_srst_get_val();
	case BMP_TYPE_JLINK:
		return jlink_srst_get_val(&info);
	default:
		break;
	}
	return false;
}

void platform_buffer_flush(void)
{
	switch (info.bmp_type) {
	case BMP_TYPE_LIBFTDI:
		return libftdi_buffer_flush();
	default:
		break;
	}
}

static void ap_decode_access(uint16_t addr, uint8_t RnW)
{
	if (RnW)
		fprintf(stderr, "Read  ");
	else
		fprintf(stderr, "Write ");
	if (addr < 0x100) {
		switch(addr) {
		case 0x00:
			if (RnW)
				fprintf(stderr, "DP_DPIDR :");
			else
				fprintf(stderr, "DP_ABORT :");
			break;
		case 0x04: fprintf(stderr, "CTRL/STAT:");
			break;
		case 0x08:
			if (RnW)
				fprintf(stderr, "RESEND   :");
			else
				fprintf(stderr, "DP_SELECT:");
			break;
		case 0x0c: fprintf(stderr, "DP_RDBUFF:");
			break;
		default: fprintf(stderr, "Unknown %02x   :", addr);
		}
	} else {
		fprintf(stderr, "AP 0x%02x ", addr >> 8);
		switch (addr & 0xff) {
		case 0x00: fprintf(stderr, "CSW   :");
			break;
		case 0x04: fprintf(stderr, "TAR   :");
			break;
		case 0x0c: fprintf(stderr, "DRW   :");
			break;
		case 0x10: fprintf(stderr, "DB0   :");
			break;
		case 0x14: fprintf(stderr, "DB1   :");
			break;
		case 0x18: fprintf(stderr, "DB2   :");
			break;
		case 0x1c: fprintf(stderr, "DB3   :");
			break;
		case 0xf8: fprintf(stderr, "BASE  :");
			break;
		case 0xf4: fprintf(stderr, "CFG   :");
			break;
		case 0xfc: fprintf(stderr, "IDR   :");
			break;
		default:   fprintf(stderr, "RSVD%02x:", addr & 0xff);
		}
	}
}

void adiv5_dp_write(ADIv5_DP_t *dp, uint16_t addr, uint32_t value)
{
	if (cl_debuglevel & BMP_DEBUG_TARGET) {
		ap_decode_access(addr, ADIV5_LOW_WRITE);
		fprintf(stderr, " 0x%08" PRIx32 "\n", value);
	}
	dp->low_access(dp, ADIV5_LOW_WRITE, addr, value);
}

uint32_t adiv5_dp_read(ADIv5_DP_t *dp, uint16_t addr)
{
	uint32_t ret = dp->dp_read(dp, addr);
	if (cl_debuglevel & BMP_DEBUG_TARGET) {
		ap_decode_access(addr, ADIV5_LOW_READ);
		fprintf(stderr, " 0x%08" PRIx32 "\n", ret);
	}
	return ret;
}

uint32_t adiv5_dp_error(ADIv5_DP_t *dp)
{
	uint32_t ret = dp->error(dp);
	DEBUG_TARGET( "DP Error 0x%08" PRIx32 "\n", ret);
	return ret;
}

uint32_t adiv5_dp_low_access(struct ADIv5_DP_s *dp, uint8_t RnW,
							 uint16_t addr, uint32_t value)
{
	uint32_t ret = dp->low_access(dp, RnW, addr, value);
	if (cl_debuglevel & BMP_DEBUG_TARGET) {
		ap_decode_access(addr, RnW);
		fprintf(stderr, " 0x%08" PRIx32 "\n", (RnW)? ret : value);
	}
	return ret;
}

uint32_t adiv5_ap_read(ADIv5_AP_t *ap, uint16_t addr)
{
	uint32_t ret = ap->dp->ap_read(ap, addr);
	if (cl_debuglevel & BMP_DEBUG_TARGET) {
		ap_decode_access(addr, ADIV5_LOW_READ);
		fprintf(stderr, " 0x%08" PRIx32 "\n", ret);
	}
	return ret;
}

void adiv5_ap_write(ADIv5_AP_t *ap, uint16_t addr, uint32_t value)
{
	if (cl_debuglevel & BMP_DEBUG_TARGET) {
		ap_decode_access(addr, ADIV5_LOW_WRITE);
		fprintf(stderr, " 0x%08" PRIx32 "\n", value);
	}
	return ap->dp->ap_write(ap, addr, value);
}

void adiv5_mem_read(ADIv5_AP_t *ap, void *dest, uint32_t src, size_t len)
{
	ap->dp->mem_read(ap, dest, src, len);
	if (cl_debuglevel & BMP_DEBUG_TARGET) {
		fprintf(stderr, "ap_memread @ %" PRIx32 " len %" PRIx32 ":",
				src, (uint32_t)len);
		uint8_t *p = (uint8_t *) dest;
		unsigned int i = len;
		if (i > 16)
			i = 16;
		while (i--)
			fprintf(stderr, " %02x", *p++);
		if (len > 16)
			fprintf(stderr, " ...");
		fprintf(stderr, "\n");
	}
	return;
}
void adiv5_mem_write_sized(	ADIv5_AP_t *ap, uint32_t dest, const void *src,
							size_t len, enum align align)
{
	if (cl_debuglevel & BMP_DEBUG_TARGET) {
		fprintf(stderr, "ap_mem_write_sized @ %" PRIx32 " len %" PRIx32
				", align %d:", dest, (uint32_t)len, 1 << align);
		uint8_t *p = (uint8_t *) src;
		unsigned int i = len;
		if (i > 16)
			i = 16;
		while (i--)
			fprintf(stderr, " %02x", *p++);
		if (len > 16)
			fprintf(stderr, " ...");
		fprintf(stderr, "\n");
	}
	return ap->dp->mem_write_sized(ap, dest, src, len, align);
}

void adiv5_dp_abort(struct ADIv5_DP_s *dp, uint32_t abort)
{
	DEBUG_TARGET("Abort: %08" PRIx32 "\n", abort);
	return dp->abort(dp, abort);
}
