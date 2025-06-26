/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
 * Written by Mickael Bosch <mickael@mrrabb.it>
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
 * This file implements support for STM32U0x series devices, providing
 * memory maps and Flash programming routines.
 *
 * References:
 * RM0503 - STM32U0 series advanced Arm®-based 32-bit MCUs Rev 2
 * - https://www.st.com/resource/en/reference_manual/rm0503-stm32u0-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortex.h"
#include "cortexm.h"

/* Memory map constants for STM32U0x */
#define STM32U0_FLASH_BANK_BASE 0x08000000U
#define STM32U0_SRAM_BASE       0x20000000U

/* RM0503 §2.2 p57-60 */
#define FLASH_REGS_BASE 0x40022000U

/* RM0503 §3.3.6 p69 */
#define FLASH_KEY1_REG_VAL 0x45670123U
#define FLASH_KEY2_REG_VAL 0xcdef89abU

/* RM0503 §3.7.2 p88 */
#define FLASH_KEYR_REG (FLASH_REGS_BASE + 0x8U)

/* RM0503 §3.7.4 p88 */
#define FLASH_SR_REG (FLASH_REGS_BASE + 0x10U)

/* Errors are cleared by programming them to 1 */
#define FLASH_SR_EOP        (1U << 0U)  /* End of operation */
#define FLASH_SR_OPERR      (1U << 1U)  /* Operation error */
#define FLASH_SR_PROGERR    (1U << 3U)  /* Programming error */
#define FLASH_SR_WRPERR     (1U << 4U)  /* Write protection error */
#define FLASH_SR_PGAERR     (1U << 5U)  /* Programming alignment error  */
#define FLASH_SR_SIZERR     (1U << 6U)  /* Size error */
#define FLASH_SR_PGSERR     (1U << 7U)  /* Programming sequence error */
#define FLASH_SR_MSERR      (1U << 8U)  /* Fast programming data miss error */
#define FLASH_SR_FASTERR    (1U << 9U)  /* Fast programming error */
#define FLASH_SR_HDPOPTWERR (1U << 11U) /* HDP option bytes write error */
#define FLASH_SR_OEMOPTWERR (1U << 12U) /* OEM options byte write error */
#define FLASH_SR_OPTVERR    (1U << 15U) /* Option and engineering bits loading validity error */
#define FLASH_SR_ERROR_MASK                                                                                      \
	(FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_PGSERR | \
		FLASH_SR_MSERR | FLASH_SR_FASTERR | FLASH_SR_HDPOPTWERR | FLASH_SR_OEMOPTWERR | FLASH_SR_OPTVERR)

#define FLASH_SR_BSY    (1U << 16U) /* Busy */
#define FLASH_SR_CFGBSY (1U << 18U) /* Programming or erase configuration busy */

/* RM0503 §3.7.5 p91 */
#define FLASH_CR_REG (FLASH_REGS_BASE + 0x14U)

#define FLASH_CR_PG         (1U << 0U)
#define FLASH_CR_PER        (1U << 1U)
#define FLASH_CR_MER1       (1U << 2U)
#define FLASH_CR_PAGE_SHIFT 3U /* start bit of PNB[6:0] */
#define FLASH_CR_STRT       (1U << 16U)
#define FLASH_CR_LOCK       (1U << 31U)

/* RM0503 §37.9.4 p1301 */
#define STM32U0_DBGMCU_BASE    0x40015800U
#define STM32U0_DBGMCU_IDCODE  (STM32U0_DBGMCU_BASE + 0x000U)
#define STM32U0_DBGMCU_CR      (STM32U0_DBGMCU_BASE + 0x004U)
#define STM32U0_DBGMCU_APB1FZR (STM32U0_DBGMCU_BASE + 0x008U)

/* RM0503 §37.9.4 p1301-1304 */
#define STM32U0_DBGMCU_CR_DBG_STOP    (1U << 1U)
#define STM32U0_DBGMCU_CR_DBG_STANDBY (1U << 2U)

#define STM32U0_DBGMCU_APB1FZR_WWDG (1U << 11U)
#define STM32U0_DBGMCU_APB1FZR_IWDG (1U << 12U)

/* RM0503 $38.2 p1314 */
#define STM32U031_FLASH_SIZE_REG 0x1fff3ea0U
#define STM32U0x3_FLASH_SIZE_REG 0x1fff6ea0U

