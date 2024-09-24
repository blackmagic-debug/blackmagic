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

/*
 * This file implements decoding of SWO data when that data is an ITM SWIT data stream.
 * It puts the decoded data onto the aux USB serial interface for consumption.
 */

#include "general.h"
#include "usb_serial.h"
#include "swo.h"

static uint8_t itm_decoded_buffer[CDCACM_PACKET_SIZE];
static uint16_t itm_decoded_buffer_index = 0;
static uint32_t itm_decode_mask = 0;  /* bitmask of channels to print */
static uint8_t itm_packet_length = 0; /* decoder state */
static bool itm_decode_packet = false;

uint16_t swo_itm_decode(const uint8_t *data, uint16_t len)
{
	/* Step through each byte in the SWO data buffer */
	for (uint16_t idx = 0; idx < len; ++idx) {
		/* If we're waiting for a new ITM packet, start decoding the new byte as a header */
		if (itm_packet_length == 0) {
			/* Check that the required to be 0 bit of the SWIT packet is, and that the size bits aren't 0 */
			if ((data[idx] & 0x04U) == 0U && (data[idx] & 0x03U) != 0U) {
				/* Now extract the stimulus port address (stream number) and payload size */
				uint8_t stream = data[idx] >> 3U;
				/* Map 1 -> 1, 2 -> 2, and 3 -> 4 */
				itm_packet_length = 1U << ((data[idx] & 3U) - 1U);
				/* Determine if the packet should be displayed */
				itm_decode_packet = (itm_decode_mask & (1U << stream)) != 0U;
			} else {
				/* If the bit is not 0, this is an invalid SWIT packet, so reset state */
				itm_decode_packet = false;
				itm_decoded_buffer_index = 0;
			}
		} else {
			/* If we should actually decode this packet, then forward the data to the decoded data buffer */
			if (itm_decode_packet) {
				itm_decoded_buffer[itm_decoded_buffer_index++] = data[idx];
				/* If the buffer has filled up and needs flushing, try to flush the data to the serial endpoint */
				if (itm_decoded_buffer_index == sizeof(itm_decoded_buffer)) {
					/* However, if the link is not yet up, drop the packet data silently */
					if (usb_get_config() && gdb_serial_get_dtr())
						debug_serial_send_stdout(itm_decoded_buffer, itm_decoded_buffer_index);
					itm_decoded_buffer_index = 0U;
				}
			}
			/* Mark the byte consumed regardless */
			--itm_packet_length;
		}
	}
	return len;
}

void swo_itm_decode_set_mask(uint32_t mask)
{
	itm_decode_mask = mask;
}
