/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
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

#include "general.h"
#include "platform.h"
#include "gdb_packet.h"
#include "swo.h"
#include "swo_internal.h"

#include <stdatomic.h>
#include <malloc.h>
#include <libopencmsis/core_cm3.h>

/*
 * Management and muxing layer for the SWO implementations
 *
 * SWO_ENCODING takes 3 possible states guaranteed by the build system:
 * 1: Manchester coded SWO only
 * 2: UART/NRZ coded SWO only
 * 3: Both enabled w/ the full switching mechanism provided
 *
 * It is an error for SWO_ENCODING to be undefined if PLATFORM_HAS_TRACESWO is
 * defined by the platform. It is an error to include this file in the build
 * under this circumstance as it requires SWO_ENCODING to be defined and valid.
 */

/* Current SWO decoding mode being used */
swo_coding_e swo_current_mode;

/* Whether ITM decoding is engaged */
bool swo_itm_decoding = false;

/*
 * Dynamically-allocated data buffer, current read index, current write index,
 * current fill level, and total available buffer size. We initialise to 0 just
 * to get a consistent starting point, but the indexes do not matter once up and
 * running. It only matters the post-condition of swo_deinit() that no bytes
 * are available and the indicies are equal to each other are kept for entry
 * into swo_init() and the successful execution of it all.
 */
uint8_t *swo_buffer;
uint16_t swo_buffer_read_index = 0U;
uint16_t swo_buffer_write_index = 0U;
_Atomic uint16_t swo_buffer_bytes_available = 0U;

void swo_init(const swo_coding_e swo_mode, const uint32_t baudrate, const uint32_t itm_stream_bitmask)
{
#if SWO_ENCODING == 1
	(void)baudrate;
#endif
	/* Make sure any existing SWO capture is first spun down */
	if (swo_current_mode != swo_none)
		swo_deinit(false);
	/* If we're spinning this up fresh, allocate a buffer for the data */
	else {
		/*
		 * This needs to be at least 2 endpoint buffers large, more is better to a point but
		 * it has diminishing returns. Aim for no more than 8KiB of buffer as after that, larger
		 * is entirely pointless.
		 */
		swo_buffer = malloc(SWO_BUFFER_SIZE);
		/* Check for allocation failure and abort initialisation if we see it failed */
		if (!swo_buffer) {
			DEBUG_ERROR("malloc: failed in %s\n", __func__);
			return;
		}
	}

	/* Configure the ITM decoder and state */
	swo_itm_decode_set_mask(itm_stream_bitmask);
	swo_itm_decoding = itm_stream_bitmask != 0;

	/* Now determine which mode to enable and initialise it */
#if SWO_ENCODING == 1 || SWO_ENCODING == 3
	if (swo_mode == swo_manchester)
		swo_manchester_init();
#endif
#if SWO_ENCODING == 2 || SWO_ENCODING == 3
	if (swo_mode == swo_nrz_uart) {
		/* Ensure the baud rate is something sensible */
		swo_uart_init(baudrate ? baudrate : SWO_DEFAULT_BAUD);
		gdb_outf("Baudrate: %" PRIu32 " ", swo_uart_get_baudrate());
	}
#endif
	/* Make a note of which mode we initialised into */
	swo_current_mode = swo_mode;
}

void swo_deinit(const bool deallocate)
{
#if SWO_ENCODING == 1 || SWO_ENCODING == 3
	if (swo_current_mode == swo_manchester)
		swo_manchester_deinit();
#endif
#if SWO_ENCODING == 2 || SWO_ENCODING == 3
	if (swo_current_mode == swo_nrz_uart)
		swo_uart_deinit();
#endif

	/* Spin waiting for all data to finish being transmitted */
	while (swo_buffer_bytes_available) {
		swo_send_buffer(usbdev, SWO_ENDPOINT);
		__WFI();
	}

	/* If we're being asked to give the SWO buffer back, then free it */
	if (deallocate)
		free(swo_buffer);
	swo_current_mode = swo_none;
}

void swo_send_buffer(usbd_device *const dev, const uint8_t ep)
{
	/* NOTLINTNEXTLINE(clang-diagnostic-error) */
	static atomic_flag reentry_flag = ATOMIC_FLAG_INIT;

	/* If we are already in this routine then we don't need to come in again */
	if (atomic_flag_test_and_set_explicit(&reentry_flag, memory_order_relaxed))
		return;

	const uint16_t bytes_available = swo_buffer_bytes_available;
	/*
	 * If there is somthing to move, move the next up-to SWO_ENDPOINT_SIZE bytes chunk of it (USB)
	 * or the whole lot (ITM decoding) as appropriate
	 */
	if (bytes_available) {
		uint16_t result;
		/* If we're doing decoding, hand the data to the ITM decoder */
		if (swo_itm_decoding) {
			/* If we're in UART mode, hand as much as we can all at once */
			if (swo_current_mode == swo_nrz_uart)
				result = swo_itm_decode(
					swo_buffer + swo_buffer_read_index, MIN(bytes_available, SWO_BUFFER_SIZE - swo_buffer_read_index));
			/* Otherwise, if we're in Manchester mode, manage the amount moved the same as we do USB */
			else
				result = swo_itm_decode(swo_buffer + swo_buffer_read_index, MIN(bytes_available, SWO_ENDPOINT_SIZE));
		} else
			/* Otherwise, queue the new data to the SWO data endpoint */
			result = usbd_ep_write_packet(
				dev, ep, swo_buffer + swo_buffer_read_index, MIN(bytes_available, SWO_ENDPOINT_SIZE));

		/* If we actually queued/processed some data, update indicies etc */
		if (result) {
			/*
			 * Update the amount read and consumed */
			swo_buffer_read_index += result;
			swo_buffer_read_index &= SWO_BUFFER_SIZE - 1U;
			swo_buffer_bytes_available -= result;
		}
	}

	atomic_flag_clear_explicit(&reentry_flag, memory_order_relaxed);
}
