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
#if !defined(__JLINK_H_)
#define __JLINK_H_

#include "jtagtap.h"

/** @cond PRIVATE */
#define CMD_GET_VERSION         0x01
#define CMD_GET_HW_STATUS       0x07
#define CMD_GET_SPEED           0xc0
#define CMD_GET_SELECT_IF       0xc7
#define CMD_HW_JTAG3            0xcf
#define CMD_HW_RESET0           0xdc
#define CMD_HW_RESET1           0xdd
#define CMD_GET_CAPS            0xe8
#define CMD_GET_EXT_CAPS        0xed
#define CMD_GET_HW_VERSION      0xf0

#define JLINK_IF_GET_ACTIVE    0xfe
#define JLINK_IF_GET_AVAILABLE 0xff

#define JLINK_CAP_GET_HW_VERSION  2
#define JLINK_IF_JTAG             1
#define JLINK_IF_SWD              2

#define SELECT_IF_JTAG            0
#define SELECT_IF_SWD             1


int jlink_init(bmp_info_t *info);
int jlink_swdp_scan(bmp_info_t *info);
int jlink_jtagtap_init(bmp_info_t *info, jtag_proc_t *jtag_proc);
const char *jlink_target_voltage(bmp_info_t *info);
void jlink_srst_set_val(bmp_info_t *info, bool assert);
bool jlink_srst_get_val(bmp_info_t *info);
#endif
