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

bool jlink_init(void);
uint32_t jlink_swdp_scan(bmp_info_s *info);
bool jlink_jtag_init(void);
const char *jlink_target_voltage(void);
void jlink_nrst_set_val(bool assert);
bool jlink_nrst_get_val(void);
void jlink_max_frequency_set(uint32_t freq);
uint32_t jlink_max_frequency_get(void);

#endif /* PLATFORMS_HOSTED_JLINK_H */
