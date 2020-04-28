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

typedef struct cable_desc_s {
	int vendor;
	int product;
	int interface;
	uint8_t dbus_data;
	uint8_t dbus_ddr;
	uint8_t cbus_data;
	uint8_t cbus_ddr;
	uint8_t bitbang_tms_in_port_cmd;
	uint8_t bitbang_tms_in_pin;
	uint8_t bitbang_swd_dbus_read_data;
	/* bitbang_swd_dbus_read_data is same as dbus_data,
	 * as long as CBUS is not involved.*/
	char *description;
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

#define MPSSE_TDI 2
#define MPSSE_TDO 4
#define MPSSE_TMS 8

#endif
