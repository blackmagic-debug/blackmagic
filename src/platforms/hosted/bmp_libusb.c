/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright(C) 2020 - 2022 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
#include <libusb.h>
#include "cli.h"
#include "ftdi_bmp.h"
#include "version.h"

#define NO_SERIAL_NUMBER "<no serial number>"

void bmp_ident(bmp_info_s *info)
{
	DEBUG_INFO("Black Magic Debug App %s\n for Black Magic Probe, ST-Link v2 and v3, CMSIS-DAP, "
			   "J-Link and FTDI (MPSSE)\n",
		FIRMWARE_VERSION);
	if (info && info->vid && info->pid) {
		DEBUG_INFO("Using %04x:%04x %s %s\n %s\n", info->vid, info->pid,
			(info->serial[0]) ? info->serial : NO_SERIAL_NUMBER, info->manufacturer, info->product);
	}
}

void libusb_exit_function(bmp_info_s *info)
{
	if (!info->usb_link)
		return;
	if (info->usb_link->device_handle) {
		libusb_release_interface(info->usb_link->device_handle, 0);
		libusb_close(info->usb_link->device_handle);
	}
}

static bmp_type_t find_cmsis_dap_interface(libusb_device *dev, bmp_info_s *info)
{
	bmp_type_t type = BMP_TYPE_NONE;

	libusb_config_descriptor_s *conf;
	char interface_string[128];

	int res = libusb_get_active_config_descriptor(dev, &conf);
	if (res < 0) {
		DEBUG_ERROR("libusb_get_active_config_descriptor() failed: %s", libusb_strerror(res));
		return type;
	}

	libusb_device_handle *handle;
	res = libusb_open(dev, &handle);
	if (res != LIBUSB_SUCCESS) {
		DEBUG_INFO("libusb_open() failed: %s\n", libusb_strerror(res));
		libusb_free_config_descriptor(conf);
		return type;
	}

	for (uint8_t i = 0; i < conf->bNumInterfaces; ++i) {
		const libusb_interface_descriptor_s *interface = &conf->interface[i].altsetting[0];

		if (!interface->iInterface)
			continue;

		res = libusb_get_string_descriptor_ascii(
			handle, interface->iInterface, (uint8_t *)interface_string, sizeof(interface_string));
		if (res < 0) {
			DEBUG_ERROR("libusb_get_string_descriptor_ascii() failed: %s\n", libusb_strerror(res));
			continue;
		}

		if (!strstr(interface_string, "CMSIS"))
			continue;
		type = BMP_TYPE_CMSIS_DAP;

		if (interface->bInterfaceClass == 0xffU && interface->bNumEndpoints == 2U) {
			info->interface_num = interface->bInterfaceNumber;

			for (uint8_t j = 0; j < interface->bNumEndpoints; ++j) {
				const uint8_t n = interface->endpoint[j].bEndpointAddress;
				if (n & 0x80U)
					info->in_ep = n;
				else
					info->out_ep = n;
			}

			/* V2 is preferred, return early. */
			break;
		}
	}
	libusb_free_config_descriptor(conf);
	return type;
}

