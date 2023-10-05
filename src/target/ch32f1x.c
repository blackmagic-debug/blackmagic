/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by mean00 <fixounet@free.fr>
 * Modified by Rafael Silva <perigoso@riseup.net>
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

/* This file implements CH32F1x target specific functions. */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "buffer_utils.h"
#include "ch32_flash.h"

/* IDCODE register */
#define CH32F1X_IDCODE                    0xe0042000U
#define CH32F1X_IDCODE_REVISION_ID_OFFSET 16U
#define CH32F1X_IDCODE_REVIDION_ID_MASK   (0xffffU << CH32F1X_IDCODE_REVISION_ID_OFFSET)
#define CH32F1X_IDCODE_DEVICE_ID_MASK     0xfffU

#define CH32F103X_DEVICE_ID   0x410U  /* Matches CH32F103, CKS32F103, APM32F103 */
#define CH32F103X_REVISION_ID 0x2000U /* Only matches CH32F103 (hopefully!) */

/* Electronic Signature (ESIG) registers */
#define CH32F1X_ESIG_BASE      0x1ffff7e0U                 /* Electronic signature base address */
#define CH32F1X_ESIG_FLASH_CAP (CH32F1X_ESIG_BASE + 0x00U) /* Flash capacity register, 16 bits, KiB units */
#define CH32F1X_ESIG_UID1      (CH32F1X_ESIG_BASE + 0x08U) /* Unique ID register, bits 0:31 */
#define CH32F1X_ESIG_UID2      (CH32F1X_ESIG_BASE + 0x0cU) /* Unique ID register, bits 32:63 */
#define CH32F1X_ESIG_UID3      (CH32F1X_ESIG_BASE + 0x10U) /* Unique ID register, bits 64:95 */

/* Memory mapping */
#define CH32F103X_FLASH_MEMORY_ADDR 0x08000000U
#define CH32F103X_SRAM_ADDR         0x20000000U

static bool ch32f1x_uid_cmd(target_s *target, int argc, const char **argv);

const command_s ch32f1x_cmd_list[] = {
	{"uid", ch32f1x_uid_cmd, "Prints 96 bit unique id"},
	{"option", stm32_option_bytes_cmd, "Manipulate option bytes"},
	{NULL, NULL, NULL},
};

/* Check if the FPEC has the CH32 fast mode extension */
static bool ch32f1x_has_fast_mode_extension(target_s *const target)
{
	const uint32_t fpec_base = CH32_FPEC_BASE;

	/* Start with reset state flash and fast mode locked */
	ch32_flash_lock(target, fpec_base);

	/* Check if the flash and fast mode are locked */
	if (!stm32_flash_locked(target, fpec_base, 0U) || ch32_flash_fast_mode_locked(target, fpec_base))
		return false;

	/* Try to unlock the flash and fast mode, if this fails the fast mode extension is not available */
	const bool result = stm32_flash_unlock(target, fpec_base, 0U) && ch32_flash_fast_mode_unlock(target, fpec_base);

	/* Lock the flash again */
	ch32_flash_lock(target, fpec_base);

	return result;
}

/* Reads the flash capacity in KiB */
static size_t ch32f1x_read_flash_capacity(target_s *const target)
{
	/* Get flash capacity from ESIG register */
	const size_t flash_capacity = target_mem_read16(target, CH32F1X_ESIG_FLASH_CAP);
	if (flash_capacity == 0U) {
		/*
		 * Some CH32F103C8T6 MCUs seem to have an errata, having zero (0) in the flash capacity ESIG register
		 * If CH32F103C6xx can be affected this fixup is wrong, as they only have 32KiB of flash
		 */
		DEBUG_WARN("CH32F1x errata? ESIG_FLASH_CAP = 0, assuming CH32F103C8T6 with 64 KiB flash!\n");
		return 64U; /* 64KiB */
	}
	return flash_capacity;
}

/* Reads the 96 bit unique id */
static void ch32f1x_read_uid(target_s *const target, uint8_t *const uid)
{
	for (size_t uid_reg_offset = 0; uid_reg_offset < 3U; uid_reg_offset++)
		write_be4(uid, uid_reg_offset, target_mem_read32(target, CH32F1X_ESIG_UID1 + (uid_reg_offset << 2U)));
}

/* Try to identify CH32F1x chip family */
bool ch32f1x_probe(target_s *const target)
{
	if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) != CORTEX_M3)
		return false;

	const uint32_t idcode = target_mem_read32(target, CH32F1X_IDCODE);
	const uint16_t device_id = idcode & CH32F1X_IDCODE_DEVICE_ID_MASK;
	const uint16_t revision_id = (idcode & CH32F1X_IDCODE_REVIDION_ID_MASK) >> CH32F1X_IDCODE_REVISION_ID_OFFSET;

	DEBUG_INFO("%s IDCODE 0x%" PRIx32 ", Device ID 0x%03" PRIx16 ", Revision ID 0x%04" PRIx16 " \n", __func__, idcode,
		device_id, revision_id);

	if (device_id != CH32F103X_DEVICE_ID || revision_id != CH32F103X_REVISION_ID)
		return false;

	/* Check if the FPEC has the CH32 fast mode extension, if not this isn't a CH32F1 */
	if (!ch32f1x_has_fast_mode_extension(target))
		return false;

	target->part_id = device_id;
	target->driver = "CH32F103x";

	const size_t flash_capacity = ch32f1x_read_flash_capacity(target);
	const size_t ram_capacity = flash_capacity == 32U ? 10U : 20U; /* 10KiB or 20KiB */

	/* KiB to bytes */
	target_add_ram(target, CH32F103X_SRAM_ADDR, ram_capacity << 10U);
	ch32f1x_add_flash(target, CH32F103X_FLASH_MEMORY_ADDR, flash_capacity << 10U);

	target_add_commands(target, ch32f1x_cmd_list, target->driver);

	return true;
}

/* Reads the 96 bit unique id */
static bool ch32f1x_uid_cmd(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;

	uint8_t uid[12U];
	ch32f1x_read_uid(target, uid);

	tc_printf(target, "Unique id: 0x");
	for (size_t i = 0U; i < sizeof(uid); i++)
		tc_printf(target, "%02" PRIx8, uid[i]);
	tc_printf(target, "\n");

	return true;
}
