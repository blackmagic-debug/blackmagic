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

#include <stdarg.h>

int gdb_getpacket(char *packet, int size)
{
	unsigned char c;
	unsigned char csum;
	char recv_csum[3];
	int i;

	while(1) {
		/* Wait for packet start */
		while((packet[0] = gdb_if_getchar()) != '$')
			if(packet[0] == 0x04) return 1;

		i = 0; csum = 0;
		/* Capture packet data into buffer */
		while((c = gdb_if_getchar()) != '#') {

			if(i == size) break; /* Oh shit */

			if(c == '$') { /* Restart capture */
				i = 0;
				csum = 0;
				continue;
			}
			if(c == '}') { /* escaped char */
				c = gdb_if_getchar();
				csum += c + '}';
				packet[i++] = c ^ 0x20;
				continue;
			}
			csum += c;
			packet[i++] = c;
		}
		recv_csum[0] = gdb_if_getchar();
		recv_csum[1] = gdb_if_getchar();
		recv_csum[2] = 0;

		/* return packet if checksum matches */
		if(csum == strtol(recv_csum, NULL, 16)) break;

		/* get here if checksum fails */
		gdb_if_putchar('-', 1); /* send nack */
	}
	gdb_if_putchar('+', 1); /* send ack */
	packet[i] = 0;

#ifdef DEBUG_GDBPACKET
	DEBUG("%s : ", __func__);
	for(int j = 0; j < i; j++) {
		c = packet[j];
		if ((c >= 32) && (c < 127))
			DEBUG("%c", c);
		else
			DEBUG("\\x%02X", c);
	}
	DEBUG("\n");
#endif
	return i;
}

void gdb_putpacket(const char *packet, int size)
{
	int i;
	unsigned char csum;
	unsigned char c;
	char xmit_csum[3];
	int tries = 0;

	do {
#ifdef DEBUG_GDBPACKET
		DEBUG("%s : ", __func__);
#endif
		csum = 0;
		gdb_if_putchar('$', 0);
		for(i = 0; i < size; i++) {
			c = packet[i];
#ifdef DEBUG_GDBPACKET
			if ((c >= 32) && (c < 127))
				DEBUG("%c", c);
			else
				DEBUG("\\x%02X", c);
#endif
			if((c == '$') || (c == '#') || (c == '}')) {
				gdb_if_putchar('}', 0);
				gdb_if_putchar(c ^ 0x20, 0);
				csum += '}' + (c ^ 0x20);
			} else {
				gdb_if_putchar(c, 0);
				csum += c;
			}
		}
		gdb_if_putchar('#', 0);
		sprintf(xmit_csum, "%02X", csum);
		gdb_if_putchar(xmit_csum[0], 0);
		gdb_if_putchar(xmit_csum[1], 1);
#ifdef DEBUG_GDBPACKET
		DEBUG("\n");
#endif
	} while((gdb_if_getchar_to(2000) != '+') && (tries++ < 3));
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
	char *hexdata;
	int i;

	hexdata = alloca((i = strlen(buf)*2 + 1) + 1);
	hexdata[0] = 'O';
	hexify(hexdata+1, buf, strlen(buf));
	gdb_putpacket(hexdata, i);
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
