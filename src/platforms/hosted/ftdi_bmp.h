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

#ifndef __FTDI_BMP_H
#define __FTDI_BMP_H

#include "cl_utils.h"
#include "swdptap.h"
#include "jtagtap.h"

typedef struct data_desc_s {
	int16_t data_low;
	int16_t ddr_low;
	int16_t data_high;
	int16_t ddr_high;
}data_desc_t;

typedef struct pin_settings_s {
	uint8_t set_data_low;
	uint8_t clr_data_low;
	uint8_t set_data_high;
	uint8_t clr_data_high;
}pin_settings_t;

typedef struct cable_desc_s {
	int vendor;
	int product;
	int interface;
	/* Needed level on DBUS output pins to drive the JTAG signals and for
	 * other functionality like SRST, TRST. etc.*/
	uint8_t dbus_data;
	/* Write '1' for DBUS pins needed as output in JTAG mode and for
	 * other functionality like SRST, TRST. etc..*/
	uint8_t dbus_ddr;
	/* Needed level on CBUS output pins to drive the JTAG signals and for
	 * other functionality like SRST, TRST. etc.
	 * Often not needed at all.*/
	uint8_t cbus_data;
	/* Write '1' for CBUS pins needed as output in JTAG mode and for
	 * other functionality like SRST, TRST. etc.
	 * Often not needed at all.*/
	uint8_t cbus_ddr;
	/* MPSSE command to read TMS/SWDIO Port in bitbanging SWD.
	 * In many cases this is the TMS port of the FTDI and
	 * so "GET_BITS_LOW" */
	uint8_t bitbang_tms_in_port_cmd;
	/* Pin mask where TMS/SWDIO can be read in bitbanging SWD.
	 * In many cases this is the TMS port of the FTDI
	 * and so "MPSSE_TMS".*/
	uint8_t bitbang_tms_in_pin;
	/* Dbus data to allow bitbanging SWD read.*/
	uint8_t bitbang_swd_dbus_read_data;
	uint8_t bitbang_swd_direct;
	/* dbus_data, dbus_ddr, cbus_data, cbus_ddr value to assert SRST.
	 *	E.g. with CBUS Pin 1 low,
	 *	give data_high = ~PIN1, ddr_high = PIN1 */
	data_desc_t assert_srst;
	/* dbus_data, dbus_ddr, cbus_data, cbus_ddr value to release SRST.
	 *	E.g. with CBUS Pin 1 floating with internal pull up,
	 *	give data_high = PIN1, ddr_high = ~PIN1 */
	data_desc_t deassert_srst;
	/* Command to read back SRST. If 0, port from assert_srst is used*/
	uint8_t srst_get_port_cmd;
	/* PIN to read back as SRST. if 0 port from assert_srst is ised.
	*  Use PINX if active high, use Complement (~PINX) if active low*/
	uint8_t srst_get_pin;
	/* dbus data for pure MPSSE SWD read.
	 * Use together with swd_write if by some bits on DBUS,
	 * SWDIO can be routed to TDI and TDO.
	 * If both swd_read|write and
	 * bitbang_swd_dbus_read_data/bitbang_tms_in_port_cmd/bitbang_tms_in_pin
	 * are provided, pure MPSSE SWD is choosen.
	 * If neither a complete set of swd_read|write or
	 * bitbang_swd_dbus_read_data/bitbang_tms_in_port_cmd/bitbang_tms_in_pin
	 * are provided, SWD can not be done.
	 * swd_read.set_data_low ==  swd_write.set_data_low == MPSSE_DO
	 * indicated resistor SWD and inhibits Jtag.*/
	pin_settings_t swd_read;
	/* dbus data for pure MPSSE SWD write.*/
	pin_settings_t swd_write;
	/* USB readable description of the device.*/
	char *description;
	/* Command line argument to -c option to select this device.*/
	char * name;
}cable_desc_t;

extern cable_desc_t *active_cable;
extern struct ftdi_context *ftdic;

int ftdi_bmp_init(BMP_CL_OPTIONS_t *cl_opts, bmp_info_t *info);

int libftdi_swdptap_init(swd_proc_t *swd_proc);
int libftdi_jtagtap_init(jtag_proc_t *jtag_proc);
void libftdi_buffer_flush(void);
int libftdi_buffer_write(const uint8_t *data, int size);
int libftdi_buffer_read(uint8_t *data, int size);
const char *libftdi_target_voltage(void);
void libftdi_jtagtap_tdi_tdo_seq(
	uint8_t *DO, const uint8_t final_tms, const uint8_t *DI, int ticks);

#define MPSSE_SK 1
#define PIN0     1
#define MPSSE_DO 2
#define PIN1     2
#define MPSSE_DI 4
#define PIN2     4
#define MPSSE_CS 8
#define PIN3     8
#define PIN4     0x10
#define PIN5     0x20
#define PIN6     0x40
#define PIN7     0x80
#endif
