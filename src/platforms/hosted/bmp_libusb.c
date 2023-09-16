/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 - 2022 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Written by Sid Price <sid@sidprice.com>
 * Written by Rachel Mant <git@dragonmux.network>
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
#if (defined __has_include && __has_include(<uchar.h>)) || !defined(__APPLE__)
#include <uchar.h>
#else
//https://sourceware.org/bugzilla/show_bug.cgi?id=17979
#include <stdint.h>
typedef int_least16_t char16_t;
#endif
#include "cli.h"
#include "ftdi_bmp.h"
#include "version.h"
#include "probe_info.h"
#include "utils.h"
#include "hex_utils.h"

#define NO_SERIAL_NUMBER "<no serial number>"

void bmp_read_product_version(libusb_device_descriptor_s *device_descriptor, libusb_device *device,
	libusb_device_handle *handle, char **product, char **manufacturer, char **serial, char **version);
void stlinkv2_read_serial(libusb_device_descriptor_s *device_descriptor, libusb_device *device,
	libusb_device_handle *handle, char **product, char **manufacturer, char **serial, char **version);

typedef struct debugger_device {
	uint16_t vendor;
	uint16_t product;
	probe_type_e type;
	void (*function)(
		libusb_device_descriptor_s *, libusb_device *, libusb_device_handle *, char **, char **, char **, char **);
	char *type_string;
} debugger_device_s;

/* Create the list of debuggers BMDA works with */
static const debugger_device_s debugger_devices[] = {
	{VENDOR_ID_BMP, PRODUCT_ID_BMP, PROBE_TYPE_BMP, bmp_read_product_version, "Black Magic Probe"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV2, PROBE_TYPE_STLINK_V2, stlinkv2_read_serial, "ST-Link v2"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV21, PROBE_TYPE_STLINK_V2, NULL, "ST-Link v2.1"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV21_MSD, PROBE_TYPE_STLINK_V2, NULL, "ST-Link v2.1 MSD"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV3_NO_MSD, PROBE_TYPE_STLINK_V2, NULL, "ST-Link v2.1 No MSD"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV3, PROBE_TYPE_STLINK_V2, NULL, "ST-Link v3"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV3E, PROBE_TYPE_STLINK_V2, NULL, "ST-Link v3E"},
	{VENDOR_ID_SEGGER, PRODUCT_ID_ANY, PROBE_TYPE_JLINK, NULL, "Segger JLink"},
	{VENDOR_ID_FTDI, PRODUCT_ID_FTDI_FT2232, PROBE_TYPE_FTDI, NULL, "FTDI FT2232"},
	{VENDOR_ID_FTDI, PRODUCT_ID_FTDI_FT4232, PROBE_TYPE_FTDI, NULL, "FTDI FT4232"},
	{VENDOR_ID_FTDI, PRODUCT_ID_FTDI_FT232, PROBE_TYPE_FTDI, NULL, "FTDI FT232"},
	{0, 0, PROBE_TYPE_NONE, NULL, ""},
};

const debugger_device_s *get_debugger_device_from_vid_pid(const uint16_t probe_vid, const uint16_t probe_pid)
{
	/* Iterate over the list excluding the last entry (PROBE_TYPE_NONE) */
	for (size_t index = 0; index < ARRAY_LENGTH(debugger_devices) - 1U; index++) {
		/* Check for a vendor id match */
		if (debugger_devices[index].vendor != probe_vid)
			continue;

		/* If the map product id is "any", then we accept all products ids, otherwise check for a match */
		if (debugger_devices[index].product == PRODUCT_ID_ANY || probe_pid == debugger_devices[index].product)
			return &debugger_devices[index];
	}
	/* Return the last entry in the list (PROBE_TYPE_NONE) */
	return &debugger_devices[ARRAY_LENGTH(debugger_devices) - 1U];
}

void bmp_ident(bmda_probe_s *info)
{
	DEBUG_INFO("Black Magic Debug App " FIRMWARE_VERSION "\n for Black Magic Probe, ST-Link v2 and v3, CMSIS-DAP, "
			   "J-Link and FTDI (MPSSE)\n");
	if (info && info->vid && info->pid) {
		DEBUG_INFO("Using %04x:%04x %s %s\n %s %s\n", info->vid, info->pid,
			(info->serial[0]) ? info->serial : NO_SERIAL_NUMBER, info->manufacturer, info->product, info->version);
	}
}

void libusb_exit_function(bmda_probe_s *info)
{
	if (!info->usb_link)
		return;
	if (info->usb_link->device_handle) {
		libusb_release_interface(info->usb_link->device_handle, 0);
		libusb_close(info->usb_link->device_handle);
	}
}

