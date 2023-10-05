/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
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

/*
 * This file implements RISC-V CH32V3x target specific functions
 * Macros named CH32FV2X_V3X are shared between CH32F2x, CH32V2x and CH32V3x
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "buffer_utils.h"
#include "ch32_flash.h"

/* IDCODE register */
#define CH32FV2X_V3X_IDCODE                    0x1ffff704U
#define CH32FV2X_V3X_IDCODE_REVISION_ID_OFFSET 16U
#define CH32FV2X_V3X_IDCODE_REVIDION_ID_MASK   (0xffffU << CH32FV2X_V3X_IDCODE_REVISION_ID_OFFSET)
#define CH32FV2X_V3X_IDCODE_DEVICE_ID_MASK     0xfffU

/* 
 * Known IDCODE values:
 * CH32V303CBT6: 0x303 3 0 5x4
 * CH32V303RBT6: 0x303 2 0 5x4
 * CH32V303RCT6: 0x303 1 0 5x4
 * CH32V303VCT6: 0x303 0 0 5x4
 * CH32V305FBP6: 0x305 2 0 5x8
 * CH32V305RBT6: 0x305 0 0 5x8
 * CH32V307WCU6: 0x307 3 0 5x8
 * CH32V307FBP6: 0x307 2 0 5x8
 * CH32V307RCT6: 0x307 1 0 5x8
 * CH32V307VCT6: 0x307 0 0 5x8
 */
#define CH32FV2X_V3X_DEVICE_ID_MASK 0xf0fU /* Helper to glob families (FIXME: verify) */
#define CH32V303X_DEVICE_ID         0x504U /* Matches CH32V303x */
#define CH32V305X_7X_DEVICE_ID      0x508U /* Matches CH32V305x and CH32V307x */

#define CH32V2X_3X_REVISION_ID_FAMILY_OFFSET 4U /* Helper to extract family code */
#define CH32V2X_3X_REVISION_ID_FAMILY_MASK   (0xfffU << CH32V2X_3X_REVISION_ID_FAMILY_OFFSET)
#define CH32V203X_REVISION_ID_FAMILY         0x203U
#define CH32V208X_REVISION_ID_FAMILY         0x208U
#define CH32V305X_REVISION_ID_FAMILY         0x305U
#define CH32V303X_REVISION_ID_FAMILY         0x303U
#define CH32V307X_REVISION_ID_FAMILY         0x307U

/* Electronic Signature (ESIG) registers */
#define CH32FV2X_V3X_ESIG_BASE      0x1ffff7e0U                      /* Electronic signature base address */
#define CH32FV2X_V3X_ESIG_FLASH_CAP (CH32FV2X_V3X_ESIG_BASE + 0x00U) /* Flash capacity register, 16 bits, KiB units */
#define CH32FV2X_V3X_ESIG_UID1      (CH32FV2X_V3X_ESIG_BASE + 0x08U) /* Unique ID register, bits 0:31 */
#define CH32FV2X_V3X_ESIG_UID2      (CH32FV2X_V3X_ESIG_BASE + 0x0cU) /* Unique ID register, bits 32:63 */
#define CH32FV2X_V3X_ESIG_UID3      (CH32FV2X_V3X_ESIG_BASE + 0x10U) /* Unique ID register, bits 64:95 */

/* Memory mapping */
#define CH32FV2X_V3X_FLASH_MEMORY_ADDR 0x08000000U
#define CH32FV2X_V3X_SRAM_ADDR         0x20000000U

static bool ch32fv2x_v3x_uid_cmd(target_s *target, int argc, const char **argv);

const command_s ch32fv2x_v3x_cmd_list[] = {
	{"uid", ch32fv2x_v3x_uid_cmd, "Prints 96 bit unique id"},
	{"option", stm32_option_bytes_cmd, "Manipulate option bytes"},
	{NULL, NULL, NULL},
};

/* Reads the 96 bit unique id */
static void ch32fv2x_v3x_read_uid(target_s *const target, uint8_t *const uid)
{
	for (size_t uid_reg_offset = 0; uid_reg_offset < 3U; uid_reg_offset++)
		write_be4(uid, uid_reg_offset, target_mem_read32(target, CH32FV2X_V3X_ESIG_UID1 + (uid_reg_offset << 2U)));
}

/* Reads the flash capacity in KiB */
static inline size_t ch32fv2x_v3x_read_flash_capacity(target_s *const target)
{
	return target_mem_read16(target, CH32FV2X_V3X_ESIG_FLASH_CAP);
}

