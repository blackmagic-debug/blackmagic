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
#include "platform.h"
#include "remote.h"
#include "cli.h"

#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#define NT_DEV_SUFFIX     "\\\\.\\"
#define NT_DEV_SUFFIX_LEN ARRAY_LENGTH(NT_DEV_SUFFIX)

static char *format_string(const char *format, ...) DEBUG_FORMAT_ATTR;

static char *format_string(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	const int len = vsnprintf(NULL, 0, format, args);
	va_end(args);
	if (len <= 0)
		return NULL;
	char *const ret = (char *)malloc(len + 1);
	if (!ret)
		return NULL;
	va_start(args, format);
	vsprintf(ret, format, args);
	va_end(args);
	return ret;
}

#define READ_BUFFER_LENGTH 4096U

/* Windows handle for the connection to the remote BMP */
static HANDLE port_handle = INVALID_HANDLE_VALUE;
/* Buffer for read request data + fullness and next read position values */
static uint8_t read_buffer[READ_BUFFER_LENGTH];
static size_t read_buffer_fullness = 0U;
static size_t read_buffer_offset = 0U;

static void display_error(const LSTATUS error, const char *const operation, const char *const path)
{
	char *message = NULL;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
		error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char *)&message, 0, NULL);
	DEBUG_ERROR("Error %s %s, got error %08lx: %s\n", operation, path, error, message);
	LocalFree(message);
}

static void handle_dev_error(HANDLE device, const char *const operation)
{
	const DWORD error = GetLastError();
	char *message = NULL;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
		error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char *)&message, 0, NULL);
	DEBUG_ERROR("Error %s (%08lx): %s\n", operation, error, message);
	LocalFree(message);
	if (device != INVALID_HANDLE_VALUE)
		CloseHandle(device);
}

static HKEY open_hklm_registry_path(const char *const path, const REGSAM permissions)
{
	HKEY handle = INVALID_HANDLE_VALUE;
	const LSTATUS result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, path, 0, permissions, &handle);
	if (result != ERROR_SUCCESS) {
		display_error(result, "opening registry key", path);
		return INVALID_HANDLE_VALUE;
	}
	return handle;
}

static char *read_key_from_path(const char *const subpath, const char *const key_name)
{
	char *key_path = format_string(
		"SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_%04X&PID_%04X%s", VENDOR_ID_BMP, PRODUCT_ID_BMP, subpath);
	if (!key_path)
		return NULL;

	HKEY key_path_handle = open_hklm_registry_path(key_path, KEY_READ);
	free(key_path);
	if (key_path_handle == INVALID_HANDLE_VALUE)
		return NULL;

	DWORD value_len = 0;
	const LSTATUS result = RegGetValue(key_path_handle, NULL, key_name, RRF_RT_REG_SZ, NULL, NULL, &value_len);
	if (result != ERROR_SUCCESS && result != ERROR_MORE_DATA) {
		display_error(result, "retrieving value for key", key_name);
		RegCloseKey(key_path_handle);
		return NULL;
	}

	char *value = calloc(1, value_len);
	if (!value) {
		DEBUG_ERROR("Could not allocate sufficient memory for key value\n");
		RegCloseKey(key_path_handle);
		return NULL;
	}
	assert(RegGetValue(key_path_handle, NULL, key_name, RRF_RT_REG_SZ, NULL, value, &value_len) == ERROR_SUCCESS);
	RegCloseKey(key_path_handle);
	return value;
}

static char *find_bmp_by_serial(const char *serial)
{
	char *serial_path = format_string("\\%s", serial);
	if (!serial_path)
		return NULL;
	char *prefix = read_key_from_path(serial_path, "ParentIdPrefix");
	free(serial_path);
	if (!prefix)
		return NULL;
	DEBUG_INFO("prefix: %s\n", prefix);

	char *parameter_path = format_string("&MI_00\\%s&0000\\Device Parameters", prefix);
	if (!parameter_path) {
		free(prefix);
		return NULL;
	}
	char *port_name = read_key_from_path(parameter_path, "PortName");
	free(prefix);
	if (!port_name)
		return NULL;
	DEBUG_WARN("Using BMP at %s\n", port_name);
	return port_name;
}

