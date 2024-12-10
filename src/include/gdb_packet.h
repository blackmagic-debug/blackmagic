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
#define GDB_OUT_PACKET_MAX_SIZE ((GDB_PACKET_BUFFER_SIZE - 1U) / 2U)

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

/*
 * GDB packet structure
 * This is used to store the packet data during transmission and reception
 * This will be statically allocated and aligned to 8 bytes to allow the remote protocol to re-use it
 * A single packet instance exists in the system and is re-used for all packet operations
 * This means transmiting a packet will invalidate any previously obtained packets
 * Do not use this structure directly or you might risk runing out of memory
 */
typedef struct gdb_packet {
	/* Data must be first to ensure alignment */
	char data[GDB_PACKET_BUFFER_SIZE + 1U]; /* Packet data */
	size_t size;                            /* Packet data size */
	bool notification;                      /* Notification packet */
} gdb_packet_s;

/* GDB packet transmission configuration */
void gdb_set_noackmode(bool enable);

/* Raw GDB packet transmission */
gdb_packet_s *gdb_packet_receive(void);
void gdb_packet_send(const gdb_packet_s *packet);

char *gdb_packet_buffer(void);

/* Convenience wrappers */
void gdb_putpacket(const char *preamble, size_t preamble_size, const char *data, size_t data_size, bool hex_data);

static inline void gdb_putpacketz(const char *const str)
{
	gdb_putpacket(str, strlen(str), NULL, 0, false);
}

static inline void gdb_putpacketx(const void *const data, const size_t size)
{
	gdb_putpacket(NULL, 0, (const char *)data, size, true);
}

void gdb_put_notificationz(const char *const str);

/* Formatted output */
void gdb_putpacket_f(const char *fmt, ...) GDB_FORMAT_ATTR;

void gdb_out(const char *str);
void gdb_voutf(const char *fmt, va_list ap);
void gdb_outf(const char *fmt, ...) GDB_FORMAT_ATTR;

#endif /* INCLUDE_GDB_PACKET_H */
