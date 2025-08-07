/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

/* This file implements Atmel SAM3/4 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * Supported devices: SAM3N, SAM3S, SAM3U, SAM3X, SAM4S, SAME70, SAMS70, SAMV71, SAMV70
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

/* Enhanced Embedded Flash Controller (EEFC) Register Map */
#define SAMx7x_EEFC_BASE   0x400e0c00U
#define SAM3N_EEFC_BASE    0x400e0a00U
#define SAM3X_EEFC_BASE(x) (0x400e0a00U + ((x) * 0x200U))
#define SAM3U_EEFC_BASE(x) (0x400e0800U + ((x) * 0x200U))
#define SAM4S_EEFC_BASE(x) (0x400e0a00U + ((x) * 0x200U))
#define EEFC_FMR(base)     ((base) + 0x00U)
#define EEFC_FCR(base)     ((base) + 0x04U)
#define EEFC_FSR(base)     ((base) + 0x08U)
#define EEFC_FRR(base)     ((base) + 0x0cU)

#define EEFC_FCR_FKEY      (0x5aU << 24U)
#define EEFC_FCR_FCMD_GETD 0x00U
#define EEFC_FCR_FCMD_WP   0x01U
#define EEFC_FCR_FCMD_WPL  0x02U
#define EEFC_FCR_FCMD_EWP  0x03U
#define EEFC_FCR_FCMD_EWPL 0x04U
#define EEFC_FCR_FCMD_EA   0x05U
#define EEFC_FCR_FCMD_EPA  0x07U
#define EEFC_FCR_FCMD_SLB  0x08U
#define EEFC_FCR_FCMD_CLB  0x09U
#define EEFC_FCR_FCMD_GLB  0x0aU
#define EEFC_FCR_FCMD_SGPB 0x0bU
#define EEFC_FCR_FCMD_CGPB 0x0cU
#define EEFC_FCR_FCMD_GGPB 0x0dU
#define EEFC_FCR_FCMD_STUI 0x0eU
#define EEFC_FCR_FCMD_SPUI 0x0fU

#define EEFC_FSR_FRDY   (1U << 0U)
#define EEFC_FSR_FCMDE  (1U << 1U)
#define EEFC_FSR_FLOCKE (1U << 2U)
#define EEFC_FSR_ERROR  (EEFC_FSR_FCMDE | EEFC_FSR_FLOCKE)

#define SAM_SMALL_PAGE_SIZE 256U
#define SAM_LARGE_PAGE_SIZE 512U

/* CHIPID Register Map */
#define SAM_CHIPID_BASE      0x400e0940U
#define SAM_CHIPID_CIDR      (SAM_CHIPID_BASE + 0x0U)
#define SAM_CHIPID_EXID      (SAM_CHIPID_BASE + 0x4U)
#define SAM34NSU_CHIPID_CIDR 0x400e0740U

#define CHIPID_CIDR_VERSION_MASK 0x1fU

#define CHIPID_CIDR_EPROC_MASK (0x7U << 5U)
#define CHIPID_CIDR_EPROC_CM7  (0x0U << 5U)
#define CHIPID_CIDR_EPROC_CM3  (0x3U << 5U)
#define CHIPID_CIDR_EPROC_CM4  (0x7U << 5U)

#define CHIPID_CIDR_NVPSIZ_MASK  (0xfU << 8U)
#define CHIPID_CIDR_NVPSIZ_8K    (0x1U << 8U)
#define CHIPID_CIDR_NVPSIZ_16K   (0x2U << 8U)
#define CHIPID_CIDR_NVPSIZ_32K   (0x3U << 8U)
#define CHIPID_CIDR_NVPSIZ_64K   (0x5U << 8U)
#define CHIPID_CIDR_NVPSIZ_128K  (0x7U << 8U)
#define CHIPID_CIDR_NVPSIZ_256K  (0x9U << 8U)
#define CHIPID_CIDR_NVPSIZ_512K  (0xaU << 8U)
#define CHIPID_CIDR_NVPSIZ_1024K (0xcU << 8U)
#define CHIPID_CIDR_NVPSIZ_2048K (0xeU << 8U)