static char *device_to_path(const char *const device)
{
	if (memcmp(device, NT_DEV_SUFFIX, NT_DEV_SUFFIX_LEN - 1U) == 0)
		return strdup(device);

	const size_t path_len = strlen(device) + NT_DEV_SUFFIX_LEN;
	char *const path = malloc(path_len);
	memcpy(path, NT_DEV_SUFFIX, NT_DEV_SUFFIX_LEN);
	memcpy(path + NT_DEV_SUFFIX_LEN - 1U, device, path_len - NT_DEV_SUFFIX_LEN);
	path[path_len - 1U] = '\0';
	return path;
}

static char *find_bmp_device(const bmda_cli_options_s *const cl_opts, const char *const serial)
{
	if (cl_opts->opt_device)
		return device_to_path(cl_opts->opt_device);
	char *const device = find_bmp_by_serial(serial);
	if (!device)
		return NULL;
	char *const result = device_to_path(device);
	free(device);
	return result;
}

bool serial_open(const bmda_cli_options_s *const cl_opts, const char *const serial)
{
	/* Figure out what the device node is for the requested device */
	char *const device = find_bmp_device(cl_opts, serial);
	if (!device) {
		DEBUG_ERROR("Unexpected problems finding the device!\n");
		return false;
	}

	/* Try and open the node so we can start communications with the the device */
	port_handle = CreateFile(device,                    /* NT path to the device */
		GENERIC_READ | GENERIC_WRITE,                   /* Read + Write */
		0,                                              /* No Sharing */
		NULL,                                           /* Default security attributes */
		OPEN_EXISTING,                                  /* Open an existing device only */
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, /* Normal I/O without buffering */
		NULL);                                          /* Do not use a template file */
	free(device);

	/* If opening the device node failed for any reason, error out early */
	if (port_handle == INVALID_HANDLE_VALUE) {
		handle_dev_error(port_handle, "opening device");
		return false;
	}

	/* Get the current device state from the device */
	DCB serial_params = {0};
	serial_params.DCBlength = sizeof(serial_params);
	if (!GetCommState(port_handle, &serial_params)) {
		handle_dev_error(port_handle, "getting communication state from device");
		return false;
	}

	/* Adjust the device state to enable communications to work and be in the right mode */
	serial_params.fParity = FALSE;
	serial_params.fOutxCtsFlow = FALSE;
	serial_params.fOutxDsrFlow = FALSE;
	serial_params.fDtrControl = DTR_CONTROL_ENABLE;
	serial_params.fDsrSensitivity = FALSE;
	serial_params.fOutX = FALSE;
	serial_params.fInX = FALSE;
	serial_params.fRtsControl = RTS_CONTROL_DISABLE;
	serial_params.ByteSize = 8;
	serial_params.Parity = NOPARITY;
	if (!SetCommState(port_handle, &serial_params)) {
		handle_dev_error(port_handle, "setting communication state on device");
		return false;
	}

	COMMTIMEOUTS timeouts = {0};
	/*
	 * Turn off read timeouts so that ReadFill() instantly returns even if there's no data waiting
	 * (we implement our own mechanism below for that case as we only want to wait if we get no data)
	 */
	timeouts.ReadIntervalTimeout = MAXDWORD;
	timeouts.ReadTotalTimeoutConstant = 0;
	timeouts.ReadTotalTimeoutMultiplier = 0;
	/*
	 * Configure an exactly 100ms write timeout - we want this triggering to be fatal as something
	 * has gone very wrong if we ever hit this.
	 */
	timeouts.WriteTotalTimeoutConstant = 100;
	timeouts.WriteTotalTimeoutMultiplier = 0;
	if (!SetCommTimeouts(port_handle, &timeouts)) {
		handle_dev_error(port_handle, "setting communication timeouts for device");
		return false;
	}

	/* Having adjusted the line state, discard anything sat in the receive buffer */
	PurgeComm(port_handle, PURGE_RXCLEAR);
	return true;
}

