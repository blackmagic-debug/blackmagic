/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2016 Gareth McMullin <gareth@blacksphere.co.nz>
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

enum iap_cmd {
	IAP_CMD_INIT = 49,
	IAP_CMD_PREPARE = 50,
	IAP_CMD_PROGRAM = 51,
	IAP_CMD_ERASE = 52,
	IAP_CMD_BLANKCHECK = 53,
	IAP_CMD_SET_ACTIVE_BANK = 60,
};

enum iap_status {
	IAP_STATUS_CMD_SUCCESS = 0,
	IAP_STATUS_INVALID_COMMAND = 1,
	IAP_STATUS_SRC_ADDR_ERROR = 2,
	IAP_STATUS_DST_ADDR_ERROR = 3,
	IAP_STATUS_SRC_ADDR_NOT_MAPPED = 4,
	IAP_STATUS_DST_ADDR_NOT_MAPPED = 5,
	IAP_STATUS_COUNT_ERROR = 6,
	IAP_STATUS_INVALID_SECTOR = 7,
	IAP_STATUS_SECTOR_NOT_BLANK = 8,
	IAP_STATUS_SECTOR_NOT_PREPARED = 9,
	IAP_STATUS_COMPARE_ERROR = 10,
	IAP_STATUS_BUSY = 11,
};

/* CPU Frequency */
#define CPU_CLK_KHZ 12000

struct lpc_flash {
	struct target_flash f;
	uint8_t base_sector;
	uint8_t bank;
	/* Info filled in by specific driver */
	void (*wdt_kick)(target *t);
	uint32_t iap_entry;
	uint32_t iap_ram;
	uint32_t iap_msp;
};

struct lpc_flash *lpc_add_flash(target *t, target_addr addr, size_t length);
enum iap_status lpc_iap_call(struct lpc_flash *f, enum iap_cmd cmd, ...);
int lpc_flash_erase(struct target_flash *f, target_addr addr, size_t len);
int lpc_flash_write(struct target_flash *f,
                    target_addr dest, const void *src, size_t len);
int lpc_flash_write_magic_vect(struct target_flash *f,
                               target_addr dest, const void *src, size_t len);

#endif

