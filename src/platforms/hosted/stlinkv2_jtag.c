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

#define STLINK_JTAG_MAX_DEVS 2U

static int stlink_enter_debug_jtag(void);
static size_t stlink_read_idcodes(uint32_t *idcodes);

bool stlink_jtag_scan(void)
{
	uint32_t idcodes[STLINK_JTAG_MAX_DEVS];
	target_list_free();

	jtag_dev_count = 0;
	memset(jtag_devs, 0, sizeof(jtag_devs));
	if (stlink_enter_debug_jtag())
		return false;
	jtag_dev_count = stlink_read_idcodes(idcodes);
	/* Check for known devices and handle accordingly */
	for (uint32_t i = 0; i < jtag_dev_count; ++i)
		jtag_devs[i].jd_idcode = idcodes[i];

	for (uint32_t i = 0; i < jtag_dev_count; ++i) {
		for (size_t j = 0; dev_descr[j].idcode; ++j) {
			if ((jtag_devs[i].jd_idcode & dev_descr[j].idmask) == dev_descr[j].idcode) {
				if (dev_descr[j].handler)
					dev_descr[j].handler(i);
				break;
			}
		}
	}
	return jtag_dev_count > 0;
}

static int stlink_enter_debug_jtag(void)
{
	stlink_leave_state();
	uint8_t data[2];
	stlink_simple_request(
		STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_ENTER, STLINK_DEBUG_ENTER_JTAG_NO_RESET, data, sizeof(data));
	return stlink_usb_error_check(data, true);
}

static size_t stlink_read_idcodes(uint32_t *idcodes)
{
	uint8_t data[12];
	stlink_simple_query(STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_READ_IDCODES, data, sizeof(data));
	if (stlink_usb_error_check(data, true))
		return 0;
	idcodes[0] = data[4] | (data[5] << 8U) | (data[6] << 16U) | (data[7] << 24U);
	idcodes[1] = data[8] | (data[9] << 8U) | (data[10] << 16U) | (data[11] << 24U);
	return STLINK_JTAG_MAX_DEVS;
}

void stlink_jtag_dp_init(adiv5_debug_port_s *dp)
{
	dp->error = stlink_dp_error;
	dp->low_access = stlink_raw_access;
	dp->abort = stlink_dp_abort;
}
