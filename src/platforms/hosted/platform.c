/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020
 * Written by Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
#include "timing.h"
#include "cl_utils.h"
#include <signal.h>

#define VENDOR_ID_BMP            0x1d50
#define PRODUCT_ID_BMP           0x6018

#define VENDOR_ID_STLINK         0x0483
#define PRODUCT_ID_STLINK_MASK   0xffe0
#define PRODUCT_ID_STLINK_GROUP  0x3740
#define PRODUCT_ID_STLINKV1      0x3744
#define PRODUCT_ID_STLINKV2      0x3748
#define PRODUCT_ID_STLINKV21     0x374b
#define PRODUCT_ID_STLINKV21_MSD 0x3752
#define PRODUCT_ID_STLINKV3      0x374f
#define PRODUCT_ID_STLINKV3E     0x374e

bmp_info_t info;

swd_proc_t swd_proc;
jtag_proc_t jtag_proc;

static void exit_function(void)
{
	libusb_exit(info.libusb_ctx);
	fprintf(stderr, "INFO: Cleanup\n");
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
        fprintf(stderr, "WARN:libusb_get_device_list() failed");
		return -1;
	}
	bool report = false;
	int found_debuggers;
	struct libusb_device_descriptor desc;
	char serial[64];
	char manufacturer[128];
	char product[128];
	bmp_type_t type = BMP_TYPE_NONE;
  rescan:
	found_debuggers = 0;
	for (int i = 0;  devs[i]; i++) {
		libusb_device *dev =  devs[i];
		int res = libusb_get_device_descriptor(dev, &desc);
		if (res < 0) {
            fprintf(stderr, "WARN: libusb_get_device_descriptor() failed: %s",
					libusb_strerror(res));
			libusb_free_device_list(devs, 0);
			continue;
		}
		libusb_device_handle *handle;
		res = libusb_open(dev, &handle);
		if (res != LIBUSB_SUCCESS) {
			fprintf(stderr,"WARN: Open failed\n");
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
				fprintf(stderr, "WARN:"
						"libusb_get_string_descriptor_ascii "
						"for ident_string failed: %s\n",
						libusb_strerror(res));
				libusb_close(handle);
				continue;
			}
		}
		if (cl_opts->opt_ident_string) {
			char *match_manu = NULL;
			char *match_product = NULL;
			match_manu = strstr(manufacturer,	cl_opts->opt_ident_string);
			match_product = strstr(product, cl_opts->opt_ident_string);
			if (!match_manu && !match_product) {
				libusb_close(handle);
				continue;
			}
		}
		/* Either serial and/or ident_string match or are not given.
		 * Check type.*/
		if ((desc.idVendor == VENDOR_ID_BMP) &&
			(desc.idProduct == PRODUCT_ID_BMP)) {
			type = BMP_TYPE_BMP;
		} else if (desc.idVendor ==  VENDOR_ID_STLINK) {
			if (desc.idProduct == PRODUCT_ID_STLINKV1) {
				fprintf(stderr, "INFO: STLINKV1 not supported\n");
				continue;
			}
			if ((desc.idProduct == PRODUCT_ID_STLINKV2) ||
				(desc.idProduct == PRODUCT_ID_STLINKV21) ||
				(desc.idProduct == PRODUCT_ID_STLINKV21_MSD) ||
				(desc.idProduct == PRODUCT_ID_STLINKV3) ||
				(desc.idProduct == PRODUCT_ID_STLINKV3E)) {
				type = BMP_TYPE_STLINKV2;
			}
		} else {
			continue;
		}
		found_debuggers ++;
		if (report) {
			printf("%2d: %s, %s, %s\n", found_debuggers,
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
			(cl_opts->opt_position == found_debuggers)) {
			found_debuggers = 1;
			break;
		}
	}
	if (found_debuggers > 1) {
		if (!report) {
			printf("%d debuggers found! Select with -P <num>, -s <string> "
				   "and/or -S <string>\n",
				   found_debuggers);
			report = true;
			goto rescan;
		}
	}
	libusb_free_device_list(devs, 0);
	return 0;
}

void platform_init(int argc, char **argv)
{
	BMP_CL_OPTIONS_t cl_opts = {0};
	cl_opts.opt_idstring = "Blackmagic PC-Hosted";
	cl_init(&cl_opts, argc, argv);
	atexit(exit_function);
	signal(SIGTERM, sigterm_handler);
	signal(SIGINT, sigterm_handler);
	int res = libusb_init(&info.libusb_ctx);
	if (res) {
		fprintf(stderr, "Fatal: Failed to get USB context: %s\n",
				libusb_strerror(res));
		exit(-1);
	}
	if (find_debuggers(&cl_opts, &info)) {
		exit(-1);
	}
	printf("Using %s %s %s\n", info.serial,
		   info.manufacturer,
		   info.product);
	exit(-1);
}

int platform_adiv5_swdp_scan(void)
{
	return -1;
}

int platform_swdptap_init(void)
{
	return -1;
}

int platform_jtag_scan(const uint8_t *lrlens)
{
	(void) lrlens;
	return -1;
}

int platform_jtagtap_init(void)
{
	return 0;
}

int platform_adiv5_dp_defaults(void *arg)
{
	(void)arg;
	return -1;
}

int platform_jtag_dp_init()
{
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
	return NULL;
}

void platform_srst_set_val(bool assert) {(void) assert;}

bool platform_srst_get_val(void) { return false;}
void platform_buffer_flush(void) {}
