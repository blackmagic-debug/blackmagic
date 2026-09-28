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
 *   * TM4C129XNCZAD Datasheet (https://www.ti.com/lit/gpn/tm4c129xnczad)
 *   * LM3S3748 Datasheet (https://www.ti.com/lit/ds/symlink/lm3s3748.pdf)
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define LMI_SRAM_BASE        0x20000000U
#define LMI_STUB_BUFFER_BASE ALIGN(LMI_SRAM_BASE + sizeof(lmi_flash_write_stub), 4U)

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
#define LMI_DID0_CLASS_MASK                0x00ff0000U
#define LMI_DID0_CLASS_STELLARIS_SANDSTORM 0x00000000U
#define LMI_DID0_CLASS_STELLARIS_FURY      0x00010000U
#define LMI_DID0_CLASS_STELLARIS_DUSTDEVIL 0x00030000U
#define LMI_DID0_CLASS_TIVA_BLIZZARD       0x00050000U
#define LMI_DID0_CLASS_TIVA_SNOWFLAKE      0x000a0000U

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

/* clang-format off */
/* Stellaris Fury/DustDevil */
#define LMI_DID1_LM3S3748      0x1049U
#define LMI_DID1_LM3S5732      0x1096U
#define LMI_DID1_LM3S8962      0x10a6U
/* Tiva-C Blizzard (TM4C123) */
#define LMI_DID1_TM4C123GH6PM  0x10a1U
#define LMI_DID1_TM4C1230C3PM  0x1022U
/* Tiva-C Snowflake (TM4C129x) */
#define LMI_DID1_TM4C1294NCPDT 0x101fU
#define LMI_DID1_TM4C1294KCPDT 0x1034U
#define LMI_DID1_TM4C129XNCZAD 0x1032U

#define LMI_FLASH_BASE 0x400fd000U
#define LMI_FLASH_FMA  (LMI_FLASH_BASE + 0x000U)
#define LMI_FLASH_FMD  (LMI_FLASH_BASE + 0x004U)
#define LMI_FLASH_FMC  (LMI_FLASH_BASE + 0x008U)

#define LMI_FLASH_FMC_WRITE  (1U << 0U)
#define LMI_FLASH_FMC_ERASE  (1U << 1U)
#define LMI_FLASH_FMC_MERASE (1U << 2U)
#define LMI_FLASH_FMC_COMT   (1U << 3U)
#define LMI_FLASH_FMC_WRKEY  0xa4420000U
/* clang-format on */

// The erase size can be very large.  The maximum
// write size is limited by the memory in the probe.
#define LMI_FLASH_WRITESIZE 0x400U

static const uint16_t lmi_flash_write_stub[] = {
#include "flashstub/lmi.stub"
};

// Put the pointer first so that its aligned on all platforms.
typedef struct lmi_device {
	const char *driver;
	uint16_t did1;
	uint16_t ram_size_k;
	uint16_t flash_size_k;
	uint16_t block_size_k;
	uint32_t target_options;
	uint8_t dp_quirks;
} lmi_device_s;

static const lmi_device_s lmi_devices[] = {
	/* Stellaris Fury/DustDevil — 1 KiB erase blocks */
	{"Stellaris", LMI_DID1_LM3S3748, 64U, 128U, 1U, 0, 0},
	{"Stellaris", LMI_DID1_LM3S5732, 64U, 128U, 1U, 0, 0},
	{"Stellaris", LMI_DID1_LM3S8962, 64U, 256U, 1U, 0, 0},
	/* Tiva-C Blizzard (TM4C123) — 1 KiB erase blocks */
	{"Tiva-C", LMI_DID1_TM4C123GH6PM, 64U, 512U, 1U, TOPT_INHIBIT_NRST, ADIV5_DP_QUIRK_DUPED_AP},
	{"Tiva-C", LMI_DID1_TM4C1230C3PM, 96U, 64U, 1U, TOPT_INHIBIT_NRST, ADIV5_DP_QUIRK_DUPED_AP},
	/* Tiva-C Snowflake (TM4C129x) — 16 KiB erase blocks */
	{"Tiva-C", LMI_DID1_TM4C1294KCPDT, 256U, 512U, 16U, TOPT_INHIBIT_NRST, ADIV5_DP_QUIRK_DUPED_AP},
	{"Tiva-C", LMI_DID1_TM4C1294NCPDT, 256U, 1024U, 16U, TOPT_INHIBIT_NRST, ADIV5_DP_QUIRK_DUPED_AP},
	{"Tiva-C", LMI_DID1_TM4C129XNCZAD, 256U, 1024U, 16U, TOPT_INHIBIT_NRST, ADIV5_DP_QUIRK_DUPED_AP},
};

static bool lmi_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool lmi_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);

static void lmi_add_flash(target_s *target, size_t length, size_t block_size)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = 0;
	flash->length = length;
	flash->blocksize = block_size;
	flash->writesize = LMI_FLASH_WRITESIZE;
	flash->erase = lmi_flash_erase;
	flash->write = lmi_flash_write;
	flash->erased = 0xff;
	target_add_flash(target, flash);
}

