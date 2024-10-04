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

/* Find all known serial connected debuggers */

#include "general.h"
#if defined(_WIN32) || defined(__CYGWIN__)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <usbiodef.h>
#include <devpkey.h>
#include <setupapi.h>
#include <assert.h>
#else
#include <dirent.h>
#endif
#include <string.h>
#include <errno.h>
#include "bmp_hosted.h"
#include "probe_info.h"
#include "utils.h"
#include "version.h"

#define BMP_PRODUCT_STRING "Black Magic Probe"

void bmp_ident(bmda_probe_s *info)
{
	DEBUG_INFO("Black Magic Debug App (for BMP only) %s\n", FIRMWARE_VERSION);
	if (!info)
		return;
	DEBUG_INFO("Using:\n %s %s %s\n", info->manufacturer, info->version, info->serial);
}

void libusb_exit_function(bmda_probe_s *info)
{
	(void)info;
};

#ifdef __APPLE__
bool find_debuggers(bmda_cli_options_s *cl_opts, bmda_probe_s *info)
{
	DEBUG_ERROR("Please use full BMDA on macOS, BMP-only not supported\n");
	(void)cl_opts;
	(void)info;
	return false;
}
#elif defined(_WIN32) || defined(__CYGWIN__)
#define BMD_INSTANCE_PREFIX_LENGTH 22U
#define BMP_PRODUCT_STRING_LENGTH  ARRAY_LENGTH(BMP_PRODUCT_STRING)

static void display_error(const LSTATUS error, const char *const operation, const char *const path)
{
	char *message = NULL;
	/* Format the status into a message to present to the user */
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
		error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char *)&message, 0, NULL);
	/* Tell them what happened and what went bad, then free the buffer FormatMessage() gives us */
	DEBUG_ERROR("Error %s %s, got error %08lx: %s\n", operation, path, error, message);
	LocalFree(message);
}

static HKEY open_hklm_registry_path(const char *const path, const REGSAM permissions)
{
	HKEY handle = INVALID_HANDLE_VALUE;
	/* Attempt to open a HKLM registry key path */
	const LSTATUS result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, path, 0, permissions, &handle);
	if (result != ERROR_SUCCESS) {
		display_error(result, "opening registry key", path);
		return INVALID_HANDLE_VALUE;
	}
	return handle;
}

static bool read_value_u32_from_path(HKEY path_handle, const char *const value_name, uint32_t *const result_value)
{
	DWORD value = 0U;
	DWORD value_len = sizeof(value);
	/* Try to retreive the requested value */
	const LSTATUS result = RegGetValue(path_handle, NULL, value_name, RRF_RT_REG_DWORD, NULL, &value, &value_len);
	/* If that fails, tell the user and return failure */
	if (result != ERROR_SUCCESS || value_len != sizeof(value)) {
		display_error(result, "retriving registry value", value_name);
		return false;
	}
	/* Otherwise convert the result to uint32_t, storing it, and return success */
	*result_value = value;
	return true;
}

static const char *read_value_str_from_path(HKEY path_handle, const char *const value_name, size_t *const result_len)
{
	DWORD value_len = 0U;
	/* Start by trying to discover how long the string held by the key is */
	const LSTATUS result = RegGetValue(path_handle, NULL, value_name, RRF_RT_REG_SZ, NULL, NULL, &value_len);
	/* If that didn't work, we have no hoope, so bail */
	if (result != ERROR_SUCCESS && result != ERROR_MORE_DATA) {
		display_error(result, "retrieving registry value", value_name);
		return NULL;
	}

	/* It worked! Okay, now allocate storage for the result string large enough to hold it */
	*result_len = value_len;
	char *value = calloc(1, value_len);
	/* calloc() failure pointing to heap exhaustion(?!) */
	if (!value) {
		DEBUG_ERROR("Could not allocate sufficient memory for key value\n");
		return NULL;
	}

	/* Finally, try reading the value and return it to the user if this didn't explode */
	assert(RegGetValue(path_handle, NULL, value_name, RRF_RT_REG_SZ, NULL, value, &value_len) == ERROR_SUCCESS);
	return value;
}

