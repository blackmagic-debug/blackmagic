/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Copyright (C) 2023 Sid Price <sid@sidprice.com>
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

#include "general.h"
#include "target.h"
#include "target_internal.h"

static bool apollo_3_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool apollo_3_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);

static target_addr_t flash_base_address;
static size_t flash_size;
static size_t flash_block_size;

static void apollo_3_add_flash(target_s *target)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = flash_base_address;
	flash->length = flash_size;
	flash->blocksize = flash_block_size;
	flash->erase = apollo_3_flash_erase;
	flash->write = apollo_3_flash_write;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

bool apollo_3_probe(target_s *target)
{
	/* Positively identify the target device somehow */
	// if (target_mem_read32(t, APOLLO_3_DEVID_ADDR) != APOLLO_3_DEVID)
	// 	return false;

	target->driver = "apollo 3"; // TODO build the part number from data read
	/* Add RAM mappings */
	// target_add_ram(t, APOLLO_3_RAM_BASE, APOLLO_3_RAM_SIZE);
	/* Add Flash mappings */
	apollo_3_add_flash(target);
	return true;
}

static bool apollo_3_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len)
{
	(void)flash;
	(void)addr;
	(void)len;
	return false;
}

static bool apollo_3_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	(void)flash;
	(void)dest;
	(void)src;
	(void)len;
	return false;
}