/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2014,2015 Marc Singer <elf@woollysoft.com>
 * Copyright (C) 2022-2024 1BitSquared <info@1bitsquared.com>
 * Written by Marc Singer <elf@woollysoft.com>
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
 * This file implements STM32L0 and STM32L1 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * References:
 * RM0377 - Ultra-low-power STM32L0x1 advanced Arm®-based 32-bit MCUs, Rev. 10
 * - https://www.st.com/resource/en/reference_manual/rm0377-ultralowpower-stm32l0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * RM0038 - STM32L100xx, STM32L151xx, STM32L152xx and STM32L162xx advanced Arm®-based 32-bit MCUs, Rev. 17
 * - https://www.st.com/resource/en/reference_manual/rm0038-stm32l100xx-stm32l151xx-stm32l152xx-and-stm32l162xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 *
 * Note:
 * This implementation has a few known defficiencies and quirks, these are:
 * - Error handling -> We should probably clear Flash controller statusu register errors
 *   immediately after detecting them. If we don't then we must always wait for the controller
 *   to complete the previous operatioon before starting the next.
 * - Minor inconsistencies between the STM32L0 and STM32L1 Flash controllers that should be handled
 * - On the STM32L1, the Flash controller PECR can only be changed when the controller is
 *   idle, while on the STM32L0 it may be updated while an operation is in progress
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "stm32_common.h"

#define STM32Lx_FLASH_BANK_BASE 0x08000000U
#define STM32L0_FLASH_BANK_SIZE 0x00010000U
#define STM32L0_FLASH_PAGE_SIZE 0x00000080U
#define STM32L1_FLASH_PAGE_SIZE 0x00000100U
#define STM32Lx_EEPROM_BASE     0x08080000U
#define STM32Lx_SRAM_BASE       0x20000000U
#define STM32L0_SRAM_SIZE       0x00005000U
#define STM32L1_SRAM_SIZE       0x00014000U

#define STM32Lx_FLASH_PECR(flash_base)    ((flash_base) + 0x04U)
#define STM32Lx_FLASH_PEKEYR(flash_base)  ((flash_base) + 0x0cU)
#define STM32Lx_FLASH_PRGKEYR(flash_base) ((flash_base) + 0x10U)
#define STM32Lx_FLASH_OPTKEYR(flash_base) ((flash_base) + 0x14U)
#define STM32Lx_FLASH_SR(flash_base)      ((flash_base) + 0x18U)
#define STM32Lx_FLASH_OPTR(flash_base)    ((flash_base) + 0x1cU)

#define STM32L0_FLASH_BASE             UINT32_C(0x40022000)
#define STM32L0_FLASH_OPT_SIZE         12U
#define STM32L0_FLASH_EEPROM_CAT1_SIZE 512U  // 512B
#define STM32L0_FLASH_EEPROM_CAT2_SIZE 1024U // 1KiB
#define STM32L0_FLASH_EEPROM_CAT3_SIZE 2048U // 2KiB
#define STM32L0_FLASH_EEPROM_CAT5_SIZE 6144U // 6KiB

#define STM32L1_FLASH_BASE        UINT32_C(0x40023c00)
#define STM32L1_FLASH_OPT_SIZE    32U
#define STM32L1_FLASH_EEPROM_SIZE 16384U // 16KiB

#define STM32Lx_FLASH_OPT_BASE    UINT32_C(0x1ff80000)
#define STM32Lx_FLASH_EEPROM_BASE UINT32_C(0x08080000)

#define STM32Lx_FLASH_PEKEY1  UINT32_C(0x89abcdef)
#define STM32Lx_FLASH_PEKEY2  UINT32_C(0x02030405)
#define STM32Lx_FLASH_PRGKEY1 UINT32_C(0x8c9daebf)
#define STM32Lx_FLASH_PRGKEY2 UINT32_C(0x13141516)
#define STM32Lx_FLASH_OPTKEY1 UINT32_C(0xfbead9c8)
#define STM32Lx_FLASH_OPTKEY2 UINT32_C(0x24252627)

#define STM32Lx_FLASH_PECR_OBL_LAUNCH (1U << 18U)
#define STM32Lx_FLASH_PECR_ERRIE      (1U << 17U)
#define STM32Lx_FLASH_PECR_EOPIE      (1U << 16U)
#define STM32Lx_FLASH_PECR_FPRG       (1U << 10U)
#define STM32Lx_FLASH_PECR_ERASE      (1U << 9U)
#define STM32Lx_FLASH_PECR_FIX        (1U << 8U) /* FTDW */
#define STM32Lx_FLASH_PECR_DATA       (1U << 4U)
#define STM32Lx_FLASH_PECR_PROG       (1U << 3U)
#define STM32Lx_FLASH_PECR_OPTLOCK    (1U << 2U)
#define STM32Lx_FLASH_PECR_PRGLOCK    (1U << 1U)
#define STM32Lx_FLASH_PECR_PELOCK     (1U << 0U)

#define STM32Lx_FLASH_SR_NOTZEROERR (1U << 16U)
#define STM32Lx_FLASH_SR_SIZERR     (1U << 10U)
#define STM32Lx_FLASH_SR_PGAERR     (1U << 9U)
#define STM32Lx_FLASH_SR_WRPERR     (1U << 8U)
#define STM32Lx_FLASH_SR_EOP        (1U << 1U)
#define STM32Lx_FLASH_SR_BSY        (1U << 0U)
#define STM32Lx_FLASH_SR_ERR_MASK \
	(STM32Lx_FLASH_SR_WRPERR | STM32Lx_FLASH_SR_PGAERR | STM32Lx_FLASH_SR_SIZERR | STM32Lx_FLASH_SR_NOTZEROERR)