int find_debuggers(bmda_cli_options_s *cl_opts, bmp_info_s *info)
{
	libusb_device **devs;
	int res = libusb_init(&info->libusb_ctx);
	if (res) {
		DEBUG_ERROR("Failed to get USB context: %s\n", libusb_strerror(res));
		exit(-1);
	}
	if (cl_opts->opt_cable) {
		if (!strcmp(cl_opts->opt_cable, "list") || !strcmp(cl_opts->opt_cable, "l")) {
			const cable_desc_s *cable = cable_desc;
			DEBUG_WARN("Available cables:\n");
			for (; cable->name; ++cable)
				DEBUG_WARN("\t%s%c\n", cable->name, cable->description ? ' ' : '*');

			DEBUG_WARN("*: No auto-detection possible! Give cable name as argument!\n");
			exit(0);
		}
		info->bmp_type = BMP_TYPE_FTDI;
	}
	ssize_t n_devs = libusb_get_device_list(info->libusb_ctx, &devs);
	if (n_devs < 0) {
		DEBUG_ERROR("libusb_get_device_list() failed");
		return -1;
	}
	bool report = false;
	size_t found_debuggers;
	struct libusb_device_descriptor desc;
	char serial[64];
	char manufacturer[128];
	char product[128];
	bool access_problems = false;
	char *active_cable = NULL;
	bool ftdi_unknown = false;
rescan:
	found_debuggers = 0;
	serial[0] = 0;
	manufacturer[0] = 0;
	product[0] = 0;
	access_problems = false;
	active_cable = NULL;
	ftdi_unknown = false;
	for (size_t i = 0; devs[i]; ++i) {
		libusb_device *dev = devs[i];
		int res = libusb_get_device_descriptor(dev, &desc);
		if (res < 0) {
			DEBUG_ERROR("libusb_get_device_descriptor() failed: %s", libusb_strerror(res));
			libusb_free_device_list(devs, 1);
			continue;
		}
		/* Exclude hubs from testing. Probably more classes could be excluded here!*/
		switch (desc.bDeviceClass) {
		case LIBUSB_CLASS_HUB:
		case LIBUSB_CLASS_WIRELESS:
			continue;
		}
		libusb_device_handle *handle = NULL;
		res = libusb_open(dev, &handle);
		if (res != LIBUSB_SUCCESS) {
			if (!access_problems) {
				DEBUG_INFO("Open USB %04x:%04x class %2x failed\n", desc.idVendor, desc.idProduct, desc.bDeviceClass);
				access_problems = true;
			}
			continue;
		}
		/* If the device even has a serial number string, fetch it */
		if (desc.iSerialNumber) {
			res = libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, (uint8_t *)serial, sizeof(serial));
			/* If the call fails and it's not because the device gave us STALL, continue to the next one */
			if (res < 0 && res != LIBUSB_ERROR_PIPE) {
				libusb_close(handle);
				continue;
			}
			/* Device has no serial and that's ok. */
			if (res <= 0)
				serial[0] = '\0';
		} else
			serial[0] = '\0';
		if (cl_opts->opt_serial && !strstr(serial, cl_opts->opt_serial)) {
			libusb_close(handle);
			continue;
		}
		/* Attempt to get the manufacturer string */
		if (desc.iManufacturer) {
			res = libusb_get_string_descriptor_ascii(
				handle, desc.iManufacturer, (uint8_t *)manufacturer, sizeof(manufacturer));
			/* If the call fails and it's not because the device gave us STALL, continue to the next one */
			if (res < 0 && res != LIBUSB_ERROR_PIPE) {
				DEBUG_ERROR("libusb_get_string_descriptor_ascii() call to fetch manufacturer string failed: %s\n",
					libusb_strerror(res));
				libusb_close(handle);
				continue;
			}
			/* Device has no manufacturer string and that's ok. */
			if (res <= 0)
				manufacturer[0] = '\0';
		} else
			manufacturer[0] = '\0';
		/* Attempt to get the product string */
		if (desc.iProduct) {
			res = libusb_get_string_descriptor_ascii(handle, desc.iProduct, (uint8_t *)product, sizeof(product));
			/* If the call fails and it's not because the device gave us STALL, continue to the next one */
			if (res < 0 && res != LIBUSB_ERROR_PIPE) {
				DEBUG_ERROR("libusb_get_string_descriptor_ascii() call to fetch product string failed: %s\n",
					libusb_strerror(res));
				libusb_close(handle);
				continue;
			}
			/* Device has no product string and that's ok. */
			if (res <= 0)
				product[0] = '\0';
		} else
			product[0] = '\0';
		libusb_close(handle);
		if (cl_opts->opt_ident_string) {
			char *match_manu = NULL;
			char *match_product = NULL;
			match_manu = strstr(manufacturer, cl_opts->opt_ident_string);
			match_product = strstr(product, cl_opts->opt_ident_string);
			if (!match_manu && !match_product)
				continue;
		}
		/* Either serial and/or ident_string match or are not given.
		 * Check type.*/
		bmp_type_t type = BMP_TYPE_NONE;
		if (desc.idVendor == VENDOR_ID_BMP) {
			if (desc.idProduct == PRODUCT_ID_BMP)
				type = BMP_TYPE_BMP;
			else {
				if (desc.idProduct == PRODUCT_ID_BMP_BL)
					DEBUG_WARN("BMP in bootloader mode found. Restart or reflash!\n");
				continue;
			}
		} else if (find_cmsis_dap_interface(dev, info) == BMP_TYPE_CMSIS_DAP || strstr(manufacturer, "CMSIS") ||
			strstr(product, "CMSIS"))
			type = BMP_TYPE_CMSIS_DAP;
		else if (desc.idVendor == VENDOR_ID_STLINK) {
			switch (desc.idProduct) {
			case PRODUCT_ID_STLINKV2:
			case PRODUCT_ID_STLINKV21:
			case PRODUCT_ID_STLINKV21_MSD:
			case PRODUCT_ID_STLINKV3_NO_MSD:
			case PRODUCT_ID_STLINKV3_BL:
			case PRODUCT_ID_STLINKV3:
			case PRODUCT_ID_STLINKV3E:
				type = BMP_TYPE_STLINK_V2;
				break;
			case PRODUCT_ID_STLINKV1:
				DEBUG_WARN("STLINKV1 not supported\n");
			default:
				continue;
			}
		} else if (desc.idVendor == VENDOR_ID_SEGGER)
			type = BMP_TYPE_JLINK;
		else {
			const cable_desc_s *cable = cable_desc;
			for (; cable->name; ++cable) {
				bool found = false;
				if (cable->vendor != desc.idVendor || cable->product != desc.idProduct)
					continue; /* VID/PID do not match*/
				if (cl_opts->opt_cable) {
					if (strncmp(cable->name, cl_opts->opt_cable, strlen(cable->name)) != 0)
						continue; /* cable names do not match*/
					found = true;
				}
				if (cable->description) {
					if (strncmp(cable->description, product, strlen(cable->description)) != 0)
						continue; /* descriptions do not match*/
					found = true;
				} else {                                 /* VID/PID fits, but no cl_opts->opt_cable and no description*/
					if (cable->vendor == 0x0403 &&       /* FTDI*/
						(cable->product == 0x6010 ||     /* FT2232C/D/H*/
							cable->product == 0x6011 ||  /* FT4232H Quad HS USB-UART/FIFO IC */
							cable->product == 0x6014)) { /* FT232H Single HS USB-UART/FIFO IC */
						ftdi_unknown = true;
						continue; /* Cable name is needed */
					}
				}
				if (found) {
					active_cable = cable->name;
					type = BMP_TYPE_FTDI;
					break;
				}
			}
			if (!cable->name)
				continue;
		}

		if (report)
			DEBUG_WARN("%2zu: %s, %s, %s\n", found_debuggers + 1U, serial[0] ? serial : NO_SERIAL_NUMBER, manufacturer,
				product);

		info->libusb_dev = dev;
		info->vid = desc.idVendor;
		info->pid = desc.idProduct;
		info->bmp_type = type;
		strncpy(info->serial, serial, sizeof(info->serial));
		strncpy(info->product, product, sizeof(info->product));
		strncpy(info->manufacturer, manufacturer, sizeof(info->manufacturer));
		if (cl_opts->opt_position && cl_opts->opt_position == found_debuggers + 1U) {
			found_debuggers = 1;
			break;
		}
		++found_debuggers;
	}
	if (found_debuggers == 0 && ftdi_unknown && !cl_opts->opt_cable)
		DEBUG_WARN("Generic FTDI MPSSE VID/PID found. Please specify exact type with \"-c <cable>\" !\n");
	if (found_debuggers == 1U && !cl_opts->opt_cable && info->bmp_type == BMP_TYPE_FTDI)
		cl_opts->opt_cable = active_cable;
	if (!found_debuggers && cl_opts->opt_list_only)
		DEBUG_ERROR("No usable debugger found\n");
	if (found_debuggers > 1U || (found_debuggers == 1U && cl_opts->opt_list_only)) {
		if (!report) {
			if (found_debuggers > 1U)
				DEBUG_WARN("%zu debuggers found!\nSelect with -P <pos> or -s <(partial)serial no.>\n", found_debuggers);
			report = true;
			goto rescan;
		}
		if (found_debuggers > 0)
			access_problems = false;
		found_debuggers = 0;
	}
	if (!found_debuggers && access_problems)
		DEBUG_ERROR("No debugger found. Please check access rights to USB devices!\n");
	if (info->libusb_dev)
		libusb_ref_device(info->libusb_dev);
	libusb_free_device_list(devs, 1);
	return found_debuggers == 1U ? 0 : -1;
}

