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

/* This file implements the GDB Remote Serial Debugging protocol packet
 * reception and transmission as well as some convenience functions.
 */

#include "general.h"
#include "gdb_if.h"
#include "gdb_packet.h"
#include "hex_utils.h"
#include "remote.h"

#include <stdarg.h>

size_t gdb_getpacket(char *packet, size_t size)
{
	unsigned char csum;
	char recv_csum[3];
	size_t offset = 0;

	while (1) {
		/* Wait for packet start */
		do {
			/* Spin waiting for a start of packet character - either a gdb
             * start ('$') or a BMP remote packet start ('!').
			 */
			do {
				/* Smells like bad code */
				packet[0] = (char)gdb_if_getchar();
				if (packet[0] == 0x04)
					return 1;
			} while ((packet[0] != '$') && (packet[0] != REMOTE_SOM));
#if PC_HOSTED == 0
			if (packet[0] == REMOTE_SOM) {
				/* This is probably a remote control packet
				 * - get and handle it */
				offset = 0;
				bool gettingRemotePacket = true;
				while (gettingRemotePacket) {
					/* Smells like bad code */
					const char c = (char)gdb_if_getchar();
					switch (c) {
					case REMOTE_SOM: /* Oh dear, packet restarts */
						offset = 0;
						break;

					case REMOTE_EOM: /* Complete packet for processing */
						packet[offset] = 0;
						remotePacketProcess(offset, packet);
						gettingRemotePacket = false;
						break;

					case '$': /* A 'real' gdb packet, best stop squatting now */
						packet[0] = '$';
						gettingRemotePacket = false;
						break;

					default:
						if (offset < size) {
							packet[offset++] = c;
						} else {
							/* Who knows what is going on...return to normality */
							gettingRemotePacket = false;
						}
						break;
					}
				}
				/* Reset the packet buffer start character to zero, because function
				 * 'remotePacketProcess()' above overwrites this buffer, and
				 * an arbitrary character may have been placed there. If this is a '$'
				 * character, this will cause this loop to be terminated, which is wrong.
				 */
				packet[0] = 0;
			}
#endif
		} while (packet[0] != '$');

		offset = 0;
		csum = 0;
		char c;
		/* Capture packet data into buffer */
		while ((c = (char)gdb_if_getchar()) != '#') {
			/* If we run out of buffer space, exit early */
			if (offset == size)
				break;

			if (c == '$') { /* Restart capture */
				offset = 0;
				csum = 0;
				continue;
			}
			if (c == '}') { /* escaped char */
				c = gdb_if_getchar();
				csum += c + '}';
				packet[offset++] = c ^ 0x20;
				continue;
			}
			csum += c;
			packet[offset++] = c;
		}
		recv_csum[0] = (char)gdb_if_getchar();
		recv_csum[1] = (char)gdb_if_getchar();
		recv_csum[2] = 0;

		/* return packet if checksum matches */
		if (csum == strtol(recv_csum, NULL, 16))
			break;

		/* get here if checksum fails */
		gdb_if_putchar('-', 1); /* send nack */
	}
	gdb_if_putchar('+', 1); /* send ack */
	packet[offset] = 0;

#if PC_HOSTED == 1
	DEBUG_GDB_WIRE("%s : ", __func__);
	for (size_t j = 0; j < offset; j++) {
		const char c = packet[j];
		if (c >= ' ' && c < 0x7F)
			DEBUG_GDB_WIRE("%c", c);
		else
			DEBUG_GDB_WIRE("\\x%02X", c);
	}
	DEBUG_GDB_WIRE("\n");
#endif
	return offset;
}

static void gdb_next_char(char c, unsigned char *csum)
{
#if PC_HOSTED == 1
	if ((c >= 32) && (c < 127))
		DEBUG_GDB_WIRE("%c", c);
	else
		DEBUG_GDB_WIRE("\\x%02X", c);
#endif
	if ((c == '$') || (c == '#') || (c == '}') || (c == '*')) {
		gdb_if_putchar('}', 0);
		gdb_if_putchar(c ^ 0x20, 0);
		*csum += '}' + (c ^ 0x20);
	} else {
		gdb_if_putchar(c, 0);
		*csum += c;
	}
}

void gdb_putpacket2(const char *packet1, size_t size1, const char *packet2, size_t size2)
{
	char xmit_csum[3];
	size_t tries = 0;

	do {
		DEBUG_GDB_WIRE("%s: ", __func__);
		unsigned char csum = 0;
		gdb_if_putchar('$', 0);

		for (size_t i = 0; i < size1; ++i)
			gdb_next_char(packet1[i], &csum);
		for (size_t i = 0; i < size2; ++i)
			gdb_next_char(packet2[i], &csum);

		gdb_if_putchar('#', 0);
		snprintf(xmit_csum, sizeof(xmit_csum), "%02X", csum);
		gdb_if_putchar(xmit_csum[0], 0);
		gdb_if_putchar(xmit_csum[1], 1);
		DEBUG_GDB_WIRE("\n");
	} while (gdb_if_getchar_to(2000) != '+' && tries++ < 3);
}

void gdb_putpacket(const char *packet, size_t size)
{
	char xmit_csum[3];
	size_t tries = 0;

	do {
		DEBUG_GDB_WIRE("%s: ", __func__);
		unsigned char csum = 0;
		gdb_if_putchar('$', 0);
		for (size_t i = 0; i < size; ++i)
			gdb_next_char(packet[i], &csum);
		gdb_if_putchar('#', 0);
		snprintf(xmit_csum, sizeof(xmit_csum), "%02X", csum);
		gdb_if_putchar(xmit_csum[0], 0);
		gdb_if_putchar(xmit_csum[1], 1);
		DEBUG_GDB_WIRE("\n");
	} while (gdb_if_getchar_to(2000) != '+' && tries++ < 3);
}

void gdb_put_notification(const char *const packet, const size_t size)
{
	char xmit_csum[3];

	DEBUG_GDB_WIRE("%s: ", __func__);
	uint8_t csum = 0;
	gdb_if_putchar('%', 0);
	for (size_t i = 0; i < size; ++i)
		gdb_next_char(packet[i], &csum);
	gdb_if_putchar('#', 0);
	snprintf(xmit_csum, sizeof(xmit_csum), "%02X", csum);
	gdb_if_putchar(xmit_csum[0], 0);
	gdb_if_putchar(xmit_csum[1], 1);
	DEBUG_GDB_WIRE("\n");
}

void gdb_putpacket_f(const char *fmt, ...)
{
	va_list ap;
	char *buf;
	int size;

	va_start(ap, fmt);
	size = vasprintf(&buf, fmt, ap);
	gdb_putpacket(buf, size);
	free(buf);
	va_end(ap);
}

void gdb_out(const char *buf)
{
	int l = strlen(buf);
	char *hexdata = calloc(1, 2 * l + 1);
	if (!hexdata)
		return;
	hexify(hexdata, buf, l);
	gdb_putpacket2("O", 1, hexdata, 2 * l);
	free(hexdata);
}

void gdb_voutf(const char *fmt, va_list ap)
{
	char *buf;

	if (vasprintf(&buf, fmt, ap) < 0)
		return;
	gdb_out(buf);
	free(buf);
}

void gdb_outf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	gdb_voutf(fmt, ap);
	va_end(ap);
}