static const char *query_product_description(const char *const instance_id, size_t *const result_len)
{
	/* Start by asking for all the device information for the device */
	HDEVINFO device_info = SetupDiGetClassDevs(&GUID_DEVINTERFACE_USB_DEVICE, instance_id, NULL, DIGCF_DEVICEINTERFACE);
	/* Check if that suceeded, and bail if it didn't.. */
	if (device_info == INVALID_HANDLE_VALUE) {
		display_error((LSTATUS)GetLastError(), "querying", "device information");
		return NULL;
	}

	/* Extract the device info data from the information set so we can then query a property */
	SP_DEVINFO_DATA device_data = {
		.cbSize = sizeof(SP_DEVINFO_DATA),
	};
	if (!SetupDiOpenDeviceInfo(device_info, instance_id, NULL, 0U, &device_data)) {
		/* If that didn't work, report the error and bail out */
		display_error((LSTATUS)GetLastError(), "retreiving", "device information");
		SetupDiDestroyDeviceInfoList(device_info);
		return NULL;
	}

	/* Find out how big a buffer is needed for the string */
	DEVPROPTYPE property_type = DEVPROP_TYPE_NULL;
	DWORD value_len = 0U;
	/*
	 * This call is guaranteed to "fail" because we don't provide a big enough buffer,
	 * so we have to be careful how we check the result
	 */
	if (!SetupDiGetDevicePropertyW(device_info, &device_data, &DEVPKEY_Device_BusReportedDeviceDesc, &property_type,
			NULL, 0U, &value_len, 0U)) {
		const DWORD result = GetLastError();
		/* Single out the result code for the buffer size return and dispatch the rest to error handling */
		if (result != ERROR_INSUFFICIENT_BUFFER) {
			display_error((LSTATUS)result, "querying", "product description");
			SetupDiDestroyDeviceInfoList(device_info);
			return NULL;
		}
	}
	/* Check the value will be of the correct type */
	if (property_type != DEVPROP_TYPE_STRING) {
		DEBUG_ERROR("Product description value of improper type\n");
		SetupDiDestroyDeviceInfoList(device_info);
		return NULL;
	}

	/* Now we know how big of an allocation to make, allocate a buffer big enough to receive the value */
	uint8_t *const value = calloc(1, value_len);
	/* Check if the allocation succeeded, and bail if not */
	if (!value) {
		DEBUG_ERROR("Could not allocate sufficient memory for device product description value\n");
		SetupDiDestroyDeviceInfoList(device_info);
		return NULL;
	}
	/* Now actually retreive the description string and check the result of the call is sane */
	assert(SetupDiGetDevicePropertyW(device_info, &device_data, &DEVPKEY_Device_BusReportedDeviceDesc, &property_type,
			   value, value_len, NULL, 0U) == TRUE);
	assert(property_type == DEVPROP_TYPE_STRING);
	/* Clean up working with the device info */
	SetupDiDestroyDeviceInfoList(device_info);
	/*
	 * The result of all this will be a wide character string, which is not particularly useful to us,
	 * so, convert the string to UTF-8 for use in BMD and return that instead.
	 */
	const size_t value_len_chars = wcsnlen((wchar_t *)value, value_len / 2U) + 1U;
	*result_len = (size_t)WideCharToMultiByte(CP_UTF8, 0U, (wchar_t *)value, (int)value_len_chars, NULL, 0, NULL, NULL);
	char *const result = malloc(*result_len);
	WideCharToMultiByte(CP_UTF8, 0U, (wchar_t *)value, (int)value_len_chars, result, (int)*result_len, NULL, NULL);
	free(value);
	return result;
}

static char *strndup(const char *const src, const size_t size)
{
	/* Determine how many bytes to copy to the new string, including the NULL terminator */
	const size_t length = MIN(size, strlen(src)) + 1U;
	/* Try to allocate storage for the new string */
	char *result = malloc(length);
	if (!result)
		return NULL;
	/* Now we have storage, copy the bytes over */
	memcpy(result, src, length - 1U);
	/* And finally terminate the string to return */
	result[length - 1U] = '\0';
	return result;
}

