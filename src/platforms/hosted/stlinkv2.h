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

#ifndef PLATFORMS_HOSTED_STLINKV2_H
#define PLATFORMS_HOSTED_STLINKV2_H

#include "bmp_hosted.h"

#define STLINK_ERROR_FAIL (-1)
#define STLINK_ERROR_OK   0
#define STLINK_ERROR_WAIT 1

bool stlink_init(void);
bool stlink_swd_scan(void);
bool stlink_jtag_scan(void);
int stlink_hwversion(void);
const char *stlink_target_voltage(void);
void stlink_nrst_set_val(bool assert);
bool stlink_nrst_get_val(void);
void stlink_adiv5_dp_init(adiv5_debug_port_s *dp);
void stlink_jtag_dp_init(adiv5_debug_port_s *dp);
void stlink_exit_function(bmp_info_s *info);
void stlink_max_frequency_set(uint32_t freq);
uint32_t stlink_max_frequency_get(void);

#endif /* PLATFORMS_HOSTED_STLINKV2_H */
