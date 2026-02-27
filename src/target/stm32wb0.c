/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
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
 * This file implements support for STM32WB0x and STM32WL3x series devices,
 * providing memory maps and Flash programming routines.
 *
 * Both families share the same flash controller register layout and command
 * set; the main differences are the flash bank base address, the debug
 * infrastructure registers, and the write granularity.
 *
 * References:
 * RM0530 - STM32WB07xC and STM32WB06xC ultra-low power wireless 32-bit MCUs Arm®-based Cortex®-M0+ with Bluetooth® Low Energy and 2.4 GHz radio solution, Rev. 1
 * - https://www.st.com/resource/en/reference_manual/rm0530--stm32wb07xc-and-stm32wb06xc-ultralow-power-wireless-32bit-mcus-armbased-cortexm0-with-bluetooth-low-energy-and-24-ghz-radio-solution-stmicroelectronics.pdf
 * RM0505 - STM32WL33 ultra-low power sub-GHz wireless 32-bit MCU Arm®-based Cortex®-M0+, Rev. 1
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortex.h"
#include "stm32_common.h"

/* Memory map constants for STM32WB0x */
#define STM32WB0_FLASH_BANK_BASE 0x10000000U
#define STM32WB0_FLASH_BANK_SIZE 0x00080000U
#define STM32WB0_SRAM_BASE       0x20000000U
#define STM32WB0_SRAM_SIZE       0x00010000U

/* Memory map constants for STM32WL3x */
#define STM32WL3_FLASH_BANK_BASE 0x10040000U

/* Shared flash controller registers (same base and layout for both families) */
#define STM32WB0_FLASH_BASE       0x40001000U
#define STM32WB0_FLASH_COMMAND    (STM32WB0_FLASH_BASE + 0x000U)
#define STM32WB0_FLASH_STATUS     (STM32WB0_FLASH_BASE + 0x008U) /* WB0x: masked interrupt status */
#define STM32WL3_FLASH_IRQRAW     (STM32WB0_FLASH_BASE + 0x010U) /* WL3x: raw interrupt status */
#define STM32WB0_FLASH_FLASH_SIZE (STM32WB0_FLASH_BASE + 0x014U)
#define STM32WB0_FLASH_ADDRESS    (STM32WB0_FLASH_BASE + 0x018U)
#define STM32WB0_FLASH_DATA0      (STM32WB0_FLASH_BASE + 0x040U)

#define STM32WB0_FLASH_PAGE_SIZE   0x00000100U /* WB0x write granularity */
#define STM32WB0_FLASH_SECTOR_SIZE 0x00000800U /* erase granularity (both families) */

/* Status/interrupt bits (same bit positions for both families) */
#define STM32WB0_FLASH_STATUS_CMDDONE    (1U << 0U)
#define STM32WB0_FLASH_STATUS_CMDSTART   (1U << 1U)
#define STM32WB0_FLASH_STATUS_ERROR_MASK 0x0000001cU /* WB0x: bits [4:2] */
#define STM32WL3_FLASH_IRQ_ERROR_MASK    0x0000000cU /* WL3x: bits [3:2] */

/* Flash commands (shared between both families) */
#define STM32WB0_FLASH_COMMAND_SECTOR_ERASE 0x11U
#define STM32WB0_FLASH_COMMAND_MASS_ERASE   0x22U
#define STM32WB0_FLASH_COMMAND_WRITE        0x33U
#define STM32WB0_FLASH_COMMAND_WAKEUP       0xbbU
#define STM32WB0_FLASH_COMMAND_BURST_WRITE  0xccU

/* STM32WB0x debug: power control register */
#define STM32WB0_PWRC_BASE            0x48500000U
#define STM32WB0_PWRC_DBGR            (STM32WB0_PWRC_BASE + 0x084U)
#define STM32WB0_PWRC_DBGR_DEEPSTOP2  (1U << 0U)

