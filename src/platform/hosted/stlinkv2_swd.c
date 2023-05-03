/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

#include "general.h"
#include "stlinkv2.h"
#include "stlinkv2_protocol.h"
#include "jtag_devs.h"
#include "target_internal.h"

uint32_t stlink_swd_scan(void)
{
	target_list_free();

	stlink_leave_state();

	const stlink_simple_request_s command = {
		.command = STLINK_DEBUG_COMMAND,
		.operation = STLINK_DEBUG_APIV2_ENTER,
		.param = STLINK_DEBUG_ENTER_SWD_NO_RESET,
	};
	uint8_t data[2];
	stlink_send_recv_retry(&command, sizeof(command), data, sizeof(data));
	if (stlink_usb_error_check(data, true))
		return 0;

	adiv5_debug_port_s *dp = calloc(1, sizeof(*dp));
	if (!dp) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return 0;
	}

	dp->dp_read = firmware_swdp_read;
	dp->error = stlink_dp_error;
	dp->low_access = stlink_raw_access;
	dp->abort = stlink_dp_abort;

	adiv5_dp_error(dp);
	adiv5_dp_init(dp, 0);
	return target_list ? 1U : 0U;
}
