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

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include "general.h"
#include "platform.h"
#include "bmp_hosted.h"
#include "version.h"

void bmp_ident(bmp_info_t *info)
{
	(void) info;
	DEBUG_INFO("BMP hosted (BMP Only) %s\n", FIRMWARE_VERSION);
}

void libusb_exit_function(bmp_info_t *info) {(void)info;};


#ifdef __APPLE__
int find_debuggers(BMP_CL_OPTIONS_t *cl_opts, bmp_info_t *info)
{
	(void)cl_opts;
	(void)info;
	return -1;
}
#elif defined(__WIN32__) || defined(__CYGWIN__)


/* This source has been used as an example:
 * https://stackoverflow.com/questions/3438366/setupdigetdeviceproperty-usage-example */

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>   // for MAX_DEVICE_ID_LEN, CM_Get_Parent and CM_Get_Device_ID
#include <tchar.h>
#include <stdio.h>

/* include DEVPKEY_Device_BusReportedDeviceDesc from WinDDK\7600.16385.1\inc\api\devpropdef.h */
#ifdef DEFINE_DEVPROPKEY
#undef DEFINE_DEVPROPKEY
#endif
#define DEFINE_DEVPROPKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) const DEVPROPKEY DECLSPEC_SELECTANY name = { { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }, pid }

/* include DEVPKEY_Device_BusReportedDeviceDesc from WinDDK\7600.16385.1\inc\api\devpkey.h */
DEFINE_DEVPROPKEY(DEVPKEY_Device_BusReportedDeviceDesc,  0x540b947e, 0x8b40, 0x45bc, 0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2, 4);     // DEVPROP_TYPE_STRING

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
	TCHAR szDeviceInstanceID [MAX_DEVICE_ID_LEN];
	WCHAR busReportedDeviceSesc[4096];
	int probes_found = 0;
	bool is_printing_probes_info = cl_opts->opt_list_only != 0;

	info->bmp_type = BMP_TYPE_BMP;

	hDevInfo = SetupDiGetClassDevs (0, "USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	if (hDevInfo == INVALID_HANDLE_VALUE)
		return -1;
print_probes_info:
	for (i = 0; ; i++)  {
		char serial_number[sizeof info->serial];
		DeviceInfoData.cbSize = sizeof (DeviceInfoData);
		if (!SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData))
			break;

		status = CM_Get_Device_ID(DeviceInfoData.DevInst, szDeviceInstanceID , MAX_PATH, 0);
		if (status != CR_SUCCESS)
			continue;

		if (!sscanf(szDeviceInstanceID, "USB\\VID_1D50&PID_6018\\%s", serial_number))
			continue;

		if (SetupDiGetDevicePropertyW (hDevInfo, &DeviceInfoData, &DEVPKEY_Device_BusReportedDeviceDesc,
		       &ulPropertyType, (BYTE*)busReportedDeviceSesc, sizeof busReportedDeviceSesc, &dwSize, 0))
		{
			probes_found ++;
			if (is_printing_probes_info)
			{
				DEBUG_WARN("%2d: %s, %ls\n", probes_found,
					   serial_number, busReportedDeviceSesc);
			}
			else
			{
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
#define BMP_IDSTRING "usb-Black_Sphere_Technologies_Black_Magic_Probe"
#define DEVICE_BY_ID "/dev/serial/by-id/"

/*
 * Extract type, version and serial from /dev/serial/by_id
 * Return 0 on success
 *
 * Old versions have different strings. Try to cope!
 */
static int scan_linux_id(char *name, char *type, char *version, char  *serial)
{
	name += strlen(BMP_IDSTRING) + 1;
	while (*name == '_')
		name++;
	if (!*name) {
		DEBUG_WARN("Unexpected end\n");
		return -1;
	}
	char *p = strchr(name, '_');
	if (!p) {
		DEBUG_WARN("type not found\n");
		return -1;
	}
	strncpy(type, name, p - name);
	name = p;
	while (*name != 'v')
		name++;
	if (!*name) {
		DEBUG_WARN("Unexpected end after type\n");
		return -1;
	}
	p = strchr(name, '_');
	if (!p) {
		DEBUG_WARN("version not found\n");
		return -1;
	}
	strncpy(version, name, p - name);
	name = p;
	while (*name == '_')
		name++;
	if (!*name) {
		DEBUG_WARN("Unexpected end after version\n");
		return -1;
	}
	p = strchr(name, '-');
	if (!p) {
		DEBUG_WARN("Serial not found\n");
		return -1;
	}
	strncpy(serial, name, p - name);
	return 0;
}

int find_debuggers(BMP_CL_OPTIONS_t *cl_opts, bmp_info_t *info)
{
	char name[4096];
	if (cl_opts->opt_device)
		return 1;
	info->bmp_type = BMP_TYPE_BMP;
	DIR *dir = opendir(DEVICE_BY_ID);
	if (!dir) {
		DEBUG_WARN("Could not opendir %s: %s\n", name, strerror(errno));
		return -1;
	}
	int found_bmps = 0;
	struct dirent *dp;
	int i = 0;
	while ((dp = readdir(dir)) != NULL) {
		if ((strstr(dp->d_name, BMP_IDSTRING)) &&
			(strstr(dp->d_name, "-if00"))) {
			i++;
			char type[256], version[256], serial[256];
			if (scan_linux_id(dp->d_name, type, version, serial)) {
				DEBUG_WARN("Unexpected device name found \"%s\"\n",
						   dp->d_name);
			}
			if ((cl_opts->opt_serial && strstr(serial, cl_opts->opt_serial)) ||
				(cl_opts->opt_position && cl_opts->opt_position == i)) {
				/* With serial number given and partial match, we are done!*/
				strncpy(info->serial, serial, sizeof(info->serial));
				strncpy(info->manufacturer, "BMP", sizeof(info->manufacturer));
				strncpy(info->product, type, sizeof(info->product));
				strncpy(info->version, version, sizeof(info->version));
				found_bmps = 1;
				break;
			} else {
				found_bmps++;
			}
		}
	}
	closedir(dir);
	if (found_bmps < 1) {
		DEBUG_WARN("No BMP probe found\n");
		return -1;
	} else if (found_bmps > 1) {
		DEBUG_INFO("Available Probes:\n");
	}
	dir = opendir(DEVICE_BY_ID);
	i = 0;
	while ((dp = readdir(dir)) != NULL) {
		if ((strstr(dp->d_name, BMP_IDSTRING)) &&
			(strstr(dp->d_name, "-if00"))) {
			i++;
			char type[256], version[256], serial[256];
			if (scan_linux_id(dp->d_name, type, version, serial)) {
				DEBUG_WARN("Unexpected device name found \"%s\"\n",
						   dp->d_name);
			} else if (found_bmps == 1) {
				strncpy(info->serial, serial, sizeof(info->serial));
				found_bmps = 1;
				strncpy(info->serial, serial, sizeof(info->serial));
				strncpy(info->manufacturer, "BMP", sizeof(info->manufacturer));
				strncpy(info->product, type, sizeof(info->product));
				strncpy(info->version, version, sizeof(info->version));
				break;
			} else if (found_bmps > 1) {
				DEBUG_WARN("%2d: %s, Black Sphere Technologies, Black Magic "
						   "Probe (%s), %s\n", i, serial, type, version);
			}
		}
	}
	closedir(dir);
	return (found_bmps == 1) ? 0 : 1;
}
#endif