/* STM32WL3x debug: DBGMCU registers */
#define STM32WL3_DBGMCU_BASE          0x40008000U
#define STM32WL3_DBGMCU_CR            (STM32WL3_DBGMCU_BASE + 0x000U)
#define STM32WL3_DBGMCU_APB0FZ        (STM32WL3_DBGMCU_BASE + 0x004U)
#define STM32WL3_DBGMCU_CR_DBG_SLEEP  (1U << 0U) /* Keep debug active in sleep */
#define STM32WL3_DBGMCU_CR_DBG_STOP   (1U << 1U) /* Keep debug active in stop */
#define STM32WL3_DBGMCU_APB0FZ_IWDG   (1U << 14U) /* Freeze IWDG in debug */

/* STM32WL3x FLASH_SIZE register: bit 17 selects RAM size (0=16KB, 1=32KB) */
#define STM32WL3_FLASH_SIZE_RAM_BIT    (1U << 17U)

#define ID_STM32WB0 0x01eU
#define ID_STM32WL3 0x027U

static bool stm32wb0_enter_flash_mode(target_s *target);
static bool stm32wb0_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool stm32wb0_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool stm32wb0_mass_erase(target_s *target, platform_timeout_s *print_progess);

static void stm32wb0_add_flash(
	target_s *const target, const uint32_t start, const size_t length, const size_t writesize)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = start;
	flash->length = length;
	flash->blocksize = STM32WB0_FLASH_SECTOR_SIZE;
	flash->writesize = writesize;
	flash->erase = stm32wb0_flash_erase;
	flash->write = stm32wb0_flash_write;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

uint32_t stm32wb0_ram_size(const uint32_t signature)
{
	/* Determine how much RAM is available on the device */
	switch ((signature >> 17U) & 3U) {
	case 2U:
		/* 48KiB */
		return UINT32_C(48) * 1024U;
	case 3U:
		/* 64KiB */
		return UINT32_C(64) * 1024U;
	default:
		/* 32KiB */
		return UINT32_C(32) * 1024U;
	}
}

bool stm32wb0_probe(target_s *const target)
{
	const adiv5_access_port_s *const ap = cortex_ap(target);
	/* Use the partno from the AP always to handle the difference between JTAG and SWD */
	if (ap->partno != ID_STM32WB0 && ap->partno != ID_STM32WL3)
		return false;
	target->part_id = ap->partno;

	target->mass_erase = stm32wb0_mass_erase;
	target->enter_flash_mode = stm32wb0_enter_flash_mode;

	const uint32_t signature = target_mem32_read32(target, STM32WB0_FLASH_FLASH_SIZE);
	const size_t flash_size = ((signature & 0x0000ffffU) + 1U) << 2U;

	if (ap->partno == ID_STM32WL3) {
		/* STM32WL3x: use DBGMCU to keep debug active in sleep/stop and freeze IWDG */
		target_mem32_write32(target, STM32WL3_DBGMCU_CR,
			STM32WL3_DBGMCU_CR_DBG_SLEEP | STM32WL3_DBGMCU_CR_DBG_STOP);
		target_mem32_write32(target, STM32WL3_DBGMCU_APB0FZ, STM32WL3_DBGMCU_APB0FZ_IWDG);

		target->driver = "STM32WL3";
		/* RAM size: 32 KB if bit 17 is set, 16 KB otherwise */
		const size_t ram_size = (signature & STM32WL3_FLASH_SIZE_RAM_BIT) ? 0x8000U : 0x4000U;
		target_add_ram32(target, STM32WB0_SRAM_BASE, ram_size);
		/* WL3x writes one 32-bit word at a time */
		stm32wb0_add_flash(target, STM32WL3_FLASH_BANK_BASE, flash_size, 4U);
	} else {
		/* STM32WB0x: prevent deep sleeping from taking the debug link out */
		target_mem32_write16(target, STM32WB0_PWRC_DBGR, STM32WB0_PWRC_DBGR_DEEPSTOP2);

		target->driver = "STM32WB0";
		target_add_ram32(target, STM32WB0_SRAM_BASE, stm32wb0_ram_size(signature));
		stm32wb0_add_flash(target, STM32WB0_FLASH_BANK_BASE, flash_size, STM32WB0_FLASH_PAGE_SIZE);
	}
	return true;
}

