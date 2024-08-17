/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Matthew Via <via@matthewvia.info>
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
 * This file implements S32K3xx target specific functions providing
 * the XML memory map and Flash memory programming.
 */

#include "command.h"
#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "adiv5.h"
#include "cortexm.h"

#define SIUL2_MIDR1 0x40290004U
#define MDMAPCTL    0x40250604U

#define C40ASF_MCR     0x402ec000U
#define C40ASF_MCR_PGM (1U << 8U)
#define C40ASF_MCR_ERS (1U << 4U)
#define C40ASF_MCR_EHV (1U << 0U)

#define C40ASF_MCRS      0x402ec004U
#define C40ASF_MCRS_PEP  (1U << 17U)
#define C40ASF_MCRS_PES  (1U << 16U)
#define C40ASF_MCRS_DONE (1U << 15U)
#define C40ASF_MCRS_PEG  (1U << 14U)

#define C40ASF_PEADR    0x402ec014U
#define C40ASF_DATA0    0x402ec100U
#define C40ASF_DATA1    0x402ec104U
#define PFCPGM_PEADR_L  0x40268300U
#define PFCBLKU_SPELOCK 0x40268358U

#define PFCBLK0_SSPELOCK 0x4026835cU
#define PFCBLK1_SSPELOCK 0x40268360U
#define PFCBLK2_SSPELOCK 0x40268364U
#define PFCBLK3_SSPELOCK 0x40268368U

#define PFCBLK0_SPELOCK 0x40268340U
#define PFCBLK1_SPELOCK 0x40268344U
#define PFCBLK2_SPELOCK 0x40268348U
#define PFCBLK3_SPELOCK 0x4026834cU
#define PFCBLK4_SPELOCK 0x40268350U

#define PAGE_SIZE         32U
#define QUAD_PAGE_SIZE    128U
#define SECTOR_SIZE       8192U
#define SUPER_SECTOR_SIZE 65536U

static inline uint32_t C40ASF_DATA_REG(const uint32_t x)
{
	return 0x402ec100U + (4U * x);
}

static inline uint32_t C40ASF_SSPELOCK_REG(const uint32_t block)
{
	return PFCBLK0_SSPELOCK + (4U * block);
}

static inline uint32_t C40ASF_SPELOCK_REG(const uint32_t block)
{
	return PFCBLK0_SPELOCK + (4U * block);
}

static bool s32k3xx_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool s32k3xx_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool s32k3xx_unlock_address(target_flash_s *const flash, target_addr_t addr);
static bool s32k3xx_flash_trigger_mcr(target_flash_s *const flash, uint32_t mcr_bits);
static void s32k3xx_flash_prepare(target_flash_s *const flash);
static void s32k3xx_reset(target_s *target);

typedef struct s32k3xx_flash {
	target_flash_s flash;
	uint8_t block;
} s32k3xx_flash_s;