static char *get_device_descriptor_string(libusb_device_handle *handle, uint16_t string_index)
{
	char read_string[128U] = {0};
	if (string_index != 0) {
		const int result =
			libusb_get_string_descriptor_ascii(handle, string_index, (uint8_t *)read_string, sizeof(read_string));
		if (result < LIBUSB_SUCCESS)
			DEBUG_ERROR(
				"%s: Failed to read string from device (%d): %s\n", __func__, result, libusb_error_name(result));
	}
	return strdup(read_string);
}

/*
 * BMP Probes have their version information in the product string.
 *
 * Extract the product and version, skip the manufacturer string
 */
void bmp_read_product_version(libusb_device_descriptor_s *device_descriptor, libusb_device *device,
	libusb_device_handle *handle, char **product, char **manufacturer, char **serial, char **version)
{
	(void)device;
	(void)serial;
	(void)manufacturer;
	*product = get_device_descriptor_string(handle, device_descriptor->iProduct);

	char *start_of_version = strrchr(*product, ' ');
	if (start_of_version == NULL) {
		*version = NULL;
		return;
	}

	while (start_of_version[0] == ' ' && start_of_version != *product)
		--start_of_version;
	start_of_version[1U] = '\0';
	start_of_version += 2U;
	while (start_of_version[0] == ' ')
		++start_of_version;
	*version = strdup(start_of_version);
}

/*
 * ST-Link v2's incorrectly report their serial number.
 * Extract it, and decode it as hexadecimal
 */
void stlinkv2_read_serial(libusb_device_descriptor_s *device_descriptor, libusb_device *device,
	libusb_device_handle *handle, char **product, char **manufacturer, char **serial, char **version)
{
	(void)device;
	(void)product;
	(void)manufacturer;
	(void)version;
	/* libusb_get_string_descriptor requires a byte buffer, but returns char16_t's */
	char16_t raw_serial[64U] = {0};
	const int raw_length = libusb_get_string_descriptor(
		handle, device_descriptor->iSerialNumber, 0x0409U, (uint8_t *)raw_serial, sizeof(raw_serial));
	if (raw_length < LIBUSB_SUCCESS)
		return;

	/*
	 * Re-encode the resulting chunk of data as hex, skipping the first char16_t which
	 * contains the header of the string descriptor
	 */
	char encoded_serial[128U] = {0};
	for (size_t offset = 0; offset < (size_t)raw_length - 2U; offset += 2U) {
		uint8_t digit = raw_serial[1 + (offset / 2U)];
		encoded_serial[offset + 0] = hex_digit(digit >> 4U);
		encoded_serial[offset + 1U] = hex_digit(digit & 0x0fU);
	}
	*serial = strdup(encoded_serial);
}

#if defined(_WIN32) || defined(__CYGWIN__)
static probe_info_s *process_ftdi_probe(void)
{
	DWORD ftdi_dev_count = 0;
	if (FT_CreateDeviceInfoList(&ftdi_dev_count) != FT_OK)
		return NULL;

	FT_DEVICE_LIST_INFO_NODE *dev_info =
		(FT_DEVICE_LIST_INFO_NODE *)malloc(sizeof(FT_DEVICE_LIST_INFO_NODE) * ftdi_dev_count);
	if (dev_info == NULL) {
		DEBUG_ERROR("%s: Memory allocation failed\n", __func__);
		return NULL;
	}

	if (FT_GetDeviceInfoList(dev_info, &ftdi_dev_count) != FT_OK) {
		free(dev_info);
		return NULL;
	}

	probe_info_s *probe_list = NULL;
	const char *probe_skip = NULL;
	bool use_serial = true;
	/* Device list is loaded, iterate over the found probes */
	for (size_t index = 0; index < ftdi_dev_count; ++index) {
		const uint16_t vid = (dev_info[index].ID >> 16U) & 0xffffU;
		const uint16_t pid = dev_info[index].ID & 0xffffU;
		bool add_probe = true;

		char *serial = strdup(dev_info[index].SerialNumber);
		char *const product = strdup(dev_info[index].Description);
		size_t serial_len = strlen(serial);
		if (!serial_len) {
			free(serial);
			serial = strdup("---"); // Unknown serial number
		} else {
			--serial_len;
			if (serial[serial_len] == 'A') {
				serial[serial_len] = '\0'; // Remove the trailing "A"

				if (probe_skip) { // Clean up any previous serial number to skip
					use_serial = true;
					free((void *)probe_skip);
					probe_skip = NULL;
				}

				// If the serial number is valid, save it for later interface skip testing
				if (serial_len)
					probe_skip = strdup(serial); // Save the fixed serial number so we can skip over other interfaces

				size_t product_len = strlen(product); // Product has " A" appended
				product_len -= 2;
				product[product_len] = '\0'; // Remove it

				// If we don't have a saved serial number, use the truncated product for the probe skip test
				if (!probe_skip) {
					use_serial = false;
					probe_skip = strdup(product);
				}
			} else {
				if (probe_skip) {
					if (use_serial)
						// Skip this interface if the serial matches
						add_probe = strstr(serial, probe_skip) == NULL;
					else
						// Skip this interface if the product name matches
						add_probe = strstr(product, probe_skip) == NULL;
				}
			}
		}
		if (add_probe) {
			const char *const manufacturer = strdup("FTDI");
			probe_list = probe_info_add_by_id(
				probe_list, PROBE_TYPE_FTDI, NULL, vid, pid, manufacturer, product, serial, strdup("---"));
		} else {
			free(serial);
			free(product);
		}
	}
	if (probe_skip)
		free((void *)probe_skip);
	free(dev_info);
	return probe_list;
}
#endif

