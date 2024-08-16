/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This file implements support for STM32H5xx series devices, providing
 * memory maps and Flash programming routines.
 *
 * References:
 * RM0481 - STM32H563, H573 and H562 Arm®-based 32-bit MCUs, Rev. 1
 *   https://www.st.com/resource/en/reference_manual/rm0481-stm32h563h573-and-stm32h562-armbased-32bit-mcus-stmicroelectronics.pdf
 * RM0492 - STM32H503 Arm®-based 32-bit MCUs, Rev. 2
 *   https://www.st.com/resource/en/reference_manual/rm0492-stm32h503-line-armbased-32bit-mcus-stmicroelectronics.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "stm32_common.h"

/* Memory map constants for STM32H5xx */
#define STM32H5_FLASH_BANK1_BASE 0x08000000U
#define STM32H5_FLASH_BANK2_BASE 0x08100000U
#define STM32H5_FLASH_BANK_SIZE  0x00100000U
#define STM32H5_SRAM1_BASE       0x0a000000U
#define STM32H5_SRAM1_SIZE       0x00040000U
#define STM32H5_SRAM2_BASE       0x0a040000U
#define STM32H5_SRAM2_SIZE       0x00010000U
#define STM32H5_SRAM3_BASE       0x0a050000U
#define STM32H5_SRAM3_SIZE       0x00050000U
/* NB: Take all base addresses and add 0x04000000U to find their TrustZone addresses */

/* Memory map constants for the STM32H503 */
#define STM32H503_FLASH_BANK1_BASE 0x08000000U
#define STM32H503_FLASH_BANK2_BASE 0x08010000U
#define STM32H503_FLASH_BANK_SIZE  0x00010000U
#define STM32H503_SRAM1_BASE       0x0a000000U
#define STM32H503_SRAM1_SIZE       0x00004000U
#define STM32H503_SRAM2_BASE       0x0a004000U
#define STM32H503_SRAM2_SIZE       0x00004000U
#define STM32H503_SRAM1_ALIAS      0x20000000U
#define STM32H503_SRAM2_ALIAS      0x20004000U

#define STM32H5_FLASH_BASE        0x40022000
#define STM32H5_FLASH_ACCESS_CTRL (STM32H5_FLASH_BASE + 0x000U)
#define STM32H5_FLASH_KEY         (STM32H5_FLASH_BASE + 0x004U)
#define STM32H5_FLASH_OPTION_KEY  (STM32H5_FLASH_BASE + 0x00cU)
#define STM32H5_FLASH_STATUS      (STM32H5_FLASH_BASE + 0x020U)
#define STM32H5_FLASH_CTRL        (STM32H5_FLASH_BASE + 0x028U)
#define STM32H5_FLASH_CLEAR_CTRL  (STM32H5_FLASH_BASE + 0x030U)

#define STM32H5_FLASH_KEY1              0x45670123U
#define STM32H5_FLASH_KEY2              0xcdef89abU
#define STM32H5_FLASH_STATUS_BUSY       (1U << 0U)
#define STM32H5_FLASH_STATUS_EOP        (1U << 16U)
#define STM32H5_FLASH_STATUS_ERROR_MASK 0x00fc0000U
#define STM32H5_FLASH_CTRL_LOCK         (1U << 0U)
#define STM32H5_FLASH_CTRL_PROGRAM      (1U << 1U)
#define STM32H5_FLASH_CTRL_SECTOR_ERASE (1U << 2U)
#define STM32H5_FLASH_CTRL_BANK_ERASE   (1U << 3U)
#define STM32H5_FLASH_CTRL_START        (1U << 5U)
#define STM32H5_FLASH_CTRL_SECTOR(x)    (((x)&0x7fU) << 6U)
#define STM32H5_FLASH_CTRL_MASS_ERASE   (1U << 15U)
#define STM32H5_FLASH_CTRL_BANK1        (0U << 31U)
#define STM32H5_FLASH_CTRL_BANK2        (1U << 31U)

