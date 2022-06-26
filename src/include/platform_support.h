/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015 Gareth McMullin <gareth@blacksphere.co.nz>
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

#ifndef __PLATFORM_SUPPORT_H
#define __PLATFORM_SUPPORT_H

#ifndef __GENERAL_H
#	error "Include 'general.h' instead"
#endif

#include "target.h"

#if PC_HOSTED == 1
void platform_init(int argc, char **argv);
#else
void platform_init(void);
#endif

typedef struct platform_timeout platform_timeout;
void platform_timeout_set(platform_timeout *t, uint32_t ms);
bool platform_timeout_is_expired(platform_timeout *t);
void platform_delay(uint32_t ms);

#define POWER_CONFLICT_THRESHOLD	5 /* in 0.1V, so 5 stands for 0.5V */
extern bool connect_assert_nrst;
uint32_t platform_target_voltage_sense(void);
const char *platform_target_voltage(void);
int platform_hwversion(void);
void platform_nrst_set_val(bool assert);
bool platform_nrst_get_val(void);
bool platform_target_get_power(void);
void platform_target_set_power(bool power);
void platform_request_boot(void);
void platform_max_frequency_set(uint32_t frequency);
uint32_t platform_max_frequency_get(void);

#endif
