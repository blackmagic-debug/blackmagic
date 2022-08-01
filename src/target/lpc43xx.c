/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2014 Allen Ibara <aibara>
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

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "lpc_common.h"

#define LPC43xx_CHIPID                0x40043200U
#define LPC43xx_CHIPID_FAMILY_MASK    0x0fffffffU
#define LPC43xx_CHIPID_FAMILY_CODE    0x0906002bU
#define LPC43xx_CHIPID_CHIP_MASK      0xf0000000U
#define LPC43xx_CHIPID_CHIP_SHIFT     28U
#define LPC43xx_CHIPID_CORE_TYPE_MASK 0xff0ffff0U
#define LPC43xx_CHIPID_CORE_TYPE_M0   0x4100c200U
#define LPC43xx_CHIPID_CORE_TYPE_M4   0x4100c240U

#define LPC43xx_PARTID_LOW     0x40045000U
#define LPC43xx_PARTID_HIGH    0x40045004U
#define LPC43xx_PARTID_INVALID 0x00000000U

/* Flashless parts */
#define LPC43xx_PARTID_LPC4310 0xa00acb3fU
#define LPC43xx_PARTID_LPC4320 0xa000cb3cU
#define LPC43xx_PARTID_LPC4330 0xa0000a30U
#define LPC43xx_PARTID_LPC4350 0xa0000830U
#define LPC43xx_PARTID_LPC4370 0x00000230U

/* On-chip Flash parts */
#define LPC43xx_PARTID_LPC4312 0xa00bcb3fU
#define LPC43xx_PARTID_LPC4315 0xa001cb3fU
#define LPC43xx_PARTID_LPC4322 0xa00bcb3cU
#define LPC43xx_PARTID_LPC4325 0xa001cb3cU
#define LPC43xx_PARTID_LPC433x 0xa0010a30U
#define LPC43xx_PARTID_LPC435x 0xa0010830U

/* Flash configurations */
#define LPC43xx_PARTID_FLASH_CONFIG_MASK 0x000000ffU
#define LPC43xx_PARTID_FLASH_CONFIG_NONE 0x00U

#define IAP_ENTRYPOINT_LOCATION 0x10400100U

#define LPC43xx_ETBAHB_SRAM_BASE 0x2000c000U
#define LPC43xx_ETBAHB_SRAM_SIZE (16U * 1024U)

#define LPC43xx_CGU_BASE               0x40050000U
#define LPC43xx_CGU_CPU_CLK            (LPC43xx_CGU_BASE + 0x06cU)
#define LPC43xx_CGU_BASE_CLK_AUTOBLOCK (1U << 11U)
#define LPC43xx_CGU_BASE_CLK_SEL_IRC   (1U << 24U)

/* Cortex-M4 Application Interrupt and Reset Control Register */
#define LPC43xx_AIRCR 0xe000ed0cU
/* Magic value reset key */
#define LPC43xx_AIRCR_RESET 0x05fa0004U

#define LPC43xx_WDT_MODE       0x40080000U
#define LPC43xx_WDT_CNT        0x40080004U
#define LPC43xx_WDT_FEED       0x40080008U
#define LPC43xx_WDT_PERIOD_MAX 0xffffffU
#define LPC43xx_WDT_PROTECT    (1U << 4U)

#define IAP_RAM_SIZE LPC43xx_ETBAHB_SRAM_SIZE
#define IAP_RAM_BASE LPC43xx_ETBAHB_SRAM_BASE

#define IAP_PGM_CHUNKSIZE 4096U

#define FLASH_NUM_BANK   2U
#define FLASH_NUM_SECTOR 15U

typedef struct lpc43xx_partid {
	uint32_t part;
	uint8_t flash_config;
} lpc43xx_partid_s;

static bool lpc43xx_cmd_reset(target_s *t, int argc, const char **argv);
static bool lpc43xx_cmd_mkboot(target_s *t, int argc, const char **argv);

static lpc43xx_partid_s lpc43xx_read_partid_onchip_flash(target_s *t);
static lpc43xx_partid_s lpc43xx_read_partid_flashless(target_s *t);
static bool lpc43xx_iap_init(target_flash_s *flash);
static bool lpc43xx_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool lpc43xx_mass_erase(target_s *t);
static void lpc43xx_wdt_set_period(target_s *t);
static void lpc43xx_wdt_kick(target_s *t);

const command_s lpc43xx_cmd_list[] = {
	{"reset", lpc43xx_cmd_reset, "Reset target"},
	{"mkboot", lpc43xx_cmd_mkboot, "Make flash bank bootable"},
	{NULL, NULL, NULL},
};