#define STM32L0_FLASH_OPTR_BOOT1        (1U << 31U)
#define STM32Lx_FLASH_OPTR_WDG_SW       (1U << 20U)
#define STM32L0_FLASH_OPTR_WPRMOD       (1U << 8U)
#define STM32Lx_FLASH_OPTR_RDPROT_SHIFT 0U
#define STM32Lx_FLASH_OPTR_RDPROT_MASK  0xffU
#define STM32Lx_FLASH_OPTR_RDPROT_0     0xaaU
#define STM32Lx_FLASH_OPTR_RDPROT_2     0xccU

#define STM32L1_FLASH_OPTR_nBFB2         (1U << 23U)
#define STM32L1_FLASH_OPTR_nRST_STDBY    (1U << 22U)
#define STM32L1_FLASH_OPTR_nRST_STOP     (1U << 21U)
#define STM32L1_FLASH_OPTR_BOR_LEV_SHIFT 16U
#define STM32L1_FLASH_OPTR_BOR_LEV_MASK  0xfU
#define STM32L1_FLASH_OPTR_SPRMOD        (1U << 8U)

#define STM32L0_DBGMCU_BASE       UINT32_C(0x40015800)
#define STM32L0_DBGMCU_IDCODE     (STM32L0_DBGMCU_BASE + 0x000U)
#define STM32L0_DBGMCU_CONFIG     (STM32L0_DBGMCU_BASE + 0x004U)
#define STM32L0_DBGMCU_APB1FREEZE (STM32L0_DBGMCU_BASE + 0x008U)
#define STM32L0_UID_BASE          0x1ff80050U
#define STM32L0_UID_FLASH_SIZE    0x1ff8007cU

/*
 * NB: The L1 has two different UID and Flash size register base addresses!
 * The L1xxxB ones are for Category 1 & 2 devices only. The L1xxxx ones
 * are for Category 3, 4, 5 and 6 as the devices have two different memory maps
 * that depend on the category code.
 */
#define STM32L1_DBGMCU_BASE        UINT32_C(0xe0042000)
#define STM32L1_DBGMCU_IDCODE      (STM32L1_DBGMCU_BASE + 0x000U)
#define STM32L1_DBGMCU_CONFIG      (STM32L1_DBGMCU_BASE + 0x004U)
#define STM32L1_DBGMCU_APB1FREEZE  (STM32L1_DBGMCU_BASE + 0x008U)
#define STM32L1xxxB_UID_BASE       0x1ff80050U
#define STM32L1xxxB_UID_FLASH_SIZE 0x1ff8004cU
#define STM32L1xxxx_UID_BASE       0x1ff800d0U
#define STM32L1xxxx_UID_FLASH_SIZE 0x1ff800ccU

#define STM32Lx_DBGMCU_CONFIG_DBG_SLEEP   (1U << 0U)
#define STM32Lx_DBGMCU_CONFIG_DBG_STOP    (1U << 1U)
#define STM32Lx_DBGMCU_CONFIG_DBG_STANDBY (1U << 2U)
#define STM32Lx_DBGMCU_APB1FREEZE_WWDG    (1U << 11U)
#define STM32Lx_DBGMCU_APB1FREEZE_IWDG    (1U << 12U)

/* Taken from DBGMCU_IDCODE in §27.4.1 in RM0377 rev 10, pg820 */
#define ID_STM32L01x 0x457U /* Category 1 */
#define ID_STM32L03x 0x425U /* Category 2 */
#define ID_STM32L05x 0x417U /* Category 3 */
#define ID_STM32L07x 0x447U /* Category 5 */

/* Taken from DBGMCU_IDCODE in §30.6.1 in RM0038 rev 17, pg861 */
#define ID_STM32L1xxxB   0x416U /* Category 1 */
#define ID_STM32L1xxxBxA 0x429U /* Category 2 */
#define ID_STM32L1xxxC   0x427U /* Category 3 */
#define ID_STM32L1xxxD   0x436U /* Category 3/4 */
#define ID_STM32L1xxxE   0x437U /* Category 5/6 */

static bool stm32lx_cmd_option(target_s *target, int argc, const char **argv);
static bool stm32lx_cmd_eeprom(target_s *target, int argc, const char **argv);

static const command_s stm32lx_cmd_list[] = {
	{"option", stm32lx_cmd_option, "Manipulate option bytes"},
	{"eeprom", stm32lx_cmd_eeprom, "Manipulate EEPROM (FLASH data) memory"},
	{NULL, NULL, NULL},
};

static bool stm32l0_attach(target_s *target);
static bool stm32l1_attach(target_s *target);
static void stm32l0_detach(target_s *target);
static void stm32l1_detach(target_s *target);
static bool stm32lx_flash_erase(target_flash_s *flash, target_addr_t addr, size_t length);
static bool stm32lx_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t length);
static bool stm32lx_eeprom_erase(target_flash_s *flash, target_addr_t addr, size_t length);
static bool stm32lx_eeprom_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t length);
static bool stm32lx_mass_erase(target_s *target);

typedef struct stm32l_priv {
	target_addr32_t uid_taddr;
	uint32_t dbgmcu_config;
	char stm32l_variant[21];
} stm32l_priv_s;

static bool stm32lx_is_stm32l1(const target_s *const target)
{
	return target->part_id != ID_STM32L01x && target->part_id != ID_STM32L03x && target->part_id != ID_STM32L05x &&
		target->part_id != ID_STM32L07x;
}

