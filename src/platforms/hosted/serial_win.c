/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020  Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

#include "general.h"
#include <windows.h>
#include "remote.h"
#include "cli.h"

static HANDLE port_handle;

static char *find_bmp_by_serial(const char *serial)
{
	char regpath[258];
	/* First find the containers of the BMP comports */
	sprintf(
		regpath, "SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_%04X&PID_%04X\\%s", VENDOR_ID_BMP, PRODUCT_ID_BMP, serial);
	HKEY hkeySection;
	LSTATUS res;
	res = RegOpenKeyEx(HKEY_LOCAL_MACHINE, regpath, 0, KEY_READ, &hkeySection);
	if (res != ERROR_SUCCESS)
		return NULL;
	BYTE prefix[128];
	DWORD maxlen = sizeof(prefix);
	res = RegQueryValueEx(hkeySection, "ParentIdPrefix", NULL, NULL, prefix, &maxlen);
	RegCloseKey(hkeySection);
	if (res != ERROR_SUCCESS)
		return NULL;
	printf("prefix %s\n", prefix);
	sprintf(regpath,
		"SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_%04X&PID_%04X&MI_00\\%s"
		"&0000\\Device Parameters",
		VENDOR_ID_BMP, PRODUCT_ID_BMP, prefix);
	printf("%s\n", regpath);
	res = RegOpenKeyEx(HKEY_LOCAL_MACHINE, regpath, 0, KEY_READ, &hkeySection);
	if (res != ERROR_SUCCESS) {
		printf("Failuere\n");
		return NULL;
	}
	BYTE port[128];
	maxlen = sizeof(port);
	res = RegQueryValueEx(hkeySection, "PortName", NULL, NULL, port, &maxlen);
	RegCloseKey(hkeySection);
	if (res != ERROR_SUCCESS)
		return NULL;
	printf("Portname %s\n", port);
	return strdup((char *)port);
}

int serial_open(const BMP_CL_OPTIONS_t *const cl_opts, const char *const serial)
{
	char device[256];
	if (!cl_opts->opt_device)
		cl_opts->opt_device = find_bmp_by_serial(serial);
	if (!cl_opts->opt_device) {
		DEBUG_WARN("Unexpected problems finding the device!\n");
		return -1;
	}
	if (strstr(cl_opts->opt_device, "\\\\.\\")) {
		strncpy(device, cl_opts->opt_device, sizeof(device) - 1);
	} else {
		strcpy(device, "\\\\.\\");
		strncat(device, cl_opts->opt_device, sizeof(device) - strlen(device) - 1);
	}
	port_handle = CreateFile(device,        // NT path to the port
		GENERIC_READ | GENERIC_WRITE, // Read/Write
		0,                            // No Sharing
		NULL,                         // No Security
		OPEN_EXISTING,                // Open existing port only
		0,                            // Non Overlapped I/O
		NULL);                        // Null for Comm Devices
	if (port_handle == INVALID_HANDLE_VALUE) {
		DEBUG_WARN("Could not open %s: %ld\n", device, GetLastError());
		return -1;
	}
	DCB serial_params;
	memset(&serial_params, 0, sizeof(DCB));
	serial_params.DCBlength = sizeof(serial_params);
	if (!GetCommState(port_handle, &serial_params)) {
		DEBUG_WARN("GetCommState failed %ld\n", GetLastError());
		return -1;
	}
	serial_params.ByteSize = 8;
	serial_params.fDtrControl = DTR_CONTROL_ENABLE;
	if (!SetCommState(port_handle, &serial_params)) {
		DEBUG_WARN("SetCommState failed %ld\n", GetLastError());
		return -1;
	}
	COMMTIMEOUTS timeouts = {0};
	timeouts.ReadIntervalTimeout = 10;
	timeouts.ReadTotalTimeoutConstant = 10;
	timeouts.ReadTotalTimeoutMultiplier = 10;
	timeouts.WriteTotalTimeoutConstant = 10;
	timeouts.WriteTotalTimeoutMultiplier = 10;
	if (!SetCommTimeouts(port_handle, &timeouts)) {
		DEBUG_WARN("SetCommTimeouts failed %ld\n", GetLastError());
		return -1;
	}
	return 0;
}

void serial_close(void)
{
	CloseHandle(port_handle);
}

/* XXX: This should return bool and the size parameter should be size_t as it cannot be negative. */
int platform_buffer_write(const uint8_t *data, int size)
{
	DEBUG_WIRE("%s\n", data);
	DWORD written = 0;
	for (size_t offset = 0; offset < (size_t)size; offset += written)
		if (!WriteFile(port_handle, data + offset, size - offset, &written, NULL)) {
			DEBUG_WARN("Serial write failed %lu, written %\u\n", GetLastError(), offset);
			return -1;
		}
		offset += written;
	}
	return 0;
}

/* XXX: The size parameter should be size_t and we should either return size_t or bool */
/* XXX: This needs documenting that it can abort the program with exit(), or the error handling fixed */
int platform_buffer_read(uint8_t *data, int maxsize)
{
	DWORD read = 0;
	char response = 0;
	const uint32_t start_time = platform_time_ms();
	const uint32_t end_time = start_time + cortexm_wait_timeout;
	/* Drain the buffer for the remote till we see a start-of-response byte */
	while (response != REMOTE_RESP) {
		if (!ReadFile(port_handle, &response, 1, &read, NULL)) {
			DEBUG_WARN("error occured while reading response: %lu\n", GetLastError());
			exit(-3);
		}
		if (platform_time_ms() > end_time) {
			DEBUG_WARN("Timeout while waiting for BMP response\n");
			exit(-4);
		}
	}
	/* Now collect the response */
	for (size_t offset = 0; offset < (size_t)maxsize && platform_time_ms() < end_time;) {
		if (!ReadFile(port_handle, data + offset, 1, &read, NULL)) {
			DEBUG_WARN("Error on read\n");
			exit(-3);
		}
		if (read > 0) {
			DEBUG_WIRE("%c", data[offset]);
			if (data[offset] == REMOTE_EOM) {
				data[offset] = 0;
				DEBUG_WIRE("\n");
				return offset;
			}
			++offset
		}
	}
	DEBUG_WARN("Failed to read EOM at %u\n", platform_time_ms() - start_time);
	exit(-3);
	return 0;
}
