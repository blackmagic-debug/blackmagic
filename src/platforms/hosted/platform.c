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
#include "stlinkv2.h"
#include "ftdi_bmp.h"
#include "jlink.h"
#include "cmsis_dap.h"
#include "version.h"

#define VENDOR_ID_STLINK         0x0483
#define PRODUCT_ID_STLINK_MASK   0xffe0
#define PRODUCT_ID_STLINK_GROUP  0x3740
#define PRODUCT_ID_STLINKV1      0x3744
#define PRODUCT_ID_STLINKV2      0x3748
#define PRODUCT_ID_STLINKV21     0x374b
#define PRODUCT_ID_STLINKV21_MSD 0x3752
#define PRODUCT_ID_STLINKV3      0x374f
#define PRODUCT_ID_STLINKV3E     0x374e

#define VENDOR_ID_SEGGER         0x1366

bmp_info_t info;

swd_proc_t swd_proc;
jtag_proc_t jtag_proc;

static void exit_function(void)
{
	if(info.usb_link) {
		libusb_free_transfer(info.usb_link->req_trans);
		libusb_free_transfer(info.usb_link->rep_trans);
		if (info.usb_link->ul_libusb_device_handle) {
			libusb_release_interface (
				info.usb_link->ul_libusb_device_handle, 0);
			libusb_close(info.usb_link->ul_libusb_device_handle);
		}
	}
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

static int find_debuggers(	BMP_CL_OPTIONS_t *cl_opts,bmp_info_t *info)
{
	libusb_device **devs;
	int n_devs = libusb_get_device_list(info->libusb_ctx, &devs);
    if (n_devs < 0) {
        DEBUG_WARN( "WARN:libusb_get_device_list() failed");
		return -1;
	}
	bool report = false;
	int found_debuggers;
	struct libusb_device_descriptor desc;
	char serial[64];
	char manufacturer[128];
	char product[128];
	bmp_type_t type = BMP_TYPE_NONE;
	bool access_problems = false;
	char *active_cable = NULL;
	bool ftdi_unknown = false;
  rescan:
	found_debuggers = 0;
	for (int i = 0;  devs[i]; i++) {
		libusb_device *dev =  devs[i];
		int res = libusb_get_device_descriptor(dev, &desc);
		if (res < 0) {
            DEBUG_WARN( "WARN: libusb_get_device_descriptor() failed: %s",
					libusb_strerror(res));
			libusb_free_device_list(devs, 1);
			continue;
		}
		/* Exclude hubs from testing. Probably more classes could be excluded here!*/
		if (desc.bDeviceClass == LIBUSB_CLASS_HUB) {
			continue;
		}
		libusb_device_handle *handle;
		res = libusb_open(dev, &handle);
		if (res != LIBUSB_SUCCESS) {
			if (!access_problems) {
				DEBUG_INFO("INFO: Open USB %04x:%04x class %2x failed\n",
						   desc.idVendor, desc.idProduct, desc.bDeviceClass);
				access_problems = true;
			}
			continue;
		}
		res = libusb_get_string_descriptor_ascii(
			handle, desc.iSerialNumber, (uint8_t*)serial,
			sizeof(serial));
		if (res <= 0) {
			/* This can fail for many devices. Continue silent!*/
			libusb_close(handle);
			continue;
		}
		if (cl_opts->opt_serial && !strstr(serial, cl_opts->opt_serial)) {
			libusb_close(handle);
			continue;
		}
		res = libusb_get_string_descriptor_ascii(
			handle, desc.iManufacturer, (uint8_t*)manufacturer,
			sizeof(manufacturer));
		if (res > 0) {
			res = libusb_get_string_descriptor_ascii(
				handle, desc.iProduct, (uint8_t*)product,
				sizeof(product));
			if (res <= 0) {
				DEBUG_WARN( "WARN:"
						"libusb_get_string_descriptor_ascii "
						"for ident_string failed: %s\n",
						libusb_strerror(res));
				libusb_close(handle);
				continue;
			}
		}
		libusb_close(handle);
		if (cl_opts->opt_ident_string) {
			char *match_manu = NULL;
			char *match_product = NULL;
			match_manu = strstr(manufacturer,	cl_opts->opt_ident_string);
			match_product = strstr(product, cl_opts->opt_ident_string);
			if (!match_manu && !match_product) {
				continue;
			}
		}
		/* Either serial and/or ident_string match or are not given.
		 * Check type.*/
		if (desc.idVendor == VENDOR_ID_BMP) {
			if (desc.idProduct == PRODUCT_ID_BMP) {
				type = BMP_TYPE_BMP;
			} else if (desc.idProduct == PRODUCT_ID_BMP_BL) {
				DEBUG_WARN("BMP in botloader mode found. Restart or reflash!\n");
				continue;
			}
		} else if ((strstr(manufacturer, "CMSIS")) || (strstr(product, "CMSIS"))) {
			type = BMP_TYPE_CMSIS_DAP;
		} else if (desc.idVendor ==  VENDOR_ID_STLINK) {
			if ((desc.idProduct == PRODUCT_ID_STLINKV2) ||
				(desc.idProduct == PRODUCT_ID_STLINKV21) ||
				(desc.idProduct == PRODUCT_ID_STLINKV21_MSD) ||
				(desc.idProduct == PRODUCT_ID_STLINKV3) ||
				(desc.idProduct == PRODUCT_ID_STLINKV3E)) {
				type = BMP_TYPE_STLINKV2;
			} else {
				if (desc.idProduct == PRODUCT_ID_STLINKV1)
					DEBUG_WARN( "INFO: STLINKV1 not supported\n");
				continue;
			}
		} else if (desc.idVendor ==  VENDOR_ID_SEGGER) {
			type = BMP_TYPE_JLINK;
		} else {
			cable_desc_t *cable = &cable_desc[0];
			for (; cable->name; cable++) {
				bool found = false;
				if ((cable->vendor != desc.idVendor) || (cable->product != desc.idProduct))
					continue; /* VID/PID do not match*/
				if (cl_opts->opt_cable) {
					if (strcmp(cable->name, cl_opts->opt_cable))
						continue; /* cable names do not match*/
					else
						found = true;
				}
				if (cable->description) {
					if (strcmp(cable->description, product))
						continue; /* discriptions do not match*/
					else
						found = true;
				} else { /* VID/PID fits, but no cl_opts->opt_cable and no description*/
					if ((cable->vendor == 0x0403) && /* FTDI*/
						((cable->product == 0x6010) || /* FT2232C/D/H*/
						 (cable->product == 0x6011) || /* FT4232H Quad HS USB-UART/FIFO IC */
						 (cable->product == 0x6014))) {  /* FT232H Single HS USB-UART/FIFO IC */
						ftdi_unknown = true;
						continue; /* Cable name is needed */
					}
				}
				if (found) {
					active_cable = cable->name;
					type = BMP_TYPE_LIBFTDI;
					break;
				}
			}
			if (!cable->name)
				continue;
		}
		if (report) {
			DEBUG_WARN("%2d: %s, %s, %s\n", found_debuggers + 1,
				   serial,
				   manufacturer,product);
		}
		info->vid = desc.idVendor;
		info->pid = desc.idProduct;
		info->bmp_type = type;
		strncpy(info->serial, serial, sizeof(info->serial));
		strncpy(info->product, product, sizeof(info->product));
		strncpy(info->manufacturer, manufacturer, sizeof(info->manufacturer));
		if (cl_opts->opt_position &&
			(cl_opts->opt_position == (found_debuggers + 1))) {
			found_debuggers = 1;
			break;
		} else {
			found_debuggers++;
		}
	}
	if ((found_debuggers == 0) && ftdi_unknown)
		DEBUG_WARN("Generic FTDI MPSSE VID/PID found. Please specify exact type with \"-c <cable>\" !\n");
	if ((found_debuggers == 1) && !cl_opts->opt_cable && (type == BMP_TYPE_LIBFTDI))
		cl_opts->opt_cable = active_cable;
	if (!found_debuggers && cl_opts->opt_list_only)
		DEBUG_WARN("No usable debugger found\n");
	if ((found_debuggers > 1) ||
		((found_debuggers == 1) && (cl_opts->opt_list_only))) {
		if (!report) {
			if (found_debuggers > 1)
				DEBUG_WARN("%d debuggers found!\nSelect with -P <pos>, "
						   "-s <(partial)serial no.> "
						   "and/or -S <(partial)description>\n",
						   found_debuggers);
			report = true;
			goto rescan;
		} else {
			if (found_debuggers > 0)
				access_problems = false;
			found_debuggers = 0;
		}
	}
	if (!found_debuggers && access_problems)
		DEBUG_WARN(
			"No debugger found. Please check access rights to USB devices!\n");
	libusb_free_device_list(devs, 1);
	return (found_debuggers == 1) ? 0 : -1;
}

static	BMP_CL_OPTIONS_t cl_opts;

void platform_init(int argc, char **argv)
{
	cl_opts.opt_idstring = "Blackmagic PC-Hosted";
	cl_init(&cl_opts, argc, argv);
	atexit(exit_function);
	signal(SIGTERM, sigterm_handler);
	signal(SIGINT, sigterm_handler);
	int res = libusb_init(&info.libusb_ctx);
	if (res) {
		DEBUG_WARN( "Fatal: Failed to get USB context: %s\n",
				libusb_strerror(res));
		exit(-1);
	}
	if (cl_opts.opt_device) {
		info.bmp_type = BMP_TYPE_BMP;
	} else if (cl_opts.opt_cable) {
		if ((!strcmp(cl_opts.opt_cable, "list")) ||
			(!strcmp(cl_opts.opt_cable, "l"))) {
			cable_desc_t *cable = &cable_desc[0];
			DEBUG_WARN("Available cables:\n");
			for (; cable->name; cable++) {
				DEBUG_WARN("\t%s\n", cable->name);
			}
			exit(0);
		}
		info.bmp_type = BMP_TYPE_LIBFTDI;
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