bool lmi_probe(target_s *const target)
{
	const uint32_t did0 = target_mem32_read32(target, LMI_SCB_DID0);
	const uint16_t did1 = target_mem32_read32(target, LMI_SCB_DID1) >> 16U;

	switch (did0 & LMI_DID0_CLASS_MASK) {
	case LMI_DID0_CLASS_STELLARIS_SANDSTORM:
	case LMI_DID0_CLASS_STELLARIS_FURY:
	case LMI_DID0_CLASS_STELLARIS_DUSTDEVIL:
	case LMI_DID0_CLASS_TIVA_BLIZZARD:
	case LMI_DID0_CLASS_TIVA_SNOWFLAKE:
		break;
	default:
		return false;
	}

	// Iterate over the device list.   If we find a match, return true.
	for (size_t i = 0U; i < ARRAY_LENGTH(lmi_devices); ++i) {
		const lmi_device_s *const dev = &lmi_devices[i];
		if (dev->did1 != did1)
			continue;
		target_add_ram32(target, LMI_SRAM_BASE, dev->ram_size_k << 10);
		lmi_add_flash(target, dev->flash_size_k << 10, dev->block_size_k << 10);
		target->driver = dev->driver;
		target->target_options |= dev->target_options;
		if (dev->dp_quirks)
			cortex_ap(target)->dp->quirks |= dev->dp_quirks;
		return true;
	}
	return false;
}

static bool lmi_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t len)
{
	target_s *target = flash->t;
	target_check_error(target);

	const bool full_erase = addr == flash->start && len == flash->length;
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);

	for (size_t offset = 0U; offset < len; offset += flash->blocksize) {
		target_mem32_write32(target, LMI_FLASH_FMA, addr + offset);
		target_mem32_write32(target, LMI_FLASH_FMC, LMI_FLASH_FMC_WRKEY | LMI_FLASH_FMC_ERASE);

		while (target_mem32_read32(target, LMI_FLASH_FMC) & LMI_FLASH_FMC_ERASE) {
			if (full_erase)
				target_print_progress(&timeout);
		}

		if (target_check_error(target))
			return false;
	}
	return true;
}

static bool lmi_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	target_s *target = flash->t;
	target_check_error(target);
	target_mem32_write(target, LMI_SRAM_BASE, lmi_flash_write_stub, sizeof(lmi_flash_write_stub));
	target_mem32_write(target, LMI_STUB_BUFFER_BASE, src, len);
	if (target_check_error(target))
		return false;

	return cortexm_run_stub(target, LMI_SRAM_BASE, dest, LMI_STUB_BUFFER_BASE, len, 0) == 0;
}
