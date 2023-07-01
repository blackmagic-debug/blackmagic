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
 * This file implements a transparent channel over which the GDB Remote
 * Serial Debugging protocol is implemented. This implementation for STM32
 * uses the USB CDC-ACM device bulk endpoints to implement the channel.
 */

#include <libopencmsis/core_cm3.h>

#include "general.h"
#include "platform.h"
#include "usb_serial.h"
#include "gdb_if.h"

static uint32_t count_out;
static uint32_t count_in;
static uint32_t out_ptr;
static char buffer_out[CDCACM_PACKET_SIZE];
static char buffer_in[CDCACM_PACKET_SIZE];
#if defined(STM32F4) || defined(STM32F7)
static volatile uint32_t count_new;
static char double_buffer_out[CDCACM_PACKET_SIZE];
#endif

void gdb_if_putchar(const char c, const int flush)
{
	buffer_in[count_in++] = c;
	if (flush || count_in == CDCACM_PACKET_SIZE) {
		/* Refuse to send if USB isn't configured, and
		 * don't bother if nobody's listening */
		if (usb_get_config() != 1 || !gdb_serial_get_dtr()) {
			count_in = 0;
			return;
		}
		while (usbd_ep_write_packet(usbdev, CDCACM_GDB_ENDPOINT, buffer_in, count_in) <= 0)
			continue;

		if (flush && count_in == CDCACM_PACKET_SIZE) {
			/* We need to send an empty packet for some hosts
			 * to accept this as a complete transfer. */
			/* libopencm3 needs a change for us to confirm when
			 * that transfer is complete, so we just send a packet
			 * containing a null byte for now.
			 */
			while (usbd_ep_write_packet(usbdev, CDCACM_GDB_ENDPOINT, "\0", 1) <= 0)
				continue;
		}

		count_in = 0;
	}
}

#if defined(STM32F4) || defined(STM32F7)
void gdb_usb_out_cb(usbd_device *dev, uint8_t ep)
{
	(void)ep;
	usbd_ep_nak_set(dev, CDCACM_GDB_ENDPOINT, 1);
	count_new = usbd_ep_read_packet(dev, CDCACM_GDB_ENDPOINT, double_buffer_out, CDCACM_PACKET_SIZE);
	if (!count_new)
		usbd_ep_nak_set(dev, CDCACM_GDB_ENDPOINT, 0);
}
#endif

static void gdb_if_update_buf(void)
{
	while (usb_get_config() != 1)
		continue;
#if !defined(STM32F4) && !defined(STM32F7)
	count_out = usbd_ep_read_packet(usbdev, CDCACM_GDB_ENDPOINT, buffer_out, CDCACM_PACKET_SIZE);
	out_ptr = 0;
#else
	__asm__ volatile("cpsid i; isb");
	if (count_new) {
		memcpy(buffer_out, double_buffer_out, count_new);
		count_out = count_new;
		count_new = 0;
		out_ptr = 0;
		usbd_ep_nak_set(usbdev, CDCACM_GDB_ENDPOINT, 0);
	}
	__asm__ volatile("cpsie i; isb");
#endif
	if (!count_out)
		__WFI();
}

char gdb_if_getchar(void)
{
	while (out_ptr >= count_out) {
		/*
		 * Detach if port closed
		 *
		 * The WFI here is safe because any interrupt, including the regular SysTick
		 * will cause the processor to resume from the WFI instruction.
		 */
		if (!gdb_serial_get_dtr()) {
			__WFI();
			return '\x04';
		}

		gdb_if_update_buf();
	}

	return buffer_out[out_ptr++];
}

char gdb_if_getchar_to(const uint32_t timeout)
{
	platform_timeout_s receive_timeout;
	platform_timeout_set(&receive_timeout, timeout);

	/* Wait while we need more data or until the timeout expires */
	while (out_ptr >= count_out && !platform_timeout_is_expired(&receive_timeout)) {
		/*
		 * Detach if port closed
		 *
		 * The WFI here is safe because any interrupt, including the regular SysTick
		 * will cause the processor to resume from the WFI instruction.
		 */
		if (!gdb_serial_get_dtr()) {
			__WFI();
			return '\x04';
		}
		gdb_if_update_buf();
	}

	if (out_ptr < count_out)
		return buffer_out[out_ptr++];
	/* XXX: Need to find a better way to error return than this. This provides '\xff' characters. */
	return -1;
}