static uint32_t stm32lx_nvm_eeprom_size(const target_s *const target)
{
	switch (target->part_id) {
	case 0x457U: /* STM32L0xx Cat1 */
		return STM32L0_FLASH_EEPROM_CAT1_SIZE;
	case 0x425U: /* STM32L0xx Cat2 */
		return STM32L0_FLASH_EEPROM_CAT2_SIZE;
	case 0x417U: /* STM32L0xx Cat3 */
		return STM32L0_FLASH_EEPROM_CAT3_SIZE;
	case 0x447U: /* STM32L0xx Cat5 */
		return STM32L0_FLASH_EEPROM_CAT5_SIZE;
	default: /* STM32L1xx */
		return STM32L1_FLASH_EEPROM_SIZE;
	}
}

static target_addr32_t stm32lx_flash_base(const target_s *const target)
{
	if (stm32lx_is_stm32l1(target))
		return STM32L1_FLASH_BASE;
	return STM32L0_FLASH_BASE;
}

static uint32_t stm32lx_nvm_option_size(const target_s *const target)
{
	if (stm32lx_is_stm32l1(target))
		return STM32L1_FLASH_OPT_SIZE;
	return STM32L0_FLASH_OPT_SIZE;
}

static void stm32l_add_flash(target_s *const target, const uint32_t addr, const size_t length, const size_t erasesize)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = addr;
	flash->length = length;
	flash->blocksize = erasesize;
	flash->erase = stm32lx_flash_erase;
	flash->write = stm32lx_flash_write;
	flash->writesize = erasesize >> 1U;
	target_add_flash(target, flash);
}

static void stm32l_add_eeprom(target_s *const target, const uint32_t addr, const size_t length)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = addr;
	flash->length = length;
	flash->blocksize = 4;
	flash->erase = stm32lx_eeprom_erase;
	flash->write = stm32lx_eeprom_write;
	target_add_flash(target, flash);
}

static void stm32l0_configure_dbgmcu(target_s *const target)
{
	/* Enable debugging during all low power modes */
	target_mem32_write32(target, STM32L0_DBGMCU_CONFIG,
		STM32Lx_DBGMCU_CONFIG_DBG_SLEEP | STM32Lx_DBGMCU_CONFIG_DBG_STANDBY | STM32Lx_DBGMCU_CONFIG_DBG_STOP);
	/* And make sure the WDTs stay synchronised to the run state of the processor */
	target_mem32_write32(
		target, STM32L0_DBGMCU_APB1FREEZE, STM32Lx_DBGMCU_APB1FREEZE_WWDG | STM32Lx_DBGMCU_APB1FREEZE_IWDG);
}

bool stm32l0_probe(target_s *const target)
{
	/* Try to identify the part, make sure it's a STM32L0 */
	if (target->part_id != ID_STM32L01x && target->part_id != ID_STM32L03x && target->part_id != ID_STM32L05x &&
		target->part_id != ID_STM32L07x)
		return false;

	/* Now we have a stable debug environment, make sure the WDTs + WFI and WFE instructions can't cause problems */
	stm32l0_configure_dbgmcu(target);

	target->driver = "STM32L0";
	target->attach = stm32l0_attach;
	target->detach = stm32l0_detach;
	target->mass_erase = stm32lx_mass_erase;
	target_add_commands(target, stm32lx_cmd_list, target->driver);

	/* Having identified that it's a STM32L0 of some sort, read out how much Flash it has */
	const uint32_t flash_size = target_mem32_read16(target, STM32L0_UID_FLASH_SIZE) * 1024U;
	/* There's no good way to tell how much RAM a part has, so use a one-size map */
	target_add_ram32(target, STM32Lx_SRAM_BASE, STM32L0_SRAM_SIZE);

	/* Now fill in the Flash map based on the part category */
	switch (target->part_id) {
	case ID_STM32L01x:
	case ID_STM32L03x:
	case ID_STM32L05x:
		/* Category 1, 2 and 3 only have one bank */
		stm32l_add_flash(target, STM32Lx_FLASH_BANK_BASE, flash_size, STM32L0_FLASH_PAGE_SIZE);
		break;
	case ID_STM32L07x: {
		/* Category 5 parts have 2 banks, split 50:50 on the total size of the Flash */
		const size_t bank_size = flash_size >> 1U;
		const target_addr32_t bank2_base = STM32Lx_FLASH_BANK_BASE + bank_size;
		stm32l_add_flash(target, STM32Lx_FLASH_BANK_BASE, bank_size, STM32L0_FLASH_PAGE_SIZE);
		stm32l_add_flash(target, bank2_base, bank_size, STM32L0_FLASH_PAGE_SIZE);
		break;
	}
	}
	/* There's also no good way to know how much EEPROM the part has, so define a one-size map for that too */
	stm32l_add_eeprom(target, STM32Lx_EEPROM_BASE, 0x1800);

	return true;
}

