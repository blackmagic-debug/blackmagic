/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Sid Price <sid@sidprice.com>
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
#include "ftdi.h"
#include "ftd2xx.h"
#include "bmp_hosted.h"

FT_HANDLE ftdi_handle;

/*
 * The following structure is mocked by this module. FTD2XX
 * does not provide a similar structure.
 *
 * Values mocked:
 *     ftdi_ctx.type - The type of FTDI chip in the adapter
 */

/* Used to fake the libusb context and pass required parameters back to the caller */
static ftdi_context_s ftdi_ctx = {0};

/*
 * This array is used to map FTD2XX device type identifiers
 * to libftdi identifiers. The array is ordered by the FTD2XX
 * values, with the array entries being the libftdi values
 */
static const int ftdi_chip_types[] = {
	TYPE_AM,
	TYPE_BM,
	-1, // FT_DEVICE_100AX not supported
	-2, // Unknown type
	TYPE_2232C,
	TYPE_R,
	TYPE_2232H,
	TYPE_4232H,
	TYPE_232H,
	TYPE_230X,
};

static const size_t number_of_ftdi_chip_types = ARRAY_LENGTH(ftdi_chip_types);

#define READ_TIMEOUT  500 // Expressed in milliseconds
#define WRITE_TIMEOUT 500

struct ftdi_context *ftdi_new(void)
{
	return &ftdi_ctx; // Just need to fake the structure being created
}

int ftdi_set_interface(ftdi_context_s *ftdi, enum ftdi_interface interface)
{
	(void)ftdi;
	/*
	 * FTD2XX needs a qualified serial number to open the correct device. Append
	 * an interface letter to the serial number by adding the number to 'A'
	 */
	char serial_number[16] = {0};
	strcpy(serial_number, bmda_probe_info.serial);
	serial_number[strlen(serial_number)] = 'A' + (interface - 1);

	if (FT_OpenEx(serial_number, FT_OPEN_BY_SERIAL_NUMBER, &ftdi_handle) != FT_OK ||
		FT_SetTimeouts(ftdi_handle, READ_TIMEOUT, WRITE_TIMEOUT) != FT_OK)
		return 0;

	FT_DEVICE device;
	DWORD device_id = 0;
	char description[64] = {'\0'};
	if (FT_GetDeviceInfo(ftdi_handle, &device, &device_id, serial_number, description, NULL) != FT_OK)
		return 0;

	const size_t device_type_index = device;
	if (device_type_index < number_of_ftdi_chip_types)
		ftdi_ctx.type = ftdi_chip_types[device_type_index];
	return 0;
}

int ftdi_usb_open_desc(struct ftdi_context *ftdi, int vendor, int product, const char *description, const char *serial)
{
	(void)ftdi;
	(void)vendor;
	(void)product;
	(void)description;
	(void)serial;
	return 0;
}

int ftdi_usb_close(struct ftdi_context *ftdi)
{
	(void)ftdi;
	if (FT_Close(ftdi_handle) != FT_OK)
		return -1;
	return 0;
}

void ftdi_free(struct ftdi_context *ftdi)
{
	(void)ftdi;
}

int ftdi_set_baudrate(struct ftdi_context *ftdi, int baudrate)
{
	(void)ftdi;
	return FT_SetBaudRate(ftdi_handle, baudrate) != FT_OK;
}

int ftdi_set_latency_timer(struct ftdi_context *ftdi, unsigned char latency)
{
	(void)ftdi;
	return FT_SetLatencyTimer(ftdi_handle, latency) != FT_OK;
}

int ftdi_set_bitmode(struct ftdi_context *ftdi, unsigned char bitmask, unsigned char mode)
{
	(void)ftdi;
	return FT_SetBitMode(ftdi_handle, bitmask, mode) != FT_OK;
}

int ftdi_usb_purge_buffers(struct ftdi_context *ftdi)
{
	(void)ftdi;
	return FT_Purge(ftdi_handle, FT_PURGE_RX | FT_PURGE_TX) != FT_OK;
}

int ftdi_read_data(struct ftdi_context *ftdi, unsigned char *buf, const int size)
{
	(void)ftdi;
	DWORD bytes_read = 0;
	if (FT_Read(ftdi_handle, buf, (DWORD)size, &bytes_read) == FT_OK && bytes_read != (DWORD)size)
		return 0; // Signal read timeout
	return bytes_read;
}

int ftdi_write_data(struct ftdi_context *ftdi, const unsigned char *buf, int size)
{
	(void)ftdi;
	DWORD bytes_written;
	if (FT_Write(ftdi_handle, (unsigned char *)buf, size, &bytes_written) != FT_OK)
		return 0;
	return bytes_written;
}

int ftdi_write_data_set_chunksize(struct ftdi_context *ftdi, unsigned int chunksize)
{
	(void)ftdi;
	(void)chunksize;
	return 0;
}

const char *ftdi_get_error_string(struct ftdi_context *ftdi)
{
	(void)ftdi;
	return "Error in ftdi.c (Windows)";
}
