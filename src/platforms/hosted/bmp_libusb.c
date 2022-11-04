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

void bmp_read_product_version(libusb_device_descriptor_s *device_descriptor, libusb_device *device,
	libusb_device_handle *handle, char **product, char **manufacturer, char **serial, char **version);

typedef struct debugger_device {
	uint16_t vendor;
	uint16_t product;
	bmp_type_t type;
	void (*function)(
		libusb_device_descriptor_s *, libusb_device *, libusb_device_handle *, char **, char **, char **, char **);
	char *type_string;
} debugger_device_s;

/* Create the list of debuggers BMDA works with */
debugger_device_s debugger_devices[] = {
	{VENDOR_ID_BMP, PRODUCT_ID_BMP, BMP_TYPE_BMP, bmp_read_product_version, "Black Magic Probe"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV2, BMP_TYPE_STLINK_V2, NULL, "ST-Link v2"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV21, BMP_TYPE_STLINK_V2, NULL, "ST-Link v2.1"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV21_MSD, BMP_TYPE_STLINK_V2, NULL, "ST-Link v2.1 MSD"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV3_NO_MSD, BMP_TYPE_STLINK_V2, NULL, "ST-Link v2.1 No MSD"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV3, BMP_TYPE_STLINK_V2, NULL, "ST-Link v3"},
	{VENDOR_ID_STLINK, PRODUCT_ID_STLINKV3E, BMP_TYPE_STLINK_V2, NULL, "ST-Link v3E"},
	{VENDOR_ID_SEGGER, PRODUCT_ID_UNKNOWN, BMP_TYPE_JLINK, NULL, "Segger JLink"},
	{VENDOR_ID_FTDI, PRODUCT_ID_FTDI_FT2232, BMP_TYPE_FTDI, NULL, "FTDI FT2232"},
	{VENDOR_ID_FTDI, PRODUCT_ID_FTDI_FT4232, BMP_TYPE_FTDI, NULL, "FTDI FT4232"},
	{VENDOR_ID_FTDI, PRODUCT_ID_FTDI_FT232, BMP_TYPE_FTDI, NULL, "FTDI FT232"},
	{0, 0, BMP_TYPE_NONE, NULL, ""},
};

bmp_type_t get_type_from_vid_pid(const uint16_t probe_vid, const uint16_t probe_pid)
{
	bmp_type_t probe_type = BMP_TYPE_NONE;
	/* Segger probe PIDs are unknown, if we have a Segger probe force the type to JLink */
	if (probe_vid == VENDOR_ID_SEGGER)
		return BMP_TYPE_JLINK;

	for (size_t index = 0; debugger_devices[index].type != BMP_TYPE_NONE; index++) {
		if (debugger_devices[index].vendor == probe_vid && debugger_devices[index].product == probe_pid)
			return debugger_devices[index].type;
	}
	return probe_type;
}

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

static char *get_device_descriptor_string(libusb_device_handle *handle, uint16_t string_index)
{
	char read_string[128] = {0};
	if (string_index != 0)
		libusb_get_string_descriptor_ascii(handle, string_index, (uint8_t *)read_string, sizeof(read_string));
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
	char *start_of_version;
	*product = get_device_descriptor_string(handle, device_descriptor->iProduct);
	start_of_version = strrchr(*product, ' ');
	if (start_of_version == NULL) {
		version = NULL;
	} else {
		while (start_of_version[0] == ' ' && start_of_version != *product)
			--start_of_version;
		start_of_version[1] = '\0';
		start_of_version += 2;
		while (start_of_version[0] == ' ' && start_of_version[0] != '\0')
			++start_of_version;
		*version = strdup(start_of_version);
	}
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
	/* Device list is loaded, iterate over the found probes */
	for (size_t index = 0; index < ftdi_dev_count; ++index) {
		const uint16_t vid = (dev_info[index].ID >> 16U) & 0xffffU;
		const uint16_t pid = dev_info[index].ID & 0xffffU;
		char *serial = strdup(dev_info[index].SerialNumber);
		const char *const product = strdup(dev_info[index].Description);
		size_t serial_len = strlen(serial);
		if (serial_len <= 1) {
			free(serial);
			serial = strdup("Unknown serial");
		} else {
			--serial_len;
			if (serial[serial_len] == 'A')
				serial[serial_len] = '\0';
		}
		const char *const manufacturer = strdup("FTDI");
		probe_list =
			probe_info_add_by_id(probe_list, BMP_TYPE_LIBFTDI, vid, pid, manufacturer, product, serial, strdup("---"));
	}
	free(dev_info);
	return probe_list;
}
#endif

