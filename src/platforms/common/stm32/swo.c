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

#include "swo.h"
#include "swo_internal.h"

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

swo_coding_e swo_current_coding;

void swo_deinit(void)
{
#if SWO_ENCODING == 1 || SWO_ENCODING == 3
	if (swo_current_coding == swo_manchester)
		swo_manchester_deinit();
#endif
#if SWO_ENCODING == 2 || SWO_ENCODING == 3
	if (swo_current_coding == swo_nrz_uart)
		swo_uart_deinit();
#endif
	swo_current_coding = swo_none;
}

void swo_send_buffer(usbd_device *const dev, const uint8_t ep)
{
#if SWO_ENCODING == 1 || SWO_ENCODING == 3
	if (swo_current_coding == swo_manchester)
		swo_manchester_send_buffer(dev, ep);
#endif
#if SWO_ENCODING == 2 || SWO_ENCODING == 3
	if (swo_current_coding == swo_nrz_uart)
		swo_uart_send_buffer(dev, ep);
#endif
}
