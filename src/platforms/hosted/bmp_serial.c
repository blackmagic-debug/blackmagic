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
#include "utils.h"
#include "version.h"

void bmp_ident(bmp_info_t *info)
{
	PRINT_INFO("Black Magic Debug App (for BMP only) %s\n", FIRMWARE_VERSION);
	if (!info)
		return;
	PRINT_INFO("Using:\n %s %s %s\n", info->manufacturer, info->version, info->serial);
}

void libusb_exit_function(bmp_info_t *info)
{
	(void)info;
};

#ifdef __APPLE__
int find_debuggers(BMP_CL_OPTIONS_t *cl_opts, bmp_info_t *info)
{
	DEBUG_WARN("Please implement find_debuggers for MACOS!\n");
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
int find_debuggers(BMP_CL_OPTIONS_t *cl_opts, bmp_info_t *info)
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

	info->bmp_type = BMP_TYPE_BMP;

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
				DEBUG_WARN("%2d: %s, %ls\n", probes_found, serial_number, busReportedDeviceSesc);
			} else {
				bool probe_identified = true;
				if ((cl_opts->opt_serial && strstr(serial_number, cl_opts->opt_serial)) ||
					(cl_opts->opt_position && cl_opts->opt_position == probes_found) ||
					/* Special case for the very first probe found. */
					(probe_identified = false, probes_found == 1)) {
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
	if (probes_found == 1)
		/* Exactly one probe found. Its information has already been filled
		 * in the detection loop, so use this probe. */
		return 0;
	if (probes_found < 1) {
		DEBUG_WARN("No BMP probe found\n");
		return -1;
	}
	/* Otherwise, if this line is reached, then more than one probe has been found,
	 * and no probe was identified as selected by the user.
	 * Restart the identification loop, this time printing the probe information,
	 * and then return. */
	DEBUG_WARN("%d debuggers found!\nSelect with -P <pos>, or "
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
#define DEVICE_BY_ID             "/dev/serial/by-id/"

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
	result[result_length - 1] = '\0';
	return result;
}

/*
 * Extract type, version and serial from /dev/serial/by_id
 * Return 0 on success
 *
 * Old versions have different strings. Try to cope!
 */
static int scan_linux_id(const char *name, char **const type, char **const version, char **const serial)
{
	const size_t name_len = strlen(name);
	/* Find the correct prefix */
	size_t prefix_length = find_prefix_length(name, name_len);
	/* and skip past any leading _'s */
	while (name[prefix_length] == '_' && prefix_length < name_len)
		++prefix_length;
	if (prefix_length == name_len) {
		DEBUG_WARN("Unexpected end\n");
		return -1;
	}

	size_t offsets[2] = {0, 0};
	size_t underscores = 0;
	for (size_t offset = prefix_length; offset < name_len; ++offset) {
		if (name[offset] == '_') {
			/* Device paths with more than 2 underscore delimited sections can't be valid BMP strings. */
			if (underscores >= 2)
				return -1;
			/* Skip over consecutive strings of underscores */
			while (name[offset + 1U] == '_' && offset < name_len)
				++offset;
			/* Bounds check it */
			if (offset == name_len)
				break;
			offsets[underscores++] = offset;
		}
	}

	*serial = extract_serial(name, name_len);
	if (!*serial)
		return -1;

	/* If the device name has no underscores after the prefix, it's an original BMP */
	if (underscores == 0) {
		*version = strdup("Unknown");
		*type = strdup("Native");
	/*
	 * If the device name has two underscores delimted sections after the prefix,
	 * it's a non-native device running the Black Magic Firmware.
	 */
	} else if (underscores == 2) {
		*version = strndup(name + prefix_length, offsets[0] - prefix_length - 1U);
		*type = strndup(name + offsets[0], offsets[1] - offsets[0] - 1U);
	/* Otherwise it's a native BMP */
	} else {
		/* The first section should start with a 'v' indicating the version info */
		if (name[prefix_length] == 'v') {
			*version = strndup(name + prefix_length, offsets[0] - prefix_length - 1U);
			*type = strdup("Native");
		/* But if not then it's actually a non-native device and has no version string. */
		} else {
			*version = strdup("Unknown");
			*type = strndup(name + prefix_length, offsets[0] - prefix_length - 1U);
		}
	}
	return 0;
}

void copy_to_info(bmp_info_t *const info, const char *const type, const char *const version, const char *const serial)
{
	const size_t serial_len = MIN(strlen(serial), sizeof(info->serial) - 1U);
	memcpy(info->serial, serial, serial_len);
	info->serial[serial_len] = '\0';

	const size_t version_len = MIN(strlen(version), sizeof(info->version) - 1U);
	memcpy(info->version, version, version_len);
	info->version[version_len] = '\0';

	const int result = snprintf(info->manufacturer, sizeof(info->manufacturer), "Black Magic Probe (%s)", type);
	if (result)
		DEBUG_WARN("snprintf() overflowed while generating manfacturer string\n");
}

int find_debuggers(BMP_CL_OPTIONS_t *cl_opts, bmp_info_t *info)
{
	if (cl_opts->opt_device)
		return 1;
	info->bmp_type = BMP_TYPE_BMP;
	DIR *dir = opendir(DEVICE_BY_ID);
	if (!dir) /* No serial device connected!*/
		return 0;
	size_t total = 0;
	while (true) {
		const struct dirent *const entry = readdir(dir);
		if (entry == NULL)
			break;
		if (device_is_bmp_gdb_port(entry->d_name)) {
			++total;
			char *type = NULL;
			char *version = NULL;
			char *serial = NULL;
			if (scan_linux_id(entry->d_name, &type, &version, &serial))
				DEBUG_WARN("Unexpected device name found \"%s\"\n", entry->d_name);

			/* If either the (partial) serial matches, or the device is in the right position in the detection order */
			if ((cl_opts->opt_serial && strstr(serial, cl_opts->opt_serial)) ||
				(cl_opts->opt_position && cl_opts->opt_position == total)) {
				copy_to_info(info, type, version, serial);
				total = 1;
				free(type);
				free(version);
				free(serial);
				break;
			}
			free(type);
			free(version);
			free(serial);
		}
	}
	closedir(dir);
	if (!total) {
		DEBUG_WARN("No BMP probe found\n");
		return -1;
	}
	if (total > 1 || cl_opts->opt_list_only)
		DEBUG_WARN("Available Probes:\n");
	dir = opendir(DEVICE_BY_ID);
	size_t i = 0;
	while (true) {
		const struct dirent *const entry = readdir(dir);
		if (entry == NULL)
			break;
		if (device_is_bmp_gdb_port(entry->d_name)) {
			++i;
			char *type = NULL;
			char *version = NULL;
			char *serial = NULL;
			if (scan_linux_id(entry->d_name, &type, &version, &serial)) {
				DEBUG_WARN("Unexpected device name found \"%s\"\n", entry->d_name);
			} else if (total == 1 && !cl_opts->opt_list_only) {
				copy_to_info(info, type, version, serial);
				total = 1;
				free(type);
				free(version);
				free(serial);
				break;
			} else if (total > 0) {
				DEBUG_WARN("%2d: %s, Black Magic Debug, Black Magic "
						   "Probe (%s), %s\n",
					i, serial, type, version);
			}
			free(type);
			free(version);
			free(serial);
		}
	}
	closedir(dir);
	return (total == 1 && !cl_opts->opt_list_only) ? 0 : 1;
}
#endif
