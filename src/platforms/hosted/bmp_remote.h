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

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "jtagtap.h"
#include "jtag_devs.h"
#include "adiv5.h"
#include "target.h"
#include "target_internal.h"

#define REMOTE_MAX_MSG_SIZE 1024U

typedef struct bmp_remote_protocol {
	bool (*swd_init)(void);
	bool (*jtag_init)(void);
	bool (*adiv5_init)(adiv5_debug_port_s *dp);
	void (*add_jtag_dev)(uint32_t dev_index, const jtag_dev_s *jtag_dev);
	uint32_t (*get_comms_frequency)(void);
	bool (*set_comms_frequency)(uint32_t freq);
	void (*target_clk_output_enable)(bool enable);
} bmp_remote_protocol_s;

extern bmp_remote_protocol_s remote_funcs;

bool platform_buffer_write(const void *data, size_t size);
int platform_buffer_read(void *data, size_t size);

bool remote_init(bool power_up);
bool remote_swd_init(void);
bool remote_jtag_init(void);
bool remote_target_get_power(void);
const char *remote_target_voltage(void);
bool remote_target_set_power(bool power);
void remote_nrst_set_val(bool assert);
bool remote_nrst_get_val(void);
void remote_max_frequency_set(uint32_t freq);
uint32_t remote_max_frequency_get(void);
void remote_target_clk_output_enable(bool enable);

void remote_adiv5_dp_init(adiv5_debug_port_s *dp);
void remote_add_jtag_dev(uint32_t dev_index, const jtag_dev_s *jtag_dev);

uint64_t remote_decode_response(const char *response, size_t digits);
uint64_t remote_hex_string_to_num(uint32_t limit, const char *str);

#endif /* PLATFORMS_HOSTED_BMP_REMOTE_H */
