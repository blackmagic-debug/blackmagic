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

#ifndef TARGET_JTAG_DEVS_H
#define TARGET_JTAG_DEVS_H

#include <stdint.h>

typedef struct jtag_ir_quirks {
	uint16_t ir_value;
	uint8_t ir_length;
} jtag_ir_quirks_s;

typedef struct jtag_dev_descr {
	uint32_t idcode;
	uint32_t idmask;
#ifdef ENABLE_DEBUG
	const char *descr;
#endif
	void (*handler)(uint8_t jd_index);
	jtag_ir_quirks_s ir_quirks;
} jtag_dev_descr_s;

extern const jtag_dev_descr_s dev_descr[];

#endif /* TARGET_JTAG_DEVS_H */