/* Returns RAM capacity based on the flash capacity */
static inline size_t ch32fv2x_v3x_get_ram_capacity(const uint16_t family, const size_t flash_capacity)
{
	/*
	 * With exception of CH32FV208x all lines follow the same pattern:
	 * ┌────────────┬─────────┬────────┐
	 * │  Family    │  Flash  │  RAM   │
	 * ├────────────┴─────────┴────────┤
	 * │    Low/medium-density line    │
	 * ├────────────┬─────────┬────────┤
	 * │ CH32FV203x │ 32 KiB  │ 10KiB  │
	 * │ CH32FV203x │ 64 KiB  │ 20KiB  │
	 * ├────────────┴─────────┴────────┤
	 * │   High-density general line   │
	 * ├────────────┬─────────┬────────┤
	 * │ CH32F203x  │ 128 KiB │ 32 KiB │
	 * │ CH32F203x  │ 256 KiB │ 64 KiB │
	 * │ CH32V303x  │ 128 KiB │ 32 KiB │
	 * │ CH32V303x  │ 256 KiB │ 64 KiB │
	 * ├────────────┴─────────┴────────┤
	 * │       Connectivity line       │
	 * ├────────────┬─────────┬────────┤
	 * │ CH32F205x  │ 128 KiB │ 32 KiB │
	 * │ CH32V305x  │ 128 KiB │ 32 KiB │
	 * ├────────────┴─────────┴────────┤
	 * │     Interconnectivity line    │
	 * ├────────────┬─────────┬────────┤
	 * │ CH32F207x  │ 256 KiB │ 64 KiB │
	 * │ CH32V307x  │ 256 KiB │ 64 KiB │
	 * ├────────────┴─────────┴────────┤
	 * │         Wireless line         │
	 * ├────────────┬─────────┬────────┤
	 * │ CH32FV208x │ 128 KiB │ 64 KiB │
	 * └────────────┴─────────┴────────┘
	 */

	/* FIXME: CH32*F*208x may not share this family code */
	if (family == CH32V208X_REVISION_ID_FAMILY)
		return 64U; /* 64 KiB */
	else if (flash_capacity <= 32U)
		return 10U; /* 10 KiB */
	else if (flash_capacity <= 64U)
		return 20U; /* 20 KiB */
	else if (flash_capacity <= 128U)
		return 32U; /* 32 KiB */
	else
		return 64U; /* 64 KiB */
}

/* Probe for Risc-V CH32V3x family */
bool ch32v3x_probe(target_s *const target)
{
	const uint32_t idcode = target_mem_read32(target, CH32FV2X_V3X_IDCODE);
	const uint16_t device_id = idcode & CH32FV2X_V3X_IDCODE_DEVICE_ID_MASK;
	const uint16_t revision_id =
		(idcode & CH32FV2X_V3X_IDCODE_REVIDION_ID_MASK) >> CH32FV2X_V3X_IDCODE_REVISION_ID_OFFSET;
	const uint16_t family = (revision_id & CH32V2X_3X_REVISION_ID_FAMILY_MASK) >> CH32V2X_3X_REVISION_ID_FAMILY_OFFSET;

	DEBUG_INFO("%s IDCODE 0x%" PRIx32 ", Device ID 0x%03" PRIx16 ", Revision ID 0x%04" PRIx16 ", Family 0x%03" PRIx16
			   " \n",
		__func__, idcode, device_id, revision_id, family);

	if (device_id != CH32V303X_DEVICE_ID && device_id != CH32V305X_7X_DEVICE_ID)
		return false;

	switch (family) {
	case CH32V305X_REVISION_ID_FAMILY:
		target->driver = "CH32V305x";
		break;
	case CH32V303X_REVISION_ID_FAMILY:
		target->driver = "CH32V303x";
		break;
	case CH32V307X_REVISION_ID_FAMILY:
		target->driver = "CH32V307x";
		break;
	default:
		return false;
	}

	target->part_id = idcode;

	const size_t flash_capacity = ch32fv2x_v3x_read_flash_capacity(target);
	const size_t ram_capacity = ch32fv2x_v3x_get_ram_capacity(family, flash_capacity);

	DEBUG_INFO("%s Flash size: %zu KiB, RAM size: %zu KiB\n", __func__, flash_capacity, ram_capacity);

	/* KiB to bytes */
	target_add_ram(target, CH32FV2X_V3X_SRAM_ADDR, ram_capacity << 10U);
	ch32fv2x_v3x_add_flash(target, CH32FV2X_V3X_FLASH_MEMORY_ADDR, flash_capacity << 10U);

	target_add_commands(target, ch32fv2x_v3x_cmd_list, target->driver);

	return true;
}

/* Reads the 96 bit unique id */
static bool ch32fv2x_v3x_uid_cmd(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;

	uint8_t uid[12U];
	ch32fv2x_v3x_read_uid(target, uid);

	tc_printf(target, "Unique id: 0x");
	for (size_t i = 0U; i < sizeof(uid); i++)
		tc_printf(target, "%02" PRIx8, uid[i]);
	tc_printf(target, "\n");

	return true;
}