void orbtrace_read_version(libusb_device *device, libusb_device_handle *handle, char *version, size_t buffer_size)
{
	libusb_config_descriptor_s *config;
	if (libusb_get_active_config_descriptor(device, &config) != 0)
		return;
	for (size_t iface = 0; iface < config->bNumInterfaces; ++iface) {
		const libusb_interface_s *interface = &config->interface[iface];
		for (int altmode = 0; altmode < interface->num_altsetting; ++altmode) {
			const libusb_interface_descriptor_s *descriptor = &interface->altsetting[altmode];
			uint8_t string_index = descriptor->iInterface;
			if (string_index == 0)
				continue;
			if (libusb_get_string_descriptor_ascii(handle, string_index, (uint8_t *)version, (int)buffer_size) < 0)
				continue; /* We failed but that's a soft error at this point. */

			const size_t version_len = strlen(version);
			if (begins_with(version, version_len, "Version")) {
				/* Chop off the "Version: " prefix */
				memmove(version, version + 9U, version_len - 8);
				break;
			}
		}
	}
	libusb_free_config_descriptor(config);
}

static void process_cmsis_interface(const libusb_device_descriptor_s *const device_descriptor,
	libusb_device *const device, libusb_device_handle *const handle, probe_info_s **probe_list)
{
	char read_string[128U];
	char *version;
	if (device_descriptor->idVendor == VENDOR_ID_ORBCODE && device_descriptor->idProduct == PRODUCT_ID_ORBTRACE) {
		orbtrace_read_version(device, handle, read_string, sizeof(read_string));
		version = strdup(read_string);
	} else
		version = strdup("---");
	char *serial;
	if (device_descriptor->iSerialNumber == 0)
		serial = strdup("Unknown serial number");
	else
		serial = get_device_descriptor_string(handle, device_descriptor->iSerialNumber);
	char *manufacturer;
	if (device_descriptor->iManufacturer == 0)
		manufacturer = strdup("Unknown manufacturer");
	else
		manufacturer = get_device_descriptor_string(handle, device_descriptor->iManufacturer);
	char *product;
	if (device_descriptor->iProduct == 0)
		product = strdup("Unknown product");
	else
		product = get_device_descriptor_string(handle, device_descriptor->iProduct);

	*probe_list = probe_info_add_by_id(*probe_list, PROBE_TYPE_CMSIS_DAP, device, device_descriptor->idVendor,
		device_descriptor->idProduct, manufacturer, product, serial, version);
}

static bool process_cmsis_interface_probe(
	libusb_device_descriptor_s *device_descriptor, libusb_device *device, probe_info_s **probe_list, bmda_probe_s *info)
{
	(void)info;
	/* Try to get the active configuration descriptor for the device */
	libusb_config_descriptor_s *config;
	if (libusb_get_active_config_descriptor(device, &config) != LIBUSB_SUCCESS)
		return false;

	/* Try to open the device */
	libusb_device_handle *handle;
	if (libusb_open(device, &handle) != LIBUSB_SUCCESS) {
		libusb_free_config_descriptor(config);
		return false;
	}

	bool cmsis_dap = false;
	/* Enumerate the device's interfaces and all their alt modes */
	for (uint8_t iface = 0; iface < config->bNumInterfaces && !cmsis_dap; ++iface) {
		const libusb_interface_s *interface = &config->interface[iface];
		for (int altmode = 0; altmode < interface->num_altsetting; ++altmode) {
			const libusb_interface_descriptor_s *descriptor = &interface->altsetting[altmode];
			uint8_t string_index = descriptor->iInterface;
			/* If we've found an interface without a description string, ignore it */
			if (string_index == 0)
				continue;
			char interface_string[128];
			/* Read out the string */
			if (libusb_get_string_descriptor_ascii(
					handle, string_index, (unsigned char *)interface_string, sizeof(interface_string)) < 0)
				continue; /* We failed but that's a soft error at this point. */

			/* Check if it's a CMSIS-DAP interface */
			if (strstr(interface_string, "CMSIS") != NULL) {
				process_cmsis_interface(device_descriptor, device, handle, probe_list);
				cmsis_dap = true;
				break;
			}
		}
	}
	libusb_close(handle);
	libusb_free_config_descriptor(config);
	return cmsis_dap;
}

