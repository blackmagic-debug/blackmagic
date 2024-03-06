/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022-2024 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
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
 * This file implements TI/LMI LM3S target specific functions providing
 * the XML memory map and Flash memory programming.
 *
 * According to:
 *   * TivaTM TM4C123GH6PM Microcontroller Datasheet
 *   * TM4C1294KCPDT Datasheet (https://www.ti.com/lit/ds/symlink/tm4c1294kcpdt.pdf)
 *   * LM3S3748 Datasheet (https://www.ti.com/lit/ds/symlink/lm3s3748.pdf)
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define SRAM_BASE        0x20000000U
#define STUB_BUFFER_BASE ALIGN(SRAM_BASE + sizeof(lmi_flash_write_stub), 4U)

#define BLOCK_SIZE 0x400U

#define LMI_SCB_BASE 0x400fe000U
#define LMI_SCB_DID0 (LMI_SCB_BASE + 0x000U)
#define LMI_SCB_DID1 (LMI_SCB_BASE + 0x004U)

/*
 * Format for DID0:
 *  vXccMMmm
 *   * v (30:28)    DID format version (1)
 *   * X (31,27:24) Reserved
 *   * c (13:16)    Device class/product line
 *   * M (15:8)     Device major revision (die revision)
 *   * m (7:0)      Device minor revision (metal layer change)
 *
 * Full family names are:
 *  * LM3Sxxx:         Sandstorm
 *  * LM3Sxxxx:        Fury
 *  * LM3Sxxxx:        DustDevil
 *  * TM4C123/LM4Fxxx: Blizzard
 *  * TM4C129:         Snowflake
 */
#define DID0_CLASS_MASK                0x00ff0000U
#define DID0_CLASS_STELLARIS_SANDSTORM 0x00000000U
#define DID0_CLASS_STELLARIS_FURY      0x00010000U
#define DID0_CLASS_STELLARIS_DUSTDEVIL 0x00030000U
#define DID0_CLASS_TIVA_BLIZZARD       0x00050000U
#define DID0_CLASS_TIVA_SNOWFLAKE      0x000a0000U

/*
 * Format for DID1:
 *  vfppcXii
 *   * v (31:28) DID format version (0 for some LM3S (?), 1 for TM4C)
 *   * f (27:24) Family (0 for all LM3S/TM4C)
 *   * c (23:16) Part number
 *   * c (15:13) Pin count
 *   * X (12:8)  Reserved
 *   * i (7:0)   Information:
 *       (7:5)     Temperature range
 *       (4:3)     Package
 *       (2)       ROHS Status
 *       (1:0)     Qualification status
 * These part numbers here are the upper 16-bits of DID1
 */
#define DID1_LM3S3748      0x1049U
#define DID1_LM3S5732      0x1096U
#define DID1_LM3S8962      0x10a6U
#define DID1_TM4C123GH6PM  0x10a1U
#define DID1_TM4C1230C3PM  0x1022U
#define DID1_TM4C1294NCPDT 0x101fU
#define DID1_TM4C1294KCPDT 0x1034U

#define LMI_FLASH_BASE 0x400fd000U
#define LMI_FLASH_FMA  (LMI_FLASH_BASE + 0x000U)
#define LMI_FLASH_FMC  (LMI_FLASH_BASE + 0x008U)

#define LMI_FLASH_FMC_WRITE  (1U << 0U)
#define LMI_FLASH_FMC_ERASE  (1U << 1U)
#define LMI_FLASH_FMC_MERASE (1U << 2U)
#define LMI_FLASH_FMC_COMT   (1U << 3U)
#define LMI_FLASH_FMC_WRKEY  0xa4420000U

static bool lmi_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool lmi_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool lmi_mass_erase(target_s *target);

static const uint16_t lmi_flash_write_stub[] = {
#include "flashstub/lmi.stub"
};

static void lmi_add_flash(target_s *target, size_t length)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = 0;
	flash->length = length;
	flash->blocksize = 0x400;
	flash->erase = lmi_flash_erase;
	flash->write = lmi_flash_write;
	flash->erased = 0xff;
	target_add_flash(target, flash);
}

bool lm3s_probe(target_s *const target, const uint16_t did1)
{
	switch (did1) {
	case DID1_LM3S3748:
	case DID1_LM3S5732:
		target_add_ram32(target, 0x20000000U, 0x10000U);
		lmi_add_flash(target, 0x20000U);
		break;
	case DID1_LM3S8962:
		target_add_ram32(target, 0x2000000U, 0x10000U);
		lmi_add_flash(target, 0x40000U);
		break;
	default:
		return false;
	}
	target->driver = "Stellaris";
	target->mass_erase = lmi_mass_erase;
	return true;
}

bool tm4c_probe(target_s *const target, const uint16_t did1)
{
	switch (did1) {
	case DID1_TM4C123GH6PM:
		target_add_ram32(target, 0x20000000, 0x10000);
		lmi_add_flash(target, 0x80000);
		/*
		 * On Tiva targets, asserting nRST results in the debug
		 * logic also being reset. We can't assert nRST and must
		 * only use the AIRCR SYSRESETREQ.
		 */
		target->target_options |= TOPT_INHIBIT_NRST;
		break;
	case DID1_TM4C1230C3PM:
		target_add_ram32(target, 0x20000000, 0x6000);
		lmi_add_flash(target, 0x10000);
		target->target_options |= TOPT_INHIBIT_NRST;
		break;
	case DID1_TM4C1294KCPDT:
		target_add_ram32(target, 0x20000000, 0x40000);
		lmi_add_flash(target, 0x80000);
		target->target_options |= TOPT_INHIBIT_NRST;
		break;
	case DID1_TM4C1294NCPDT:
		target_add_ram32(target, 0x20000000, 0x40000);
		lmi_add_flash(target, 0x100000);
		target->target_options |= TOPT_INHIBIT_NRST;
		break;
	default:
		return false;
	}
	target->driver = "Tiva-C";
	target->mass_erase = lmi_mass_erase;
	cortex_ap(target)->dp->quirks |= ADIV5_DP_QUIRK_DUPED_AP;
	return true;
}

bool lmi_probe(target_s *const target)
{
	const uint32_t did0 = target_mem32_read32(target, LMI_SCB_DID0);
	const uint16_t did1 = target_mem32_read32(target, LMI_SCB_DID1) >> 16U;

	switch (did0 & DID0_CLASS_MASK) {
	case DID0_CLASS_STELLARIS_FURY:
	case DID0_CLASS_STELLARIS_DUSTDEVIL:
		return lm3s_probe(target, did1);
	case DID0_CLASS_TIVA_BLIZZARD:
	case DID0_CLASS_TIVA_SNOWFLAKE:
		return tm4c_probe(target, did1);
	default:
		return false;
	}
}

static bool lmi_flash_erase(target_flash_s *flash, target_addr_t addr, const size_t len)
{
	target_s *target = flash->t;
	target_check_error(target);

	const bool full_erase = addr == flash->start && len == flash->length;
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);

	for (size_t erased = 0; erased < len; erased += BLOCK_SIZE) {
		target_mem32_write32(target, LMI_FLASH_FMA, addr);
		target_mem32_write32(target, LMI_FLASH_FMC, LMI_FLASH_FMC_WRKEY | LMI_FLASH_FMC_ERASE);

		while (target_mem32_read32(target, LMI_FLASH_FMC) & LMI_FLASH_FMC_ERASE) {
			if (full_erase)
				target_print_progress(&timeout);
		}

		if (target_check_error(target))
			return false;

		addr += BLOCK_SIZE;
	}
	return true;
}

static bool lmi_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	target_s *target = flash->t;
	target_check_error(target);
	target_mem32_write(target, SRAM_BASE, lmi_flash_write_stub, sizeof(lmi_flash_write_stub));
	target_mem32_write(target, STUB_BUFFER_BASE, src, len);
	if (target_check_error(target))
		return false;

	return cortexm_run_stub(target, SRAM_BASE, dest, STUB_BUFFER_BASE, len, 0) == 0;
}

static bool lmi_mass_erase(target_s *target)
{
	return lmi_flash_erase(target->flash, target->flash->start, target->flash->length);
}
