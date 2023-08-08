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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include "general.h"
#include "bmp_hosted.h"
#include "probe_info.h"
#include "utils.h"
#include "version.h"

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
int find_debuggers(bmda_cli_options_s *cl_opts, bmda_probe_s *info)
{
	DEBUG_ERROR("Please implement find_debuggers for MACOS!\n");
	(void)cl_opts;
	(void)info;
	return -1;
}
#elif defined(__WIN32__) || defined(__CYGWIN__)

/* This source has been used as an example:
 * https://stackoverflow.com/questions/3438366/setupdigetdeviceproperty-usage-example */

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h> // for MAX_DEVICE_ID_LEN, CM_Get_Parent and CM_Get_Device_ID
#include <tchar.h>
#include <stdio.h>

/* include DEVPKEY_Device_BusReportedDeviceDesc from WinDDK\7600.16385.1\inc\api\devpropdef.h */
#ifdef DEFINE_DEVPROPKEY
#undef DEFINE_DEVPROPKEY
#endif
#define DEFINE_DEVPROPKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) \
	const DEVPROPKEY DECLSPEC_SELECTANY name = {{l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}, pid}

/* include DEVPKEY_Device_BusReportedDeviceDesc from WinDDK\7600.16385.1\inc\api\devpkey.h */
DEFINE_DEVPROPKEY(DEVPKEY_Device_BusReportedDeviceDesc, 0x540b947e, 0x8b40, 0x45bc, 0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c,
	0xbd, 0xa2, 4); // DEVPROP_TYPE_STRING

/* List all USB devices with some additional information.
 * Unfortunately, this code is quite ugly. */
int find_debuggers(bmda_cli_options_s *cl_opts, bmda_probe_s *info)
{
	unsigned i;
	DWORD dwSize;
	DEVPROPTYPE ulPropertyType;
	CONFIGRET status;
	HDEVINFO hDevInfo;
	SP_DEVINFO_DATA DeviceInfoData;
	TCHAR szDeviceInstanceID[MAX_DEVICE_ID_LEN];
	WCHAR busReportedDeviceSesc[4096];
	size_t probes_found = 0;
	bool is_printing_probes_info = cl_opts->opt_list_only != 0;

	info->type = PROBE_TYPE_BMP;

	hDevInfo = SetupDiGetClassDevs(0, "USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	if (hDevInfo == INVALID_HANDLE_VALUE)
		return -1;
print_probes_info:
	for (i = 0;; i++) {
		char serial_number[sizeof info->serial];
		DeviceInfoData.cbSize = sizeof(DeviceInfoData);
		if (!SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData))
			break;

		status = CM_Get_Device_ID(DeviceInfoData.DevInst, szDeviceInstanceID, MAX_PATH, 0);
		if (status != CR_SUCCESS)
			continue;

		if (!sscanf(szDeviceInstanceID, "USB\\VID_1D50&PID_6018\\%s", serial_number))
			continue;

		if (SetupDiGetDevicePropertyW(hDevInfo, &DeviceInfoData, &DEVPKEY_Device_BusReportedDeviceDesc, &ulPropertyType,
				(BYTE *)busReportedDeviceSesc, sizeof busReportedDeviceSesc, &dwSize, 0)) {
			probes_found++;
			if (is_printing_probes_info) {
				DEBUG_WARN("%2zu: %s, %ls\n", probes_found, serial_number, busReportedDeviceSesc);
			} else {
				bool probe_identified = true;
				if ((cl_opts->opt_serial && strstr(serial_number, cl_opts->opt_serial)) ||
					(cl_opts->opt_position && cl_opts->opt_position == probes_found) ||
					/* Special case for the very first probe found. */
					(probe_identified = false, probes_found == 1U)) {
					strncpy(info->serial, serial_number, sizeof info->serial);
					strncpy(info->manufacturer, "BMP", sizeof info->manufacturer);
					snprintf(info->product, sizeof info->product, "%ls", busReportedDeviceSesc);
					/* Don't bother to parse the version string. It is a part of the
					 * product description string. It seems that at the moment it
					 * is only being used to print a version string in response
					 * to the 'monitor version' command, so it doesn't really matter
					 * if the version string is printed as a part of the product string,
					 * or as a separate string, the result is pretty much the same. */
					info->version[0] = 0;
					if (probe_identified)
						return 0;
				}
			}
		}
	}
	if (is_printing_probes_info)
		return 1;
	if (probes_found == 1U)
		/* Exactly one probe found. Its information has already been filled
		 * in the detection loop, so use this probe. */
		return 0;
	if (probes_found < 1U) {
		DEBUG_ERROR("No BMP probe found\n");
		return -1;
	}
	/* Otherwise, if this line is reached, then more than one probe has been found,
	 * and no probe was identified as selected by the user.
	 * Restart the identification loop, this time printing the probe information,
	 * and then return. */
	DEBUG_WARN("%zu debuggers found!\nSelect with -P <pos>, or "
			   "-s <(partial)serial no.>\n",
		probes_found);
	probes_found = 0;
	is_printing_probes_info = true;
	goto print_probes_info;
}

#else
/* Old ID: Black_Sphere_Technologies_Black_Magic_Probe_BFE4D6EC-if00
 * Recent: Black_Sphere_Technologies_Black_Magic_Probe_v1.7.1-212-g212292ab_7BAE7AB8-if00
 * usb-Black_Sphere_Technologies_Black_Magic_Probe__SWLINK__v1.7.1-155-gf55ad67b-dirty_DECB8811-if00
 */
#define BMP_IDSTRING_BLACKSPHERE "usb-Black_Sphere_Technologies_Black_Magic_Probe"
#define BMP_IDSTRING_BLACKMAGIC  "usb-Black_Magic_Debug_Black_Magic_Probe"
#define BMP_IDSTRING_1BITSQUARED "usb-1BitSquared_Black_Magic_Probe"
#define DEVICE_BY_ID             "/dev/serial/by-id"
#define BMP_PRODUCT_STRING       "Black Magic Probe"

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

static probe_info_s *parse_device_node(const char *name, probe_info_s *probe_list)
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
		DEBUG_ERROR("Failed to construct version of type string");
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

int find_debuggers(bmda_cli_options_s *cl_opts, bmda_probe_s *info)
{
	if (cl_opts->opt_device)
		return 1;
	/* Scan for all possible probes on the system */
	const probe_info_s *const probe_list = scan_for_devices();
	if (!probe_list) {
		DEBUG_ERROR("No BMP probe found\n");
		return -1;
	}
	/* Count up how many were found and filter the list for a match to the program options request */
	const size_t probes = probe_info_count(probe_list);
	const probe_info_s *probe = NULL;
	/* If there's just one probe and we didn't get match critera, pick it */
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
		return 1; // false;
	}

	/* We found a matching probe, populate bmda_probe_s and signal success */
	probe_info_to_bmda_probe(probe, info);
	probe_info_list_free(probe_list);
	return 0; // true;
}
#endif