/* size in Kb */
#define STM32U031_SRAM_SIZE UINT32_C(12)
#define STM32U0x3_SRAM_SIZE UINT32_C(40)

/* RM0503 §37.3.3 p1251 */
#define ID_STM32U031 0x459U /* STM32U031 */
#define ID_STM32U0x3 0x489U /* STM32U073/083 */

static bool stm32u0_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool stm32u0_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool stm32u0_mass_erase(target_s *target, platform_timeout_s *print_progess);
static bool stm32u0_attach(target_s *const target);
static void stm32u0_detach(target_s *const target);

static void stm32u0_add_flash(target_s *const target, const size_t length)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = STM32U0_FLASH_BANK_BASE;
	flash->length = length;
	flash->blocksize = 2048; /* Erase block size */
	flash->writesize = 2048; /* Write operation size */
	flash->erase = stm32u0_flash_erase;
	flash->write = stm32u0_flash_write;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

static bool stm32u0_configure_dbgmcu(target_s *const target)
{
	/* RM0503 §37.9.2 p1299: disable low power clock stop */
	target_mem32_write32(target, STM32U0_DBGMCU_CR,
		target_mem32_read32(target, STM32U0_DBGMCU_CR) | STM32U0_DBGMCU_CR_DBG_STANDBY | STM32U0_DBGMCU_CR_DBG_STOP);
	/* Disable IWDG and WWDG */
	target_mem32_write32(target, STM32U0_DBGMCU_APB1FZR,
		target_mem32_read32(target, STM32U0_DBGMCU_APB1FZR) | STM32U0_DBGMCU_APB1FZR_WWDG |
			STM32U0_DBGMCU_APB1FZR_IWDG);
	return true;
}

static void stm32u0_deconfigure_dbgmcu(target_s *const target)
{
	target_mem32_write32(target, STM32U0_DBGMCU_CR,
		target_mem32_read32(target, STM32U0_DBGMCU_CR) & ~(STM32U0_DBGMCU_CR_DBG_STANDBY | STM32U0_DBGMCU_CR_DBG_STOP));
	target_mem32_write32(target, STM32U0_DBGMCU_APB1FZR,
		target_mem32_read32(target, STM32U0_DBGMCU_APB1FZR) &
			~(STM32U0_DBGMCU_APB1FZR_WWDG | STM32U0_DBGMCU_APB1FZR_IWDG));
}

bool stm32u0_probe(target_s *const target)
{
	const adiv5_access_port_s *const ap = cortex_ap(target);
	uint32_t sram_size;
	uint32_t flash_size_reg;

	switch (ap->partno) {
	case ID_STM32U031:
		sram_size = STM32U031_SRAM_SIZE;
		flash_size_reg = STM32U031_FLASH_SIZE_REG;
		break;
	case ID_STM32U0x3:
		sram_size = STM32U0x3_SRAM_SIZE;
		flash_size_reg = STM32U0x3_FLASH_SIZE_REG;
		break;
	default:
		return false;
	}

	target->part_id = ap->partno;
	target->driver = "STM32U0";
	target->mass_erase = stm32u0_mass_erase;
	target->attach = stm32u0_attach;
	target->detach = stm32u0_detach;

	target_add_ram32(target, STM32U0_SRAM_BASE, sram_size * 1024U);
	const uint16_t flash_size = target_mem32_read16(target, flash_size_reg);
	stm32u0_add_flash(target, flash_size * 1024U);
	return true;
}

static bool stm32u0_attach(target_s *const target)
{
	return cortexm_attach(target) && stm32u0_configure_dbgmcu(target);
}

static void stm32u0_detach(target_s *const target)
{
	/* Reverse all changes to the appropriate STM32U0_DBGMCU_* registers */
	stm32u0_deconfigure_dbgmcu(target);
	cortexm_detach(target);
}

static void stm32u0_flash_unlock(target_s *const target)
{
	if (target_mem32_read32(target, FLASH_CR_REG) & FLASH_CR_LOCK) {
		target_mem32_write32(target, FLASH_KEYR_REG, FLASH_KEY1_REG_VAL);
		target_mem32_write32(target, FLASH_KEYR_REG, FLASH_KEY2_REG_VAL);
	}
}

