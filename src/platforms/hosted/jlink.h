/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019 Uwe Bonnes
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

#ifndef PLATFORMS_HOSTED_JLINK_H
#define PLATFORMS_HOSTED_JLINK_H

#include <stdbool.h>
#include "bmp_hosted.h"

/** @cond PRIVATE */
#define CMD_GET_VERSION    0x01U
#define CMD_SET_SPEED      0x05U
#define CMD_GET_HW_STATUS  0x07U
#define CMD_GET_SPEEDS     0xc0U
#define CMD_GET_SELECT_IF  0xc7U
#define CMD_HW_JTAG3       0xcfU
#define CMD_HW_RESET0      0xdcU
#define CMD_HW_RESET1      0xddU
#define CMD_GET_CAPS       0xe8U
#define CMD_GET_EXT_CAPS   0xedU
#define CMD_GET_HW_VERSION 0xf0U

#define JLINK_IF_GET_ACTIVE    0xfeU
#define JLINK_IF_GET_AVAILABLE 0xffU

#define JLINK_CAP_GET_SPEEDS     (1U << 9U)
#define JLINK_CAP_GET_HW_VERSION (1U << 1U)
#define JLINK_IF_JTAG            1U
#define JLINK_IF_SWD             2U

#define SELECT_IF_JTAG 0
#define SELECT_IF_SWD  1

#if HOSTED_BMP_ONLY == 1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
bool jlink_init(bmp_info_s *info)
{
	return false;
}
uint32_t jlink_swdp_scan(bmp_info_s *info)
{
	return 0;
}
bool jlink_jtagtap_init(bmp_info_s *info)
{
	return false;
}
const char *jlink_target_voltage(bmp_info_s *info)
{
	return "ERROR";
}
void jlink_nrst_set_val(bmp_info_s *info, bool assert)
{
}
bool jlink_nrst_get_val(bmp_info_s *info)
{
	return true;
}
void jlink_max_frequency_set(bmp_info_s *info, uint32_t freq)
{
}
uint32_t jlink_max_frequency_get(bmp_info_s *info)
{
	return 0;
}
#pragma GCC diagnostic pop
#else
/** Device capabilities. (from openocd*/
enum jaylink_device_capability {
	/** Device supports retrieval of the hardware version. */
	JAYLINK_DEV_CAP_GET_HW_VERSION = 1,
	/** Device supports adaptive clocking. */
	JAYLINK_DEV_CAP_ADAPTIVE_CLOCKING = 3,
	/** Device supports reading configuration data. */
	JAYLINK_DEV_CAP_READ_CONFIG = 4,
	/** Device supports writing configuration data. */
	JAYLINK_DEV_CAP_WRITE_CONFIG = 5,
	/** Device supports retrieval of target interface speeds. */
	JAYLINK_DEV_CAP_GET_SPEEDS = 9,
	/** Device supports retrieval of free memory size. */
	JAYLINK_DEV_CAP_GET_FREE_MEMORY = 11,
	/** Device supports retrieval of hardware information. */
	JAYLINK_DEV_CAP_GET_HW_INFO = 12,
	/** Device supports the setting of the target power supply. */
	JAYLINK_DEV_CAP_SET_TARGET_POWER = 13,
	/** Device supports target interface selection. */
	JAYLINK_DEV_CAP_SELECT_TIF = 17,
	/** Device supports retrieval of counter values. */
	JAYLINK_DEV_CAP_GET_COUNTERS = 19,
	/** Device supports capturing of SWO trace data. */
	JAYLINK_DEV_CAP_SWO = 23,
	/** Device supports file I/O operations. */
	JAYLINK_DEV_CAP_FILE_IO = 26,
	/** Device supports registration of connections. */
	JAYLINK_DEV_CAP_REGISTER = 27,
	/** Device supports retrieval of extended capabilities. */
	JAYLINK_DEV_CAP_GET_EXT_CAPS = 31,
	/** Device supports EMUCOM. */
	JAYLINK_DEV_CAP_EMUCOM = 33,
	/** Device supports ethernet connectivity. */
	JAYLINK_DEV_CAP_ETHERNET = 38
};

bool jlink_init(bmp_info_s *info);
uint32_t jlink_swdp_scan(bmp_info_s *info);
bool jlink_jtagtap_init(bmp_info_s *infp);
const char *jlink_target_voltage(bmp_info_s *info);
void jlink_nrst_set_val(bmp_info_s *info, bool assert);
bool jlink_nrst_get_val(bmp_info_s *info);
void jlink_max_frequency_set(bmp_info_s *info, uint32_t freq);
uint32_t jlink_max_frequency_get(bmp_info_s *info);
#endif

#endif /* PLATFORMS_HOSTED_JLINK_H */