void serial_close(void)
{
	CloseHandle(port_handle);
	port_handle = INVALID_HANDLE_VALUE;
}

bool platform_buffer_write(const void *const data, const size_t length)
{
	const char *const buffer = (const char *)data;
	DEBUG_WIRE("%s\n", buffer);
	DWORD written = 0;
	for (size_t offset = 0; offset < length; offset += written) {
		if (!WriteFile(port_handle, buffer + offset, length - offset, &written, NULL)) {
			DEBUG_ERROR("Serial write failed %lu, written %zu\n", GetLastError(), offset);
			return false;
		}
	}
	return true;
}

static ssize_t bmda_read_more_data(const uint32_t end_time)
{
	// Try to wait for up to 100ms for data to become available
	if (WaitForSingleObject(port_handle, 100) != WAIT_OBJECT_0) {
		DEBUG_ERROR("Timeout while waiting for BMP response: %lu\n", GetLastError());
		return -4;
	}
	DWORD bytes_received = 0;
	/* Try to fill the read buffer, and if that fails, bail */
	if (!ReadFile(port_handle, read_buffer, READ_BUFFER_LENGTH, &bytes_received, NULL)) {
		DEBUG_ERROR("Failed to read response: %lu\n", GetLastError());
		return -3;
	}
	/* If we timed out, bail differently */
	if (platform_time_ms() > end_time) {
		DEBUG_ERROR("Timeout while waiting for BMP response\n");
		return -4;
	}
	/* We now have more data, so update the read buffer counters */
	read_buffer_fullness = bytes_received;
	read_buffer_offset = 0U;
	return 0;
}

/* XXX: We should either return size_t or bool */
int platform_buffer_read(void *const data, const size_t length)
{
	char *const buffer = (char *)data;
	const uint32_t start_time = platform_time_ms();
	const uint32_t end_time = start_time + cortexm_wait_timeout;
	/* Drain the buffer for the remote till we see a start-of-response byte */
	for (char response = 0; response != REMOTE_RESP;) {
		while (read_buffer_offset == read_buffer_fullness) {
			const ssize_t result = bmda_read_more_data(end_time);
			if (result < 0)
				return result;
		}
		response = read_buffer[read_buffer_offset++];
	}
	/* Now collect the response */
	for (size_t offset = 0; offset < length;) {
		/* Check if we've exceeded the allowed time */
		if (platform_time_ms() >= end_time) {
			DEBUG_ERROR("Failed to read response after %ums\n", platform_time_ms() - start_time);
			return -4;
		}
		/* Check if we need more data or should use what's in the buffer already */
		if (read_buffer_offset == read_buffer_fullness) {
			const ssize_t result = bmda_read_more_data(end_time);
			if (result < 0)
				return result;
		}
		/* Look for an end of packet marker */
		size_t response_length = 0U;
		for (; read_buffer_offset + response_length < read_buffer_fullness && offset + response_length < length;
			 ++response_length) {
			/* If we've found a REMOTE_EOM then stop scanning */
			if (read_buffer[read_buffer_offset + response_length] == REMOTE_EOM) {
				++response_length;
				break;
			}
		}
		/* We now either have a REMOTE_EOM or need all the data from the buffer */
		memcpy(buffer + offset, read_buffer + read_buffer_offset, response_length);
		read_buffer_offset += response_length;
		offset += response_length - 1U;
		/* If this was because of REMOTE_EOM, return */
		if (buffer[offset] == REMOTE_EOM) {
			buffer[offset] = 0;
			DEBUG_WIRE("       %s\n", buffer);
			return offset;
		}
		++offset;
	}
	return length;
}