static void check_cmsis_interface_type(libusb_device *const device, bmda_probe_s *const info)
{
	/* Try to get the active configuration descriptor for the device */
	libusb_config_descriptor_s *config;
	if (libusb_get_active_config_descriptor(device, &config) != LIBUSB_SUCCESS)
		return;

	/* Try to open the device */
	libusb_device_handle *handle;
	if (libusb_open(device, &handle) != LIBUSB_SUCCESS) {
		libusb_free_config_descriptor(config);
		return;
	}

	/* Enumerate the device's interfaces and all their alt modes */
	for (uint8_t iface = 0; iface < config->bNumInterfaces; ++iface) {
		const libusb_interface_s *interface = &config->interface[iface];
		for (int altmode = 0; altmode < interface->num_altsetting; ++altmode) {
			const libusb_interface_descriptor_s *descriptor = &interface->altsetting[altmode];
			/* If we've found an interface without a description string, ignore it */
			if (descriptor->iInterface == 0)
				continue;
			char interface_string[128U];
			/* Read out the string */
			if (libusb_get_string_descriptor_ascii(
					handle, descriptor->iInterface, (unsigned char *)interface_string, sizeof(interface_string)) < 0)
				continue;

			/* Check if it's a CMSIS-DAP interface */
			if (strstr(interface_string, "CMSIS") == NULL)
				continue;

			/* Check if it's a CMSIS-DAP v2 interface */
			if (descriptor->bInterfaceClass == 0xffU && descriptor->bNumEndpoints == 2U) {
				info->interface_num = descriptor->bInterfaceNumber;
				/* Extract the endpoints required */
				for (uint8_t index = 0; index < descriptor->bNumEndpoints; ++index) {
					const uint8_t ep = descriptor->endpoint[index].bEndpointAddress;
					if (ep & 0x80U)
						info->in_ep = ep;
					else
						info->out_ep = ep;
				}
			}
		}
	}
	libusb_close(handle);
	libusb_free_config_descriptor(config);
}

static bool process_vid_pid_table_probe(
	libusb_device_descriptor_s *device_descriptor, libusb_device *device, probe_info_s **probe_list)
{
	/* Check for a match */
	const debugger_device_s *const debugger_device =
		get_debugger_device_from_vid_pid(device_descriptor->idVendor, device_descriptor->idProduct);
	if (debugger_device->type == PROBE_TYPE_NONE)
		return false;

	libusb_device_handle *handle = NULL;
	/* Try to open the device */
	if (libusb_open(device, &handle) != LIBUSB_SUCCESS)
		return false;

	char *product = NULL;
	char *manufacturer = NULL;
	char *serial = NULL;
	char *version = NULL;
	/*
	 * If the probe has a custom string reader available, use it first.
	 *
	 * This will read and process any strings that need special work, e.g., extracting
	 * a version string from a product string (BMP native)
	 */
	if (debugger_device->function != NULL)
		debugger_device->function(device_descriptor, device, handle, &product, &manufacturer, &serial, &version);

	/* Now read any strings that have not been set by a custom reader function */
	if (product == NULL)
		product = get_device_descriptor_string(handle, device_descriptor->iProduct);
	if (manufacturer == NULL)
		manufacturer = get_device_descriptor_string(handle, device_descriptor->iManufacturer);
	if (serial == NULL)
		serial = get_device_descriptor_string(handle, device_descriptor->iSerialNumber);

	if (version == NULL)
		version = strdup("---");

	*probe_list = probe_info_add_by_id(*probe_list, debugger_device->type, device, device_descriptor->idVendor,
		device_descriptor->idProduct, manufacturer, product, serial, version);
	libusb_close(handle);
	return true;
}

