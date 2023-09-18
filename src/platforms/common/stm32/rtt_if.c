/*
 * This file is part of the Black Magic Debug project.
 *
 * MIT License
 *
 * Copyright (c) 2021 Koen De Vleeschauwer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "general.h"
#include "platform.h"
#include <assert.h>
#include "usb_serial.h"
#include "rtt.h"
#include "rtt_if.h"

/*********************************************************************
*
*       rtt terminal i/o
*
**********************************************************************
*/

/* usb uart receive buffer */
static char recv_buf[RTT_DOWN_BUF_SIZE];
static uint32_t recv_head = 0;
static uint32_t recv_tail = 0;

/* data from host to target: number of free bytes in usb receive buffer */
inline static uint32_t recv_bytes_free()
{
	if (recv_tail <= recv_head)
		return RTT_DOWN_BUF_SIZE - recv_head + recv_tail - 1U;
	return recv_tail - recv_head - 1U;
}

/* data from host to target: true if not enough free buffer space and we need to close flow control */
inline static bool recv_set_nak()
{
	assert(sizeof(recv_buf) > 2U * CDCACM_PACKET_SIZE);
	return recv_bytes_free() < 2U * CDCACM_PACKET_SIZE;
}

/* debug_serial_receive_callback is called when usb uart has received new data for target.
   this routine has to be fast */

void rtt_serial_receive_callback(usbd_device *dev, uint8_t ep)
{
	(void)dev;
	(void)ep;
	char usb_buf[CDCACM_PACKET_SIZE];

	/* close flow control while processing packet */
	usbd_ep_nak_set(usbdev, CDCACM_UART_ENDPOINT, 1);

	const uint16_t len = usbd_ep_read_packet(usbdev, CDCACM_UART_ENDPOINT, usb_buf, CDCACM_PACKET_SIZE);

	/* skip flag: drop packet if not enough free buffer space */
	if (rtt_flag_skip && len > recv_bytes_free()) {
		usbd_ep_nak_set(usbdev, CDCACM_UART_ENDPOINT, 0);
		return;
	}

	/* copy data to recv_buf */
	for (int i = 0; i < len; i++) {
		uint32_t next_recv_head = (recv_head + 1U) % sizeof(recv_buf);
		if (next_recv_head == recv_tail)
			break; /* overflow */
		recv_buf[recv_head] = usb_buf[i];
		recv_head = next_recv_head;
	}

	/* block flag: flow control closed if not enough free buffer space */
	if (!(rtt_flag_block && recv_set_nak()))
		usbd_ep_nak_set(usbdev, CDCACM_UART_ENDPOINT, 0);
}

/* rtt host to target: read one character */
int32_t rtt_getchar()
{
	int retval;

	if (recv_head == recv_tail)
		return -1;
	retval = (uint8_t)recv_buf[recv_tail];
	recv_tail = (recv_tail + 1U) % sizeof(recv_buf);

	/* open flow control if enough free buffer space */
	if (!recv_set_nak())
		usbd_ep_nak_set(usbdev, CDCACM_UART_ENDPOINT, 0);

	return retval;
}

/* rtt host to target: true if no characters available for reading */
bool rtt_nodata()
{
	return recv_head == recv_tail;
}

/* rtt target to host: write string */
uint32_t rtt_write(const char *buf, uint32_t len)
{
	if (len != 0 && usbdev && usb_get_config() && gdb_serial_get_dtr()) {
		for (uint32_t p = 0; p < len; p += CDCACM_PACKET_SIZE) {
			uint32_t plen = MIN(CDCACM_PACKET_SIZE, len - p);
			uint32_t start_ms = platform_time_ms();
			while (usbd_ep_write_packet(usbdev, CDCACM_UART_ENDPOINT, buf + p, plen) <= 0) {
				if (platform_time_ms() - start_ms >= 25)
					return 0; /* drop silently */
			}
		}
		/* flush 64-byte packet on full-speed */
		if (CDCACM_PACKET_SIZE == 64 && (len % CDCACM_PACKET_SIZE) == 0)
			usbd_ep_write_packet(usbdev, CDCACM_UART_ENDPOINT, NULL, 0);
	}
	return len;
}
