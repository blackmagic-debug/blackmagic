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
 * RM0481 - STM32H563, H573 and H562 ARM®-based 32-bit MCUs, Rev. 1
 *   https://www.st.com/resource/en/reference_manual/rm0481-stm32h563h573-and-stm32h562-armbased-32bit-mcus-stmicroelectronics.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"

/* Memory map constants */
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

#define STM32H5_SECTORS_PER_BANK  128U
#define STM32H5_FLASH_SECTOR_SIZE 0x2000U

/* Taken from DP_TARGETIDR in §58.3.3 of RM0481 rev 1, pg2958 */
#define ID_STM32H5xx 0x4840U

static bool stm32h5_enter_flash_mode(target_s *target);
static bool stm32h5_exit_flash_mode(target_s *target);
static bool stm32h5_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool stm32h5_mass_erase(target_s *target);

static void stm32h5_add_flash(
	target_s *const target, const uint32_t base_addr, const size_t length, const size_t block_size)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = base_addr;
	flash->length = length;
	flash->blocksize = block_size;
	flash->erase = stm32h5_flash_erase;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

bool stm32h5_probe(target_s *const target)
{
	if (target->part_id != ID_STM32H5xx)
		return false;

	target->driver = "STM32H5";
	target->mass_erase = stm32h5_mass_erase;
	target->enter_flash_mode = stm32h5_enter_flash_mode;
	target->exit_flash_mode = stm32h5_exit_flash_mode;

	/*
	 * Build the RAM map.
	 * This uses the addresses and sizes found in §2.3.2, Figure 2, pg113 of RM0481 Rev. 1
	 */
	target_add_ram(target, STM32H5_SRAM1_BASE, STM32H5_SRAM1_SIZE);
	target_add_ram(target, STM32H5_SRAM2_BASE, STM32H5_SRAM2_SIZE);
	target_add_ram(target, STM32H5_SRAM3_BASE, STM32H5_SRAM3_SIZE);

	/* Build the Flash map */
	stm32h5_add_flash(target, STM32H5_FLASH_BANK1_BASE, STM32H5_FLASH_BANK_SIZE, STM32H5_FLASH_SECTOR_SIZE);
	stm32h5_add_flash(target, STM32H5_FLASH_BANK2_BASE, STM32H5_FLASH_BANK_SIZE, STM32H5_FLASH_SECTOR_SIZE);

	return true;
}

static bool stm32h5_flash_wait_complete(target_s *const target, platform_timeout_s *const timeout)
{
	uint32_t status = STM32H5_FLASH_STATUS_BUSY;
	/* Read the status register and poll for busy and !EOP */
	while (!(status & STM32H5_FLASH_STATUS_EOP) && (status & STM32H5_FLASH_STATUS_BUSY)) {
		status = target_mem_read32(target, STM32H5_FLASH_STATUS);
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
	target_mem_write32(
		target, STM32H5_FLASH_CLEAR_CTRL, (status & (STM32H5_FLASH_STATUS_ERROR_MASK | STM32H5_FLASH_STATUS_EOP)));
	return !(status & STM32H5_FLASH_STATUS_ERROR_MASK);
}

static bool stm32h5_enter_flash_mode(target_s *const target)
{
	target_reset(target);
	/* Wait to ensure any pending operations are complete */
	if (!stm32h5_flash_wait_complete(target, NULL))
		return false;
	/* Now, if the Flash controller's not already unlocked, unlock it */
	if (target_mem_read32(target, STM32H5_FLASH_CTRL) & STM32H5_FLASH_CTRL_LOCK) {
		target_mem_write32(target, STM32H5_FLASH_KEY, STM32H5_FLASH_KEY1);
		target_mem_write32(target, STM32H5_FLASH_KEY, STM32H5_FLASH_KEY2);
	}
	/* Success of entering Flash mode is predicated on successfully unlocking the controller */
	return !(target_mem_read32(target, STM32H5_FLASH_CTRL) & STM32H5_FLASH_CTRL_LOCK);
}

static bool stm32h5_exit_flash_mode(target_s *const target)
{
	/* On leaving Flash mode, lock the controller again */
	target_mem_write32(target, STM32H5_FLASH_CTRL, STM32H5_FLASH_CTRL_LOCK);
	target_reset(target);
	return true;
}

static bool stm32h5_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t len)
{
	target_s *const target = flash->t;
	/* Compute how many sectors should be erased (inclusive) and from which bank */
	const uint32_t begin = flash->start - addr;
	const uint32_t bank = addr < STM32H5_FLASH_BANK2_BASE ? STM32H5_FLASH_CTRL_BANK1 : STM32H5_FLASH_CTRL_BANK2;
	const size_t end_sector = (begin + len - 1U) / STM32H5_FLASH_SECTOR_SIZE;

	/* For each sector in the requested address range */
	for (size_t begin_sector = begin / STM32H5_FLASH_SECTOR_SIZE; begin_sector <= end_sector; ++begin_sector) {
		/* Erase the current Flash sector */
		const uint32_t ctrl = bank | STM32H5_FLASH_CTRL_SECTOR_ERASE | STM32H5_FLASH_CTRL_SECTOR(begin_sector);
		target_mem_write32(target, STM32H5_FLASH_CTRL, ctrl);
		target_mem_write32(target, STM32H5_FLASH_CTRL, ctrl | STM32H5_FLASH_CTRL_START);

		/* Wait for the operation to complete, reporting errors */
		if (!stm32h5_flash_wait_complete(target, NULL))
			return false;
	}
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
	target_mem_write32(target, STM32H5_FLASH_CTRL, STM32H5_FLASH_CTRL_MASS_ERASE);
	target_mem_write32(target, STM32H5_FLASH_CTRL, STM32H5_FLASH_CTRL_MASS_ERASE | STM32H5_FLASH_CTRL_START);
	/* And wait for it to complete, reporting errors along the way */
	const bool result = stm32h5_flash_wait_complete(target, &timeout);

	/* When done, leave Flash mode */
	return stm32h5_exit_flash_mode(target) && result;
}