static bool stm32wb0_flash_wait_complete(target_s *const target, platform_timeout_s *const timeout)
{
	/* WL3x polls the raw interrupt status; WB0x polls the masked status register */
	const uint32_t status_reg =
		(target->part_id == ID_STM32WL3) ? STM32WL3_FLASH_IRQRAW : STM32WB0_FLASH_STATUS;
	const uint32_t error_mask =
		(target->part_id == ID_STM32WL3) ? STM32WL3_FLASH_IRQ_ERROR_MASK : STM32WB0_FLASH_STATUS_ERROR_MASK;

	uint32_t status = 0U;
	/* Poll until the command has both started and completed */
	while ((status & (STM32WB0_FLASH_STATUS_CMDDONE | STM32WB0_FLASH_STATUS_CMDSTART)) !=
		(STM32WB0_FLASH_STATUS_CMDDONE | STM32WB0_FLASH_STATUS_CMDSTART)) {
		status = target_mem32_read32(target, status_reg);
		if (target_check_error(target)) {
			DEBUG_ERROR("%s: error reading status\n", __func__);
			return false;
		}
		if (timeout)
			target_print_progress(timeout);
	}
	if (status & error_mask)
		DEBUG_ERROR("%s: Flash error: %08" PRIx32 "\n", __func__, status);
	/* Clear all error and status bits by writing them back (write-1-clear) */
	target_mem32_write32(target, status_reg, status);
	return !(status & error_mask);
}

static bool stm32wb0_enter_flash_mode(target_s *const target)
{
	target_reset(target);
	/* Clear any pending status bits using the device-appropriate register */
	const uint32_t status_reg =
		(target->part_id == ID_STM32WL3) ? STM32WL3_FLASH_IRQRAW : STM32WB0_FLASH_STATUS;
	target_mem32_write32(target, status_reg, target_mem32_read32(target, status_reg));
	/* Wake the flash controller from sleep mode */
	target_mem32_write32(target, STM32WB0_FLASH_COMMAND, STM32WB0_FLASH_COMMAND_WAKEUP);
	return stm32wb0_flash_wait_complete(target, NULL);
}

static bool stm32wb0_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t len)
{
	(void)len;
	target_s *const target = flash->t;

	/*
	 * Compute the word address relative to the flash bank base and write it to the
	 * address register, then instruct the controller to start the sector erase.
	 */
	target_mem32_write32(target, STM32WB0_FLASH_ADDRESS, (addr - flash->start) >> 2U);
	target_mem32_write32(target, STM32WB0_FLASH_COMMAND, STM32WB0_FLASH_COMMAND_SECTOR_ERASE);
	/* Wait for the operation to complete and report any errors */
	return stm32wb0_flash_wait_complete(target, NULL);
}

static bool stm32wb0_flash_write(
	target_flash_s *const flash, const target_addr_t dest, const void *const src, const size_t len)
{
	target_s *const target = flash->t;

	/* Start by telling the controller the first word address to program */
	target_mem32_write32(target, STM32WB0_FLASH_ADDRESS, (dest - flash->start) >> 2U);

	const uint32_t *const data = (const uint32_t *)src;
	/* Now loop through each location to write, 32 bits at a time */
	for (size_t offset = 0; offset < len; offset += 4U) {
		/* Load the next 32 bits up into the staging register */
		target_mem32_write32(target, STM32WB0_FLASH_DATA0, data[offset >> 2U]);
		/* And set the write running */
		target_mem32_write32(target, STM32WB0_FLASH_COMMAND, STM32WB0_FLASH_COMMAND_WRITE);
		/* Now wait for the write to complete and report an errors */
		if (!stm32wb0_flash_wait_complete(target, NULL))
			return false;
	}
	return true;
}

static bool stm32wb0_mass_erase(target_s *const target, platform_timeout_s *const print_progess)
{
	/* To start the mass erase, prep the controller */
	if (!stm32wb0_enter_flash_mode(target))
		return false;

	/* Set up and run the mass erase */
	target_mem32_write32(target, STM32WB0_FLASH_COMMAND, STM32WB0_FLASH_COMMAND_MASS_ERASE);
	/* Then wait for the erase to complete and report any errors */
	return stm32wb0_flash_wait_complete(target, print_progess);
}