#define CHIPID_CIDR_NVPSIZ2_MASK (0xfU << 12U)

#define CHIPID_CIDR_SRAMSIZ_MASK (0xfU << 16U)
#define CHIPID_CIDR_SRAMSIZ_384K (0x2U << 16U)
#define CHIPID_CIDR_SRAMSIZ_256K (0xdU << 16U)

#define CHIPID_CIDR_ARCH_MASK    (0xffU << 20U)
#define CHIPID_CIDR_ARCH_SAME70  (0x10U << 20U)
#define CHIPID_CIDR_ARCH_SAMS70  (0x11U << 20U)
#define CHIPID_CIDR_ARCH_SAMV71  (0x12U << 20U)
#define CHIPID_CIDR_ARCH_SAMV70  (0x13U << 20U)
#define CHIPID_CIDR_ARCH_SAM3UxC (0x80U << 20U)
#define CHIPID_CIDR_ARCH_SAM3UxE (0x81U << 20U)
#define CHIPID_CIDR_ARCH_SAM3XxC (0x84U << 20U)
#define CHIPID_CIDR_ARCH_SAM3XxE (0x85U << 20U)
#define CHIPID_CIDR_ARCH_SAM3XxG (0x86U << 20U)
#define CHIPID_CIDR_ARCH_SAM3NxA (0x93U << 20U)
#define CHIPID_CIDR_ARCH_SAM3NxB (0x94U << 20U)
#define CHIPID_CIDR_ARCH_SAM3NxC (0x95U << 20U)
#define CHIPID_CIDR_ARCH_SAM3SxA (0x88U << 20U)
#define CHIPID_CIDR_ARCH_SAM3SxB (0x89U << 20U)
#define CHIPID_CIDR_ARCH_SAM3SxC (0x8aU << 20U)
#define CHIPID_CIDR_ARCH_SAM4SxA (0x88U << 20U)
#define CHIPID_CIDR_ARCH_SAM4SxB (0x89U << 20U)
#define CHIPID_CIDR_ARCH_SAM4SxC (0x8aU << 20U)
#define CHIPID_CIDR_ARCH_SAM4SDB (0x99U << 20U)
#define CHIPID_CIDR_ARCH_SAM4SDC (0x9aU << 20U)

#define CHIPID_CIDR_NVPTYP_MASK      (0x7U << 28U)
#define CHIPID_CIDR_NVPTYP_FLASH     (0x2U << 28U)
#define CHIPID_CIDR_NVPTYP_ROM_FLASH (0x3U << 28U)

#define CHIPID_CIDR_EXT (1U << 31U)

#define CHIPID_EXID_SAMX7X_PINS_MASK 0x3U
#define CHIPID_EXID_SAMX7X_PINS_Q    0x2U
#define CHIPID_EXID_SAMX7X_PINS_N    0x1U
#define CHIPID_EXID_SAMX7X_PINS_J    0x0U

/* GPNVM */
#define GPNVM_SAMX7X_SECURITY_BIT_MASK 0x01U

#define GPNVM_SAMX7X_BOOT_BIT_MASK (0x1U << 1U)
#define GPNVM_SAMX7X_BOOT_ROM      (0x0U << 1U)
#define GPNVM_SAMX7X_BOOT_FLASH    (0x1U << 1U)

#define GPNVM_SAMX7X_TCM_BIT_MASK (0x3U << 7U)
#define GPNVM_SAMX7X_TCM_0K       (0x0U << 7U)
#define GPNVM_SAMX7X_TCM_32K      (0x1U << 7U)
#define GPNVM_SAMX7X_TCM_64K      (0x2U << 7U)
#define GPNVM_SAMX7X_TCM_128K     (0x3U << 7U)

static bool sam_cmd_gpnvm(target_s *target, int argc, const char **argv);

const command_s sam_cmd_list[] = {
	{"gpnvm", sam_cmd_gpnvm, "Set/Get GPVNM bits"},
	{NULL, NULL, NULL},
};

static bool sam_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool sam3_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool sam_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool sam_mass_erase(target_flash_s *flash, platform_timeout_s *print_progress);

static bool sam_gpnvm_get(target_s *target, uint32_t base, uint32_t *gpnvm);

