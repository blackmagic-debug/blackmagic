/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * Updates for ctxLink by Sid Price - sid@sidprice.com
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
#include "WiFi_Server.h"

static uint32_t gdb_receive_amount_available;
static uint32_t gdb_send_amount_queued;
static uint32_t gdb_receive_index;
static char gdb_receive_buffer[CDCACM_PACKET_SIZE];
static char gdb_send_buffer[CDCACM_PACKET_SIZE];
static volatile uint32_t count_new;
static char double_buffer_out[CDCACM_PACKET_SIZE];

void gdb_usb_flush(const bool force)
{
	/* Flush only if there is data to flush */
	if (gdb_send_amount_queued == 0U)
		return;

	/* Refuse to send if USB isn't configured, and don't bother if nobody's listening */
	if (usb_get_config() != 1U || !gdb_serial_get_dtr()) {
		gdb_send_amount_queued = 0U;
		return;
	}
	while (usbd_ep_write_packet(usbdev, CDCACM_GDB_ENDPOINT, gdb_send_buffer, gdb_send_amount_queued) <= 0U)
		continue;

	/* We need to send an empty packet for some hosts to accept this as a complete transfer. */
	if (force && gdb_send_amount_queued == CDCACM_PACKET_SIZE) {
		/*
		 * libopencm3 needs a change for us to confirm when that transfer is complete,
		 * so we just send a packet containing a null character for now.
		 */
		while (usbd_ep_write_packet(usbdev, CDCACM_GDB_ENDPOINT, "\0", 1U) <= 0U)
			continue;
	}

	/* Reset the buffer */
	gdb_send_amount_queued = 0U;
}

void gdb_usb_putchar(const char ch, const bool flush)
{
	gdb_send_buffer[gdb_send_amount_queued++] = ch;
	if (flush || gdb_send_amount_queued == CDCACM_PACKET_SIZE)
		gdb_usb_flush(flush);
}

void gdb_usb_receive_callback(usbd_device *dev, uint8_t ep)
{
	(void)ep;
	usbd_ep_nak_set(dev, CDCACM_GDB_ENDPOINT, 1);
	count_new = usbd_ep_read_packet(dev, CDCACM_GDB_ENDPOINT, double_buffer_out, CDCACM_PACKET_SIZE);
	if (!count_new)
		usbd_ep_nak_set(dev, CDCACM_GDB_ENDPOINT, 0);
}

static void gdb_if_update_buf(void)
{
	while (usb_get_config() != 1)
		continue;
	cm_disable_interrupts();
	__asm__ volatile("isb");
	if (count_new) {
		memcpy(gdb_receive_buffer, double_buffer_out, count_new);
		gdb_receive_amount_available = count_new;
		count_new = 0;
		gdb_receive_index = 0;
		usbd_ep_nak_set(usbdev, CDCACM_GDB_ENDPOINT, 0);
	}
	cm_enable_interrupts();
	__asm__ volatile("isb");
	if (!gdb_receive_amount_available)
		__WFI();
}

char gdb_usb_getchar(void)
{
	while (gdb_receive_index >= gdb_receive_amount_available) {
		/*
		 * Detach if port closed
		 *
		 * The WFI here is safe because any interrupt, including the regular SysTick
		 * will cause the processor to resume from the WFI instruction.
		 */
		platform_tasks();
		if (!gdb_serial_get_dtr()) {
			__WFI();
			return '\x04';
		}

		gdb_if_update_buf();
	}

	return gdb_receive_buffer[gdb_receive_index++];
}

char gdb_usb_getchar_to(const uint32_t timeout)
{
	platform_timeout_s receive_timeout;
	platform_timeout_set(&receive_timeout, timeout);

	/* Wait while we need more data or until the timeout expires */
	while (gdb_receive_index >= gdb_receive_amount_available && !platform_timeout_is_expired(&receive_timeout)) {
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

	if (gdb_receive_index < gdb_receive_amount_available)
		return gdb_receive_buffer[gdb_receive_index++];
	/* TODO Need to find a better way to error return than this. This provides '\xff' characters. */
	return -1;
}

void gdb_if_putchar(const char ch, const bool flush)
{
	if (is_gdb_client_connected())
		wifi_gdb_putchar(ch, flush);
	else
		gdb_usb_putchar(ch, flush);
}

void gdb_if_flush(const bool force)
{
	if (is_gdb_client_connected())
		wifi_gdb_flush(force);
	else
		gdb_usb_flush(force);
}

char gdb_if_getchar(void)
{
	platform_tasks();
	if (is_gdb_client_connected())
		return wifi_get_next();
	else if (usb_get_config() == 1)
		return gdb_usb_getchar();
	else
		return 0xff;
}

char gdb_if_getchar_to(uint32_t timeout)
{
	platform_tasks();
	/* NOLINTNEXTLINE(bugprone-branch-clone) */
	if (is_gdb_client_connected())
		return wifi_get_next_to(timeout);
	return gdb_usb_getchar_to(timeout);
}