static const probe_info_s *scan_for_devices(bmda_probe_s *info)
{
	/*
	 * If we are running on Windows the proprietary FTD2XX library is used
	 * to collect debugger information.
	 */
#if defined(_WIN32) || defined(__CYGWIN__)
	probe_info_s *probe_list = process_ftdi_probe();
	const bool skip_ftdi = probe_list != NULL;
#else
	probe_info_s *probe_list = NULL;
	const bool skip_ftdi = false;
#endif

	libusb_device **device_list;
	const ssize_t cnt = libusb_get_device_list(info->libusb_ctx, &device_list);
	if (cnt <= 0)
		return probe_info_correct_order(probe_list);
	/* Parse the list of USB devices found */
	for (size_t device_index = 0; device_list[device_index]; ++device_index) {
		libusb_device *const device = device_list[device_index];
		libusb_device_descriptor_s device_descriptor;
		const int result = libusb_get_device_descriptor(device, &device_descriptor);
		if (result < 0) {
			DEBUG_ERROR("Failed to get device descriptor (%d): %s\n", result, libusb_error_name(result));
			return NULL;
		}
		if (device_descriptor.idVendor != VENDOR_ID_FTDI || !skip_ftdi) {
			if (!process_vid_pid_table_probe(&device_descriptor, device, &probe_list))
				process_cmsis_interface_probe(&device_descriptor, device, &probe_list, info);
		}
	}
	libusb_free_device_list(device_list, (int)cnt);
	return probe_info_correct_order(probe_list);
}

int find_debuggers(bmda_cli_options_s *cl_opts, bmda_probe_s *info)
{
	if (cl_opts->opt_device)
		return 1;

	const int result = libusb_init(&info->libusb_ctx);
	if (result != LIBUSB_SUCCESS) {
		DEBUG_ERROR("Failed to initialise libusb (%d): %s\n", result, libusb_error_name(result));
		return -1;
	}

	/* Scan for all possible probes on the system */
	const probe_info_s *probe_list = scan_for_devices(info);
	if (!probe_list) {
		DEBUG_WARN("No probes found\n");
		return -1;
	}
	/* Count up how many were found and filter the list for a match to the program options request */
	const size_t probes = probe_info_count(probe_list);
	const probe_info_s *probe = NULL;
	/* If there's just one probe and we didn't get match critera, pick it */
	if (probes == 1 && !cl_opts->opt_serial && !cl_opts->opt_position)
		probe = probe_list;
	else /* Otherwise filter the list */
		probe = probe_info_filter(probe_list, cl_opts->opt_serial, cl_opts->opt_position);

	/* If we found no matching probes, or we're in list-only mode */
	if (!probe || cl_opts->opt_list_only) {
		DEBUG_WARN("Available Probes:\n");
		probe = probe_list;
		DEBUG_WARN("     %-20s %-25s %-25s %s\n", "Name", "Serial #", "Manufacturer", "Version");
		for (size_t position = 1; probe; probe = probe->next, ++position)
			DEBUG_WARN(" %2zu. %-20s %-25s %-25s %s\n", position, probe->product, probe->serial, probe->manufacturer,
				probe->version);
		probe_info_list_free(probe_list);
		return cl_opts->opt_list_only ? 0 : 1; // false;
	}

	/* We found a matching probe, populate bmda_probe_s and signal success */
	probe_info_to_bmda_probe(probe, info);
	/* If the selected probe is an FTDI adapter try to resolve the adaptor type */
	if (probe->type == PROBE_TYPE_FTDI && !ftdi_lookup_adaptor_descriptor(cl_opts, probe)) {
		// Don't know the cable type, ask user to specify with "-c"
		DEBUG_WARN("Multiple FTDI adapters match Vendor and Product ID.\n");
		DEBUG_WARN("Please specify adapter type on command line using \"-c\" option.\n");
		probe_info_list_free(probe_list);
		return -1; //false
	}
	/* If the selected probe is CMSIS-DAP, check for v2 interfaces */
	if (probe->type == PROBE_TYPE_CMSIS_DAP)
		check_cmsis_interface_type(probe->device, info);

	probe_info_list_free(probe_list);
	return 0; // true;
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
int bmda_usb_transfer(
	usb_link_s *link, const void *tx_buffer, size_t tx_len, void *rx_buffer, size_t rx_len, uint16_t timeout)
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
		const int result = libusb_bulk_transfer(
			link->device_handle, link->ep_tx | LIBUSB_ENDPOINT_OUT, tx_data, (int)tx_len, NULL, timeout);
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
			link->device_handle, link->ep_rx | LIBUSB_ENDPOINT_IN, rx_data, (int)rx_len, &rx_bytes, timeout);
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