/*
 * Transfer data back and forth with the debug adaptor.
 *
 * If tx_len is non-zero, then send the data in tx_buffer to the adaptor.
 * If rx_len is non-zero, then receive data from the adaptor into rx_buffer.
 * The result is either the number of bytes received, or a libusb error code indicating what went wrong
 *
 * NB: The lengths represent the maximum number of expected bytes and the actual amount
 *   sent/received may be less (per libusb's documentation). If used, rx_buffer must be
 *   suitably intialised up front to avoid UB reads when accessed.
 */
int bmda_usb_transfer(usb_link_s *link, const void *tx_buffer, size_t tx_len, void *rx_buffer, size_t rx_len)
{
	/* If there's data to send */
	if (tx_len) {
		uint8_t *tx_data = (uint8_t *)tx_buffer;
		/* Display the request */
		DEBUG_WIRE(" request:");
		for (size_t i = 0; i < tx_len && i < 32U; ++i)
			DEBUG_WIRE(" %02x", tx_data[i]);
		if (tx_len > 32U)
			DEBUG_WIRE(" ...");
		DEBUG_WIRE("\n");

		/* Perform the transfer */
		const int result =
			libusb_bulk_transfer(link->device_handle, link->ep_tx | LIBUSB_ENDPOINT_OUT, tx_data, (int)tx_len, NULL, 0);
		/* Then decode the result value - if its anything other than LIBUSB_SUCCESS, something went horribly wrong */
		if (result != LIBUSB_SUCCESS) {
			DEBUG_ERROR(
				"%s: Sending request to adaptor failed (%d): %s\n", __func__, result, libusb_error_name(result));
			if (result == LIBUSB_ERROR_PIPE)
				libusb_clear_halt(link->device_handle, link->ep_tx | LIBUSB_ENDPOINT_OUT);
			return result;
		}
	}
	/* If there's data to receive */
	if (rx_len) {
		uint8_t *rx_data = (uint8_t *)rx_buffer;
		int rx_bytes = 0;
		/* Perform the transfer */
		const int result = libusb_bulk_transfer(
			link->device_handle, link->ep_rx | LIBUSB_ENDPOINT_IN, rx_data, (int)rx_len, &rx_bytes, 0);
		/* Then decode the result value - if its anything other than LIBUSB_SUCCESS, something went horribly wrong */
		if (result != LIBUSB_SUCCESS) {
			DEBUG_ERROR(
				"%s: Receiving response from adaptor failed (%d): %s\n", __func__, result, libusb_error_name(result));
			if (result == LIBUSB_ERROR_PIPE)
				libusb_clear_halt(link->device_handle, link->ep_rx | LIBUSB_ENDPOINT_IN);
			return result;
		}

		/* Display the response */
		DEBUG_WIRE("response:");
		for (size_t i = 0; i < (size_t)rx_bytes && i < 32U; ++i)
			DEBUG_WIRE(" %02x", rx_data[i]);
		if (rx_bytes > 32)
			DEBUG_WIRE(" ...");
		DEBUG_WIRE("\n");
		return rx_bytes;
	}
	return LIBUSB_SUCCESS;
}
