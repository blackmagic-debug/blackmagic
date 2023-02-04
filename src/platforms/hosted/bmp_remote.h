/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Uwe Bonnes
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

#ifndef PLATFORMS_HOSTED_BMP_REMOTE_H
#define PLATFORMS_HOSTED_BMP_REMOTE_H

#include "jtagtap.h"
#include "adiv5.h"
#include "target.h"
#include "target_internal.h"

#define REMOTE_MAX_MSG_SIZE 1024U

int platform_buffer_write(const uint8_t *data, int size);
int platform_buffer_read(uint8_t *data, int size);

int remote_init(void);
int remote_swdptap_init(void);
int remote_jtagtap_init(jtag_proc_s *jtag_proc);
bool remote_target_get_power(void);
const char *remote_target_voltage(void);
bool remote_target_set_power(bool power);
void remote_nrst_set_val(bool assert);
bool remote_nrst_get_val(void);
void remote_max_frequency_set(uint32_t freq);
uint32_t remote_max_frequency_get(void);
void remote_target_clk_output_enable(bool enable);

void remote_adiv5_dp_defaults(adiv5_debug_port_s *dp);
void remote_add_jtag_dev(uint32_t i, const jtag_dev_s *jtag_dev);

#endif /* PLATFORMS_HOSTED_BMP_REMOTE_H */
