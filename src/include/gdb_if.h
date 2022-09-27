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

#ifndef INCLUDE_GDB_IF_H
#define INCLUDE_GDB_IF_H

#if PC_HOSTED == 0
#include <libopencm3/usb/usbd.h>
void gdb_usb_out_cb(usbd_device *dev, uint8_t ep);
#endif

int gdb_if_init(void);
char gdb_if_getchar(void);
char gdb_if_getchar_to(uint32_t timeout);

/* sending gdb_if_putchar(0, true) seems to work as keep alive */
void gdb_if_putchar(char c, int flush);

#endif /* INCLUDE_GDB_IF_H */
