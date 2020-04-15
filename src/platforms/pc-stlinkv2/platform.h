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

#ifndef __PLATFORM_H
#define __PLATFORM_H

#include <libusb-1.0/libusb.h>

#include "timing.h"

#ifndef _WIN32
#	include <alloca.h>
#else
#	ifndef alloca
#		define alloca __builtin_alloca
#	endif
#endif

#define PLATFORM_HAS_DEBUG

#define PLATFORM_IDENT "StlinkV2/3"
#define SET_RUN_STATE(state)
void stlink_check_detach(int state);
#define SET_IDLE_STATE(state) stlink_check_detach(state)
//#define SET_ERROR_STATE(state)

void platform_buffer_flush(void);
int platform_buffer_write(const uint8_t *data, int size);
int platform_buffer_read(uint8_t *data, int size);
int jtag_scan_stlinkv2(const uint8_t *irlens);
#endif
