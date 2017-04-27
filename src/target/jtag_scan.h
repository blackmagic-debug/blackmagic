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

#ifndef __JTAG_SCAN_H
#define __JTAG_SCAN_H

#define JTAG_MAX_DEVS	32
#define JTAG_MAX_IR_LEN	16

typedef struct jtag_dev_s {
	union {
		uint8_t dev;
		uint8_t dr_prescan;
	};
	uint8_t dr_postscan;

	uint8_t ir_len;
	uint8_t ir_prescan;
	uint8_t ir_postscan;

	uint32_t idcode;
	const char *descr;

	uint32_t current_ir;

} jtag_dev_t;

extern struct jtag_dev_s jtag_devs[JTAG_MAX_DEVS+1];
extern int jtag_dev_count;

void jtag_dev_write_ir(jtag_dev_t *dev, uint32_t ir);
void jtag_dev_shift_dr(jtag_dev_t *dev, uint8_t *dout, const uint8_t *din, int ticks);

#endif