static probe_info_s *discover_device_entry(const char *const instance_id, probe_info_s *const probe_list)
{
	/* Extract the serial number portion of the instance ID */
	const char *const serial = strdup(instance_id + BMD_INSTANCE_PREFIX_LENGTH);
	/* If that failed to duplicate the string segment needed, bail */
	if (!serial)
		return probe_list;

	/* Prepare the probe information to hand to the probe_info_s system */
	const char *version = NULL;
	const char *type = NULL;
	const char *const product = strdup(BMP_PRODUCT_STRING);

	/* Query SetupAPI to find out what the actual product description string of the device is */
	size_t description_len = 0U;
	const char *const description = query_product_description(instance_id, &description_len);
	/* Check that the query succeeded, then double-check that the string starts with the expected product string */
	if (!description || !begins_with(description, description_len, BMP_PRODUCT_STRING)) {
		if (description)
			DEBUG_ERROR("Product description for device with serial %s was not valid\n", serial);
		else
			DEBUG_ERROR("Failed to retrieve product description for device with serial %s\n", serial);
		free((void *)description);
		free((void *)product);
		free((void *)serial);
		return probe_list;
	}
	/*
	 * At this point we should have a product string that's in one of the following forms:
	 * Recent: Black Magic Probe v1.10.0-1273-g2b1ce9aee
	 *       : Black Magic Probe (ST-Link v2) v1.10.0-1273-g2b1ce9aee
	 *   Old : Black Magic Probe
	 * From this we want to extract two main things: version (if available), and probe type
	 */

	/* Now try to determine if the probe is a BMP or one of the alternate platforms */
	const char *const opening_paren = strchr(description + BMP_PRODUCT_STRING_LENGTH, '(');
	/* If there's no opening `(` in the string, it's native */
	if (!opening_paren) {
		type = strdup("Native");
		/*
		 * Scan from the end of the string, if there are some remaining characters after the product string proper,
		 * and look for the last space - everything right of that must be the version string.
		 */
		if (description_len > BMP_PRODUCT_STRING_LENGTH) {
			const char *version_begin = strrchr(description, ' ');
			/* Thankfully, this can't fail, so just grab the version string from what we got */
			version = strdup(version_begin + 1U);
		} else
			version = strdup("Unknown");
	} else {
		/* Otherwise, we've now got to find the closing `)` and extract the substring created */
		const char *const closing_paren = strchr(opening_paren, ')');
		if (!closing_paren) {
			DEBUG_ERROR("Production description for device with serial %s is invalid, founding opening '(' but no "
						"closing ')'\n",
				serial);
			free((void *)description);
			free((void *)product);
			free((void *)serial);
			return probe_list;
		}
		/* Grab just the substring inside the parens as the type value */
		type = strndup(opening_paren + 1U, (closing_paren - opening_paren) - 1U);
		/* Going forward a couple of characters, making sure we find a space, take the remainder as the version string */
		const char *version_begin = strchr(closing_paren, ' ');
		/* Check if that failed, and if it didn't, grab what we got as the version string */
		if (version_begin)
			version = strdup(version_begin + 1U);
		else
			version = strdup("Unknown");
	}
	free((void *)description);

	if (!version || !type) {
		DEBUG_ERROR("Failed to construct version or type string");
		free((void *)serial);
		free((void *)version);
		free((void *)type);
		free((void *)product);
		return probe_list;
	}

	/* Finish up by adding the new probe to the list */
	return probe_info_add_by_serial(probe_list, PROBE_TYPE_BMP, type, product, serial, version);
}

static const probe_info_s *scan_for_devices(void)
{
	HKEY driver_handle = open_hklm_registry_path("SYSTEM\\CurrentControlSet\\Services\\usbccgp\\Enum", KEY_READ);
	/* Check if we failed to open the registry key for enumeration */
	if (driver_handle == INVALID_HANDLE_VALUE) {
		DEBUG_INFO("No composite devices have been enumerated on this system since boot\n");
		return NULL;
	}
	/*
	 * Now we've got a key to work with, grab the "Count" value from the USBCCGP driver so we know
	 * how many composite devices exist at this moment on the user's system
	 */
	uint32_t device_count = 0U;
	if (!read_value_u32_from_path(driver_handle, "Count", &device_count)) {
		DEBUG_ERROR("Failed to determine how many USB devices are attached to your computer\n");
		RegCloseKey(driver_handle);
		return NULL;
	}
	if (device_count == 0U) {
		DEBUG_INFO("No composite devices currently plugged in");
		RegCloseKey(driver_handle);
		return NULL;
	}

	/* Before looping through all the composite devices, make a string of the expected prefix to look for */
	const char *const bmd_instance_prefix = format_string("USB\\VID_%04X&PID_%04X\\", VENDOR_ID_BMP, PRODUCT_ID_BMP);
	/* Check if string formatting failed for some reason */
	if (!bmd_instance_prefix)
		return NULL;

	probe_info_s *probe_list = NULL;
	/* Loop through each of the devices which are named by number and extract serial numbers */
	for (uint32_t device_index = 0U; device_index < device_count; ++device_index) {
		/* Turn the index into a string */
		const char *const value_name = format_string("%" PRIu32, device_index);
		if (!value_name) {
			free((void *)bmd_instance_prefix);
			RegCloseKey(driver_handle);
			return NULL;
		}
		/* Now read the registry value to get an instance ID */
		size_t instance_id_len = 0U;
		const char *const instance_id = read_value_str_from_path(driver_handle, value_name, &instance_id_len);
		/* Free the index string before error checking or doing anything else */
		free((void *)value_name);
		/* Check that the value read is valid and that it begins with the expected prefix for a BMD probe */
		if (!instance_id || !begins_with(instance_id, instance_id_len, bmd_instance_prefix)) {
			free((void *)instance_id);
			continue;
		}
		/* If we got a valid prefix, go figure out what the device is exactly and make a probe_info_s entry for it */
		probe_info_s *probe_info = discover_device_entry(instance_id, probe_list);
		/* If the operation would have succeeded but probe_info_add_by_serial fails, we exhausted memory. */
		if (!probe_info) {
			free((void *)instance_id);
			probe_info_list_free(probe_list);
			probe_list = NULL;
			break;
		}
		/* If the operation returned the probe_list unchanged, it failed to discover the device */
		if (probe_info == probe_list)
			DEBUG_ERROR(
				"Error discovering potential probe with serial \"%s\"\n", instance_id + BMD_INSTANCE_PREFIX_LENGTH);
		free((void *)instance_id);
		probe_list = probe_info;
	}
	free((void *)bmd_instance_prefix);
	RegCloseKey(driver_handle);
	return probe_info_correct_order(probe_list);
}
#else
/*
 * Old ID: Black_Sphere_Technologies_Black_Magic_Probe_BFE4D6EC-if00
 * Recent: Black_Sphere_Technologies_Black_Magic_Probe_v1.7.1-212-g212292ab_7BAE7AB8-if00
 * usb-Black_Sphere_Technologies_Black_Magic_Probe__SWLINK__v1.7.1-155-gf55ad67b-dirty_DECB8811-if00
 */