typedef enum sam_driver {
	DRIVER_SAM3X,
	DRIVER_SAM3U,
	DRIVER_SAM4S,
	DRIVER_SAM3NS,
	DRIVER_SAMX7X,
} sam_driver_e;

typedef struct sam_flash {
	target_flash_s f;
	uint32_t eefc_base;
	uint8_t write_cmd;
} sam_flash_s;

typedef struct samx7x_descr {
	char product_code;
	uint8_t product_id;
	char pins;
	uint32_t ram_size;
	uint8_t density;
	char revision;
} samx7x_descr_s;

typedef struct sam_priv {
	samx7x_descr_s descr;
	char sam_variant_string[16];
} sam_priv_s;

static void sam3_add_flash(target_s *target, uint32_t eefc_base, uint32_t addr, size_t length)
{
	sam_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *target_flash = &flash->f;
	target_flash->start = addr;
	target_flash->length = length;
	target_flash->blocksize = SAM_SMALL_PAGE_SIZE;
	target_flash->erase = sam3_flash_erase;
	target_flash->write = sam_flash_write;
	target_flash->writesize = SAM_SMALL_PAGE_SIZE;
	flash->eefc_base = eefc_base;
	flash->write_cmd = EEFC_FCR_FCMD_EWP;
	target_add_flash(target, target_flash);
}

static void sam_add_flash(target_s *target, uint32_t eefc_base, uint32_t addr, size_t length, uint32_t page_size)
{
	sam_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *target_flash = &flash->f;
	target_flash->start = addr;
	target_flash->length = length;
	target_flash->blocksize = page_size * 8U;
	target_flash->erase = sam_flash_erase;
	target_flash->write = sam_flash_write;
	target_flash->mass_erase = sam_mass_erase;
	target_flash->writesize = page_size;
	flash->eefc_base = eefc_base;
	flash->write_cmd = EEFC_FCR_FCMD_WP;
	target_add_flash(target, target_flash);
}

static void samx7x_add_ram(target_s *target, uint32_t tcm_config, uint32_t ram_size)
{
	uint32_t itcm_size = 0;
	uint32_t dtcm_size = 0;

	switch (tcm_config) {
	case GPNVM_SAMX7X_TCM_32K:
		itcm_size = dtcm_size = 0x8000;
		break;
	case GPNVM_SAMX7X_TCM_64K:
		itcm_size = dtcm_size = 0x10000;
		break;
	case GPNVM_SAMX7X_TCM_128K:
		itcm_size = dtcm_size = 0x20000;
		break;
	}

	if (dtcm_size > 0)
		target_add_ram32(target, 0x20000000, dtcm_size);
	if (itcm_size > 0)
		target_add_ram32(target, 0x00000000, itcm_size);

	const uint32_t sram_size = ram_size - (itcm_size + dtcm_size);
	if (sram_size > 0)
		target_add_ram32(target, 0x20400000, sram_size);
}

static size_t sam_flash_size(uint32_t cidr)
{
	switch (cidr & CHIPID_CIDR_NVPSIZ_MASK) {
	case CHIPID_CIDR_NVPSIZ_8K:
		return 0x2000U;
	case CHIPID_CIDR_NVPSIZ_16K:
		return 0x4000U;
	case CHIPID_CIDR_NVPSIZ_32K:
		return 0x8000U;
	case CHIPID_CIDR_NVPSIZ_64K:
		return 0x10000U;
	case CHIPID_CIDR_NVPSIZ_128K:
		return 0x20000U;
	case CHIPID_CIDR_NVPSIZ_256K:
		return 0x40000U;
	case CHIPID_CIDR_NVPSIZ_512K:
		return 0x80000U;
	case CHIPID_CIDR_NVPSIZ_1024K:
		return 0x100000U;
	case CHIPID_CIDR_NVPSIZ_2048K:
		return 0x200000U;
	default:
		return 0;
	}
}

static size_t sam_sram_size(uint32_t cidr)
{
	switch (cidr & CHIPID_CIDR_SRAMSIZ_MASK) {
	case CHIPID_CIDR_SRAMSIZ_256K:
		return 0x40000U;
	case CHIPID_CIDR_SRAMSIZ_384K:
		return 0x60000U;
	default:
		return 0;
	}
}

