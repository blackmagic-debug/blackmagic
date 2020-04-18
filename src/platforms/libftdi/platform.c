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
#include "platform.h"
#include "target.h"

#include <assert.h>
#include <unistd.h>
#include <sys/time.h>

struct ftdi_context *ftdic;

#include "cl_utils.h"

#define BUF_SIZE 4096
static uint8_t outbuf[BUF_SIZE];
static uint16_t bufptr = 0;

cable_desc_t *active_cable;

cable_desc_t cable_desc[] = {
	{
		/* Direct connection from FTDI to Jtag/Swd.*/
		.vendor = 0x0403,
		.product = 0x6010,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x1B,
		.bitbang_tms_in_port_cmd = GET_BITS_LOW,
		.bitbang_tms_in_pin = MPSSE_TMS,
		.description = "FLOSS-JTAG",
		.name = "flossjtag"
	},
	{
		/* Buffered connection from FTDI to Jtag/Swd.
		 * TCK and TMS not independant switchable!
		 * SWD not possible. */
		.vendor = 0x0403,
		.product = 0x6010,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x1B,
		.description = "FTDIJTAG",
		.name = "ftdijtag"
	},
	{
/* UART/SWO on Interface A
 * JTAG and control on INTERFACE_B
 * Bit 5 high selects SWD-READ (TMS routed to TDO)
 * Bit 6 high selects JTAG vs SWD (TMS routed to TDI/TDO)
 * BCBUS 1 (Output) N_SRST
 * BCBUS 2 (Input) V_ISO available
 *
 * For bitbanged SWD, set Bit 5 low and select SWD read with
 * Bit 6 low. Read Connector TMS as FTDI TDO.
 *
 * TDO is routed to Interface 0 RXD as SWO or with Uart
 * Connector pin 10 pulled to ground will connect Interface 0 RXD
 * to UART connector RXD
 */
		.vendor = 0x0403,
		.product = 0x6010,
		.interface = INTERFACE_B,
		.dbus_data = 0x6A,
		.dbus_ddr  = 0x6B,
		.cbus_data = 0x02,
		.cbus_ddr  = 0x02,
		.bitbang_tms_in_port_cmd = GET_BITS_LOW,
		.bitbang_tms_in_pin = MPSSE_TDO, /* keep bit 5 low*/
		.bitbang_swd_dbus_read_data = 0x02,
		.name = "ftdiswd"
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
		/* Buffered connection from FTDI to Jtag/Swd.
		 * TCK and TMS not independant switchable!
		 * => SWD not possible. */
		.vendor = 0x0403,
		.product = 0xbdc8,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x1B,
		.name = "turtelizer"
	},
	{
		/* https://reference.digilentinc.com/jtag_hs1/jtag_hs1
		 * No schmeatics available.
		 * Buffered from FTDI to Jtag/Swd announced
		 * Independant switch for TMS not known
		 * => SWD not possible. */
		.vendor = 0x0403,
		.product = 0xbdc8,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x1B,
		.name = "jtaghs1"
	},
	{
		/* Direct connection from FTDI to Jtag/Swd assumed.*/
		.vendor = 0x0403,
		.product = 0xbdc8,
		.interface = INTERFACE_A,
		.dbus_data = 0xA8,
		.dbus_ddr  = 0xAB,
		.bitbang_tms_in_port_cmd = GET_BITS_LOW,
		.bitbang_tms_in_pin = MPSSE_TMS,
		.name = "ftdi"
	},
	{
		/* Product name not unique! Assume SWD not possible.*/
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
		/* Direct connection from FTDI to Jtag/Swd assumed.*/
		.vendor = 0x0403,
		.product = 0x6014,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x0B,
		.bitbang_tms_in_port_cmd = GET_BITS_LOW,
		.bitbang_tms_in_pin = MPSSE_TMS,
		.name = "ft232h"
	},
	{
		/* Direct connection from FTDI to Jtag/Swd assumed.*/
		.vendor = 0x0403,
		.product = 0x6011,
		.interface = INTERFACE_A,
		.dbus_data = 0x08,
		.dbus_ddr  = 0x0B,
		.bitbang_tms_in_port_cmd = GET_BITS_LOW,
		.bitbang_tms_in_pin = MPSSE_TMS,
		.name = "ft4232h"
	},
	{
		/* http://www.olimex.com/dev/pdf/ARM-USB-OCD.pdf.
		 * BDUS 4 global enables JTAG Buffer.
		 * => TCK and TMS not independant switchable!
		 * => SWD not possible. */
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

int platform_adiv5_swdp_scan(void)
{
	return adiv5_swdp_scan();
}

int platform_jtag_scan(const uint8_t *lrlens)
{
	return jtag_scan(lrlens);
}

int platform_jtag_dp_init()
{
	return 0;
}

void platform_init(int argc, char **argv)
{
	BMP_CL_OPTIONS_t cl_opts = {0};
	cl_opts.opt_idstring = "Blackmagic Debug Probe for FTDI/MPSSE";
	cl_opts.opt_cable = "ftdi";
	cl_init(&cl_opts, argc, argv);

	int err;
	unsigned index = 0;
	int ret = -1;
	for(index = 0; index < sizeof(cable_desc)/sizeof(cable_desc[0]);
		index++)
		 if (strcmp(cable_desc[index].name, cl_opts.opt_cable) == 0)
		 break;

	if (index == sizeof(cable_desc)/sizeof(cable_desc[0])){
		fprintf(stderr, "No cable matching %s found\n", cl_opts.opt_cable);
		exit(-1);
	}

	active_cable = &cable_desc[index];

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
	if((err = ftdi_set_interface(ftdic, active_cable->interface)) != 0) {
		fprintf(stderr, "ftdi_set_interface: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		goto error_1;
	}
	if((err = ftdi_usb_open_desc(
		ftdic, active_cable->vendor, active_cable->product,
		active_cable->description, cl_opts.opt_serial)) != 0) {
		fprintf(stderr, "unable to open ftdi device: %d (%s)\n",
			err, ftdi_get_error_string(ftdic));
		goto error_1;
	}

	if((err = ftdi_set_latency_timer(ftdic, 1)) != 0) {
		fprintf(stderr, "ftdi_set_latency_timer: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		goto error_2;
	}
	if((err = ftdi_set_baudrate(ftdic, 1000000)) != 0) {
		fprintf(stderr, "ftdi_set_baudrate: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		goto error_2;
	}
	if((err = ftdi_write_data_set_chunksize(ftdic, BUF_SIZE)) != 0) {
		fprintf(stderr, "ftdi_write_data_set_chunksize: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		goto error_2;
	}
	if (cl_opts.opt_mode != BMP_MODE_DEBUG) {
		ret = cl_execute(&cl_opts);
	} else {
		assert(gdb_if_init() == 0);
		return;
	}
  error_2:
	ftdi_usb_close(ftdic);
  error_1:
	ftdi_free(ftdic);
	exit(ret);
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
	outbuf[bufptr++] = SEND_IMMEDIATE;
	platform_buffer_flush();
	while((index += ftdi_read_data(ftdic, data + index, size-index)) != size);
	return size;
}

const char *platform_target_voltage(void)
{
	return "not supported";
}

void platform_adiv5_dp_defaults(void *arg)
{
	(void) arg;
}