#define STM32H5_SECTORS_PER_BANK        128U
#define STM32H5_FLASH_SECTOR_SIZE       0x00002000U
#define STM32H503_SECTORS_PER_BANK      8U
#define STM32H5_FLASH_BANK_MASK         0x80000000U
#define STM32H5_FLASH_SECTOR_COUNT_MASK 0x000000ffU

/*
 * Both on H56x and H503 DBGMCU is visible via AP0 on Debug APB at 0xe0044000,
 * and via AP1 by the processor at 0x44024000 alias.
 */
#define STM32H5_DBGMCU_BASE        0x44024000
#define STM32H5_DBGMCU_IDCODE      (STM32H5_DBGMCU_BASE + 0x00U)
#define STM32H5_DBGMCU_CONFIG      (STM32H5_DBGMCU_BASE + 0x04U)
#define STM32H5_DBGMCU_APB1LFREEZE (STM32H5_DBGMCU_BASE + 0x08U)
#define STM32H5_DBGMCU_APB1HFREEZE (STM32H5_DBGMCU_BASE + 0x0cU)
#define STM32H5_DBGMCU_APB2FREEZE  (STM32H5_DBGMCU_BASE + 0x10U)
#define STM32H5_DBGMCU_APB3FREEZE  (STM32H5_DBGMCU_BASE + 0x14U)
#define STM32H5_DBGMCU_AHB1FREEZE  (STM32H5_DBGMCU_BASE + 0x20U)
#define STM32H5_UID_BASE           0x08fff800U

#define STM32H5_DBGMCU_IDCODE_DEV_MASK    0x00000fffU
#define STM32H5_DBGMCU_IDCODE_REV_MASK    0xffff0000U
#define STM32H5_DBGMCU_IDCODE_REV_SHIFT   16U
#define STM32H5_DBGMCU_CONFIG_DBG_STOP    (1U << 1U)
#define STM32H5_DBGMCU_CONFIG_DBG_STANDBY (1U << 2U)
#define STM32H5_DBGMCU_APB1LFREEZE_WWDG   (1U << 11U)
#define STM32H5_DBGMCU_APB1LFREEZE_IWDG   (1U << 12U)

/* Taken from DBGMCU_IDCODE in §18.12.4 of RM0481 rev 1, pg3085 */
#define ID_STM32H5xx 0x484U
/* Taken from DBGMCU_IDCODE in §41.124 of RM0492 rev 2, pg1807 */
#define ID_STM32H503 0x474U

typedef struct stm32h5_flash {
	target_flash_s target_flash;
	uint32_t bank_and_sector_count;
} stm32h5_flash_s;

typedef struct stm32h5_priv {
	uint32_t dbgmcu_config;
} stm32h5_priv_s;

static bool stm32h5_cmd_uid(target_s *target, int argc, const char **argv);
static bool stm32h5_cmd_rev(target_s *target, int argc, const char **argv);

const command_s stm32h5_cmd_list[] = {
	{"uid", stm32h5_cmd_uid, "Print unique device ID"},
	{"revision", stm32h5_cmd_rev, "Returns the Device ID and Revision"},
	{NULL, NULL, NULL},
};

static bool stm32h5_attach(target_s *target);
static void stm32h5_detach(target_s *target);
static bool stm32h5_enter_flash_mode(target_s *target);
static bool stm32h5_exit_flash_mode(target_s *target);
static bool stm32h5_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool stm32h5_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool stm32h5_mass_erase(target_s *target);

static void stm32h5_add_flash(
	target_s *const target, const uint32_t base_addr, const size_t length, const uint32_t bank_and_sector_count)
{
	stm32h5_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *target_flash = &flash->target_flash;
	target_flash->start = base_addr;
	target_flash->length = length;
	target_flash->blocksize = STM32H5_FLASH_SECTOR_SIZE;
	target_flash->erase = stm32h5_flash_erase;
	target_flash->write = stm32h5_flash_write;
	target_flash->erased = 0xffU;
	target_add_flash(target, target_flash);
	flash->bank_and_sector_count = bank_and_sector_count;
}

