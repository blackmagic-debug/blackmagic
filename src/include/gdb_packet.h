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

/* Allow override in other platforms if needed */
#ifndef GDB_PACKET_BUFFER_SIZE
#define GDB_PACKET_BUFFER_SIZE 1024U
#endif

/* Limit out packet string size to the maximum packet size before hexifying */
#define GDB_OUT_PACKET_MAX_SIZE ((GDB_PACKET_BUFFER_SIZE / 2U) - 1U)

#define GDB_PACKET_START              '$'
#define GDB_PACKET_END                '#'
#define GDB_PACKET_ACK                '+'
#define GDB_PACKET_NACK               '-'
#define GDB_PACKET_ESCAPE             '}'
#define GDB_PACKET_RUNLENGTH_START    '*'
#define GDB_PACKET_NOTIFICATION_START '%'
#define GDB_PACKET_ESCAPE_XOR         (0x20U)

#define GDB_PACKET_RETRIES 3U /* Number of times to retry sending a packet */

#if defined(__MINGW32__) || defined(__MINGW64__) || defined(__CYGWIN__)
#define GDB_FORMAT_ATTR __attribute__((format(__MINGW_PRINTF_FORMAT, 1, 2)))
#elif defined(__GNUC__) || defined(__clang__)
#define GDB_FORMAT_ATTR __attribute__((format(printf, 1, 2)))
#else
#define GDB_FORMAT_ATTR
#endif

/* GDB packet transmission configuration */
void gdb_set_noackmode(bool enable);

/* Raw GDB packet transmission */
size_t gdb_getpacket(char *packet, size_t size);
void gdb_putpacket(const char *preamble, size_t preamble_size, const char *data, size_t data_size, bool hexify);
void gdb_put_notification(const char *data, size_t size);

/* Convenience wrappers */
static inline void gdb_putpacketz(const char *const str)
{
	gdb_putpacket(NULL, 0, str, strlen(str), false);
}

static inline void gdb_putpacketx(const void *const data, const size_t size)
{
	gdb_putpacket(NULL, 0, (const char *)data, size, true);
}

static inline void gdb_put_notificationz(const char *const str)
{
	gdb_put_notification(str, strlen(str));
}

/* Formatted output */
void gdb_putpacket_f(const char *fmt, ...) GDB_FORMAT_ATTR;

void gdb_out(const char *str);
void gdb_voutf(const char *fmt, va_list ap);
void gdb_outf(const char *fmt, ...) GDB_FORMAT_ATTR;

#endif /* INCLUDE_GDB_PACKET_H */
