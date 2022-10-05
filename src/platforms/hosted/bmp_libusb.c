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
#if defined(_WIN32) || defined(__CYGWIN__)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#include "ftd2xx.h"
#else
#include <libusb.h>
#include <ftdi.h>
#endif
#include "cli.h"
#include "ftdi_bmp.h"
#include "version.h"
#include "probe_info.h"

#define NO_SERIAL_NUMBER "<no serial number>"

typedef struct debugger_device {
	uint16_t vendor;
	uint16_t product;
	bmp_type_t type;
	char *type_string;
} debugger_device_s;

/* Create the list of debuggers BMDA works with */
debugger_device_s debugger_devices[] = {
	{VENDOR_ID_BMP, PRODUCT_ID_BMP, BMP_TYPE_BMP, "Black Magic Probe"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV2, BMP_TYPE_STLINK_V2, "ST-Link v2"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV21, BMP_TYPE_STLINK_V2, "ST-Link v2.1"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV21_MSD, BMP_TYPE_STLINK_V2, "ST-Link v2.1 MSD"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV3_NO_MSD, BMP_TYPE_STLINK_V2, "ST-Link v2.1 No MSD"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV3, BMP_TYPE_STLINK_V2, "ST-Link v3"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV3E, BMP_TYPE_STLINK_V2, "ST-Link v3E"},
	{VENDOR_ID_SEGGER, PRODUCT_ID_UNKNOWN, BMP_TYPE_JLINK, "Segger JLink"},
	{VENDOR_ID_FTDI, PRODUCT_ID_FTDI_FT2232, BMP_TYPE_FTDI, "FTDI FT2232"},
	{VENDOR_ID_FTDI, PRODUCT_ID_FTDI_FT4232, BMP_TYPE_FTDI, "FTDI FT4232"},
	{VENDOR_ID_FTDI, PRODUCT_ID_FTDI_FT232, BMP_TYPE_FTDI, "FTDI FT232"},
	{0, 0, BMP_TYPE_NONE, ""},
};

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

#if defined(_WIN32) || defined(__CYGWIN__)
static size_t process_ftdi_probe(probe_info_s *probe_list)
{
	DWORD ftdi_dev_count = 0;
	if (FT_CreateDeviceInfoList(&ftdi_dev_count) != FT_OK)
		return 0;

	FT_DEVICE_LIST_INFO_NODE *dev_info =
		(FT_DEVICE_LIST_INFO_NODE *)malloc(sizeof(FT_DEVICE_LIST_INFO_NODE) * ftdi_dev_count);
	if (dev_info == NULL) {
		DEBUG_ERROR("%s: Memory allocation failed\n", __func__);
		return 0;
	}

	if (FT_GetDeviceInfoList(dev_info, &ftdi_dev_count) != FT_OK) {
		free(dev_info);
		return 0;
	}

	size_t devices_found = 0;
	// Device list is loaded, iterate over the found probes
	for (size_t index = 0; index < ftdi_dev_count; ++index) {
		probe_list->manufactuer = strdup(dev_info[index].Description);
		const size_t serial_len = strlen(dev_info[index].SerialNumber) - 1U;
		if (dev_info[index].SerialNumber[serial_len] == 'A')
			dev_info[index].SerialNumber[serial_len] = '\0';

		if (serial_len == 0)
			probe_list->serial = strdup("Unknown");
		else
			probe_list->serial = strdup(dev_info[index].SerialNumber);
		++devices_found;
		++probe_list;
	}
	return devices_found;
}
#endif

static int scan_for_probes(void)
{
	size_t debugger_count = 0;
	/*
	 * If we are running on Windows the proprietary FTD2XX library is used
	 * to collect debugger information.
	 */
#if defined(_WIN32) || defined(__CYGWIN__)
	debugger_count += process_ftdi_probe(probes);
	const bool skip_ftdi = true;
#else
	const bool skip_ftdi = false;
#endif

	libusb_device **device_list;
	const ssize_t cnt = libusb_get_device_list(info.libusb_ctx, &device_list);
	if (cnt > 0)
		libusb_free_device_list(device_list, (int)cnt);
	return debugger_count == 1 ? 0 : -1;
}

int find_debuggers(bmda_cli_options_s *cl_opts, bmp_info_s *info)
{
	(void)cl_opts;

	const int result = libusb_init(&info->libusb_ctx);
	if (result != LIBUSB_SUCCESS) {
		DEBUG_ERROR("Failed to initialise libusb (%d): %s\n", result, libusb_error_name(result));
		return -1;
	}

	return scan_for_probes();
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