static bool stm32h5_configure_dbgmcu(target_s *const target)
{
	/* If we're in the probe phase */
	if (target->target_storage == NULL) {
		/* Allocate target-specific storage */
		stm32h5_priv_s *const priv_storage = calloc(1, sizeof(*priv_storage));
		if (!priv_storage) { /* calloc failed: heap exhaustion */
			DEBUG_ERROR("calloc: failed in %s\n", __func__);
			return false;
		}
		target->target_storage = priv_storage;
		/* Get the current value of the debug config register (and store it for later) */
		priv_storage->dbgmcu_config = target_mem32_read32(target, STM32H5_DBGMCU_CONFIG);

		target->attach = stm32h5_attach;
		target->detach = stm32h5_detach;
	}

	const stm32h5_priv_s *const priv = (stm32h5_priv_s *)target->target_storage;
	/* Now we have a stable debug environment, make sure the WDTs can't bonk the processor out from under us */
	target_mem32_write32(
		target, STM32H5_DBGMCU_APB1LFREEZE, STM32H5_DBGMCU_APB1LFREEZE_IWDG | STM32H5_DBGMCU_APB1LFREEZE_WWDG);
	/* Then Reconfigure the config register to prevent WFI/WFE from cutting debug access */
	target_mem32_write32(target, STM32H5_DBGMCU_CONFIG,
		priv->dbgmcu_config | STM32H5_DBGMCU_CONFIG_DBG_STANDBY | STM32H5_DBGMCU_CONFIG_DBG_STOP);
	return true;
}

bool stm32h5_probe(target_s *const target)
{
	const adiv5_access_port_s *const ap = cortex_ap(target);
	/* Use the partno from the AP always to handle the difference between JTAG and SWD */
	if (ap->partno != ID_STM32H5xx && ap->partno != ID_STM32H503)
		return false;
	target->part_id = ap->partno;

	/* Now we have a stable debug environment, make sure the WDTs + WFI and WFE instructions can't cause problems */
	if (!stm32h5_configure_dbgmcu(target))
		return false;

	target->driver = "STM32H5";
	target->mass_erase = stm32h5_mass_erase;
	target->enter_flash_mode = stm32h5_enter_flash_mode;
	target->exit_flash_mode = stm32h5_exit_flash_mode;
	target_add_commands(target, stm32h5_cmd_list, target->driver);

	switch (target->part_id) {
	case ID_STM32H5xx:
		/*
		 * Build the RAM map.
		 * This uses the addresses and sizes found in §2.3.2, Figure 2, pg113 of RM0481 Rev. 1
		 */
		target_add_ram32(target, STM32H5_SRAM1_BASE, STM32H5_SRAM1_SIZE);
		target_add_ram32(target, STM32H5_SRAM2_BASE, STM32H5_SRAM2_SIZE);
		target_add_ram32(target, STM32H5_SRAM3_BASE, STM32H5_SRAM3_SIZE);

		/* Build the Flash map */
		stm32h5_add_flash(target, STM32H5_FLASH_BANK1_BASE, STM32H5_FLASH_BANK_SIZE,
			STM32H5_SECTORS_PER_BANK | STM32H5_FLASH_CTRL_BANK1);
		stm32h5_add_flash(target, STM32H5_FLASH_BANK2_BASE, STM32H5_FLASH_BANK_SIZE,
			STM32H5_SECTORS_PER_BANK | STM32H5_FLASH_CTRL_BANK2);
		break;
	case ID_STM32H503:
		/*
		 * Build the RAM map.
		 * This uses the addresses and sizes found in §2.2.2, Figure 2, pg70 of RM0492 Rev. 2
		 */
		target_add_ram32(target, STM32H503_SRAM1_BASE, STM32H503_SRAM1_SIZE);
		target_add_ram32(target, STM32H503_SRAM2_BASE, STM32H503_SRAM2_SIZE);
		target_add_ram32(target, STM32H503_SRAM1_ALIAS, STM32H503_SRAM1_SIZE);
		target_add_ram32(target, STM32H503_SRAM2_ALIAS, STM32H503_SRAM2_SIZE);

		/* Build the Flash map */
		stm32h5_add_flash(target, STM32H503_FLASH_BANK1_BASE, STM32H503_FLASH_BANK_SIZE,
			STM32H503_SECTORS_PER_BANK | STM32H5_FLASH_CTRL_BANK1);
		stm32h5_add_flash(target, STM32H503_FLASH_BANK2_BASE, STM32H503_FLASH_BANK_SIZE,
			STM32H503_SECTORS_PER_BANK | STM32H5_FLASH_CTRL_BANK2);
		break;
	}

	return true;
}