samx7x_descr_s samx7x_parse_id(uint32_t cidr, uint32_t exid)
{
	samx7x_descr_s descr = {0};

	switch (cidr & CHIPID_CIDR_ARCH_MASK) {
	case CHIPID_CIDR_ARCH_SAME70:
		descr.product_code = 'E';
		descr.product_id = 70;
		break;
	case CHIPID_CIDR_ARCH_SAMS70:
		descr.product_code = 'S';
		descr.product_id = 70;
		break;
	case CHIPID_CIDR_ARCH_SAMV71:
		descr.product_code = 'V';
		descr.product_id = 71;
		break;
	case CHIPID_CIDR_ARCH_SAMV70:
		descr.product_code = 'V';
		descr.product_id = 70;
		break;
	}

	// A = Revision A, legacy version
	// B = Revision B, current variant
	switch (exid & CHIPID_CIDR_VERSION_MASK) {
	case 0:
		descr.revision = 'A';
		break;
	case 1:
		descr.revision = 'B';
		break;
	default:
		descr.revision = '_';
		break;
	}

	// Q = 144 pins
	// N = 100 pins
	// J = 64 pins
	switch (exid & CHIPID_EXID_SAMX7X_PINS_MASK) {
	case CHIPID_EXID_SAMX7X_PINS_Q:
		descr.pins = 'Q';
		break;
	case CHIPID_EXID_SAMX7X_PINS_N:
		descr.pins = 'N';
		break;
	case CHIPID_EXID_SAMX7X_PINS_J:
		descr.pins = 'J';
		break;
	}

	descr.ram_size = sam_sram_size(cidr);

	switch (cidr & CHIPID_CIDR_NVPSIZ_MASK) {
	case CHIPID_CIDR_NVPSIZ_2048K:
		descr.density = 21;
		break;
	case CHIPID_CIDR_NVPSIZ_1024K:
		descr.density = 20;
		break;
	case CHIPID_CIDR_NVPSIZ_512K:
		descr.density = 19;
		break;
	default:
		descr.density = 0;
		break;
	}

	return descr;
}

bool samx7x_probe(target_s *target)
{
	/* Start by reading out the ChipID peripheral's CIDR, and if that indicates there's an EXID, that too */
	const uint32_t cidr = target_mem32_read32(target, SAM_CHIPID_CIDR);
	uint32_t exid = 0;
	if (cidr & CHIPID_CIDR_EXT)
		exid = target_mem32_read32(target, SAM_CHIPID_EXID);

	/* Check that this is one of the supported SAMx7x family parts */
	switch (cidr & CHIPID_CIDR_ARCH_MASK) {
	case CHIPID_CIDR_ARCH_SAME70:
	case CHIPID_CIDR_ARCH_SAMS70:
	case CHIPID_CIDR_ARCH_SAMV71:
	case CHIPID_CIDR_ARCH_SAMV70:
		break;
	default:
		return false;
	}

	/* Now we have a positive ID on the part, allocate storage for the private struct data */
	sam_priv_s *priv_storage = calloc(1, sizeof(*priv_storage));
	if (!priv_storage) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	target->target_storage = priv_storage;

	/* Figure out which exact chip it is and what its part code characteristics are */
	priv_storage->descr = samx7x_parse_id(cidr, exid);

	/* Check and see what TCM config is set up on the device */
	uint32_t tcm_config = 0;
	if (!sam_gpnvm_get(target, SAMx7x_EEFC_BASE, &tcm_config))
		return false;
	tcm_config &= GPNVM_SAMX7X_TCM_BIT_MASK;

	/* Wait for the Flash controller to become idle and then read the Flash descriptor */
	while (!(target_mem32_read32(target, EEFC_FSR(SAMx7x_EEFC_BASE)) & EEFC_FSR_FRDY))
		continue;
	target_mem32_write32(target, EEFC_FCR(SAMx7x_EEFC_BASE), EEFC_FCR_FKEY | EEFC_FCR_FCMD_GETD);
	while (!(target_mem32_read32(target, EEFC_FSR(SAMx7x_EEFC_BASE)) & EEFC_FSR_FRDY))
		continue;
#ifndef DEBUG_TARGET_IS_NOOP
	/* Now FRR contains FL_ID, so read that to discard it (reporting it as info) */
	const uint32_t flash_id =
#endif
		target_mem32_read32(target, EEFC_FRR(SAMx7x_EEFC_BASE));
	DEBUG_TARGET("Flash ID: %08" PRIx32 "\n", flash_id);
	/* Now extract the Flash size and then the Flash page size */
	const uint32_t flash_size = target_mem32_read32(target, EEFC_FRR(SAMx7x_EEFC_BASE));
	const uint32_t flash_page_size = target_mem32_read32(target, EEFC_FRR(SAMx7x_EEFC_BASE));
	DEBUG_TARGET(
		"Found %" PRIu32 " bytes of Flash with a %" PRIu32 " byte Flash page size\n", flash_size, flash_page_size);

	/* Register appropriate RAM and Flash for the part */
	samx7x_add_ram(target, tcm_config, priv_storage->descr.ram_size);
	sam_add_flash(target, SAMx7x_EEFC_BASE, 0x00400000, flash_size, flash_page_size);
	/* Register target-specific commands */
	target_add_commands(target, sam_cmd_list, "SAMx7x");

	snprintf(priv_storage->sam_variant_string, sizeof(priv_storage->sam_variant_string), "SAM%c%02d%c%d%c",
		priv_storage->descr.product_code, priv_storage->descr.product_id, priv_storage->descr.pins,
		priv_storage->descr.density, priv_storage->descr.revision);

	target->driver = priv_storage->sam_variant_string;
	return true;
}