static bool stm32l1_configure_dbgmcu(target_s *const target)
{
	/* If we're in the probe phase */
	if (target->target_storage == NULL) {
		/* Allocate and save private storage */
		stm32l_priv_s *const priv_storage = calloc(1, sizeof(*priv_storage));
		if (!priv_storage) { /* calloc failed: heap exhaustion */
			DEBUG_ERROR("calloc: failed in %s\n", __func__);
			return false;
		}
		/* Get the current value of the debug config register (and store it for later) */
		priv_storage->dbgmcu_config = target_mem32_read32(target, STM32L1_DBGMCU_CONFIG);
		target->target_storage = priv_storage;

		target->attach = stm32l1_attach;
		target->detach = stm32l1_detach;
	}

	const stm32l_priv_s *const priv = (stm32l_priv_s *)target->target_storage;
	/* Now we have a stable debug environment, make sure the WDTs can't bonk the processor out from under us */
	target_mem32_write32(
		target, STM32L1_DBGMCU_APB1FREEZE, STM32Lx_DBGMCU_APB1FREEZE_WWDG | STM32Lx_DBGMCU_APB1FREEZE_IWDG);
	/* Then Reconfigure the config register to prevent WFI/WFE from cutting debug access */
	target_mem32_write32(target, STM32L1_DBGMCU_CONFIG,
		priv->dbgmcu_config | STM32Lx_DBGMCU_CONFIG_DBG_SLEEP | STM32Lx_DBGMCU_CONFIG_DBG_STANDBY |
			STM32Lx_DBGMCU_CONFIG_DBG_STOP);
	return true;
}

bool stm32l1_probe(target_s *const target)
{
	/* Try to identify the part, make sure it's a STM32L1 */
	const adiv5_access_port_s *const ap = cortex_ap(target);
	/* Use the partno from the AP always to handle the difference between JTAG and SWD */
	if (ap->partno != ID_STM32L1xxxB && ap->partno != ID_STM32L1xxxBxA && ap->partno != ID_STM32L1xxxC &&
		ap->partno != ID_STM32L1xxxD && ap->partno != ID_STM32L1xxxE)
		return false;
	target->part_id = ap->partno;

	/* Now we have a stable debug environment, make sure the WDTs + WFI and WFE instructions can't cause problems */
	stm32l1_configure_dbgmcu(target);

	target->driver = "STM32L1";
	target->mass_erase = stm32lx_mass_erase;
	target_add_commands(target, stm32lx_cmd_list, target->driver);
	/* There's no good way to tell how much RAM a part has, so use a one-size map */
	target_add_ram32(target, STM32Lx_SRAM_BASE, STM32L1_SRAM_SIZE);

	/* Having identified that it's a STM32L1 of some sort, dispatch Flash map setup based on the category */
	target_addr32_t flash_size_taddr = 0U;
	stm32l_priv_s *const priv = (stm32l_priv_s *)target->target_storage;
	switch (target->part_id) {
	case ID_STM32L1xxxB:
	case ID_STM32L1xxxBxA:
		flash_size_taddr = STM32L1xxxB_UID_FLASH_SIZE;
		priv->uid_taddr = STM32L1xxxB_UID_BASE;
		break;
	case ID_STM32L1xxxC:
	case ID_STM32L1xxxD:
	case ID_STM32L1xxxE:
		flash_size_taddr = STM32L1xxxx_UID_FLASH_SIZE;
		priv->uid_taddr = STM32L1xxxx_UID_BASE;
		break;
	}
	/* Read out the appropriate Flash size register value */
	uint32_t flash_size = target_mem32_read16(target, flash_size_taddr);
	/* Having read out the Flash size register, deal with two special cases before converting to an actual Flash size */
	if (target->part_id == ID_STM32L1xxxBxA)
		/* Only the lowest byte is valid on category 2 parts */
		flash_size &= 0xffU;
	else if (target->part_id == ID_STM32L1xxxD)
		/* Cat 3/4 parts have values of 0 or 1, convert to actual Flash sizes for these parts (384KiB or 256KiB) */
		flash_size = flash_size == 0U ? 384U : 256U;
	/* Finally, now all that's done.. convert the Flash size value to bytes */
	flash_size *= 1024U;

	/* Dispatch again on the category to complete Flash map setup */
	switch (target->part_id) {
	case ID_STM32L1xxxB:
	case ID_STM32L1xxxBxA:
	case ID_STM32L1xxxC:
	case ID_STM32L1xxxD: {
		/*
		 * Category 1, 2, and 3 only have one bank. This bank is split into up-to 64 4KiB sectors of 256 byte pages.
		 * Sectors are the write protection primitive, pages are the erase size primitive. The manual displays these
		 * as split with 1KiB of 256 byte pages, 3KiB of 1KiB pages, up to 124KiB of 4KiB pages, and then finally
		 * the rest of the Flash as 64KiB pages. However this is inaccurate.
		 * Category 4 has 2 banks but the first bank is laid out exactly the same as the first 3 categories.
		 * Category 4's second bank starts at the 192KiB mark and looks like it extends with a 128KiB page and a
		 * 64KiB page for another 192KiB for 384KiB of Flash. This bank, however, works the same as the first.
		 * This is documented in §3.2, tables 8, 9, and 10 on pg53 of RM0038, rev 17
		 */
		const bool category4 = flash_size == 0x00060000U;
		/*
		 * Determine bank 1's size. Category 4 parts have their 384KiB of Flash split evenly between the two
		 * banks, while the others all have their entire Flash on the first bank only.
		 */
		const uint32_t bank_size = category4 ? flash_size >> 1U : flash_size;
		stm32l_add_flash(target, STM32Lx_FLASH_BANK_BASE, bank_size, STM32L1_FLASH_PAGE_SIZE);
		/* Now deal with the second bank on Category 4 parts */
		if (category4)
			stm32l_add_flash(target, STM32Lx_FLASH_BANK_BASE + 0x00030000U, bank_size, STM32L1_FLASH_PAGE_SIZE);
		break;
	}
	case ID_STM32L1xxxE: {
		/*
		 * Category 5 has 2 banks, documented in §3.2, table 11 on pg56 of RM0038, rev 17.
		 * These banks are split up into sectors and pages the same as any other for the L1 series.
		 * The manual displays this as first bank being split into 1KiB of 256 byte pages, 3KiB of 1KiB pages,
		 * 124KiB of 4KiB pages, and a 128KiB page for 256KiB. It then shows the second bank is split into two
		 * 128KiB pages for a second 256KiB.  However this is inaccurate.
		 * This gives a total of 512KiB of Flash, which is the only way to tell these parts apart from category 6.
		 *
		 * Category 6 has 2 banks as well, documented in §3.2, table 12 on pg58 of RM0038, rev 17.
		 * The manual displays this as the first bank starting the same as a Category 5 device, right up until 128KiB
		 * in, after which it shows the bank being concluded by a single 64KiB page for 192KiB. Bank 2 is then shown as
		 * 1 128KiB page and one 64KiB page for another 192KiB, giving a total of 384KiB of Flash same as Category 4
		 * parts. While the total amount is accurate, this is an inaccurate representation. These too use
		 * the same sectors and pages arrangements as the other L1 parts, however the bank split location for
		 * the Category 5 and 6 parts is the same 256KiB mark, causing the Category 6 parts to have a small hole
		 * between the two banks, unlike Category 4 where the banks are contiguous.
		 */
		const uint32_t bank_size = flash_size >> 1U;
		stm32l_add_flash(target, STM32Lx_FLASH_BANK_BASE, bank_size, STM32L1_FLASH_PAGE_SIZE);
		stm32l_add_flash(target, STM32Lx_FLASH_BANK_BASE + 0x00040000U, bank_size, STM32L1_FLASH_PAGE_SIZE);
		break;
	}
	}

	return true;
}

