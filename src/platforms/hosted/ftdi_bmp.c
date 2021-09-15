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
#include "target.h"

#include <assert.h>
#include <unistd.h>
#include <sys/time.h>

#include "ftdi_bmp.h"
#include <ftdi.h>

struct ftdi_context *ftdic;

#define BUF_SIZE 4096
static uint8_t outbuf[BUF_SIZE];
static uint16_t bufptr = 0;

cable_desc_t *active_cable;
data_desc_t active_state;

cable_desc_t cable_desc[] = {
	{
		/* Direct connection from FTDI to Jtag/Swd.
		 Pin 6 direct connected to RST.*/
		.vendor = 0x0403,
		.product = 0x6014,
		.interface = INTERFACE_A,
		// No explicit reset
		.bb_swdio_in_port_cmd = GET_BITS_LOW,
		.bb_swdio_in_pin = MPSSE_CS,
		.description = "UM232H",
		.name = "um232h"
	},
	{
		/* Direct connection from FTDI to Jtag/Swd.
		 Pin 6 direct connected to RST.*/
		.vendor = 0x0403,
		.product = 0x6010,
		.interface = INTERFACE_A,
		.init.data_low = PIN6, /* PULL nRST high*/
		.bb_swdio_in_port_cmd = GET_BITS_LOW,
		.bb_swdio_in_pin = MPSSE_CS,
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
		 * JTAG not possible
		 * PIN6     (DB6) ----------- NRST */
		.vendor  = 0x0403,
		.product = 0x6010,/*FT2232H*/
		.interface = INTERFACE_B,
		.init.data_low = PIN4, /* Pull up pin 4 */
		.init.ddr_low  = PIN4, /* Pull up pin 4 */
		.mpsse_swd_read.set_data_low  = MPSSE_DO,
		.mpsse_swd_write.set_data_low = MPSSE_DO,
		.assert_srst.data_low   = ~PIN6,
		.assert_srst.ddr_low    =  PIN6,
		.deassert_srst.data_low =  PIN6,
		.deassert_srst.ddr_low  = ~PIN6,
		.target_voltage_cmd  = GET_BITS_LOW,
		.target_voltage_pin  = PIN4, /* Always read as target voltage present.*/
		.description = "USBMATE",
		.name = "usbmate"
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
		.mpsse_swd_read.set_data_low  = MPSSE_DO,
		.mpsse_swd_write.set_data_low = MPSSE_DO,
		.name = "ft232h_resistor_swd"
	},
	{
		/* Buffered connection from FTDI to Jtag/Swd.
		 * TCK and TMS not independant switchable!
		 * SWD not possible.
		 * PIN4 low enables buffers
		 * PIN5 Low indicates VRef applied
		 * PIN6 reads back SRST
		 * CBUS PIN1 Sets SRST
		 * CBUS PIN2 low drives SRST
		 */
		.vendor = 0x0403,
		.product = 0x6010,
		.interface = INTERFACE_A,
		.init.ddr_low = PIN4,
		.init.data_high = PIN4 | PIN3 | PIN2,
		.init.ddr_high = PIN4 | PIN3 | PIN2 | PIN1 | PIN0,
		.assert_srst.data_high   = ~PIN3,
		.deassert_srst.data_high =  PIN3,
		.srst_get_port_cmd = GET_BITS_LOW,
		.srst_get_pin = PIN6,
		.description = "FTDIJTAG",
		.name = "ftdijtag"
	},
	{
/* UART/SWO on Interface A
 * JTAG and control on INTERFACE_B
 * Bit 5 high selects SWD-WRITE (TMS routed to MPSSE_DI)
 * Bit 6 high selects JTAG vs SWD (TMS routed to MPSSE_CS)
 * BCBUS 1 (Output) N_SRST
 * BCBUS 2 (Input/ Internal Pull Up) V_ISO available
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
		.init.data_low = PIN6 | PIN5,
		.init.ddr_low  = PIN6 | PIN5,
		.init.data_high = PIN1 | PIN2,
		.assert_srst.data_high     = ~PIN1,
		.assert_srst.ddr_high      =  PIN1,
		.deassert_srst.data_high   =  PIN1,
		.deassert_srst.ddr_high    = ~PIN1,
		.mpsse_swd_read.clr_data_low  = PIN5 | PIN6,
		.mpsse_swd_write.set_data_low = PIN5,
		.mpsse_swd_write.clr_data_low = PIN6,
		.jtag.set_data_low            = PIN6,
		.target_voltage_cmd  = GET_BITS_HIGH,
		.target_voltage_pin  = ~PIN2,
		.name = "ftdiswd",
		.description = "FTDISWD"
	},
	{
		.vendor = 0x15b1,
		.product = 0x0003,
		.interface = INTERFACE_A,
		.init.ddr_low  = PIN5,
		.name = "olimex"
	},
	{
		/* Buffered connection from FTDI to Jtag/Swd.
		 * TCK and TMS not independant switchable!
		 * => SWD not possible.
		 * DBUS PIN4 / JTAGOE low enables buffers
		 * DBUS PIN5 / TRST high drives nTRST low OC
		 * DBUS PIN6 / RST high drives nSRST low OC
		 * CBUS PIN0 reads back SRST
		 */
		.vendor = 0x0403,
		.product = 0xbdc8,
		.interface = INTERFACE_A,
		/* Drive low to activate JTAGOE and deassert TRST/RST.*/
		.init.data_low  = 0,
		.init.ddr_low  = PIN6 | PIN5 | PIN4,
		.init.ddr_high = PIN2, /* ONE LED */
		.assert_srst.data_low = PIN6,
		.deassert_srst.data_low = ~PIN6,
		.srst_get_port_cmd = GET_BITS_HIGH,
		.srst_get_pin = PIN0,
		.name = "turtelizer",
		.description = "Turtelizer JTAG/RS232 Adapter"
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
		.name = "jtaghs1"
	},
	{
		/* Direct connection from FTDI to Jtag/Swd assumed.*/
		.vendor = 0x0403,
		.product = 0xbdc8,
		.interface = INTERFACE_A,
		.init.data_low = MPSSE_CS | MPSSE_DO | MPSSE_DI,
		.init.ddr_low  = MPSSE_CS | MPSSE_DO | MPSSE_SK,
		.bb_swdio_in_port_cmd = GET_BITS_LOW,
		.bb_swdio_in_pin = MPSSE_CS,
		.name = "ftdi"
	},
	{
		/* Product name not unique! Assume SWD not possible.*/
		.vendor = 0x0403,
		.product = 0x6014,
		.interface = INTERFACE_A,
		.init.data_low = PIN7,
		.init.ddr_low = PIN7,
		.init.data_high = PIN5,
		.init.ddr_high = PIN5 | PIN4 | PIN3 | PIN2 | PIN1 | PIN0,
		.name = "digilent"
	},
	{
		/* Direct connection from FTDI to Jtag/Swd assumed.*/
		.vendor = 0x0403,
		.product = 0x6014,
		.interface = INTERFACE_A,
		.init.data_low = MPSSE_CS | MPSSE_DO | MPSSE_DI,
		.init.ddr_low  = MPSSE_CS | MPSSE_DO | MPSSE_SK,
		.bb_swdio_in_port_cmd = GET_BITS_LOW,
		.bb_swdio_in_pin = MPSSE_CS,
		.name = "ft232h"
	},
	{
		/* Direct connection from FTDI to Jtag/Swd assumed.*/
		.vendor = 0x0403,
		.product = 0x6011,
		.interface = INTERFACE_A,
		.bb_swdio_in_port_cmd = GET_BITS_LOW,
		.bb_swdio_in_pin = MPSSE_CS,
		.name = "ft4232h"
	},
	{
		/* http://www.olimex.com/dev/pdf/ARM-USB-OCD.pdf.
		 * DBUS 4 global enables JTAG Buffer.
		 * => TCK and TMS not independant switchable!
		 * => SWD not possible. */
		.vendor = 0x15ba,
		.product = 0x002b,
		.interface = INTERFACE_A,
		.init.ddr_low = PIN4,
		.init.data_high = PIN3 | PIN1 | PIN0,
		.init.ddr_high =  PIN4 | PIN3 | PIN1 | PIN0,
		.name = "arm-usb-ocd-h"
	},
	{
	}
};

