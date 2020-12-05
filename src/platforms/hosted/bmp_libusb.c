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

/* Find all known usb connected debuggers */
#include "general.h"
#include "libusb-1.0/libusb.h"
#include "cl_utils.h"
#include "ftdi_bmp.h"

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

void libusb_exit_function(bmp_info_t *info)
{
	if (!info->usb_link)
		return;
	libusb_free_transfer(info->usb_link->req_trans);
	libusb_free_transfer(info->usb_link->rep_trans);
	if (info->usb_link->ul_libusb_device_handle) {
		libusb_release_interface (
			info->usb_link->ul_libusb_device_handle, 0);
		libusb_close(info->usb_link->ul_libusb_device_handle);
	}
}

int find_debuggers(BMP_CL_OPTIONS_t *cl_opts,bmp_info_t *info)
{
	libusb_device **devs;
	int res = libusb_init(&info->libusb_ctx);
	if (res) {
		DEBUG_WARN( "Fatal: Failed to get USB context: %s\n",
				libusb_strerror(res));
		exit(-1);
	}
	res = libusb_init(&info->libusb_ctx);
	if (res) {
		DEBUG_WARN( "Fatal: Failed to get USB context: %s\n",
				libusb_strerror(res));
		exit(-1);
	}
	if (cl_opts->opt_cable) {
		if ((!strcmp(cl_opts->opt_cable, "list")) ||
			(!strcmp(cl_opts->opt_cable, "l"))) {
			cable_desc_t *cable = &cable_desc[0];
			DEBUG_WARN("Available cables:\n");
			for (; cable->name; cable++) {
				DEBUG_WARN("\t%s\n", cable->name);
			}
			exit(0);
		}
		info->bmp_type = BMP_TYPE_LIBFTDI;
	}
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
static void LIBUSB_CALL on_trans_done(struct libusb_transfer *trans)
{
    struct trans_ctx * const ctx = trans->user_data;

    if (trans->status != LIBUSB_TRANSFER_COMPLETED)
    {
		DEBUG_WARN("on_trans_done: ");
        if(trans->status == LIBUSB_TRANSFER_TIMED_OUT)  {
            DEBUG_WARN(" Timeout\n");
        } else if (trans->status == LIBUSB_TRANSFER_CANCELLED) {
            DEBUG_WARN(" cancelled\n");
        } else if (trans->status == LIBUSB_TRANSFER_NO_DEVICE) {
            DEBUG_WARN(" no device\n");
        } else {
            DEBUG_WARN(" unknown\n");
		}
        ctx->flags |= TRANS_FLAGS_HAS_ERROR;
    }
    ctx->flags |= TRANS_FLAGS_IS_DONE;
}

static int submit_wait(usb_link_t *link, struct libusb_transfer *trans) {
	struct trans_ctx trans_ctx;
	enum libusb_error error;

	trans_ctx.flags = 0;

	/* brief intrusion inside the libusb interface */
	trans->callback = on_trans_done;
	trans->user_data = &trans_ctx;

	if ((error = libusb_submit_transfer(trans))) {
		DEBUG_WARN("libusb_submit_transfer(%d): %s\n", error,
			  libusb_strerror(error));
		exit(-1);
	}

	uint32_t start_time = platform_time_ms();
	while (trans_ctx.flags == 0) {
		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		if (libusb_handle_events_timeout(link->ul_libusb_ctx, &timeout)) {
			DEBUG_WARN("libusb_handle_events()\n");
			return -1;
		}
		uint32_t now = platform_time_ms();
		if (now - start_time > 1000) {
			libusb_cancel_transfer(trans);
			DEBUG_WARN("libusb_handle_events() timeout\n");
			return -1;
		}
	}
	if (trans_ctx.flags & TRANS_FLAGS_HAS_ERROR) {
		DEBUG_WARN("libusb_handle_events() | has_error\n");
		return -1;
	}

	return 0;
}

/* One USB transaction */
int send_recv(usb_link_t *link,
					 uint8_t *txbuf, size_t txsize,
					 uint8_t *rxbuf, size_t rxsize)
{
	int res = 0;
	if( txsize) {
		int txlen = txsize;
		libusb_fill_bulk_transfer(link->req_trans,
								  link->ul_libusb_device_handle,
								  link->ep_tx | LIBUSB_ENDPOINT_OUT,
								  txbuf, txlen,
								  NULL, NULL, 0);
		int i = 0;
		DEBUG_WIRE(" Send (%3d): ", txlen);
		for (; i < txlen; i++) {
			DEBUG_WIRE("%02x", txbuf[i]);
			if ((i & 7) == 7)
				DEBUG_WIRE(".");
			if ((i & 31) == 31)
				DEBUG_WIRE("\n             ");
		}
		if (!(i & 31))
			DEBUG_WIRE("\n");
		if (submit_wait(link, link->req_trans)) {
			libusb_clear_halt(link->ul_libusb_device_handle, link->ep_tx);
			return -1;
		}
	}
	/* send_only */
	if (rxsize != 0) {
		/* read the response */
		libusb_fill_bulk_transfer(link->rep_trans, link->ul_libusb_device_handle,
								  link->ep_rx | LIBUSB_ENDPOINT_IN,
								  rxbuf, rxsize, NULL, NULL, 0);

		if (submit_wait(link, link->rep_trans)) {
			DEBUG_WARN("clear 1\n");
			libusb_clear_halt(link->ul_libusb_device_handle, link->ep_rx);
			return -1;
		}
		res = link->rep_trans->actual_length;
		if (res >0) {
			int i;
			uint8_t *p = rxbuf;
			DEBUG_WIRE(" Rec (%zu/%d)", rxsize, res);
			for (i = 0; i < res && i < 32 ; i++) {
				if ( i && ((i & 7) == 0))
					DEBUG_WIRE(".");
				DEBUG_WIRE("%02x", p[i]);
			}
		}
	}
	DEBUG_WIRE("\n");
	return res;
}
