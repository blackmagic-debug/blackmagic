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

/*
 * This file implements the GDB Remote Serial Debugging protocol packet
 * reception and transmission as well as some convenience functions.
 */

#include "general.h"
#include "gdb_if.h"
#include "gdb_packet.h"
#include "hex_utils.h"
#include "remote.h"

#include <stdarg.h>

size_t gdb_getpacket(char *const packet, const size_t size)
{
	unsigned char csum;
	char recv_csum[3];
	size_t offset = 0;

	while (true) {
		/* Wait for packet start */
		do {
			/*
			 * Spin waiting for a start of packet character - either a gdb
             * start ('$') or a BMP remote packet start ('!').
			 */
			do {
				/* Smells like bad code */
				packet[0] = gdb_if_getchar();
				if (packet[0] == '\x04')
					return 1;
			} while (packet[0] != '$' && packet[0] != REMOTE_SOM);
#if PC_HOSTED == 0
			if (packet[0] == REMOTE_SOM) {
				/* This is probably a remote control packet - get and handle it */
				offset = 0;
				bool getting_remote_packet = true;
				while (getting_remote_packet) {
					/* Smells like bad code */
					const char c = gdb_if_getchar();
					switch (c) {
					case REMOTE_SOM: /* Oh dear, packet restarts */
						offset = 0;
						break;

					case REMOTE_EOM: /* Complete packet for processing */
						packet[offset] = 0;
						remote_packet_process(offset, packet);
						getting_remote_packet = false;
						break;

					case '$': /* A 'real' gdb packet, best stop squatting now */
						packet[0] = '$';
						getting_remote_packet = false;
						break;

					default:
						if (offset < size)
							packet[offset++] = c;
						else
							/* Who knows what is going on...return to normality */
							getting_remote_packet = false;
						break;
					}
				}
				/*
				 * Reset the packet buffer start character to zero, because function
				 * 'remote_packet_process()' above overwrites this buffer, and
				 * an arbitrary character may have been placed there. If this is a '$'
				 * character, this will cause this loop to be terminated, which is wrong.
				 */
				packet[0] = '\0';
			}
#endif
		} while (packet[0] != '$');

		offset = 0;
		csum = 0;
		char c = '\0';
		/* Capture packet data into buffer */
		while (c != '#') {
			c = gdb_if_getchar();
			if (c == '#')
				break;
			/* If we run out of buffer space, exit early */
			if (offset == size)
				break;

			if (c == '$') { /* Restart capture */
				offset = 0;
				csum = 0;
				continue;
			}
			if (c == '}') { /* Escaped char */
				c = gdb_if_getchar();
				csum += c + '}';
				packet[offset++] = (char)((uint8_t)c ^ 0x20U);
				continue;
			}
			csum += c;
			packet[offset++] = c;
		}
		recv_csum[0] = gdb_if_getchar();
		recv_csum[1] = gdb_if_getchar();
		recv_csum[2] = 0;

		/* Return packet if checksum matches */
		if (csum == strtol(recv_csum, NULL, 16))
			break;

		/* Get here if checksum fails */
		gdb_if_putchar('-', 1); /* Send nack */
	}
	gdb_if_putchar('+', 1); /* Send ack */
	packet[offset] = '\0';

	DEBUG_GDB("%s: ", __func__);
	for (size_t j = 0; j < offset; j++) {
		const char value = packet[j];
		if (value >= ' ' && value < '\x7f')
			DEBUG_GDB("%c", value);
		else
			DEBUG_GDB("\\x%02X", value);
	}
	DEBUG_GDB("\n");
	return offset;
}

static void gdb_next_char(const char value, uint8_t *const csum)
{
	if (value >= ' ' && value < '\x7f')
		DEBUG_GDB("%c", value);
	else
		DEBUG_GDB("\\x%02X", value);
	if (value == '$' || value == '#' || value == '}' || value == '*') {
		gdb_if_putchar('}', 0);
		gdb_if_putchar((char)((uint8_t)value ^ 0x20U), 0);
		*csum += '}' + (value ^ 0x20U);
	} else {
		gdb_if_putchar(value, 0);
		*csum += value;
	}
}

void gdb_putpacket2(const char *const packet1, const size_t size1, const char *const packet2, const size_t size2)
{
	char xmit_csum[3];
	size_t tries = 0;

	do {
		DEBUG_GDB("%s: ", __func__);
		uint8_t csum = 0;
		gdb_if_putchar('$', 0);

		for (size_t i = 0; i < size1; ++i)
			gdb_next_char(packet1[i], &csum);
		for (size_t i = 0; i < size2; ++i)
			gdb_next_char(packet2[i], &csum);

		gdb_if_putchar('#', 0);
		snprintf(xmit_csum, sizeof(xmit_csum), "%02X", csum);
		gdb_if_putchar(xmit_csum[0], 0);
		gdb_if_putchar(xmit_csum[1], 1);
		DEBUG_GDB("\n");
	} while (gdb_if_getchar_to(2000) != '+' && tries++ < 3U);
}

void gdb_putpacket(const char *const packet, const size_t size)
{
	char xmit_csum[3];
	size_t tries = 0;

	do {
		DEBUG_GDB("%s: ", __func__);
		uint8_t csum = 0;
		gdb_if_putchar('$', 0);
		for (size_t i = 0; i < size; ++i)
			gdb_next_char(packet[i], &csum);
		gdb_if_putchar('#', 0);
		snprintf(xmit_csum, sizeof(xmit_csum), "%02X", csum);
		gdb_if_putchar(xmit_csum[0], 0);
		gdb_if_putchar(xmit_csum[1], 1);
		DEBUG_GDB("\n");
	} while (gdb_if_getchar_to(2000) != '+' && tries++ < 3U);
}

void gdb_put_notification(const char *const packet, const size_t size)
{
	char xmit_csum[3];

	DEBUG_GDB("%s: ", __func__);
	uint8_t csum = 0;
	gdb_if_putchar('%', 0);
	for (size_t i = 0; i < size; ++i)
		gdb_next_char(packet[i], &csum);
	gdb_if_putchar('#', 0);
	snprintf(xmit_csum, sizeof(xmit_csum), "%02X", csum);
	gdb_if_putchar(xmit_csum[0], 0);
	gdb_if_putchar(xmit_csum[1], 1);
	DEBUG_GDB("\n");
}

void gdb_putpacket_f(const char *const fmt, ...)
{
	va_list ap;
	char *buf;

	va_start(ap, fmt);
	const int size = vasprintf(&buf, fmt, ap);
	if (size > 0)
		gdb_putpacket(buf, size);
	free(buf);
	va_end(ap);
}

void gdb_out(const char *const buf)
{
	const size_t buf_len = strlen(buf);
	char *hexdata = calloc(1, 2U * buf_len + 1U);
	if (!hexdata)
		return;

	hexify(hexdata, buf, buf_len);
	gdb_putpacket2("O", 1, hexdata, 2U * buf_len);
	free(hexdata);
}

void gdb_voutf(const char *const fmt, va_list ap)
{
	char *buf;
	if (vasprintf(&buf, fmt, ap) < 0)
		return;

	gdb_out(buf);
	free(buf);
}

void gdb_outf(const char *const fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	gdb_voutf(fmt, ap);
	va_end(ap);
}
