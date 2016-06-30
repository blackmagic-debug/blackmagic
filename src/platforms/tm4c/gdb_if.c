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

#include "general.h"
#include "gdb_if.h"
#include "cdcacm.h"

#include <libopencm3/usb/usbd.h>

static volatile uint32_t head_out, tail_out;
static volatile uint32_t count_in;
static volatile uint8_t buffer_out[16*CDCACM_PACKET_SIZE];
static volatile uint8_t buffer_in[CDCACM_PACKET_SIZE];

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
		while(usbd_ep_write_packet(usbdev, CDCACM_GDB_ENDPOINT,
			(uint8_t *)buffer_in, count_in) <= 0);
		count_in = 0;
	}
}

void gdb_usb_out_cb(usbd_device *dev, uint8_t ep)
{
	(void)ep;
	static uint8_t buf[CDCACM_PACKET_SIZE];

	usbd_ep_nak_set(dev, CDCACM_GDB_ENDPOINT, 1);
        uint32_t count = usbd_ep_read_packet(dev, CDCACM_GDB_ENDPOINT,
                                        (uint8_t *)buf, CDCACM_PACKET_SIZE);

	
	uint32_t idx;
	for (idx=0; idx<count; idx++) {
		buffer_out[head_out++ % sizeof(buffer_out)] = buf[idx];
	}

	usbd_ep_nak_set(dev, CDCACM_GDB_ENDPOINT, 0);
}

unsigned char gdb_if_getchar(void)
{

	while(tail_out == head_out) {
		/* Detach if port closed */
		if(!cdcacm_get_dtr())
			return 0x04;

		while(cdcacm_get_config() != 1);
	}

	return buffer_out[tail_out++ % sizeof(buffer_out)];
}

unsigned char gdb_if_getchar_to(int timeout)
{
	platform_timeout t;
	platform_timeout_set(&t, timeout);

	if(head_out == tail_out) do {
		/* Detach if port closed */
		if(!cdcacm_get_dtr())
			return 0x04;

		while(cdcacm_get_config() != 1);
	} while(!platform_timeout_is_expired(&t) && head_out == tail_out);

	if(head_out != tail_out)
		return gdb_if_getchar();

	return -1;
}

