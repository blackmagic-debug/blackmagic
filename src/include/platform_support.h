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

#ifndef INCLUDE_PLATFORM_SUPPORT_H
#define INCLUDE_PLATFORM_SUPPORT_H

#ifndef INCLUDE_GENERAL_H
#error "Include 'general.h' instead"
#endif

#if PC_HOSTED == 0
#include "stdio_newlib.h"
#endif
#include "target.h"
#include "spi_types.h"

#if PC_HOSTED == 1
void platform_init(int argc, char **argv);
void platform_pace_poll(void);
#else
void platform_init(void);

inline void platform_pace_poll(void)
{
}
#endif

typedef struct platform_timeout platform_timeout_s;
void platform_timeout_set(platform_timeout_s *target, uint32_t ms);
bool platform_timeout_is_expired(const platform_timeout_s *target);
void platform_delay(uint32_t ms);

#define POWER_CONFLICT_THRESHOLD 5U /* in 0.1V, so 5 stands for 0.5V */

extern bool connect_assert_nrst;
uint32_t platform_target_voltage_sense(void);
const char *platform_target_voltage(void);
int platform_hwversion(void);
void platform_nrst_set_val(bool assert);
bool platform_nrst_get_val(void);
bool platform_target_get_power(void);
bool platform_target_set_power(bool power);
void platform_request_boot(void);
void platform_max_frequency_set(uint32_t frequency);
uint32_t platform_max_frequency_get(void);

void platform_target_clk_output_enable(bool enable);

#if PC_HOSTED == 0
bool platform_spi_init(spi_bus_e bus);
bool platform_spi_deinit(spi_bus_e bus);

bool platform_spi_chip_select(uint8_t device_select);
uint8_t platform_spi_xfer(spi_bus_e bus, uint8_t value);
#endif

#endif /* INCLUDE_PLATFORM_SUPPORT_H */
