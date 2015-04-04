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

#ifndef __LPC_COMMON_H
#define __LPC_COMMON_H

#define IAP_CMD_INIT		49
#define IAP_CMD_PREPARE		50
#define IAP_CMD_PROGRAM		51
#define IAP_CMD_ERASE		52
#define IAP_CMD_BLANKCHECK	53
#define IAP_CMD_SET_ACTIVE_BANK	60

#define IAP_STATUS_CMD_SUCCESS		0
#define IAP_STATUS_INVALID_COMMAND	1
#define IAP_STATUS_SRC_ADDR_ERROR	2
#define IAP_STATUS_DST_ADDR_ERROR	3
#define IAP_STATUS_SRC_ADDR_NOT_MAPPED	4
#define IAP_STATUS_DST_ADDR_NOT_MAPPED	5
#define IAP_STATUS_COUNT_ERROR		6
#define IAP_STATUS_INVALID_SECTOR	7
#define IAP_STATUS_SECTOR_NOT_BLANK	8
#define IAP_STATUS_SECTOR_NOT_PREPARED	9
#define IAP_STATUS_COMPARE_ERROR	10
#define IAP_STATUS_BUSY			11

/* CPU Frequency */
#define CPU_CLK_KHZ 12000

struct flash_param {
	uint16_t opcode;/* opcode to return to after calling the ROM */
	uint16_t pad0;
	uint32_t command;/* IAP command */
	union {
		uint32_t words[5];/* command parameters */
		struct {
			uint32_t start_sector;
			uint32_t end_sector;
			uint32_t flash_bank;
		} prepare;
		struct {
			uint32_t start_sector;
			uint32_t end_sector;
			uint32_t cpu_clk_khz;
			uint32_t flash_bank;
		} erase;
		struct {
			uint32_t dest;
			uint32_t source;
			uint32_t byte_count;
			uint32_t cpu_clk_khz;
		} program;
		struct {
			uint32_t start_sector;
			uint32_t end_sector;
			uint32_t flash_bank;
		} blank_check;
		struct {
			uint32_t flash_bank;
			uint32_t cpu_clk_khz;
		} make_active;
	};
	uint32_t result[5];	/* result data */
} __attribute__((aligned(4)));

#endif