static void lpc43xx_add_flash(
	target_s *t, uint32_t iap_entry, uint8_t bank, uint8_t base_sector, uint32_t addr, size_t len, size_t erasesize)
{
	lpc_flash_s *lf = lpc_add_flash(t, addr, len);
	lf->f.erase = lpc43xx_flash_erase;
	lf->f.blocksize = erasesize;
	lf->f.writesize = IAP_PGM_CHUNKSIZE;
	lf->bank = bank;
	lf->base_sector = base_sector;
	lf->iap_entry = iap_entry;
	lf->iap_ram = IAP_RAM_BASE;
	lf->iap_msp = IAP_RAM_BASE + IAP_RAM_SIZE;
	lf->wdt_kick = lpc43xx_wdt_kick;
}

static void lpc43xx_detect_flash(target_s *const t, const lpc43xx_partid_s part_id)
{
	(void)part_id;
	/* LPC4337 */
	const uint32_t iap_entry = target_mem_read32(t, IAP_ENTRYPOINT_LOCATION);
	target_add_ram(t, 0, 0x1a000000);
	lpc43xx_add_flash(t, iap_entry, 0, 0, 0x1a000000, 0x10000, 0x2000);
	lpc43xx_add_flash(t, iap_entry, 0, 8, 0x1a010000, 0x70000, 0x10000);
	target_add_ram(t, 0x1a080000, 0xf80000);
	lpc43xx_add_flash(t, iap_entry, 1, 0, 0x1b000000, 0x10000, 0x2000);
	lpc43xx_add_flash(t, iap_entry, 1, 8, 0x1b010000, 0x70000, 0x10000);
	target_add_commands(t, lpc43xx_cmd_list, "LPC43xx");
	target_add_ram(t, 0x1b080000, 0xe4f80000UL);
}

static void lpc43xx_detect_flashless(target_s *const t, const lpc43xx_partid_s part_id)
{
	(void)t;
	(void)part_id;
}

bool lpc43xx_probe(target_s *const t)
{
	const uint32_t chipid = target_mem_read32(t, LPC43xx_CHIPID);
	if ((chipid & LPC43xx_CHIPID_FAMILY_MASK) != LPC43xx_CHIPID_FAMILY_CODE)
		return false;

	const uint32_t chip_code = (chipid & LPC43xx_CHIPID_CHIP_MASK) >> LPC43xx_CHIPID_CHIP_SHIFT;
	t->target_options |= CORTEXM_TOPT_INHIBIT_NRST;

	/* 4 is for parts with on-chip Flash, 7 is undocumented but might be for LM43S parts */
	if (chip_code == 4U || chip_code == 7U) {
		const lpc43xx_partid_s part_id = lpc43xx_read_partid_onchip_flash(t);
		// DEBUG_WARN("LPC43xx part ID: 0x%08" PRIx32 ":%02x\n", part_id.part, part_id.flash_config);
		gdb_outf("LPC43xx part ID: 0x%08" PRIx32 ":%02x\n", part_id.part, part_id.flash_config);
		if (part_id.part == LPC43xx_PARTID_INVALID)
			return false;

		t->mass_erase = lpc43xx_mass_erase;
		lpc43xx_detect_flash(t, part_id);
	} else if (chip_code == 5U || chip_code == 6U) {
		const lpc43xx_partid_s part_id = lpc43xx_read_partid_flashless(t);
		// DEBUG_WARN("LPC43xx part ID: 0x%08" PRIx32 ":%02x\n", part_id.part, part_id.flash_config);
		gdb_outf("LPC43xx part ID: 0x%08" PRIx32 ":%02x\n", part_id.part, part_id.flash_config);
		if (part_id.part == LPC43xx_PARTID_INVALID)
			return false;

		lpc43xx_detect_flashless(t, part_id);
	} else
		return false;
	return true;
}

static bool lpc43xx_mass_erase(target_s *t)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	lpc43xx_iap_init(t->flash);

	for (size_t bank = 0; bank < FLASH_NUM_BANK; ++bank) {
		lpc_flash_s *f = (lpc_flash_s *)t->flash;
		if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, 0, FLASH_NUM_SECTOR - 1U, bank) ||
			lpc_iap_call(f, NULL, IAP_CMD_ERASE, 0, FLASH_NUM_SECTOR - 1U, CPU_CLK_KHZ, bank))
			return false;
		target_print_progress(&timeout);
	}

	return true;
}

static bool lpc43xx_iap_init(target_flash_s *const flash)
{
	target_s *const t = flash->t;
	lpc_flash_s *const f = (lpc_flash_s *)flash;
	/* Deal with WDT */
	lpc43xx_wdt_set_period(t);

	/* Force internal clock */
	target_mem_write32(t, LPC43xx_CGU_CPU_CLK, LPC43xx_CGU_BASE_CLK_AUTOBLOCK | LPC43xx_CGU_BASE_CLK_SEL_IRC);

	/* Initialize flash IAP */
	return lpc_iap_call(f, NULL, IAP_CMD_INIT) == IAP_STATUS_CMD_SUCCESS;
}

