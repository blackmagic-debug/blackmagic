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

/* This file implements a transparent channel over which the GDB Remote
 * Serial Debugging protocol is implemented.  This implementation for STM32
 * uses the USB CDC-ACM device bulk endpoints to implement the channel.
 */
#include "platform.h"
#include <libopencm3/usb/usbd.h>

#include "gdb_if.h"

static uint32_t count_out;
static uint32_t count_in;
static uint32_t out_ptr;
static uint8_t buffer_out[CDCACM_PACKET_SIZE];
static uint8_t buffer_in[CDCACM_PACKET_SIZE];

void gdb_if_putchar(unsigned char c, int flush)
{
	buffer_in[count_in++] = c;
	if(flush || (count_in == CDCACM_PACKET_SIZE)) {
		/* Refuse to send if USB isn't configured, and
		 * don't bother if nobody's listening */
		if((cdcacm_get_config() != 1) || !cdcacm_get_dtr()) {
			count_in = 0;
			return;
		}
		while(usbd_ep_write_packet(1, buffer_in, count_in) <= 0);
		count_in = 0;
	}
}

unsigned char gdb_if_getchar(void)
{
	while(!(out_ptr < count_out)) {
		/* Detach if port closed */
		if(!cdcacm_get_dtr())
			return 0x04;

		while(cdcacm_get_config() != 1);
		count_out = usbd_ep_read_packet(1, buffer_out, 
					CDCACM_PACKET_SIZE);
		out_ptr = 0;
	}

	return buffer_out[out_ptr++];
}

unsigned char gdb_if_getchar_to(int timeout)
{
	timeout_counter = timeout/100;

	if(!(out_ptr < count_out)) do {
		/* Detach if port closed */
		if(!cdcacm_get_dtr())
			return 0x04;

		count_out = usbd_ep_read_packet(1, buffer_out, 
					CDCACM_PACKET_SIZE);
		out_ptr = 0;
	} while(timeout_counter && !(out_ptr < count_out));

	if(out_ptr < count_out) 
		return gdb_if_getchar();

	return -1;
}