#define BMP_IDSTRING_BLACKSPHERE "usb-Black_Sphere_Technologies_Black_Magic_Probe"
#define BMP_IDSTRING_BLACKMAGIC  "usb-Black_Magic_Debug_Black_Magic_Probe"
#define BMP_IDSTRING_1BITSQUARED "usb-1BitSquared_Black_Magic_Probe"
#define DEVICE_BY_ID             "/dev/serial/by-id"

typedef struct dirent dirent_s;

typedef enum scan_mode {
	SCAN_FIND,
	SCAN_LIST
} scan_mode_e;

size_t find_prefix_length(const char *name, const size_t name_len)
{
	if (begins_with(name, name_len, BMP_IDSTRING_BLACKSPHERE))
		return sizeof(BMP_IDSTRING_BLACKSPHERE);
	if (begins_with(name, name_len, BMP_IDSTRING_BLACKMAGIC))
		return sizeof(BMP_IDSTRING_BLACKMAGIC);
	if (begins_with(name, name_len, BMP_IDSTRING_1BITSQUARED))
		return sizeof(BMP_IDSTRING_1BITSQUARED);
	return 0;
}

char *extract_serial(const char *const device, const size_t length)
{
	const char *const last_underscore = strrchr(device, '_');
	/* Fail the match if we can't find the _ just before the serial string. */
	if (!last_underscore)
		return NULL;
	/* This represents the first byte of the serial number string */
	const char *const begin = last_underscore + 1;
	/* This represents one past the last byte of the serial number string */
	const char *const end = device + length - 5;
	/* We now allocate memory for the chunk and copy it */
	const size_t result_length = end - begin;
	char *const result = (char *)malloc(result_length);
	memcpy(result, begin, result_length);
	result[result_length - 1U] = '\0';
	return result;
}

