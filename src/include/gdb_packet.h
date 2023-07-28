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

#ifndef INCLUDE_GDB_PACKET_H
#define INCLUDE_GDB_PACKET_H

#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>

#define GDB_PACKET_START              '$'
#define GDB_PACKET_END                '#'
#define GDB_PACKET_ACK                '+'
#define GDB_PACKET_NACK               '-'
#define GDB_PACKET_ESCAPE             '}'
#define GDB_PACKET_RUNLENGTH_START    '*'
#define GDB_PACKET_NOTIFICATION_START '%'
#define GDB_PACKET_ESCAPE_XOR         (0x20U)

void gdb_set_noackmode(bool enable);
size_t gdb_getpacket(char *packet, size_t size);
void gdb_putpacket(const char *packet, size_t size);
void gdb_putpacket2(const char *packet1, size_t size1, const char *packet2, size_t size2);
#define gdb_putpacketz(packet) gdb_putpacket((packet), strlen(packet))
void gdb_putpacket_f(const char *packet, ...);
void gdb_put_notification(const char *packet, size_t size);
#define gdb_put_notificationz(packet) gdb_put_notification((packet), strlen(packet))

void gdb_out(const char *buf);
void gdb_voutf(const char *fmt, va_list);
void gdb_outf(const char *fmt, ...);

#endif /* INCLUDE_GDB_PACKET_H */