static void s32k3xx_add_flash(
	target_s *const target, const uint32_t addr, const size_t length, const size_t erasesize, const uint8_t block)
{
	s32k3xx_flash_s *s32_flash = calloc(1, sizeof(*s32_flash));
	if (!s32_flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *flash = &s32_flash->flash;
	flash->start = addr;
	flash->length = length;
	flash->blocksize = erasesize;
	flash->writesize = 128U;
	flash->erase = s32k3xx_flash_erase;
	flash->write = s32k3xx_flash_write;
	flash->erased = 0xffU;
	s32_flash->block = block;
	target_add_flash(target, flash);
}

bool s32k3xx_probe(target_s *const target)
{
	uint32_t midr1 = target_mem32_read32(target, SIUL2_MIDR1);
	char product_letter = (midr1 >> 26U) & 0x3fU;
	uint32_t part_no = (midr1 >> 16U) & 0x3ffU;

	if (product_letter != 0xbU)
		return false;

	switch (part_no) {
	case 0x158U: /* S32K344 */
		target->driver = "S32K344";
		target_add_ram32(target, 0x20400000U, 0x00050000U);
		target_add_ram32(target, 0x00000000U, 0x00010000U);
		target_add_ram32(target, 0x20000000U, 0x00020000U);
		s32k3xx_add_flash(target, 0x00400000U, 0x00100000U, 0x2000U, 0U);
		s32k3xx_add_flash(target, 0x00500000U, 0x00100000U, 0x2000U, 1U);
		s32k3xx_add_flash(target, 0x00600000U, 0x00100000U, 0x2000U, 2U);
		s32k3xx_add_flash(target, 0x00700000U, 0x00100000U, 0x2000U, 3U);
		s32k3xx_add_flash(target, 0x10000000U, 0x00020000U, 0x2000U, 4U);
		break;
	}
	target->unsafe_enabled = false;
	target->target_options |= TOPT_INHIBIT_NRST;
	target->extended_reset = s32k3xx_reset;
	return true;
}

static bool s32k3xx_unlock_address(target_flash_s *const flash, target_addr_t addr)
{
	s32k3xx_flash_s *const s32flash = (s32k3xx_flash_s *)flash;

	/* Single (8KB) sector size locks are used only for the last 256 KB of a
   * block, and are the only type of lock if the block is less than 256 KB */
	target_addr_t start_of_single_sectors;
	if (flash->length < (256U * 1024U))
		start_of_single_sectors = flash->start;
	else
		start_of_single_sectors = flash->start + flash->length - (256U * 1024U);

	if (addr >= start_of_single_sectors) {
		/* Use 8KB sectors */
		uint8_t sector = (addr - start_of_single_sectors) / SECTOR_SIZE;
		uint32_t spelock_reg = C40ASF_SPELOCK_REG(s32flash->block);

		uint32_t spelock_val = target_mem32_read32(flash->t, spelock_reg);
		spelock_val &= ~(1U << sector);
		target_mem32_write32(flash->t, spelock_reg, spelock_val);
	} else {
		/* Use super sector unlock */
		uint8_t supersector = (addr - flash->start) / SUPER_SECTOR_SIZE;
		uint32_t sspelock_reg = C40ASF_SSPELOCK_REG(s32flash->block);

		uint32_t sspelock_val = target_mem32_read32(flash->t, sspelock_reg);
		sspelock_val &= ~(1U << supersector);
		target_mem32_write32(flash->t, sspelock_reg, sspelock_val);
	}
	return true;
}

static bool s32k3xx_flash_trigger_mcr(target_flash_s *const flash, uint32_t mcr_bits)
{
	uint32_t mcr = target_mem32_read32(flash->t, C40ASF_MCR);
	mcr |= mcr_bits;
	target_mem32_write32(flash->t, C40ASF_MCR, mcr);

	/* Set EVH to trigger operation */
	mcr |= C40ASF_MCR_EHV;
	target_mem32_write32(flash->t, C40ASF_MCR, mcr);

	/* Wait for DONE to be set.
	 * According to section 9.1 of S32KXX DS, lifetime max times for:
	 * Quad-page program: 450 uS
	 * 8 KB sector erase: 30 ms (typ 8.5),
	 * First wait 1 ms, then wait 10 ms at a time until we timeout
	 */
	platform_timeout_s wait_timeout;
	platform_timeout_set(&wait_timeout, 60);
	platform_delay(1);
	while (
		!(target_mem32_read32(flash->t, C40ASF_MCRS) & C40ASF_MCRS_DONE) && !platform_timeout_is_expired(&wait_timeout))
		platform_delay(10);

	if (!(target_mem32_read32(flash->t, C40ASF_MCRS) & C40ASF_MCRS_DONE)) {
		DEBUG_ERROR("MCRS[DONE] not set after operation\n");
		return false;
	}

	/* Clear the EVH bit first */
	mcr = target_mem32_read32(flash->t, C40ASF_MCR);
	mcr &= ~C40ASF_MCR_EHV;
	target_mem32_write32(flash->t, C40ASF_MCR, mcr);

	uint32_t mcrs = target_mem32_read32(flash->t, C40ASF_MCRS);

	/* Then clear the operation bits */
	mcr &= ~mcr_bits;
	target_mem32_write32(flash->t, C40ASF_MCR, mcr);

	if ((mcrs & C40ASF_MCRS_PEG) == 0U) {
		DEBUG_ERROR("MCRS[PEG] not set after operation\n");
		return false;
	}

	if ((mcrs & 0xffff0000U) > 0U) {
		DEBUG_ERROR("Operation failed, MCRS: %" PRIx32 "\n", mcrs);
		return false;
	}
	return true;
}

static void s32k3xx_flash_prepare(target_flash_s *const flash)
{
	uint32_t mcrs = target_mem32_read32(flash->t, C40ASF_MCRS);
	mcrs |= C40ASF_MCRS_PEP | C40ASF_MCRS_PES;
	target_mem32_write32(flash->t, C40ASF_MCRS, mcrs);
}

static bool s32k3xx_flash_erase(target_flash_s *const flash, target_addr_t addr, size_t len)
{
	(void)len;
	s32k3xx_flash_prepare(flash);
	s32k3xx_unlock_address(flash, addr);

	target_mem32_write32(flash->t, PFCPGM_PEADR_L, addr);
	target_mem32_write32(flash->t, C40ASF_DATA0, 0U);
	if (!s32k3xx_flash_trigger_mcr(flash, C40ASF_MCR_ERS))
		return false;

	return true;
}

static bool s32k3xx_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	const uint32_t *const s_data = src;
	s32k3xx_flash_prepare(flash);
	target_mem32_write32(flash->t, PFCPGM_PEADR_L, dest);
	for (size_t i = 0; i < len; i += 4) {
		const size_t word = i / 4;
		target_mem32_write32(flash->t, C40ASF_DATA_REG(word), s_data[word]);
	}

	if (!s32k3xx_flash_trigger_mcr(flash, C40ASF_MCR_PGM))
		return false;
	return true;
}

static void s32k3xx_reset(target_s *target)
{
	target_mem32_write32(target, CORTEXM_AIRCR, CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_VECTRESET);
}
