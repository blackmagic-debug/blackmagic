/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

#ifndef PLATFORMS_HOSTED_JLINK_PROTOCOL_H
#define PLATFORMS_HOSTED_JLINK_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

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

typedef struct jlink_set_freq {
	uint8_t command;
	uint8_t frequency[2];
} jlink_set_freq_s;

int jlink_simple_query(uint8_t command, void *rx_buffer, size_t rx_len);
int jlink_simple_request(uint8_t command, uint8_t operation, void *rx_buffer, size_t rx_len);

#endif /*PLATFORMS_HOSTED_JLINK_PROTOCOL_H*/