static bool stm32h5_attach(target_s *const target)
{
	/*
	 * Try to attach to the part, and then ensure that the WDTs + WFI and WFE
	 * instructions can't cause problems (this is duplicated as it's undone by detach.)
	 */
	return cortexm_attach(target) && stm32h5_configure_dbgmcu(target);
}

static void stm32h5_detach(target_s *target)
{
	const stm32h5_priv_s *const priv = (stm32h5_priv_s *)target->target_storage;
	/* Reverse all changes to STM32F4_DBGMCU_CTRL */
	target_mem32_write32(target, STM32H5_DBGMCU_CONFIG, priv->dbgmcu_config);
	/* Now defer to the normal Cortex-M detach routine to complete the detach */
	cortexm_detach(target);
}

static bool stm32h5_flash_wait_complete(target_s *const target, platform_timeout_s *const timeout)
{
	uint32_t status = STM32H5_FLASH_STATUS_BUSY;
	/* Read the status register and poll for busy and !EOP */
	while (!(status & STM32H5_FLASH_STATUS_EOP) && (status & STM32H5_FLASH_STATUS_BUSY)) {
		status = target_mem32_read32(target, STM32H5_FLASH_STATUS);
		if (target_check_error(target)) {
			DEBUG_ERROR("%s: error reading status\n", __func__);
			return false;
		}
		if (timeout)
			target_print_progress(timeout);
	}
	if (status & STM32H5_FLASH_STATUS_ERROR_MASK)
		DEBUG_ERROR("%s: Flash error: %08" PRIx32 "\n", __func__, status);
	/* Clear all error and status bits */
	target_mem32_write32(
		target, STM32H5_FLASH_CLEAR_CTRL, status & (STM32H5_FLASH_STATUS_ERROR_MASK | STM32H5_FLASH_STATUS_EOP));
	return !(status & STM32H5_FLASH_STATUS_ERROR_MASK);
}

static bool stm32h5_enter_flash_mode(target_s *const target)
{
	target_reset(target);
	/* Wait to ensure any pending operations are complete */
	if (!stm32h5_flash_wait_complete(target, NULL))
		return false;
	/* Now, if the Flash controller's not already unlocked, unlock it */
	if (target_mem32_read32(target, STM32H5_FLASH_CTRL) & STM32H5_FLASH_CTRL_LOCK) {
		target_mem32_write32(target, STM32H5_FLASH_KEY, STM32H5_FLASH_KEY1);
		target_mem32_write32(target, STM32H5_FLASH_KEY, STM32H5_FLASH_KEY2);
	}
	/* Success of entering Flash mode is predicated on successfully unlocking the controller */
	return !(target_mem32_read32(target, STM32H5_FLASH_CTRL) & STM32H5_FLASH_CTRL_LOCK);
}

static bool stm32h5_exit_flash_mode(target_s *const target)
{
	/* On leaving Flash mode, lock the controller again */
	target_mem32_write32(target, STM32H5_FLASH_CTRL, STM32H5_FLASH_CTRL_LOCK);
	target_reset(target);
	return true;
}

