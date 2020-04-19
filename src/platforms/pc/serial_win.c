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

#include <sys/types.h>
#include "general.h"
#include <windows.h>
#include "remote.h"
#include "cl_utils.h"

HANDLE hComm;

int serial_open(BMP_CL_OPTIONS_t *cl_opts, char * serial)
{
	(void) serial; /* FIXME: Does Windows allow open with USB serial no? */
	if (!cl_opts->opt_device) {
		fprintf(stderr,"Specify the serial device to use!\n");
		return -1;
	}
	char device[256];
	if (strstr(device, "\\\\.\\")) {
		strncpy(device, cl_opts->opt_device, sizeof(cl_opts->opt_device) - 1);
	} else {
		strcpy(device,  "\\\\.\\");
		strncat(device, cl_opts->opt_device,
				sizeof(cl_opts->opt_device) - strlen(cl_opts->opt_device) - 1);
	}
	hComm = CreateFile(device,                //port name
                      GENERIC_READ | GENERIC_WRITE, //Read/Write
                      0,                            // No Sharing
                      NULL,                         // No Security
                      OPEN_EXISTING,// Open existing port only
                      0,            // Non Overlapped I/O
                      NULL);        // Null for Comm Devices}
	if (hComm == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "Could not open %s: %ld\n", device,
				GetLastError());
		return -1;
	}
	DCB dcbSerialParams;
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
	if (!GetCommState(hComm, &dcbSerialParams)) {
		fprintf(stderr, "GetCommState failed %ld\n", GetLastError());
		return -1;
	}
	dcbSerialParams.ByteSize = 8;
	if (!SetCommState(hComm, &dcbSerialParams)) {
		fprintf(stderr, "SetCommState failed %ld\n", GetLastError());
		return -1;
	}
	COMMTIMEOUTS timeouts = {0};
	timeouts.ReadIntervalTimeout         = 10;
	timeouts.ReadTotalTimeoutConstant    = 10;
	timeouts.ReadTotalTimeoutMultiplier  = 10;
	timeouts.WriteTotalTimeoutConstant   = 10;
	timeouts.WriteTotalTimeoutMultiplier = 10;
	if (!SetCommTimeouts(hComm, &timeouts)) {
		fprintf(stderr, "SetCommTimeouts failed %ld\n", GetLastError());
		return -1;
	}
	return 0;
}

void serial_close(void)
{
	CloseHandle(hComm);
}

int platform_buffer_write(const uint8_t *data, int size)
{
	if (cl_debuglevel)
		printf("%s\n",data);
	int s = 0;

	do {
		DWORD written;
		if (!WriteFile(hComm, data + s, size - s, &written, NULL)) {
			fprintf(stderr, "Serial write failed %ld, written %d\n",
					GetLastError(), s);
			return -1;
		}
		s += written;
	} while (s < size);
	return 0;
}
int platform_buffer_read(uint8_t *data, int maxsize)
{
	DWORD s;
	uint8_t response = 0;
	uint32_t startTime = platform_time_ms();
	uint32_t endTime = platform_time_ms() + RESP_TIMEOUT;
	do {
		if (!ReadFile(hComm, &response, 1, &s, NULL)) {
			fprintf(stderr,"ERROR on read RESP\n");
			exit(-3);
		}
		if (platform_time_ms() > endTime) {
			fprintf(stderr,"Timeout on read RESP\n");
			exit(-4);
		}
	} while (response != REMOTE_RESP);
	uint8_t *c = data;
	do {
		if (!ReadFile(hComm, c, 1, &s, NULL)) {
			fprintf(stderr,"Error on read\n");
			exit(-3);
		}
		if (s > 0 ) {
			if (cl_debuglevel)
				printf("%c", *c);
			if (*c == REMOTE_EOM) {
				*c = 0;
				if (cl_debuglevel)
					printf("\n");
				return (c - data);
			} else {
				c++;
			}
		}
	} while (((c - data) < maxsize) && (platform_time_ms() < endTime));
	fprintf(stderr,"Failed to read EOM at %d\n",
			platform_time_ms() - startTime);
	exit(-3);
	return 0;
}