/*
 * It is for reasons of errata that we don't use the IAP device identification mechanism here.
 * Instead, we have to read out the bank 0 OTP bytes to fetch the part identification code.
 * Unfortunately it appears this itself has errata and doesn't line up with the values in the datasheet.
 */
static lpc43xx_partid_s lpc43xx_read_partid_flashless(target_s *const t)
{
	lpc43xx_partid_s result;
	result.part = target_mem_read32(t, LPC43xx_PARTID_LOW);
	result.flash_config = target_mem_read32(t, LPC43xx_PARTID_HIGH) & LPC43xx_PARTID_FLASH_CONFIG_MASK;
	return result;
}

/*
 * We can for the on-chip Flash parts use the IAP, so do so as this way the ID codes line up with
 * the ones in the datasheet.
 */
static lpc43xx_partid_s lpc43xx_read_partid_onchip_flash(target_s *const t)
{
	/* Define a fake Flash structure so we can invoke the IAP system */
	lpc_flash_s flash;
	flash.f.t = t;
	flash.wdt_kick = lpc43xx_wdt_kick;
	flash.iap_entry = target_mem_read32(t, IAP_ENTRYPOINT_LOCATION);
	flash.iap_ram = IAP_RAM_BASE;
	flash.iap_msp = IAP_RAM_BASE + IAP_RAM_SIZE;

	/* Prepare a failure result in case readback fails */
	lpc43xx_partid_s result;
	result.part = LPC43xx_PARTID_INVALID;
	result.flash_config = LPC43xx_PARTID_FLASH_CONFIG_NONE;

	/* Read back the part ID
	 * XXX: We only use the first 2 values but because of limitations in lpc_iap_call,
	 * we have to declare an array of 4
	 */
	uint32_t part_id[4];
	if (!lpc43xx_iap_init(&flash.f) || lpc_iap_call(&flash, part_id, IAP_CMD_PARTID) != IAP_STATUS_CMD_SUCCESS)
		return result;

	/* Prepare the result and return it */
	result.part = part_id[0];
	result.flash_config = part_id[1] & LPC43xx_PARTID_FLASH_CONFIG_MASK;
	return result;
}

static bool lpc43xx_flash_erase(target_flash_s *f, target_addr_t addr, size_t len)
{
	if (!lpc43xx_iap_init(f))
		return false;
	return lpc_flash_erase(f, addr, len);
}

/* Reset all major systems _except_ debug */
static bool lpc43xx_cmd_reset(target_s *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* System reset on target */
	target_mem_write32(t, LPC43xx_AIRCR, LPC43xx_AIRCR_RESET);
	return true;
}

/*
 * Call Boot ROM code to make a flash bank bootable by computing and writing the
 * correct signature into the exception table near the start of the bank.
 *
 * This is done independently of writing to give the user a chance to verify flash
 * before changing it.
 */
static bool lpc43xx_cmd_mkboot(target_s *t, int argc, const char **argv)
{
	/* Usage: mkboot 0 or mkboot 1 */
	if (argc != 2) {
		tc_printf(t, "Expected bank argument 0 or 1.\n");
		return false;
	}

	const uint32_t bank = strtoul(argv[1], NULL, 0);
	if (bank > 1) {
		tc_printf(t, "Unexpected bank number, should be 0 or 1.\n");
		return false;
	}

	lpc43xx_iap_init(t->flash);

	/* special command to compute/write magic vector for signature */
	lpc_flash_s *f = (lpc_flash_s *)t->flash;
	if (lpc_iap_call(f, NULL, IAP_CMD_SET_ACTIVE_BANK, bank, CPU_CLK_KHZ)) {
		tc_printf(t, "Set bootable failed.\n");
		return false;
	}

	tc_printf(t, "Set bootable OK.\n");
	return true;
}

static void lpc43xx_wdt_set_period(target_s *t)
{
	/* Check if WDT is on */
	uint32_t wdt_mode = target_mem_read32(t, LPC43xx_WDT_MODE);

	/* If WDT on, we can't disable it, but we may be able to set a long period */
	if (wdt_mode && !(wdt_mode & LPC43xx_WDT_PROTECT))
		target_mem_write32(t, LPC43xx_WDT_CNT, LPC43xx_WDT_PERIOD_MAX);
}

static void lpc43xx_wdt_kick(target_s *t)
{
	/* Check if WDT is on */
	uint32_t wdt_mode = target_mem_read32(t, LPC43xx_WDT_MODE);

	/* If WDT on, kick it so we don't get the target reset */
	if (wdt_mode) {
		target_mem_write32(t, LPC43xx_WDT_FEED, 0xaa);
		target_mem_write32(t, LPC43xx_WDT_FEED, 0xff);
	}
}