static bool stm32u0_flash_sr_flag_wait_reset(
	target_s *const target, platform_timeout_s *const print_progess, uint32_t flag_bit)
{
	/* Read FLASH_SR to poll for the target bit to be cleared */
	uint32_t status = flag_bit;
	while (status & flag_bit) {
		status = target_mem32_read32(target, FLASH_SR_REG);
		if ((status & FLASH_SR_ERROR_MASK) || target_check_error(target)) {
			DEBUG_ERROR("stm32u0 Flash error: status %" PRIx32 "\n", status);
			return false;
		}
		if (print_progess)
			target_print_progress(print_progess);
	}
	return true;
}

static bool stm32u0_flash_busy_wait(target_s *const target, platform_timeout_s *const print_progess)
{
	return stm32u0_flash_sr_flag_wait_reset(target, print_progess, FLASH_SR_BSY);
}

static bool stm32u0_flash_cfgbusy_wait(target_s *const target, platform_timeout_s *const print_progess)
{
	return stm32u0_flash_sr_flag_wait_reset(target, print_progess, FLASH_SR_CFGBSY);
}

static bool stm32u0_flash_clear_errors(target_s *const target)
{
	uint32_t sr = target_mem32_read32(target, FLASH_SR_REG);
	target_mem32_write32(target, FLASH_SR_REG, sr | FLASH_SR_ERROR_MASK);
	return true;
}

static bool stm32u0_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t len)
{
	(void)len;
	target_s *const target = flash->t;

	stm32u0_flash_unlock(target);
	/* Procedure described in RM0503 §3.3.7 */
	/* 1. Check that no flash memory operation is ongoing by checking the BSY1 bit of the FLASH status register (FLASH_SR).*/
	if (!stm32u0_flash_busy_wait(target, NULL))
		return false;
	/* 2.  Check and clear all error programming flags due to a previous programming. If not, PGSERR is set. */
	stm32u0_flash_clear_errors(target);
	/* 3.  Check that the CFGBSY bit of FLASH status register (FLASH_SR) is cleared. */
	if (!stm32u0_flash_cfgbusy_wait(target, NULL))
		return false;
	for (size_t offset = 0; offset < len; offset += flash->blocksize) {
		const uint32_t page = (addr + offset - STM32U0_FLASH_BANK_BASE) / flash->blocksize;
		const uint32_t ctrl = FLASH_CR_PER | (page << FLASH_CR_PAGE_SHIFT);
		/* 4. Set the PER bit and select the page to erase (PNB) in the FLASH control register (FLASH_CR). */
		target_mem32_write32(target, FLASH_CR_REG, ctrl);
		/* 5. Set the STRT bit of the FLASH control register (FLASH_CR). */
		target_mem32_write32(target, FLASH_CR_REG, ctrl | FLASH_CR_STRT);
		/* 6. Wait until the CFGBSY bit of the FLASH status register (FLASH_SR) is cleared again. */
		if (!stm32u0_flash_cfgbusy_wait(target, NULL))
			return false;
	}
	return stm32u0_flash_busy_wait(target, NULL);
}

static bool stm32u0_flash_write(
	target_flash_s *const flash, const target_addr_t dest, const void *const src, const size_t len)
{
	target_s *const target = flash->t;

	target_mem32_write32(target, FLASH_CR_REG, FLASH_CR_PG);
	target_mem32_write(target, dest, src, len);
	return stm32u0_flash_busy_wait(target, NULL);
}

static bool stm32u0_mass_erase(target_s *const target, platform_timeout_s *const print_progess)
{
	stm32u0_flash_unlock(target);
	/* 1. Check that no flash memory operation is ongoing by checking the BSY1 bit of the FLASH status register (FLASH_SR).*/
	if (!stm32u0_flash_busy_wait(target, NULL))
		return false;
	/* 2. Check and clear all error programming flags due to a previous programming. If not, PGSERR is set. */
	stm32u0_flash_clear_errors(target);
	/* 3. Check that the CFGBSY bit of FLASH status register (FLASH_SR) is cleared. */
	if (!stm32u0_flash_cfgbusy_wait(target, NULL))
		return false;
	/* 4. Set the MER1 bit of the FLASH control register (FLASH_CR). */
	target_mem32_write32(target, FLASH_CR_REG, FLASH_CR_MER1);
	/* 5. Set the STRT bit of the FLASH control register (FLASH_CR). */
	target_mem32_write32(target, FLASH_CR_REG, FLASH_CR_MER1 | FLASH_CR_STRT);
	/* 6. Wait until the CFGBSY bit of the FLASH status register (FLASH_SR) is cleared again. */
	return stm32u0_flash_busy_wait(target, print_progess);
}