bool sam3x_probe(target_s *target)
{
	uint32_t cidr = target_mem32_read32(target, SAM_CHIPID_CIDR);
	size_t size = sam_flash_size(cidr);
	switch (cidr & (CHIPID_CIDR_ARCH_MASK | CHIPID_CIDR_EPROC_MASK)) {
	case CHIPID_CIDR_ARCH_SAM3XxC | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3XxE | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3XxG | CHIPID_CIDR_EPROC_CM3:
		target->driver = "Atmel SAM3X";
		target->target_options |= TOPT_INHIBIT_NRST;
		target_add_ram32(target, 0x20000000, 0x200000);
		/* 2 Flash memories back-to-back starting at 0x80000 */
		sam3_add_flash(target, SAM3X_EEFC_BASE(0), 0x80000, size / 2U);
		sam3_add_flash(target, SAM3X_EEFC_BASE(1U), 0x80000 + size / 2U, size / 2U);
		target_add_commands(target, sam_cmd_list, "SAM3X");
		return true;
	}

	cidr = target_mem32_read32(target, SAM34NSU_CHIPID_CIDR);
	size = sam_flash_size(cidr);
	switch (cidr & (CHIPID_CIDR_ARCH_MASK | CHIPID_CIDR_EPROC_MASK)) {
	case CHIPID_CIDR_ARCH_SAM3NxA | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3NxB | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3NxC | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3SxA | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3SxB | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3SxC | CHIPID_CIDR_EPROC_CM3:
		target->driver = "Atmel SAM3N/S";
		target_add_ram32(target, 0x20000000, 0x200000);
		/* These devices only have a single bank */
		sam3_add_flash(target, SAM3N_EEFC_BASE, 0x400000, size);
		target_add_commands(target, sam_cmd_list, "SAM3N/S");
		return true;
	case CHIPID_CIDR_ARCH_SAM3UxC | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3UxE | CHIPID_CIDR_EPROC_CM3:
		target->driver = "Atmel SAM3U";
		target_add_ram32(target, 0x20000000, 0x200000);
		/* One flash up to 512K at 0x80000 */
		sam3_add_flash(target, SAM3U_EEFC_BASE(0), 0x80000, MIN(size, 0x80000));
		/* Larger devices have a second bank at 0x100000 */
		if (size >= 0x80000U)
			sam3_add_flash(target, SAM3U_EEFC_BASE(1U), 0x100000, 0x80000);

		target_add_commands(target, sam_cmd_list, "SAM3U");
		return true;
	case CHIPID_CIDR_ARCH_SAM4SxA | CHIPID_CIDR_EPROC_CM4:
	case CHIPID_CIDR_ARCH_SAM4SxB | CHIPID_CIDR_EPROC_CM4:
	case CHIPID_CIDR_ARCH_SAM4SxC | CHIPID_CIDR_EPROC_CM4:
	case CHIPID_CIDR_ARCH_SAM4SDB | CHIPID_CIDR_EPROC_CM4:
	case CHIPID_CIDR_ARCH_SAM4SDC | CHIPID_CIDR_EPROC_CM4:
		target->driver = "Atmel SAM4S";
		target_add_ram32(target, 0x20000000, 0x400000);
		/* Smaller devices have a single bank */
		if (size <= 0x80000U)
			sam_add_flash(target, SAM4S_EEFC_BASE(0), 0x400000, size, SAM_LARGE_PAGE_SIZE);
		else {
			/* Larger devices are split evenly between 2 */
			sam_add_flash(target, SAM4S_EEFC_BASE(0), 0x400000, size / 2U, SAM_LARGE_PAGE_SIZE);
			sam_add_flash(target, SAM4S_EEFC_BASE(1U), 0x400000 + size / 2U, size / 2U, SAM_LARGE_PAGE_SIZE);
		}
		target_add_commands(target, sam_cmd_list, "SAM4S");
		return true;
	}
	return false;
}

