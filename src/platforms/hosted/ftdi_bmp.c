/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Copyright (C) 2018  Uwe Bonnes(bon@elektron.ikp.physik.tu-darmstadt.de)
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

#include "ftdi_bmp.h"

struct ftdi_context *ftdic;

#define BUF_SIZE 4096
static uint8_t outbuf[BUF_SIZE];
static uint16_t bufptr = 0;

cable_desc_t *active_cable;

cable_desc_t cable_desc[] = {
	{
		/* Direct connection from FTDI to Jtag/Swd.
		 Pin 6 direct connected to RST.*/
		.vendor = 0x0403,
		.product = 0x6010,
		.interface = INTERFACE_A,
		.dbus_data = PIN6 | MPSSE_CS | MPSSE_DO | MPSSE_DI,
		.dbus_ddr  = MPSSE_CS | MPSSE_DO | MPSSE_SK,
		.bitbang_tms_in_port_cmd = GET_BITS_LOW,
		.bitbang_tms_in_pin = MPSSE_CS,
		.assert_srst.data_low   = ~PIN6,
		.assert_srst.ddr_low    =  PIN6,
		.deassert_srst.data_low =  PIN6,
		.deassert_srst.ddr_low  = ~PIN6,
		.description = "FLOSS-JTAG",
		.name = "flossjtag"
	},
	{
		/* MPSSE_SK (DB0) ----------- SWDCK/JTCK
		 * MPSSE-DO (DB1) -- 470 R -- SWDIO/JTMS
		 * MPSSE-DI (DB2) ----------- SWDIO/JTMS
		 * DO is tristated with SWD read, so
		 * resistor is not necessary, but protects
		 * from contentions in case of errors.
		 * JTAG not possible.*/
		.vendor  = 0x0403,
		.product = 0x6014,/*FT232H*/
		.interface = INTERFACE_A,
		.dbus_data = MPSSE_DO | MPSSE_DI | MPSSE_CS,
		.dbus_ddr  = MPSSE_SK,
		.swd_read.set_data_low  = MPSSE_DO,
		.swd_write.set_data_low = MPSSE_DO,
		.name = "ft232h_resistor_swd"
	},
	{
		/* Buffered connection from FTDI to Jtag/Swd.
		 * TCK and TMS not independant switchable!
		 * SWD not possible.
		 * DBUS PIN6 : SRST readback.
		 * CBUS PIN1 : Drive SRST
		 * CBUS PIN4 : not tristate SRST
		 */
		.vendor = 0x0403,
		.product = 0x6010,
		.interface = INTERFACE_A,
		.dbus_data = PIN4 | MPSSE_CS | MPSSE_DI | MPSSE_DO,
		.dbus_ddr  = MPSSE_CS | MPSSE_DO | MPSSE_SK,
		.cbus_data = PIN4 | PIN3 | PIN2,
		.cbus_ddr  = PIN4 | PIN3 |PIN2 | PIN1 | PIN0,
		.assert_srst.data_high  =  ~PIN3,
		.deassert_srst.data_high =  PIN3,
		.srst_get_port_cmd = GET_BITS_LOW,
		.srst_get_pin = ~PIN6,
		.description = "FTDIJTAG",
		.name = "ftdijtag"
	},
	{
/* UART/SWO on Interface A
 * JTAG and control on INTERFACE_B
 * Bit 5 high selects SWD-WRITE (TMS routed to TDO)
 * Bit 6 high selects JTAG vs SWD (TMS routed to TDI/TDO)
 * BCBUS 1 (Output) N_SRST
 * BCBUS 2 (Input) V_ISO available
 *
 * For bitbanged SWD, set Bit 5 low and select SWD read with
 * Bit 6 low. Read Connector TMS as MPSSE_DI.
 *
 * TDO is routed to Interface 0 RXD as SWO or with Uart
 * Connector pin 10 pulled to ground will connect Interface 0 RXD
 * to UART connector RXD
 */
		.vendor = 0x0403,
		.product = 0x6010,
		.interface = INTERFACE_B,
		.dbus_data = PIN6 | PIN5 | MPSSE_CS | MPSSE_DO | MPSSE_DI,
		.dbus_ddr  = PIN6 | PIN5 | MPSSE_CS | MPSSE_DO | MPSSE_SK,
		.cbus_data = PIN1 | PIN2,
		.bitbang_tms_in_port_cmd = GET_BITS_LOW,
		.bitbang_tms_in_pin = MPSSE_DI, /* keep bit 5 low*/
		.bitbang_swd_dbus_read_data = MPSSE_DO,
		.assert_srst.data_high   = ~PIN1,
		.assert_srst.ddr_high    =  PIN1,
		.deassert_srst.data_high =  PIN1,
		.deassert_srst.ddr_high  = ~PIN1,
		.swd_read.clr_data_low   = PIN5 | PIN6,
		.swd_write.set_data_low  = PIN5,
		.swd_write.clr_data_low  = PIN6,
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
		.assert_srst.data_low = 0x40,
		.deassert_srst.data_low = ~0x40,
		.srst_get_port_cmd = GET_BITS_HIGH,
		.srst_get_pin = 0x01,
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
		.bitbang_tms_in_pin = MPSSE_CS,
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
		.bitbang_tms_in_pin = MPSSE_CS,
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
		.bitbang_tms_in_pin = MPSSE_CS,
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

int ftdi_bmp_init(BMP_CL_OPTIONS_t *cl_opts, bmp_info_t *info)
{
	int err;
	unsigned index = 0;
	for(index = 0; index < sizeof(cable_desc)/sizeof(cable_desc[0]);
		index++)
		 if (strcmp(cable_desc[index].name, cl_opts->opt_cable) == 0)
		 break;

	if (index == sizeof(cable_desc)/sizeof(cable_desc[0])) {
		DEBUG_WARN( "No cable matching %s found\n", cl_opts->opt_cable);
		return -1;
	}

	active_cable = &cable_desc[index];

	DEBUG_WARN("Black Magic Probe for FTDI/MPSSE\n");
	if(ftdic) {
		ftdi_usb_close(ftdic);
		ftdi_free(ftdic);
		ftdic = NULL;
	}
	if((ftdic = ftdi_new()) == NULL) {
		DEBUG_WARN( "ftdi_new: %s\n",
			ftdi_get_error_string(ftdic));
		abort();
	}
	info->ftdic = ftdic;
	if((err = ftdi_set_interface(ftdic, active_cable->interface)) != 0) {
		DEBUG_WARN( "ftdi_set_interface: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		goto error_1;
	}
	if((err = ftdi_usb_open_desc(
		ftdic, active_cable->vendor, active_cable->product,
		active_cable->description, cl_opts->opt_serial)) != 0) {
		DEBUG_WARN( "unable to open ftdi device: %d (%s)\n",
			err, ftdi_get_error_string(ftdic));
		goto error_1;
	}

	if((err = ftdi_set_latency_timer(ftdic, 1)) != 0) {
		DEBUG_WARN( "ftdi_set_latency_timer: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		goto error_2;
	}
	if((err = ftdi_set_baudrate(ftdic, 1000000)) != 0) {
		DEBUG_WARN( "ftdi_set_baudrate: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		goto error_2;
	}
	if((err = ftdi_write_data_set_chunksize(ftdic, BUF_SIZE)) != 0) {
		DEBUG_WARN( "ftdi_write_data_set_chunksize: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		goto error_2;
	}
	return 0;
  error_2:
	ftdi_usb_close(ftdic);
  error_1:
	ftdi_free(ftdic);
	return -1;
}

static void libftdi_set_data(data_desc_t* data)
{
	uint8_t cmd[6];
	int index = 0;
	if ((data->data_low) || (data->ddr_low)) {
		if (data->data_low > 0)
			active_cable->dbus_data |= (data->data_low & 0xff);
		else
			active_cable->dbus_data &= (data->data_low & 0xff);
		if (data->ddr_low > 0)
			active_cable->dbus_ddr  |= (data->ddr_low  & 0xff);
		else
			active_cable->dbus_ddr  &= (data->ddr_low  & 0xff);
		cmd[index++] = SET_BITS_LOW;
		cmd[index++] = active_cable->dbus_data;
		cmd[index++] = active_cable->dbus_ddr;
	}
	if ((data->data_high) || (data->ddr_high)) {
		if (data->data_high > 0)
			active_cable->cbus_data |= (data->data_high & 0xff);
		else
			active_cable->cbus_data &= (data->data_high & 0xff);
		if (data->ddr_high > 0)
			active_cable->cbus_ddr  |= (data->ddr_high  & 0xff);
		else
			active_cable->cbus_ddr  &= (data->ddr_high  & 0xff);
		cmd[index++] = SET_BITS_HIGH;
		cmd[index++] = active_cable->cbus_data;
		cmd[index++] = active_cable->cbus_ddr;
	}
	if (index) {
		libftdi_buffer_write(cmd, index);
		libftdi_buffer_flush();
	}
}

void libftdi_srst_set_val(bool assert)
{
	if (assert)
		libftdi_set_data(&active_cable->assert_srst);
	else
		libftdi_set_data(&active_cable->deassert_srst);
}

bool libftdi_srst_get_val(void)
{
	uint8_t cmd[1] = {0};
	uint8_t pin = 0;
	if (active_cable->srst_get_port_cmd && active_cable->srst_get_pin) {
		cmd[0]= active_cable->srst_get_port_cmd;
		pin   =  active_cable->srst_get_pin;
	} else if (active_cable->assert_srst.data_low &&
			   active_cable->assert_srst.ddr_low) {
		cmd[0]= GET_BITS_LOW;
		pin   = active_cable->assert_srst.data_low;
	} else if (active_cable->assert_srst.data_high &&
			   active_cable->assert_srst.ddr_high) {
		cmd[0]= GET_BITS_HIGH;
		pin   = active_cable->assert_srst.data_high;
	}else {
		return false;
	}
	libftdi_buffer_write(cmd, 1);
	uint8_t data[1];
	libftdi_buffer_read(data, 1);
	bool res = false;
	if (((pin < 0x7f) || (pin == PIN7)))
		res = data[0] & pin;
	else
		res = !(data[0] & ~pin);
	return res;
}

void libftdi_buffer_flush(void)
{
#if defined(USE_USB_VERSION_BIT)
static struct ftdi_transfer_control *tc_write = NULL;
    if (tc_write)
		ftdi_transfer_data_done(tc_write);
	tc_write = ftdi_write_data_submit(ftdic, outbuf, bufptr);
#else
	assert(ftdi_write_data(ftdic, outbuf, bufptr) == bufptr);
	DEBUG_WIRE("FT2232 libftdi_buffer flush: %d bytes\n", bufptr);
#endif
	bufptr = 0;
}

int libftdi_buffer_write(const uint8_t *data, int size)
{
	if((bufptr + size) / BUF_SIZE > 0) libftdi_buffer_flush();
	memcpy(outbuf + bufptr, data, size);
	bufptr += size;
	return size;
}

int libftdi_buffer_read(uint8_t *data, int size)
{
#if defined(USE_USB_VERSION_BIT)
	struct ftdi_transfer_control *tc;
	outbuf[bufptr++] = SEND_IMMEDIATE;
	libftdi_buffer_flush();
	tc = ftdi_read_data_submit(ftdic, data, size);
	ftdi_transfer_data_done(tc);
#else
	int index = 0;
	outbuf[bufptr++] = SEND_IMMEDIATE;
	libftdi_buffer_flush();
	while((index += ftdi_read_data(ftdic, data + index, size-index)) != size);
#endif
	return size;
}

void libftdi_jtagtap_tdi_tdo_seq(
	uint8_t *DO, const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	int rsize, rticks;

	if(!ticks) return;
	if (!DI && !DO) return;

//	printf("ticks: %d\n", ticks);
	if(final_tms) ticks--;
	rticks = ticks & 7;
	ticks >>= 3;
	uint8_t data[3];
	uint8_t cmd =  ((DO)? MPSSE_DO_READ : 0) |
		((DI)? (MPSSE_DO_WRITE | MPSSE_WRITE_NEG) : 0) | MPSSE_LSB;
	rsize = ticks;
	if(ticks) {
		data[0] = cmd;
		data[1] = ticks - 1;
		data[2] = 0;
		libftdi_buffer_write(data, 3);
		if (DI)
			libftdi_buffer_write(DI, ticks);
	}
	if(rticks) {
		int index = 0;
		rsize++;
		data[index++] = cmd | MPSSE_BITMODE;
		data[index++] = rticks - 1;
		if (DI)
			data[index++] = DI[ticks];
		libftdi_buffer_write(data, index);
	}
	if(final_tms) {
		int index = 0;
		rsize++;
		data[index++] = MPSSE_WRITE_TMS | ((DO)? MPSSE_DO_READ : 0) |
			MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG;
		data[index++] = 0;
		if (DI)
			data[index++] = (DI[ticks]) >> rticks?0x81 : 0x01;
		libftdi_buffer_write(data, index);
	}
	if (DO) {
		int index = 0;
		uint8_t *tmp = alloca(ticks);
		libftdi_buffer_read(tmp, rsize);
		if(final_tms) rsize--;

		while(rsize--) {
			/*if(rsize) printf("%02X ", tmp[index]);*/
			*DO++ = tmp[index++];
		}
		if (rticks == 0)
			*DO++ = 0;
		if(final_tms) {
			rticks++;
			*(--DO) >>= 1;
			*DO |= tmp[index] & 0x80;
		} else DO--;
		if(rticks) {
			*DO >>= (8-rticks);
		}
		/*printf("%02X\n", *DO);*/
	}
}

const char *libftdi_target_voltage(void)
{
	return "not supported";
}
