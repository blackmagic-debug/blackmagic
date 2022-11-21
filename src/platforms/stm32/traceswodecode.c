/*
 * This file is part of the Black Magic Debug project.
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	 If not, see <http://www.gnu.org/licenses/>.
 */

/* Print decoded swo stream on the usb serial */

#include "general.h"
#include "usb_serial.h"
#include "traceswo.h"

/* SWO decoding */
/* data is static in case swo packet is astride two buffers */
static uint8_t swo_buf[CDCACM_PACKET_SIZE];
static int swo_buf_len = 0;
static uint32_t swo_decode = 0; /* bitmask of channels to print */
static int swo_pkt_len = 0;     /* decoder state */
static bool swo_print = false;

/* print decoded swo packet on usb serial */
uint16_t traceswo_decode(usbd_device *usbd_dev, uint8_t addr, const void *buf, uint16_t len)
{
	if (usbd_dev == NULL)
		return 0;
	const uint8_t *const data = (const uint8_t *)buf;
	for (uint16_t i = 0; i < len; i++) {
		const uint8_t ch = data[i];
		if (swo_pkt_len == 0) {                    /* header */
			uint32_t channel = (uint32_t)ch >> 3U; /* channel number */
			uint32_t size = ch & 0x7U;             /* drop channel number */
			if (size == 0x01U)
				swo_pkt_len = 1; /* SWO packet 0x01XX */
			else if (size == 0x02U)
				swo_pkt_len = 2; /* SWO packet 0x02XXXX */
			else if (size == 0x03U)
				swo_pkt_len = 4; /* SWO packet 0x03XXXXXXXX */
			swo_print = (swo_pkt_len != 0) && ((swo_decode & (1UL << channel)) != 0UL);
		} else if (swo_pkt_len <= 4) { /* data */
			if (swo_print) {
				swo_buf[swo_buf_len++] = ch;
				if (swo_buf_len == sizeof(swo_buf)) {
					if (usb_get_config() && gdb_serial_get_dtr()) /* silently drop if usb not ready */
						usbd_ep_write_packet(usbd_dev, addr, swo_buf, swo_buf_len);
					swo_buf_len = 0;
				}
			}
			--swo_pkt_len;
		} else { /* recover */
			swo_buf_len = 0;
			swo_pkt_len = 0;
		}
	}
	return len;
}

/* set bitmask of swo channels to be decoded */
void traceswo_setmask(uint32_t mask)
{
	swo_decode = mask;
}

/* not truncated */
