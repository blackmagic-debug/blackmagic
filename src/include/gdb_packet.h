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

#ifndef __GDB_PACKET_H
#define __GDB_PACKET_H

#include <stdarg.h>

int gdb_getpacket(char *packet, int size);
void gdb_putpacket(const char *packet, int size);
#define gdb_putpacketz(packet) gdb_putpacket((packet), strlen(packet))
void gdb_putpacket_f(const char *packet, ...);

void gdb_out(const char *buf);
void gdb_voutf(const char *fmt, va_list);
void gdb_outf(const char *fmt, ...);

#endif


