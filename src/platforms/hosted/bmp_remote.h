/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
 * Copyright (C) 2022-2025 1BitSquared <info@1bitsquared.com>
 * Rewritten by Rachel Mant <git@dragonmux.network>
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
#include "riscv_debug.h"
#include "spi.h"
#include "target.h"
#include "target_internal.h"

#define REMOTE_MAX_MSG_SIZE 1024U

typedef struct bmp_remote_protocol {
	bool (*swd_init)(void);
	bool (*jtag_init)(void);
	bool (*adiv5_init)(adiv5_debug_port_s *dp);
	bool (*adiv6_init)(adiv5_debug_port_s *dp);
	bool (*riscv_jtag_init)(riscv_dmi_s *dmi);
	void (*add_jtag_dev)(uint32_t dev_index, const jtag_dev_s *jtag_dev);
	uint32_t (*get_comms_frequency)(void);
	bool (*set_comms_frequency)(uint32_t freq);
	void (*target_clk_output_enable)(bool enable);
	bool (*spi_init)(spi_bus_e bus);
	bool (*spi_deinit)(spi_bus_e bus);
	bool (*spi_chip_select)(uint8_t device_select);
	uint8_t (*spi_xfer)(spi_bus_e bus, uint8_t value);
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
void remote_adiv6_dp_init(adiv5_debug_port_s *dp);
void remote_riscv_jtag_dtm_init(riscv_dmi_s *dmi);
void remote_add_jtag_dev(uint32_t dev_index, const jtag_dev_s *jtag_dev);

bool remote_spi_init(spi_bus_e bus);
bool remote_spi_deinit(spi_bus_e bus);

bool remote_spi_chip_select(uint8_t device_select);
uint8_t remote_spi_xfer(spi_bus_e bus, uint8_t value);

uint64_t remote_decode_response(const char *response, size_t digits);

#endif /* PLATFORMS_HOSTED_BMP_REMOTE_H */
