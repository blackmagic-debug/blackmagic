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

#include <stdint.h>
#include <ftdi.h>

#define FT2232_VID	0x0403
#define FT2232_PID	0x6010

#define SET_RUN_STATE(state)
#define SET_IDLE_STATE(state)
#define SET_ERROR_STATE(state)

#define PLATFORM_FATAL_ERROR(error)	abort()
#define PLATFORM_SET_FATAL_ERROR_RECOVERY()

#define morse(x, y) do {} while(0)
#define morse_msg 0

extern struct ftdi_context *ftdic;

int platform_init(void);

void platform_buffer_flush(void);
int platform_buffer_write(const uint8_t *data, int size);
int platform_buffer_read(uint8_t *data, int size);

#endif