static probe_info_s *parse_device_node(const char *name, probe_info_s *const probe_list)
{
	/* Starting with a string such as 'usb-Black_Magic_Debug_Black_Magic_Probe_v1.8.0-650-g829308db_8BB20695-if00' */
	const size_t name_len = strlen(name) + 1U;
	/* Find the correct prefix */
	size_t prefix_length = find_prefix_length(name, name_len);
	/* and skip past any leading _'s */
	while (name[prefix_length] == '_' && prefix_length < name_len)
		++prefix_length;
	if (prefix_length == name_len) {
		DEBUG_ERROR("Unexpected end\n");
		return probe_list;
	}

	/* With the remaining string, scan for _'s and index where the string chunks lie either side */
	/* This operates on a string such as 'v1.8.0-650-g829308db_8BB20695-if00' */
	size_t offsets[3][2] = {{prefix_length, 0U}, {0U, 0U}};
	size_t underscores = 0;
	for (size_t offset = prefix_length; offset < name_len; ++offset) {
		if (name[offset] == '_') {
			offsets[underscores][1] = offset - offsets[underscores][0];
			++underscores;
			/* Device paths with more than 2 underscore delimited sections can't be valid BMP strings. */
			if (underscores > 2U)
				return probe_list;
			/* Skip over consecutive strings of underscores */
			while (name[offset + 1U] == '_' && offset < name_len)
				++offset;
			/* Bounds check it */
			if (offset == name_len)
				break;
			offsets[underscores][0] = offset;
		}
	}
	offsets[underscores][1] = name_len - offsets[underscores][0];

	/* Now we know where everything is, start by extracting the serial string on the end. */
	char *const serial = extract_serial(name, name_len);
	if (!serial)
		return probe_list;

	/* Now extract the underlying probe type and versioning information */
	char *version = NULL;
	char *type = NULL;
	char *product = strdup(BMP_PRODUCT_STRING);

	if (!product) {
		free(serial);
		return probe_list;
	}

	/* If the device name has no underscores after the prefix, it's an original BMP */
	if (underscores == 0) {
		version = strdup("Unknown");
		type = strdup("Native");
		/*
		 * If the device name has two underscores delimited sections after the prefix,
		 * it's a non-native device running the Black Magic Firmware.
		 */
	} else if (underscores == 2) {
		version = strndup(name + offsets[0][0], offsets[0][1]);
		type = strndup(name + offsets[1][0], offsets[1][1]);
		/* Otherwise it's a native BMP */
	} else {
		/* The first section should start with a 'v' indicating the version info */
		if (name[prefix_length] == 'v') {
			version = strndup(name + offsets[0][0], offsets[0][1]);
			type = strdup("Native");
			/* But if not then it's actually a non-native device and has no version string. */
		} else {
			version = strdup("Unknown");
			type = strndup(name + offsets[0][0], offsets[0][1]);
		}
	}

	if (!version || !type) {
		DEBUG_ERROR("Failed to construct version or type string");
		free(serial);
		free(version);
		free(type);
		free(product);
		return probe_list;
	}

	return probe_info_add_by_serial(probe_list, PROBE_TYPE_BMP, type, product, serial, version);
}

static const probe_info_s *scan_for_devices(void)
{
	DIR *dir = opendir(DEVICE_BY_ID);
	if (!dir) /* /dev/serial/by-id is unavailable */
		return NULL;
	probe_info_s *probe_list = NULL;
	while (true) {
		const dirent_s *const entry = readdir(dir);
		if (entry == NULL)
			break;
		if (device_is_bmp_gdb_port(entry->d_name)) {
			probe_info_s *probe_info = parse_device_node(entry->d_name, probe_list);
			/* If the operation would have succeeded but probe_info_add_by_serial fails, we exhausted memory. */
			if (!probe_info) {
				probe_info_list_free(probe_list);
				probe_list = NULL;
				break;
			}
			/* If the operation returned the probe_list unchanged, it failed to parse the node */
			if (probe_info == probe_list)
				DEBUG_ERROR("Error parsing device name \"%s\"\n", entry->d_name);
			probe_list = probe_info;
		}
	}
	closedir(dir);
	return probe_info_correct_order(probe_list);
}
#endif

#ifndef __APPLE__
bool find_debuggers(bmda_cli_options_s *cl_opts, bmda_probe_s *info)
{
	if (cl_opts->opt_device)
		return false;
	/* Scan for all possible probes on the system */
	const probe_info_s *const probe_list = scan_for_devices();
	if (!probe_list) {
		DEBUG_ERROR("No Black Magic Probes found\n");
		return false;
	}
	/* Count up how many were found and filter the list for a match to the program options request */
	const size_t probes = probe_info_count(probe_list);
	const probe_info_s *probe = NULL;
	/* If there's just one probe and we didn't get match criteria, pick it */
	if (probes == 1U && !cl_opts->opt_serial && !cl_opts->opt_position)
		probe = probe_list;
	else /* Otherwise filter the list */
		probe = probe_info_filter(probe_list, cl_opts->opt_serial, cl_opts->opt_position);

	/* If we found no matching probes, or we're in list-only mode */
	if (!probe || cl_opts->opt_list_only) {
		DEBUG_WARN("Available Probes:\n");
		probe = probe_list;
		for (size_t position = 1U; probe; probe = probe->next, ++position)
			DEBUG_WARN(
				"%2zu: %s, Black Magic Debug, %s, %s\n", position, probe->serial, probe->manufacturer, probe->version);
		probe_info_list_free(probe_list);
		return false;
	}

	/* We found a matching probe, populate bmda_probe_s and signal success */
	probe_info_to_bmda_probe(probe, info);
	probe_info_list_free(probe_list);
	return true;
}
#endif