static bool sam_flash_cmd(target_s *target, uint32_t base, uint8_t cmd, uint16_t arg)
{
	DEBUG_INFO("%s: base = 0x%08" PRIx32 " cmd = 0x%02X, arg = 0x%04X\n", __func__, base, cmd, arg);

	if (base == 0U)
		return false;

	/* Wait for the Flash controller to become idle and then initiate the command */
	while (!(target_mem32_read32(target, EEFC_FSR(base)) & EEFC_FSR_FRDY))
		continue;
	target_mem32_write32(target, EEFC_FCR(base), EEFC_FCR_FKEY | cmd | ((uint32_t)arg << 8U));

	/* Then wait for the command to complete */
	uint32_t status = 0;
	while (!(status & EEFC_FSR_FRDY)) {
		status = target_mem32_read32(target, EEFC_FSR(base));
		if (target_check_error(target))
			return false;
	}
	return !(status & EEFC_FSR_ERROR);
}

static sam_driver_e sam_driver(target_s *target)
{
	if (strcmp(target->driver, "Atmel SAM3X") == 0)
		return DRIVER_SAM3X;
	if (strcmp(target->driver, "Atmel SAM3U") == 0)
		return DRIVER_SAM3U;
	if (strcmp(target->driver, "Atmel SAM4S") == 0)
		return DRIVER_SAM4S;
	if (strcmp(target->driver, "Atmel SAM3N/S") == 0)
		return DRIVER_SAM3NS;
	return DRIVER_SAMX7X;
}

static bool sam_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len)
{
	(void)len;
	target_s *target = flash->t;
	const uint32_t base = ((sam_flash_s *)flash)->eefc_base;

	/*
	 * Devices supported through this routine use an 8 page erase size.
	 * arg[15:2] contains the page number (aligned to the nearest 8 pages) and
	 * arg[1:0] contains 0x1, indicating 8-page chunks.
	 */
	const uint32_t chunk = (addr - flash->start) / flash->writesize;
	const uint16_t arg = (chunk & 0xfffcU) | 0x0001U;
	return sam_flash_cmd(target, base, EEFC_FCR_FCMD_EPA, arg);
}

static bool sam3_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len)
{
	/*
	 * The SAM3X/SAM3N don't really have a page erase function.
	 * We do nothing here and use Erase/Write page in flash_write.
	 */
	(void)flash;
	(void)addr;
	(void)len;

	return true;
}

static bool sam_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	target_s *const target = flash->t;
	sam_flash_s *const sf = (sam_flash_s *)flash;

	const uint32_t page = (dest - flash->start) / flash->writesize;

	target_mem32_write(target, dest, src, len);
	return sam_flash_cmd(target, sf->eefc_base, sf->write_cmd, page);
}