static bool stm32l0_attach(target_s *const target)
{
	/*
	 * Try to attach to the part, and then ensure that the WDTs + WFI and WFE
	 * instructions can't cause problems (this is duplicated as it's undone by detach.)
	 */
	if (!cortexm_attach(target))
		return false;
	stm32l0_configure_dbgmcu(target);
	return true;
}

static void stm32l0_detach(target_s *target)
{
	/* Reverse all changes to STM32L0_DBGMCU_CONFIG */
	target_mem32_write32(target, STM32L0_DBGMCU_CONFIG, 0U);
	/* Now defer to the normal Cortex-M detach routine to complete the detach */
	cortexm_detach(target);
}

static bool stm32l1_attach(target_s *const target)
{
	/*
	 * Try to attach to the part, and then ensure that the WDTs + WFI and WFE
	 * instructions can't cause problems (this is duplicated as it's undone by detach.)
	 */
	return cortexm_attach(target) && stm32l1_configure_dbgmcu(target);
}

static void stm32l1_detach(target_s *target)
{
	const stm32l_priv_s *const priv = (stm32l_priv_s *)target->target_storage;
	/* Reverse all changes to STM32L1_DBGMCU_CONFIG */
	target_mem32_write32(target, STM32L1_DBGMCU_CONFIG, priv->dbgmcu_config);
	/* Now defer to the normal Cortex-M detach routine to complete the detach */
	cortexm_detach(target);
}

/* Lock the FLASH control registers preventing writes or erases. */
static void stm32lx_nvm_lock(target_s *const target, const target_addr32_t flash_base)
{
	target_mem32_write32(target, STM32Lx_FLASH_PECR(flash_base), STM32Lx_FLASH_PECR_PELOCK);
}

/*
 * Unlock the FLASH control registers for modifying program or data flash.
 * Returns true if the unlock succeeds.
 */
static bool stm32lx_nvm_prog_data_unlock(target_s *const target, const target_addr32_t flash_base)
{
	/* Always lock first because that's the only way to know that the unlock can succeed on the STM32L0's. */
	target_mem32_write32(target, STM32Lx_FLASH_PECR(flash_base), STM32Lx_FLASH_PECR_PELOCK);
	target_mem32_write32(target, STM32Lx_FLASH_PEKEYR(flash_base), STM32Lx_FLASH_PEKEY1);
	target_mem32_write32(target, STM32Lx_FLASH_PEKEYR(flash_base), STM32Lx_FLASH_PEKEY2);
	target_mem32_write32(target, STM32Lx_FLASH_PRGKEYR(flash_base), STM32Lx_FLASH_PRGKEY1);
	target_mem32_write32(target, STM32Lx_FLASH_PRGKEYR(flash_base), STM32Lx_FLASH_PRGKEY2);

	return !(target_mem32_read32(target, STM32Lx_FLASH_PECR(flash_base)) & STM32Lx_FLASH_PECR_PRGLOCK);
}

/*
 * Unlock the FLASH control registers for modifying option bytes.
 * Returns true if the unlock succeeds.
 */
static bool stm32lx_nvm_opt_unlock(target_s *const target, const target_addr32_t flash_base)
{
	/* Always lock first because that's the only way to know that the unlock can succeed on the STM32L0's. */
	target_mem32_write32(target, STM32Lx_FLASH_PECR(flash_base), STM32Lx_FLASH_PECR_PELOCK);
	target_mem32_write32(target, STM32Lx_FLASH_PEKEYR(flash_base), STM32Lx_FLASH_PEKEY1);
	target_mem32_write32(target, STM32Lx_FLASH_PEKEYR(flash_base), STM32Lx_FLASH_PEKEY2);
	target_mem32_write32(target, STM32Lx_FLASH_OPTKEYR(flash_base), STM32Lx_FLASH_OPTKEY1);
	target_mem32_write32(target, STM32Lx_FLASH_OPTKEYR(flash_base), STM32Lx_FLASH_OPTKEY2);

	return !(target_mem32_read32(target, STM32Lx_FLASH_PECR(flash_base)) & STM32Lx_FLASH_PECR_OPTLOCK);
}