static bool stm32h5_flash_erase(target_flash_s *const target_flash, const target_addr_t addr, const size_t len)
{
	target_s *const target = target_flash->t;
	const stm32h5_flash_s *const flash = (stm32h5_flash_s *)target_flash;
	/* Compute how many sectors should be erased (inclusive) and from which bank */
	const uint32_t begin = target_flash->start - addr;
	const uint32_t bank = flash->bank_and_sector_count & STM32H5_FLASH_BANK_MASK;
	const size_t end_sector = (begin + len - 1U) / STM32H5_FLASH_SECTOR_SIZE;

	/* For each sector in the requested address range */
	for (size_t begin_sector = begin / STM32H5_FLASH_SECTOR_SIZE; begin_sector <= end_sector; ++begin_sector) {
		/* Erase the current Flash sector */
		const uint32_t ctrl = bank | STM32H5_FLASH_CTRL_SECTOR_ERASE | STM32H5_FLASH_CTRL_SECTOR(begin_sector);
		target_mem32_write32(target, STM32H5_FLASH_CTRL, ctrl);
		target_mem32_write32(target, STM32H5_FLASH_CTRL, ctrl | STM32H5_FLASH_CTRL_START);

		/* Wait for the operation to complete, reporting errors */
		if (!stm32h5_flash_wait_complete(target, NULL))
			return false;
	}
	return true;
}

static bool stm32h5_flash_write(
	target_flash_s *const flash, const target_addr_t dest, const void *const src, const size_t len)
{
	target_s *const target = flash->t;
	/* Enable programming operations */
	target_mem32_write32(target, STM32H5_FLASH_CTRL, STM32H5_FLASH_CTRL_PROGRAM);
	/* Write the data to the Flash */
	target_mem32_write(target, dest, src, len);
	/* Wait for the operation to complete and report errors */
	if (!stm32h5_flash_wait_complete(target, NULL))
		return false;
	/* Disable programming operations */
	target_mem32_write32(target, STM32H5_FLASH_CTRL, 0U);
	return true;
}

static bool stm32h5_mass_erase(target_s *const target)
{
	/* To start mass erase, enter into Flash mode */
	if (!stm32h5_enter_flash_mode(target))
		return false;

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	/* Trigger the mass erase */
	target_mem32_write32(target, STM32H5_FLASH_CTRL, STM32H5_FLASH_CTRL_MASS_ERASE);
	target_mem32_write32(target, STM32H5_FLASH_CTRL, STM32H5_FLASH_CTRL_MASS_ERASE | STM32H5_FLASH_CTRL_START);
	/* And wait for it to complete, reporting errors along the way */
	const bool result = stm32h5_flash_wait_complete(target, &timeout);

	/* When done, leave Flash mode */
	return stm32h5_exit_flash_mode(target) && result;
}

static bool stm32h5_cmd_uid(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	return stm32_uid(target, STM32H5_UID_BASE);
}

static const struct {
	uint16_t rev_id;
	char revision;
} stm32h5_revisions[] = {
	{0x1000U, 'A'},
	{0x1001U, 'Z'},
	{0x1002U, 'Y'},
	{0x1007U, 'X'},
};

static bool stm32h5_cmd_rev(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* Read the device identity register */
	const uint32_t idcode = target_mem32_read32(target, STM32H5_DBGMCU_IDCODE);
	const uint16_t rev_id = (idcode & STM32H5_DBGMCU_IDCODE_REV_MASK) >> STM32H5_DBGMCU_IDCODE_REV_SHIFT;
	const uint16_t dev_id = idcode & STM32H5_DBGMCU_IDCODE_DEV_MASK;

	/* Display the device ID */
	switch (dev_id) {
	case ID_STM32H5xx:
		tc_printf(target, "STM32H56x/57x\n");
		break;
	case ID_STM32H503:
		tc_printf(target, "STM32H503\n");
		break;
	default:
		tc_printf(target, "Unknown %s. BMP may not correctly support it!\n", target->driver);
	}
	char revision = '?';
	for (size_t i = 0; i < ARRAY_LENGTH(stm32h5_revisions); ++i) {
		if (stm32h5_revisions[i].rev_id == rev_id)
			revision = stm32h5_revisions[i].revision;
	}
	tc_printf(target, "Revision %c\n", revision);
	return true;
}
