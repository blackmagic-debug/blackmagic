/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019 Uwe Bonnes
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
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

#define SELECT_IF_JTAG 0U
#define SELECT_IF_SWD  1U

bool jlink_init(bmp_info_s *info);
uint32_t jlink_swdp_scan(bmp_info_s *info);
bool jlink_jtagtap_init(bmp_info_s *info);
const char *jlink_target_voltage(bmp_info_s *info);
void jlink_nrst_set_val(bmp_info_s *info, bool assert);
bool jlink_nrst_get_val(bmp_info_s *info);
void jlink_max_frequency_set(bmp_info_s *info, uint32_t freq);
uint32_t jlink_max_frequency_get(bmp_info_s *info);

#endif /* PLATFORMS_HOSTED_JLINK_H */