static bool stm32lx_nvm_busy_wait(
	target_s *const target, const target_addr32_t flash_base, platform_timeout_s *const timeout)
{
	while (target_mem32_read32(target, STM32Lx_FLASH_SR(flash_base)) & STM32Lx_FLASH_SR_BSY) {
		if (target_check_error(target))
			return false;
		if (timeout)
			target_print_progress(timeout);
	}
	const uint32_t status = target_mem32_read32(target, STM32Lx_FLASH_SR(flash_base));
	return !target_check_error(target) && !(status & STM32Lx_FLASH_SR_ERR_MASK);
}

/*
 * Erase a region of program flash using operations through the debug interface.
 * This is slower than stubbed versions (see NOTES).
 * The flash array is erased for all pages from addr to addr + length inclusive.
 * The FLASH register base is automatically determined based on the target.
 */
static bool stm32lx_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t length)
{
	target_s *const target = flash->t;
	const target_addr32_t flash_base = stm32lx_flash_base(target);
	const bool full_erase = addr == flash->start && length == flash->length;
	if (!stm32lx_nvm_prog_data_unlock(target, flash_base))
		return false;

	/* Flash page erase instruction */
	target_mem32_write32(target, STM32Lx_FLASH_PECR(flash_base), STM32Lx_FLASH_PECR_ERASE | STM32Lx_FLASH_PECR_PROG);

	const uint32_t pecr = target_mem32_read32(target, STM32Lx_FLASH_PECR(flash_base)) &
		(STM32Lx_FLASH_PECR_PROG | STM32Lx_FLASH_PECR_ERASE);
	if (pecr != (STM32Lx_FLASH_PECR_PROG | STM32Lx_FLASH_PECR_ERASE))
		return false;

	/*
	 * Clear errors.
	 * Note that this only works when we wait for the FLASH block to complete the last operation.
	 */
	target_mem32_write32(target, STM32Lx_FLASH_SR(flash_base), STM32Lx_FLASH_SR_ERR_MASK);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	for (size_t offset = 0; offset < length; offset += flash->blocksize) {
		/* Trigger the erase by writing the first uint32_t of the page to 0 */
		target_mem32_write32(target, addr + offset, 0U);
		if (full_erase)
			target_print_progress(&timeout);
	}

	/* Disable further programming by locking PECR */
	stm32lx_nvm_lock(target, flash_base);
	/* Wait for completion or an error */
	return stm32lx_nvm_busy_wait(target, flash_base, full_erase ? &timeout : NULL);
}

/* Write to program flash using operations through the debug interface. */
static bool stm32lx_flash_write(
	target_flash_s *const flash, const target_addr_t dest, const void *const src, const size_t length)
{
	target_s *const target = flash->t;
	const target_addr32_t flash_base = stm32lx_flash_base(target);

	if (!stm32lx_nvm_prog_data_unlock(target, flash_base))
		return false;

	/* Wait for BSY to clear because we cannot write the PECR until the previous operation completes */
	if (!stm32lx_nvm_busy_wait(target, flash_base, NULL))
		return false;

	target_mem32_write32(target, STM32Lx_FLASH_PECR(flash_base), STM32Lx_FLASH_PECR_PROG | STM32Lx_FLASH_PECR_FPRG);
	target_mem32_write(target, dest, src, length);

	/* Disable further programming by locking PECR */
	stm32lx_nvm_lock(target, flash_base);

	/* Wait for completion or an error */
	return stm32lx_nvm_busy_wait(target, flash_base, NULL);
}

/*
 * Erase a region of data flash using operations through the debug interface.
 * The flash is erased for all pages from addr to addr + length, inclusive, on a word boundary.
 * The FLASH register base is automatically determined based on the target.
 */
static bool stm32lx_eeprom_erase(target_flash_s *const flash, const target_addr_t addr, const size_t length)
{
	target_s *const target = flash->t;
	const target_addr32_t flash_base = stm32lx_flash_base(target);
	if (!stm32lx_nvm_prog_data_unlock(target, flash_base))
		return false;

	/* Flash data erase instruction */
	target_mem32_write32(target, STM32Lx_FLASH_PECR(flash_base), STM32Lx_FLASH_PECR_ERASE | STM32Lx_FLASH_PECR_DATA);

	const uint32_t pecr = target_mem32_read32(target, STM32Lx_FLASH_PECR(flash_base)) &
		(STM32Lx_FLASH_PECR_ERASE | STM32Lx_FLASH_PECR_DATA);
	if (pecr != (STM32Lx_FLASH_PECR_ERASE | STM32Lx_FLASH_PECR_DATA))
		return false;

	const uint32_t aligned_addr = addr & ~3U;
	for (size_t offset = 0; offset < length; offset += flash->blocksize)
		/* Trigger the erase by writing the first uint32_t of the page to 0 */
		target_mem32_write32(target, aligned_addr + offset, 0U);

	/* Disable further programming by locking PECR */
	stm32lx_nvm_lock(target, flash_base);

	/* Wait for completion or an error */
	return stm32lx_nvm_busy_wait(target, flash_base, NULL);
}

/*
 * Write to data flash using operations through the debug interface.
 * The FLASH register base is automatically determined based on the target.
 * Unaligned destination writes are supported (though unaligned sources are not).
 */
