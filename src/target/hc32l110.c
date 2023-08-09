/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 Maciej Kulinski (vesim809@pm.me)
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This file implements the target-specific support for the HDSC HC32L110 series
 *
 * References:
 * HC32L110系列数据手册Rev2.5 (HC32L110 Series Data Sheet Rev2.5)
 *  https://www.hdsc.com.cn/cn/Index/downloadFile/modelid/65/id/8/key/0
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortex.h"

#define HC32L110_FLASH_BASE 0x00000000U
/*
 * Per §7.2 table 7-1 on pg199, the Flash is broken up into 16 sectors
 * of 512 bytes each. At most 4 bytes can be written at a time before
 * having to wait for the Flash controller to become idle again.
 */
#define HC32L110_FLASH_SECTOR_SIZE 512U
#define HC32L110_FLASH_WRITE_SIZE  4U

/*
 * This is a special register defined in §26.3 of the datasheet on pg520.
 * It contains a count of the amount of Flash present on the part.
 */
#define HC32L110_FLASH_SIZE 0x00100c70U

/* Flash controller register defines from §7.8 pg208 */
#define HC32L110_FLASH_CTRL_BASE 0x40020000U
#define HC32L110_FLASH_CR        (HC32L110_FLASH_CTRL_BASE + 0x020U)
#define HC32L110_FLASH_BYPASS    (HC32L110_FLASH_CTRL_BASE + 0x02cU)
#define HC32L110_FLASH_SLOCK     (HC32L110_FLASH_CTRL_BASE + 0x030U)

#define HC32L110_FLASH_CR_BUSY (1U << 4U)

#define HC32L110_FLASH_CR_OP_READ         0U
#define HC32L110_FLASH_CR_OP_PROGRAM      1U
#define HC32L110_FLASH_CR_OP_ERASE_SECTOR 2U
#define HC32L110_FLASH_CR_OP_ERASE_CHIP   3U

static bool hc32l110_enter_flash_mode(target_s *target);
static bool hc32l110_flash_prepare(target_flash_s *flash);
static bool hc32l110_flash_done(target_flash_s *flash);
static bool hc32l110_flash_erase(target_flash_s *flash, target_addr_t addr, size_t length);
static bool hc32l110_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t length);
static bool hc32l110_mass_erase(target_s *target);

static void hc32l110_add_flash(target_s *target, const uint32_t flash_size)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = HC32L110_FLASH_BASE;
	flash->length = flash_size;
	flash->blocksize = HC32L110_FLASH_SECTOR_SIZE;
	flash->writesize = HC32L110_FLASH_WRITE_SIZE;
	flash->erased = 0xffU;
	flash->erase = hc32l110_flash_erase;
	flash->write = hc32l110_flash_write;
	flash->prepare = hc32l110_flash_prepare;
	flash->done = hc32l110_flash_done;
	target_add_flash(target, flash);
}

bool hc32l110_probe(target_s *target)
{
	const uint32_t flash_size = target_mem_read32(target, HC32L110_FLASH_SIZE);

	switch (flash_size) {
	case 16384:
		target_add_ram(target, 0x2000000, 2048);
		break;
	case 32768:
		target_add_ram(target, 0x2000000, 4096);
		break;
	default:
		return false;
	}

	target->driver = "HC32L110";
	target->enter_flash_mode = hc32l110_enter_flash_mode;
	target->mass_erase = hc32l110_mass_erase;

	hc32l110_add_flash(target, flash_size);
	return true;
}

/* Executes the magic sequence to unlock the CR register */
static void hc32l110_flash_cr_unlock(target_s *const target)
{
	target_mem_write32(target, HC32L110_FLASH_BYPASS, 0x5a5aU);
	target_mem_write32(target, HC32L110_FLASH_BYPASS, 0xa5a5U);
}

static bool hc32l110_check_flash_completion(target_s *const target, const uint32_t timeout_ms)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, timeout_ms);
	uint32_t status = HC32L110_FLASH_CR_BUSY;
	while (status & HC32L110_FLASH_CR_BUSY) {
		status = target_mem_read32(target, HC32L110_FLASH_CR);
		if (target_check_error(target) || platform_timeout_is_expired(&timeout))
			return false;
	}
	return true;
}

/* Lock the whole flash */
static void hc32l110_slock_lock_all(target_s *const target)
{
	hc32l110_flash_cr_unlock(target);
	target_mem_write32(target, HC32L110_FLASH_SLOCK, 0);
}

/* Unlock the whole flash for writing */
static void hc32l110_slock_unlock_all(target_s *const target)
{
	hc32l110_flash_cr_unlock(target);
	target_mem_write32(target, HC32L110_FLASH_SLOCK, 0xffffU);
}

static bool hc32l110_enter_flash_mode(target_s *const target)
{
	target_reset(target);

	/*
	 * The Flash controller requires the core's program counter to be
	 * outside of the Flash to unlock all regions of the Flash
	 * (Whatever sector it is left in becomes stuck in a locked state)
	 */
	const uint32_t pc = 0xfffffffeU;
	return target_reg_write(target, CORTEX_REG_PC, &pc, sizeof(pc)) == sizeof(pc);
}

static bool hc32l110_flash_prepare(target_flash_s *const flash)
{
	hc32l110_flash_cr_unlock(flash->t);

	switch (flash->operation) {
	case FLASH_OPERATION_WRITE:
		target_mem_write32(flash->t, HC32L110_FLASH_CR, HC32L110_FLASH_CR_OP_PROGRAM);
		break;
	case FLASH_OPERATION_ERASE:
		target_mem_write32(flash->t, HC32L110_FLASH_CR, HC32L110_FLASH_CR_OP_ERASE_SECTOR);
		break;
	default:
		DEBUG_WARN("unsupported operation %u", flash->operation);
		return false;
	}

	hc32l110_slock_unlock_all(flash->t);
	return true;
}

static bool hc32l110_flash_done(target_flash_s *const flash)
{
	hc32l110_slock_lock_all(flash->t);
	return true;
}

static bool hc32l110_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t length)
{
	(void)length;
	/* The Flash controller automatically erases the whole sector after one write operation */
	target_mem_write32(flash->t, addr, 0);
	return hc32l110_check_flash_completion(flash->t, 1000);
}

static bool hc32l110_flash_write(
	target_flash_s *const flash, const target_addr_t dest, const void *const src, const size_t length)
{
	(void)length;
	target_mem_write32(flash->t, dest, *(const uint32_t *)src);
	return hc32l110_check_flash_completion(flash->t, 1000);
}

static bool hc32l110_mass_erase(target_s *target)
{
	hc32l110_enter_flash_mode(target);

	hc32l110_flash_cr_unlock(target);
	target_mem_write32(target, HC32L110_FLASH_CR, HC32L110_FLASH_CR_OP_ERASE_CHIP);
	if (!hc32l110_check_flash_completion(target, 500))
		return false;

	hc32l110_slock_unlock_all(target);

	// The Flash controller automatically erases the whole Flash after one write operation
	target_mem_write32(target, 0, 0);
	const bool result = hc32l110_check_flash_completion(target, 4000);

	hc32l110_slock_lock_all(target);
	return result;
}
