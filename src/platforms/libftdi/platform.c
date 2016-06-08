/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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
#include "gdb_if.h"
#include "version.h"

#include <assert.h>
#include <unistd.h>
#include <sys/time.h>

struct ftdi_context *ftdic;

#define BUF_SIZE 4096
static uint8_t outbuf[BUF_SIZE];
static uint16_t bufptr = 0;

static struct cable_desc_s {
	int vendor;
	int product;
	int interface;
	uint8_t dbus_data;
	uint8_t dbus_ddr;
	uint8_t cbus_data;
	uint8_t cbus_ddr;
	char *description;
	char * name;
} cable_desc[] = {
	{
		.vendor = 0x0403,
		.product = 0x6010,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x1B,
		.description = "FLOSS-JTAG",
		.name = "flossjtag"
	},
	{
		.vendor = 0x0403,
		.product = 0x6010,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x1B,
		.description = "FTDIJTAG",
		.name = "ftdijtag"
	},
	{
		.vendor = 0x15b1,
		.product = 0x0003,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x1B,
		.name = "olimex"
	},
	{
		.vendor = 0x0403,
		.product = 0xbdc8,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x1B,
		.name = "turtelizer"
	},
	{
		.vendor = 0x0403,
		.product = 0xbdc8,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x1B,
		.name = "jtaghs1"
	},
	{
		.vendor = 0x0403,
		.product = 0xbdc8,
		.interface = INTERFACE_A,
		.dbus_data = 0xA8,
		.dbus_ddr  = 0xAB,
		.name = "ftdi"
	},
	{
		.vendor = 0x0403,
		.product = 0x6014,
		.interface = INTERFACE_A,
		.dbus_data = 0x88,
		.dbus_ddr  = 0x8B,
		.cbus_data = 0x20,
		.cbus_ddr  = 0x3f,
		.name = "digilent"
	},
	{
		.vendor = 0x0403,
		.product = 0x6014,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x0B,
		.name = "ft232h"
	},
	{
		.vendor = 0x0403,
		.product = 0x6011,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x0B,
		.name = "ft4232h"
	},
	{
		.vendor = 0x15ba,
		.product = 0x002b,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x1B,
		.cbus_data = 0x00,
		.cbus_ddr  = 0x08,
		.name = "arm-usb-ocd-h"
	},
};

void platform_init(int argc, char **argv)
{
	int err;
	int c;
	unsigned index = 0;
	char *serial = NULL;
	char * cablename =  "ftdi";
	uint8_t ftdi_init[9] = {TCK_DIVISOR, 0x01, 0x00, SET_BITS_LOW, 0,0,
				SET_BITS_HIGH, 0,0};

	while((c = getopt(argc, argv, "c:s:")) != -1) {
		switch(c) {
		case 'c':
			cablename =  optarg;
			break;
		case 's':
			serial = optarg;
			break;
		}
	}

	for(index = 0; index < sizeof(cable_desc)/sizeof(cable_desc[0]);
		index++)
		 if (strcmp(cable_desc[index].name, cablename) == 0)
		 break;

	if (index == sizeof(cable_desc)/sizeof(cable_desc[0])){
		fprintf(stderr, "No cable matching %s found\n",cablename);
		exit(-1);
	}

	if (cable_desc[index].dbus_data)
		ftdi_init[4]= cable_desc[index].dbus_data;
	if (cable_desc[index].dbus_ddr)
		ftdi_init[5]= cable_desc[index].dbus_ddr;
	if (cable_desc[index].cbus_data)
		ftdi_init[7]= cable_desc[index].cbus_data;
	if(cable_desc[index].cbus_ddr)
		ftdi_init[8]= cable_desc[index].cbus_ddr;

	printf("\nBlack Magic Probe (" FIRMWARE_VERSION ")\n");
	printf("Copyright (C) 2015  Black Sphere Technologies Ltd.\n");
	printf("License GPLv3+: GNU GPL version 3 or later "
	       "<http://gnu.org/licenses/gpl.html>\n\n");

	if(ftdic) {
		ftdi_usb_close(ftdic);
		ftdi_free(ftdic);
		ftdic = NULL;
	}
	if((ftdic = ftdi_new()) == NULL) {
		fprintf(stderr, "ftdi_new: %s\n",
			ftdi_get_error_string(ftdic));
		abort();
	}
	if((err = ftdi_set_interface(ftdic, cable_desc[index].interface)) != 0) {
		fprintf(stderr, "ftdi_set_interface: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		abort();
	}
	if((err = ftdi_usb_open_desc(
		ftdic, cable_desc[index].vendor, cable_desc[index].product,
		cable_desc[index].description, serial)) != 0) {
		fprintf(stderr, "unable to open ftdi device: %d (%s)\n",
			err, ftdi_get_error_string(ftdic));
		abort();
	}

	if((err = ftdi_set_latency_timer(ftdic, 1)) != 0) {
		fprintf(stderr, "ftdi_set_latency_timer: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		abort();
	}
	if((err = ftdi_set_baudrate(ftdic, 1000000)) != 0) {
		fprintf(stderr, "ftdi_set_baudrate: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		abort();
	}
	if((err = ftdi_usb_purge_buffers(ftdic)) != 0) {
		fprintf(stderr, "ftdi_set_baudrate: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		abort();
	}
	if((err = ftdi_write_data_set_chunksize(ftdic, BUF_SIZE)) != 0) {
		fprintf(stderr, "ftdi_write_data_set_chunksize: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		abort();
	}

	if((err = ftdi_set_bitmode(ftdic, 0xAB, BITMODE_MPSSE)) != 0) {
		fprintf(stderr, "ftdi_set_bitmode: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		abort();
	}

	assert(ftdi_write_data(ftdic, ftdi_init, 9) == 9);
	assert(gdb_if_init() == 0);
}

void platform_srst_set_val(bool assert)
{
	(void)assert;
	platform_buffer_flush();
}

bool platform_srst_get_val(void) { return false; }

void platform_buffer_flush(void)
{
	assert(ftdi_write_data(ftdic, outbuf, bufptr) == bufptr);
//	printf("FT2232 platform_buffer flush: %d bytes\n", bufptr);
	bufptr = 0;
}

int platform_buffer_write(const uint8_t *data, int size)
{
	if((bufptr + size) / BUF_SIZE > 0) platform_buffer_flush();
	memcpy(outbuf + bufptr, data, size);
	bufptr += size;
	return size;
}

int platform_buffer_read(uint8_t *data, int size)
{
	int index = 0;
	platform_buffer_flush();
	while((index += ftdi_read_data(ftdic, data + index, size-index)) != size);
	return size;
}

#ifdef WIN32
#warning "This vasprintf() is dubious!"
int vasprintf(char **strp, const char *fmt, va_list ap)
{
	int size = 128, ret = 0;

	*strp = malloc(size);
	while(*strp && ((ret = vsnprintf(*strp, size, fmt, ap)) == size))
		*strp = realloc(*strp, size <<= 1);

	return ret;
}
#endif

const char *platform_target_voltage(void)
{
	return "not supported";
}

void platform_delay(uint32_t ms)
{
	usleep(ms * 1000);
}

uint32_t platform_time_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