static bool stm32lx_eeprom_write(
	target_flash_s *const flash, const target_addr_t dest, const void *const src, const size_t length)
{
	target_s *const target = flash->t;
	const target_addr32_t flash_base = stm32lx_flash_base(target);
	const bool is_stm32l1 = stm32lx_is_stm32l1(target);

	if (!stm32lx_nvm_prog_data_unlock(target, flash_base))
		return false;

	target_mem32_write32(target, STM32Lx_FLASH_PECR(flash_base), is_stm32l1 ? 0 : STM32Lx_FLASH_PECR_DATA);

	/* Sling data to the target one uint32_t at a time */
	const uint32_t *const data = (const uint32_t *)src;
	for (size_t offset = 0; offset < length; offset += 4U) {
		/* XXX: Why is this not able to use target_mem_write()? */
		if (target_mem32_write32(target, dest + offset, data[offset]))
			return false;
	}

	/* Disable further programming by locking PECR */
	stm32lx_nvm_lock(target, flash_base);
	/* Wait for completion or an error */
	return stm32lx_nvm_busy_wait(target, flash_base, NULL);
}

static bool stm32lx_mass_erase(target_s *const target)
{
	for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
		const bool result = stm32lx_flash_erase(flash, flash->start, flash->length);
		if (!result)
			return false;
	}
	return true;
}

/*
 * Write one option word.
 * The address is the physical address of the word and the value is a complete word value.
 * The caller is responsible for making sure that the value satisfies the proper
 * format where the upper 16 bits are the 1s complement of the lower 16 bits.
 * The function returns when the operation is complete.
 * The return value is true if the write succeeded.
 */
static bool stm32lx_option_write(target_s *const target, const uint32_t address, const uint32_t value)
{
	const target_addr32_t flash_base = stm32lx_flash_base(target);

	/* Erase and program option in one go. */
	target_mem32_write32(target, STM32Lx_FLASH_PECR(flash_base), STM32Lx_FLASH_PECR_FIX);
	target_mem32_write32(target, address, value);

	/* Wait for completion or an error */
	return stm32lx_nvm_busy_wait(target, flash_base, NULL);
}

/*
 * Write one eeprom value.
 * This version is more flexible than that bulk version used for writing data from the executable file.
 * The address is the physical address of the word and the value is a complete word value.
 * The function returns when the operation is complete.
 * The return value is true if the write succeeded.
 * FWIW, byte writing isn't supported because the ADIv5 layer doesn't support byte-level operations.
 */
static bool stm32lx_eeprom_write_one(
	target_s *const target, const uint32_t address, const size_t block_size, const uint32_t value)
{
	const target_addr32_t flash_base = stm32lx_flash_base(target);
	const bool is_stm32l1 = stm32lx_is_stm32l1(target);

	/* Clear errors. */
	target_mem32_write32(target, STM32Lx_FLASH_SR(flash_base), STM32Lx_FLASH_SR_ERR_MASK);

	/* Erase and program option in one go. */
	target_mem32_write32(
		target, STM32Lx_FLASH_PECR(flash_base), (is_stm32l1 ? 0 : STM32Lx_FLASH_PECR_DATA) | STM32Lx_FLASH_PECR_FIX);
	if (block_size == 4)
		target_mem32_write32(target, address, value);
	else if (block_size == 2)
		target_mem32_write16(target, address, value);
	else if (block_size == 1)
		target_mem32_write8(target, address, value);
	else
		return false;

	/* Wait for completion or an error */
	return stm32lx_nvm_busy_wait(target, flash_base, NULL);
}

static size_t stm32lx_prot_level(const uint32_t options)
{
	const uint32_t read_protection = (options >> STM32Lx_FLASH_OPTR_RDPROT_SHIFT) & STM32Lx_FLASH_OPTR_RDPROT_MASK;
	if (read_protection == STM32Lx_FLASH_OPTR_RDPROT_0)
		return 0;
	if (read_protection == STM32Lx_FLASH_OPTR_RDPROT_2)
		return 2;
	return 1;
}

