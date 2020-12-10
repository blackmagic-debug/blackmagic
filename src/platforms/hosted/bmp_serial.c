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
int find_debuggers(BMP_CL_OPTIONS_t *cl_opts, bmp_info_t *info)
{
	(void)cl_opts;
	(void)info;
	return -1;
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