int ftdi_bmp_init(BMP_CL_OPTIONS_t *cl_opts, bmp_info_t *info)
{
	int err;
	cable_desc_t *cable = &cable_desc[0];
	for(;  cable->name; cable++) {
		if (strncmp(cable->name, cl_opts->opt_cable, strlen(cable->name)) == 0)
		 break;
	}

	if (!cable->name ) {
		DEBUG_WARN( "No cable matching found for %s\n", cl_opts->opt_cable);
		return -1;
	}

	active_cable = cable;
	memcpy(&active_state, &active_cable->init, sizeof(data_desc_t));
	/* If swd_(read|write) is not given for the selected cable and
	   the 'e' command line argument is give, assume resistor SWD
	   connection.*/
	if (cl_opts->external_resistor_swd &&
		(active_cable->mpsse_swd_read.set_data_low  == 0) &&
		(active_cable->mpsse_swd_read.clr_data_low  == 0) &&
		(active_cable->mpsse_swd_read.set_data_high == 0) &&
		(active_cable->mpsse_swd_read.clr_data_high == 0) &&
		(active_cable->mpsse_swd_write.set_data_low  == 0) &&
		(active_cable->mpsse_swd_write.clr_data_low  == 0) &&
		(active_cable->mpsse_swd_write.set_data_high == 0) &&
		(active_cable->mpsse_swd_write.clr_data_high == 0)) {
			DEBUG_INFO("Using external resistor SWD\n");
			active_cable->mpsse_swd_read.set_data_low = MPSSE_DO;
			active_cable->mpsse_swd_write.set_data_low = MPSSE_DO;
	} else if (!libftdi_swd_possible(NULL, NULL) &&
			   !cl_opts->opt_usejtag) {
		DEBUG_WARN("SWD with cable not possible, trying JTAG\n");
		cl_opts->opt_usejtag = true;
	}
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
	assert(ftdic != NULL);
#ifdef _Ftdi_Pragma
	err = ftdi_tcioflush(ftdic);
#else
	 err = ftdi_usb_purge_buffers(ftdic);
#endif
	if (err != 0) {
		DEBUG_WARN("ftdi_tcioflush(ftdi_usb_purge_buffer): %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		goto error_2;
	}
	/* Reset MPSSE controller. */
	err = ftdi_set_bitmode(ftdic, 0,  BITMODE_RESET);
	if (err != 0) {
		DEBUG_WARN("ftdi_set_bitmode: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		goto error_2;
	}
	/* Enable MPSSE controller. Pin directions are set later.*/
	err = ftdi_set_bitmode(ftdic, 0, BITMODE_MPSSE);
	if (err != 0) {
		DEBUG_WARN("ftdi_set_bitmode: %d: %s\n",
			err, ftdi_get_error_string(ftdic));
		goto error_2;
	}
	uint8_t ftdi_init[16];
	/* Test for pending garbage.*/
	int garbage =  ftdi_read_data(ftdic, ftdi_init, sizeof(ftdi_init));
	if (garbage > 0) {
		DEBUG_WARN("FTDI init garbage at start:");
		for (int i = 0; i < garbage; i++)
			DEBUG_WARN(" %02x", ftdi_init[i]);
		DEBUG_WARN("\n");
	}
	int index = 0;
	ftdi_init[index++]= LOOPBACK_END; /* FT2232D gets upset otherwise*/
	switch(ftdic->type) {
	case TYPE_2232H:
	case TYPE_4232H:
	case TYPE_232H:
		ftdi_init[index++] = EN_DIV_5;
		break;
	case TYPE_2232C:
		break;
	default:
		DEBUG_WARN("FTDI Chip has no MPSSE\n");
		goto error_2;
	}
	ftdi_init[index++]= TCK_DIVISOR;
	/* Use CLK/2 for about 50 % SWDCLK duty cycle on FT2232c.*/
	ftdi_init[index++]= 1;
	ftdi_init[index++]= 0;
	ftdi_init[index++]= SET_BITS_LOW;
	ftdi_init[index++]= active_state.data_low;
	ftdi_init[index++]= active_state.ddr_low;
	ftdi_init[index++]= SET_BITS_HIGH;
	ftdi_init[index++]= active_state.data_high;
	ftdi_init[index++]= active_state.ddr_high;
	libftdi_buffer_write(ftdi_init, index);
	libftdi_buffer_flush();
	garbage =  ftdi_read_data(ftdic, ftdi_init, sizeof(ftdi_init));
	if (garbage > 0) {
		DEBUG_WARN("FTDI init garbage at end:");
		for (int i = 0; i < garbage; i++)
			DEBUG_WARN(" %02x", ftdi_init[i]);
		DEBUG_WARN("\n");
	}	return 0;

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
			active_state.data_low |= (data->data_low & 0xff);
		else if (data->data_low < 0)
			active_state.data_low &= (data->data_low & 0xff);
		if (data->ddr_low > 0)
			active_state.ddr_low  |= (data->ddr_low  & 0xff);
		else if (data->ddr_low < 0)
			active_state.ddr_low  &= (data->ddr_low  & 0xff);
		cmd[index++] = SET_BITS_LOW;
		cmd[index++] = active_state.data_low;
		cmd[index++] = active_state.ddr_low;
	}
	if ((data->data_high) || (data->ddr_high)) {
		if (data->data_high > 0)
			active_state.data_high |= (data->data_high & 0xff);
		else if (data->data_high < 0)
			active_state.data_high &= (data->data_high & 0xff);
		if (data->ddr_high > 0)
			active_state.ddr_high  |= (data->ddr_high  & 0xff);
		else if (data->ddr_high < 0)
			active_state.ddr_high  &= (data->ddr_high  & 0xff);
		cmd[index++] = SET_BITS_HIGH;
		cmd[index++] = active_state.data_high;
		cmd[index++] = active_state.ddr_high;
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
	if (!bufptr)
		return;
	DEBUG_WIRE("Flush %d\n", bufptr);
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
	DEBUG_WIRE("Write %d bytes:", size);
	for (int i = 0; i < size; i++) {
		DEBUG_WIRE(" %02x", data[i]);
		if (i && ((i & 0xf) == 0xf))
			DEBUG_WIRE("\n\t");
	}
	DEBUG_WIRE("\n");
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
	const uint8_t cmd[1] = {SEND_IMMEDIATE};
	libftdi_buffer_write(cmd, 1);
	libftdi_buffer_flush();
	while((index += ftdi_read_data(ftdic, data + index, size-index)) != size);
#endif
	DEBUG_WIRE("Read  %d bytes:", size);
	for (int i = 0; i < size; i++) {
		DEBUG_WIRE(" %02x", data[i]);
		if (i && ((i & 0xf) == 0xf))
			DEBUG_WIRE("\n\t");
	}
	DEBUG_WIRE("\n");
	return size;
}

void libftdi_jtagtap_tdi_tdo_seq(
	uint8_t *DO, const uint8_t final_tms, const uint8_t *DI, int ticks)
{
	int rsize, rticks;

	if(!ticks) return;
	if (!DI && !DO) return;

	DEBUG_WIRE("libftdi_jtagtap_tdi_tdo_seq %s ticks: %d\n",
			   (DI && DO) ? "read/write" : ((DI) ? "write" : "read"), ticks);
	if(final_tms) ticks--;
	rticks = ticks & 7;
	ticks >>= 3;
	uint8_t data[8];
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
	int index = 0;
	if(rticks) {
		rsize++;
		data[index++] = cmd | MPSSE_BITMODE;
		data[index++] = rticks - 1;
		if (DI)
			data[index++] = DI[ticks];
	}
	if(final_tms) {
		rsize++;
		data[index++] = MPSSE_WRITE_TMS | ((DO)? MPSSE_DO_READ : 0) |
			MPSSE_LSB | MPSSE_BITMODE | MPSSE_WRITE_NEG;
		data[index++] = 0;
		if (DI)
			data[index++] = (DI[ticks]) >> rticks?0x81 : 0x01;
	}
	if (index)
		libftdi_buffer_write(data, index);
	if (DO) {
		int index = 0;
		uint8_t *tmp = alloca(rsize);
		libftdi_buffer_read(tmp, rsize);
		if(final_tms) rsize--;

		while(rsize--) {
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
	}
}

const char *libftdi_target_voltage(void)
{
	uint8_t pin = active_cable->target_voltage_pin;
	if (active_cable->target_voltage_cmd && pin) {
		libftdi_buffer_write(&active_cable->target_voltage_cmd, 1);
		uint8_t data[1];
		libftdi_buffer_read(data, 1);
		bool res = false;
		if (((pin < 0x7f) || (pin == PIN7)))
			res = data[0] & pin;
		else
			res = !(data[0] & ~pin);
		if (res)
			return "Present";
		else
			return "Absent";
	}
	return NULL;
}

static uint16_t divisor;
void libftdi_max_frequency_set(uint32_t freq)
{
	uint32_t clock;
	if (ftdic->type == TYPE_2232C)
		clock = 12 * 1000 * 1000;
	else
		/* Undivided clock set during startup*/
		clock = 60 * 1000 * 1000;
	uint32_t div = (clock  + 2 * freq - 1)/ freq;
	if ((div < 4) && (ftdic->type = TYPE_2232C))
		div = 4; /* Avoid bad unsymetrict FT2232C clock at 6 MHz*/
	divisor = div / 2 - 1;
	uint8_t buf[3];
	buf[0] = TCK_DIVISOR;
	buf[1] = divisor & 0xff;
	buf[2] = (divisor >> 8) & 0xff;
	libftdi_buffer_write(buf, 3);
}

uint32_t libftdi_max_frequency_get(void)
{
	uint32_t clock;
	if (ftdic->type == TYPE_2232C)
		clock = 12 * 1000 * 1000;
	else
		/* Undivided clock set during startup*/
		clock = 60 * 1000 * 1000;
	return clock/ ( 2 *(divisor + 1));
}
