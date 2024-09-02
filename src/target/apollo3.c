/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Written by Sid Price <sid@sidprice.com>
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

#define APOLLO_3_FLASH_BASE_ADDRESS 0x00000000U
#define APOLLO_3_FLASH_SIZE         0x00100000U
#define APOLLO_3_FLASH_BLOCK_SIZE   0x2000U

#define APOLLO_3_SRAM_BASE 0x10000000U
#define APOLLO_3_SRAM_SIZE 0x00060000U

#define APOLLO_3_CHIPPN_REGISTER 0x40020000U /* Address of the Chip Part Number Register */

/*
 * Define the bitfields of the CHIPPN register.
 *
 * This register contains the part number of the MCU
 */
#define APOLLO_3_CHIPPN_PART_NUMBER_MASK         0xff000000U
#define APOLLO_3_CHIPPN_PART_NUMBER_BIT_POSITION 0x18U

#define APOLLO_3_CHIPPN_FLASH_SIZE_MASK         0x00f00000U
#define APOLLO_3_CHIPPN_FLASH_SIZE_BIT_POSITION 0x14U

#define APOLLO_3_CHIPPN_SRAM_SIZE_MASK         0x000f0000U
#define APOLLO_3_CHIPPN_SRAM_SIZE_BIT_POSITION 0x10U

#define APOLLO_3_CHIPPN_REVISION_MASK         0x0000ff00U
#define APOLLO_3_CHIPPN_REVISION_BIT_POSITION 0x08U

#define APOLLO_3_CHIPPN_PACKAGE_MASK         0x000000c0U
#define APOLLO_3_CHIPPN_PACKAGE_BIT_POSITION 0x06U

#define APOLLO_3_CHIPPN_PINS_MASK         0x00000038U
#define APOLLO_3_CHIPPN_PINS_BIT_POSITION 0x03U

#define APOLLO_3_CHIPPN_TEMP_MASK         0x00000006U
#define APOLLO_3_CHIPPN_TEMP_BIT_POSITION 0x01U

#define APOLLO_3_CHIPPN_QUALIFIED_MASK         0x00000001U
#define APOLLO_3_CHIPPN_QUALIFIED_BIT_POSITION 0x0U

#define APOLLO_3_CHIPID0_REGISTER 0x40020004U /* Chip ID Register 0 */
#define APOLLO_3_CHIPID1_REGISTER 0x40020008U /* Chip ID Register 1 */

#define APOLLO_3_CHIPREV_REGISTER 0x4002000cU /* Chip Revision Register */

/*
 * Define the bitfields of the CHIPREV register.
 *
 * This register contains the revision of the MCU
*/
#define APOLLO_3_CHIPREV_RESERVED 0xfff00000U
#define APOLLO_3_CHIPREV_SI_PART  0x000fff00U
#define APOLLO_3_CHIPREV_REVMAJ   0x000000f0U
#define APOLLO_3_CHIPREV_REVMIN   0x0000000fU

#define APOLLO_3_VENDOR_ID_ADDRESS 0x40020010U /* Vendor ID Register */
#define APOLLO_3_VENDOR_ID         0x414d4251U

static void apollo_3_add_flash(target_s *target)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = APOLLO_3_FLASH_BASE_ADDRESS;
	flash->length = APOLLO_3_FLASH_SIZE;
	flash->blocksize = APOLLO_3_FLASH_BLOCK_SIZE;
	flash->erase = apollo_3_flash_erase;
	flash->write = apollo_3_flash_write;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

bool apollo_3_probe(target_s *target)
{
	uint32_t mcu_vendor_id = target_mem32_read32(target, APOLLO_3_VENDOR_ID_ADDRESS);
	if (mcu_vendor_id != APOLLO_3_VENDOR_ID) {
		DEBUG_INFO("Invalid vendor ID read\n");
		return false;
	} else
		DEBUG_INFO("Read correct vendor ID\n");
	/* Read the CHIPPN register to gather MCU details */
	uint32_t mcu_chip_partnum = target_mem32_read32(target, APOLLO_3_CHIPPN_REGISTER);
	/* Check the chip is an Apollo 3 */
	if ((mcu_chip_partnum & APOLLO_3_CHIPPN_PART_NUMBER_MASK) != 0x06000000) {
		DEBUG_INFO("Invalid chip type read\n");
		return false;
	}

	target->driver = "Apollo 3 Blue";

	target_add_ram32(target, APOLLO_3_SRAM_BASE, APOLLO_3_SRAM_SIZE);

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
