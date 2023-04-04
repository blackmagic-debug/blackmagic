/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Copyright (C) 2023 Sid Price <sid@sidprice.com>
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

struct ftdi_context *ftdi_new(void)
{
	return NULL;
}

int ftdi_set_interface(struct ftdi_context *ftdi, enum ftdi_interface interface)
{
	(void)ftdi;
	(void)interface;
	return 1;
}

int ftdi_usb_open_desc(struct ftdi_context *ftdi, int vendor, int product, const char *description, const char *serial)
{
	(void)ftdi;
	(void)vendor;
	(void)product;
	(void)description;
	(void)serial;
	return 1;
}

int ftdi_usb_close(struct ftdi_context *ftdi)
{
	(void)ftdi;
	return 1;
}

void ftdi_free(struct ftdi_context *ftdi)
{
	(void)ftdi;
}

int ftdi_set_baudrate(struct ftdi_context *ftdi, int baudrate)
{
	(void)ftdi;
	(void)baudrate;
	return 1;
}

int ftdi_set_latency_timer(struct ftdi_context *ftdi, unsigned char latency)
{
	(void)ftdi;
	(void)latency;
	return 1;
}

int ftdi_set_bitmode(struct ftdi_context *ftdi, unsigned char bitmask, unsigned char mode)
{
	(void)ftdi;
	(void)bitmask;
	(void)mode;
	return 1;
}

int ftdi_usb_purge_buffers(struct ftdi_context *ftdi)
{
	(void)ftdi;
	return 1;
}

int ftdi_read_data(struct ftdi_context *ftdi, unsigned char *buf, int size)
{
	(void)ftdi;
	(void)buf;
	(void)size;
	return 1;
}

int ftdi_write_data(struct ftdi_context *ftdi, const unsigned char *buf, int size)
{
	(void)ftdi;
	(void)buf;
	(void)size;
	return 1;
}

int ftdi_write_data_set_chunksize(struct ftdi_context *ftdi, unsigned int chunksize)
{
	(void)ftdi;
	(void)chunksize;
	return 1;
}

const char *ftdi_get_error_string(struct ftdi_context *ftdi)
{
	(void)ftdi;
	return "Oops";
}