static bool stm32lx_cmd_option(target_s *const target, const int argc, const char **const argv)
{
	const target_addr32_t flash_base = stm32lx_flash_base(target);
	const size_t opt_size = stm32lx_nvm_option_size(target);

	if (!stm32lx_nvm_opt_unlock(target, flash_base)) {
		tc_printf(target, "unable to unlock FLASH option bytes\n");
		return true;
	}

	if (argc < 2)
		goto usage;
	const size_t command_len = strlen(argv[1]);

	if (argc == 2 && strncasecmp(argv[1], "obl_launch", command_len) == 0)
		target_mem32_write32(target, STM32Lx_FLASH_PECR(flash_base), STM32Lx_FLASH_PECR_OBL_LAUNCH);
	else if (argc == 4) {
		const bool raw_write = strncasecmp(argv[1], "raw", command_len) == 0;
		if (!raw_write && strncasecmp(argv[1], "write", command_len) != 0)
			goto usage;

		const uint32_t addr = strtoul(argv[2], NULL, 0);
		uint32_t val = strtoul(argv[3], NULL, 0);
		if (!raw_write)
			val = (val & 0xffffU) | ((~val & 0xffffU) << 16U);
		tc_printf(target, "%s %08" PRIx32 " <- %08" PRIx32 "\n", argv[1], addr, val);

		if (addr >= STM32Lx_FLASH_OPT_BASE && addr < STM32Lx_FLASH_OPT_BASE + opt_size && (addr & 3U) == 0) {
			if (!stm32lx_option_write(target, addr, val))
				tc_printf(target, "option write failed\n");
		} else
			goto usage;
	}

	/* Report the current option values */
	for (size_t i = 0; i < opt_size; i += 4U) {
		const uint32_t addr = STM32Lx_FLASH_OPT_BASE + i;
		const uint32_t val = target_mem32_read32(target, addr);
		tc_printf(target, "0x%08" PRIx32 ": 0x%04" PRIu32 " 0x%04" PRIu32 " %s\n", addr, val & 0xffffU,
			(val >> 16U) & 0xffffU, (val & 0xffffU) == ((~val >> 16U) & 0xffffU) ? "OK" : "ERR");
	}

	const uint32_t options = target_mem32_read32(target, STM32Lx_FLASH_OPTR(flash_base));
	const size_t read_protection = stm32lx_prot_level(options);
	if (stm32lx_is_stm32l1(target)) {
		tc_printf(target,
			"OPTR: 0x%08" PRIx32 ", RDPRT %" PRIu32 ", SPRMD %u, BOR %" PRIu32 " , WDG_SW %u"
			", nRST_STP %u, nRST_STBY %u, nBFB2 %u\n",
			options, (uint32_t)read_protection, (options & STM32L1_FLASH_OPTR_SPRMOD) ? 1 : 0,
			(options >> STM32L1_FLASH_OPTR_BOR_LEV_SHIFT) & STM32L1_FLASH_OPTR_BOR_LEV_MASK,
			(options & STM32Lx_FLASH_OPTR_WDG_SW) ? 1 : 0, (options & STM32L1_FLASH_OPTR_nRST_STOP) ? 1 : 0,
			(options & STM32L1_FLASH_OPTR_nRST_STDBY) ? 1 : 0, (options & STM32L1_FLASH_OPTR_nBFB2) ? 1 : 0);
	} else {
		tc_printf(target, "OPTR: 0x%08" PRIx32 ", RDPROT %" PRIu32 ", WPRMOD %" PRIu16 ", WDG_SW %u, BOOT1 %u\n",
			options, (uint32_t)read_protection, (options & STM32L0_FLASH_OPTR_WPRMOD) ? 1 : 0,
			(options & STM32Lx_FLASH_OPTR_WDG_SW) ? 1 : 0, (options & STM32L0_FLASH_OPTR_BOOT1) ? 1 : 0);
	}

	goto done;

usage:
	tc_printf(target, "usage: monitor option [ARGS]\n");
	tc_printf(target, "  show                   - Show options in FLASH and as loaded\n");
	tc_printf(target, "  obl_launch             - Reload options from FLASH\n");
	tc_printf(target, "  write <addr> <value16> - Set option half-word; complement computed\n");
	tc_printf(target, "  raw <addr> <value32>   - Set option word\n");
	tc_printf(target, "The value of <addr> must be 32-bit aligned and from 0x%08" PRIx32 " to +0x%" PRIx32 "\n",
		STM32Lx_FLASH_OPT_BASE, STM32Lx_FLASH_OPT_BASE + (uint32_t)(opt_size - 4U));

done:
	stm32lx_nvm_lock(target, flash_base);
	return true;
}

static const char *stm32lx_block_size_str(const size_t block_size)
{
	if (block_size == 4U)
		return "word";
	if (block_size == 2U)
		return "halfword";
	if (block_size == 1U)
		return "byte";
	return "";
}

static bool stm32lx_cmd_eeprom(target_s *const target, const int argc, const char **const argv)
{
	const target_addr32_t flash_base = stm32lx_flash_base(target);

	if (!stm32lx_nvm_prog_data_unlock(target, flash_base)) {
		tc_printf(target, "unable to unlock EEPROM\n");
		return true;
	}

	if (argc == 4) {
		uint32_t addr = strtoul(argv[2], NULL, 0);
		uint32_t val = strtoul(argv[3], NULL, 0);

		if (addr < STM32Lx_FLASH_EEPROM_BASE || addr >= STM32Lx_FLASH_EEPROM_BASE + stm32lx_nvm_eeprom_size(target))
			goto usage;

		const size_t command_len = strlen(argv[1]);
		size_t block_size = 0U;
		if (!strncasecmp(argv[1], "byte", command_len)) {
			val &= 0xffU;
			block_size = 1U;
		} else if (!strncasecmp(argv[1], "halfword", command_len)) {
			val &= 0xffffU;
			block_size = 2U;
			if (addr & 1U) {
				tc_printf(target, "Refusing to do unaligned write\n");
				goto usage;
			}
		} else if (!strncasecmp(argv[1], "word", command_len)) {
			block_size = 4U;
			if (addr & 3U) {
				tc_printf(target, "Refusing to do unaligned write\n");
				goto usage;
			}
		} else
			goto usage;

		tc_printf(
			target, "writing %s 0x%08" PRIx32 " with 0x%" PRIx32 "\n", stm32lx_block_size_str(block_size), addr, val);
		if (!stm32lx_eeprom_write_one(target, addr, block_size, val))
			tc_printf(target, "eeprom write failed\n");
	} else
		goto usage;

	goto done;

usage:
	tc_printf(target, "usage: monitor eeprom [ARGS]\n");
	tc_printf(target, "  byte     <addr> <value8>  - Write a byte\n");
	tc_printf(target, "  halfword <addr> <value16> - Write a half-word\n");
	tc_printf(target, "  word     <addr> <value32> - Write a word\n");
	tc_printf(target, "The value of <addr> must in the interval [0x%08" PRIx32 ", 0x%" PRIx32 ")\n",
		STM32Lx_FLASH_EEPROM_BASE, STM32Lx_FLASH_EEPROM_BASE + stm32lx_nvm_eeprom_size(target));

done:
	stm32lx_nvm_lock(target, flash_base);
	return true;
}