static bool sam_mass_erase(target_flash_s *const flash, platform_timeout_s *const print_progress)
{
	/* Extract the target and base address of the Flash controller to run this on */
	target_s *const target = flash->t;
	const uint32_t base = ((sam_flash_s *)flash)->eefc_base;

	/* Initiate the Flash erase all command */
	while (!(target_mem32_read32(target, EEFC_FSR(base)) & EEFC_FSR_FRDY))
		continue;
	target_mem32_write32(target, EEFC_FCR(base), EEFC_FCR_FKEY | EEFC_FCR_FCMD_EA);
	/* Then wait for that to complete, printing progress as required */
	while (!(target_mem32_read32(target, EEFC_FSR(base)) & EEFC_FSR_FRDY)) {
		if (print_progress)
			target_print_progress(print_progress);
	}

	return true;
}

static bool sam_gpnvm_get(target_s *target, uint32_t base, uint32_t *gpnvm)
{
	if (!gpnvm || !sam_flash_cmd(target, base, EEFC_FCR_FCMD_GGPB, 0))
		return false;

	*gpnvm = target_mem32_read32(target, EEFC_FRR(base));
	return true;
}

static bool sam_cmd_gpnvm(target_s *target, int argc, const char **argv)
{
	if (argc != 2 && argc != 4)
		goto bad_usage;

	const uint8_t arglen = strlen(argv[1]);
	if (arglen == 0)
		goto bad_usage;

	const sam_driver_e drv = sam_driver(target);
	uint32_t base = 0;
	uint32_t gpnvm_mask = 0;
	switch (drv) {
	case DRIVER_SAM3X:
		gpnvm_mask = 0x7U;
		base = SAM3X_EEFC_BASE(0);
		break;
	case DRIVER_SAM3U:
		gpnvm_mask = 0x7U;
		base = SAM3U_EEFC_BASE(0);
		break;
	case DRIVER_SAM4S:
		gpnvm_mask = 0x7U;
		base = SAM4S_EEFC_BASE(0);
		break;
	case DRIVER_SAM3NS:
		gpnvm_mask = 0x3U;
		base = SAM3N_EEFC_BASE;
		break;
	case DRIVER_SAMX7X:
		gpnvm_mask = 0x1bfU;
		base = SAMx7x_EEFC_BASE;
		break;
	default:
		/* unknown / invalid driver*/
		return false;
	}

	uint32_t mask = 0;
	/* If `gpnvm set` is requested, handle set arguments */
	if (strncmp(argv[1], "set", arglen) == 0) {
		mask = strtoul(argv[2], NULL, 0);
		if (mask == 0 || (mask & ~gpnvm_mask))
			/* Trying to write invalid bits */
			goto bad_usage;

		const uint32_t values = strtoul(argv[3], NULL, 0);
		for (size_t bit = 0; bit < 32U; ++bit) {
			const uint32_t index = 1U << bit;
			if (mask & index) {
				uint8_t cmd = (values & index) ? EEFC_FCR_FCMD_SGPB : EEFC_FCR_FCMD_CGPB;
				if (!sam_flash_cmd(target, base, cmd, bit))
					return false;
			}
		}
		/* Otherwise, if anything other than `gpnvm get` is requested, it's bad usage */
	} else if (strncmp(argv[1], "get", arglen) != 0)
		goto bad_usage;

	uint32_t gpnvm = 0;
	if (!sam_gpnvm_get(target, base, &gpnvm))
		return false;
	tc_printf(target, "GPNVM: 0x%08" PRIX32 "\n", gpnvm);

	if (drv == DRIVER_SAMX7X && (mask & GPNVM_SAMX7X_TCM_BIT_MASK)) {
		sam_priv_s *storage = (sam_priv_s *)target->target_storage;

		target_ram_map_free(target);
		samx7x_add_ram(target, gpnvm & GPNVM_SAMX7X_TCM_BIT_MASK, storage->descr.ram_size);
		target_reset(target);
	}

	return true;

bad_usage:
	tc_printf(target, "usage: monitor gpnvm get\n");
	tc_printf(target, "usage: monitor gpnvm set <mask> <val>\n");
	return false;
}