static bool process_cmsis_interface_probe(
	libusb_device_descriptor_s *device_descriptor, libusb_device *device, probe_info_s **probe_list)
{
	/* Try to get the active configuration descriptor for the device */
	libusb_config_descriptor_s *config;
	if (libusb_get_active_config_descriptor(device, &config) != 0)
		return false;

	/* Try to open the device */
	libusb_device_handle *handle;
	if (libusb_open(device, &handle) != 0) {
		libusb_free_config_descriptor(config);
		return false;
	}
	char read_string[128];

	bool cmsis_dap = false;
	for (size_t iface = 0; iface < config->bNumInterfaces && !cmsis_dap; ++iface) {
		const libusb_interface_s *interface = &config->interface[iface];
		for (int descriptorIndex = 0; descriptorIndex < interface->num_altsetting; ++descriptorIndex) {
			const libusb_interface_descriptor_s *descriptor = &interface->altsetting[descriptorIndex];
			uint8_t string_index = descriptor->iInterface;
			if (string_index == 0)
				continue;
			if (libusb_get_string_descriptor_ascii(
					handle, string_index, (unsigned char *)read_string, sizeof(read_string)) < 0)
				continue; /* We failed but that's a soft error at this point. */

			if (strstr(read_string, "CMSIS") != NULL) {
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

				*probe_list = probe_info_add_by_id(*probe_list, BMP_TYPE_CMSIS_DAP, device_descriptor->idVendor,
					device_descriptor->idProduct, manufacturer, product, serial, strdup("---"));
				cmsis_dap = true;
			}
		}
	}
	libusb_close(handle);
	libusb_free_config_descriptor(config);
	return cmsis_dap;
}

static bool process_vid_pid_table_probe(
	libusb_device_descriptor_s *device_descriptor, libusb_device *device, probe_info_s **probe_list)
{
	bool probe_added = false;
	for (size_t index = 0; debugger_devices[index].type != BMP_TYPE_NONE; ++index) {
		/* Check for a match, skip the entry if we don't get one */
		if (device_descriptor->idVendor != debugger_devices[index].vendor ||
			(device_descriptor->idProduct != debugger_devices[index].product &&
				debugger_devices[index].product != PRODUCT_ID_UNKNOWN))
			continue;

		libusb_device_handle *handle = NULL;
		/* Try to open the device */
		if (libusb_open(device, &handle) != LIBUSB_SUCCESS)
			break;

		const bmp_type_t probe_type = get_type_from_vid_pid(device_descriptor->idVendor, device_descriptor->idProduct);
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
		if (debugger_devices[index].function != NULL)
			debugger_devices[index].function(
				device_descriptor, device, handle, &product, &manufacturer, &serial, &version);

		/* Now read any strings that have not been set by a custom reader function */
		if (product == NULL)
			product = get_device_descriptor_string(handle, device_descriptor->iProduct);
		if (manufacturer == NULL)
			manufacturer = get_device_descriptor_string(handle, device_descriptor->iManufacturer);
		if (serial == NULL)
			serial = get_device_descriptor_string(handle, device_descriptor->iSerialNumber);

		if (version == NULL)
			version = strdup("---");

		*probe_list = probe_info_add_by_id(*probe_list, probe_type, device_descriptor->idVendor,
			device_descriptor->idProduct, manufacturer, product, serial, version);
		probe_added = true;
		libusb_close(handle);
	}
	return probe_added;
}

static const probe_info_s *scan_for_devices(void)
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
	const ssize_t cnt = libusb_get_device_list(info.libusb_ctx, &device_list);
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
				process_cmsis_interface_probe(&device_descriptor, device, &probe_list);
		}
	}
	libusb_free_device_list(device_list, (int)cnt);
	return probe_info_correct_order(probe_list);
}

int find_debuggers(bmda_cli_options_s *cl_opts, bmp_info_s *info)
{
	if (cl_opts->opt_device)
		return 1;

	const int result = libusb_init(&info->libusb_ctx);
	if (result != LIBUSB_SUCCESS) {
		DEBUG_ERROR("Failed to initialise libusb (%d): %s\n", result, libusb_error_name(result));
		return -1;
	}

	/* Scan for all possible probes on the system */
	const probe_info_s *probe_list = scan_for_devices();
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
		return 1; // false;
	}

	/* We found a matching probe, populate bmp_info_s and signal success */
	probe_info_to_bmp_info(probe, info);
	/* If the selected probe is an FTDI adapter try to resolve the adaptor type */
	if (probe->vid == VENDOR_ID_FTDI && !ftdi_lookup_adapter_from_vid_pid(cl_opts, probe)) {
		// Don't know the cable type, ask user to specify with "-c"
		DEBUG_WARN("Multiple FTDI adapters match Vendor and Product ID.\n");
		DEBUG_WARN("Please specify adapter type on command line using \"-c\" option.\n");
		return -1; //false
	}
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
