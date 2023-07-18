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

int scan_for_probes(void)
{
	return -1;
}

int find_debuggers(bmda_cli_options_s *cl_opts, bmp_info_s *info)
{
	(void)cl_opts;
	(void)info;
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
