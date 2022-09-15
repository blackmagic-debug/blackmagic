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

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define HC32L110_FLASH_BASE 0x00000000U
#define HC32L110_BLOCKSIZE  512U

#define HC32L110_ADDR_FLASH_SIZE   0x00100c70U
#define HC32L110_FLASH_CR_ADDR     0x40020020U
#define HC32L110_FLASH_CR_BUSY     (1U << 4U)
#define HC32L110_FLASH_BYPASS_ADDR 0x4002002cU
#define HC32L110_FLASH_SLOCK_ADDR  0x40020030U

#define HC32L110_FLASH_CR_OP_READ         0U
#define HC32L110_FLASH_CR_OP_PROGRAM      1U
#define HC32L110_FLASH_CR_OP_ERASE_SECTOR 2U
#define HC32L110_FLASH_CR_OP_ERASE_CHIP   3U

static bool hc32l110_enter_flash_mode(target_s *target);
static bool hc32l110_flash_prepare(target_flash_s *flash);
static bool hc32l110_flash_done(target_flash_s *flash);
static bool hc32l110_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool hc32l110_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);

static void hc32l110_add_flash(target_s *target, const uint32_t flash_size)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = HC32L110_FLASH_BASE;
	flash->length = flash_size;
	flash->blocksize = HC32L110_BLOCKSIZE;
	flash->writesize = HC32L110_BLOCKSIZE;
	flash->erased = 0xffU;
	flash->erase = hc32l110_flash_erase;
	flash->write = hc32l110_flash_write;
	flash->prepare = hc32l110_flash_prepare;
	flash->done = hc32l110_flash_done;
	target_add_flash(target, flash);
}

bool hc32l110_probe(target_s *t)
{
	const uint32_t flash_size = target_mem_read32(t, HC32L110_ADDR_FLASH_SIZE);

	switch (flash_size) {
	case 16384:
		t->driver = "HC32L110A";
		target_add_ram(t, 0x2000000, 2048);
		break;
	case 32768:
		t->driver = "HC32L110B";
		target_add_ram(t, 0x2000000, 4096);
		break;
	default:
		return false;
	}

	t->enter_flash_mode = hc32l110_enter_flash_mode;

	hc32l110_add_flash(t, flash_size);
	return true;
}

/* Executes the magic sequence to unlock the CR register */
static void hc32l110_flash_cr_unlock(target_s *const target)
{
	target_mem_write32(target, HC32L110_FLASH_BYPASS_ADDR, 0x5a5aU);
	target_mem_write32(target, HC32L110_FLASH_BYPASS_ADDR, 0xa5a5U);
}

static bool hc32l110_check_flash_completion(target_s *const target, const uint32_t timeout)
{
	(void)timeout;

	while (true) {
		const uint32_t status = target_mem_read32(target, HC32L110_FLASH_CR_ADDR);
		if ((status & HC32L110_FLASH_CR_BUSY) == 0)
			break;
	}

	return true;
}

/* Lock the whole flash */
static void hc32l110_slock_lock_all(target_s *const target)
{
	hc32l110_flash_cr_unlock(target);
	target_mem_write32(target, HC32L110_FLASH_SLOCK_ADDR, 0);
}

/* Unlock the whole flash for writing */
static void hc32l110_slock_unlock_all(target_s *const target)
{
	hc32l110_flash_cr_unlock(target);
	target_mem_write32(target, HC32L110_FLASH_SLOCK_ADDR, 0xffffU);
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
	return target_reg_write(target, REG_PC, &pc, sizeof(pc)) == sizeof(pc);
}

static bool hc32l110_flash_prepare(target_flash_s *const flash)
{
	hc32l110_flash_cr_unlock(flash->t);

	switch (flash->operation) {
	case FLASH_OPERATION_WRITE:
		target_mem_write32(flash->t, HC32L110_FLASH_CR_ADDR, HC32L110_FLASH_CR_OP_PROGRAM);
		break;
	case FLASH_OPERATION_ERASE:
		target_mem_write32(flash->t, HC32L110_FLASH_CR_ADDR, HC32L110_FLASH_CR_OP_ERASE_SECTOR);
		break;
	default:
		DEBUG_WARN("unsupported operation %u", flash->operation);
		return false;
	}

	hc32l110_check_flash_completion(flash->t, 1000);
	hc32l110_slock_unlock_all(flash->t);

	return true;
}

static bool hc32l110_flash_done(target_flash_s *const flash)
{
	hc32l110_slock_lock_all(flash->t);
	hc32l110_check_flash_completion(flash->t, 1000);

	return true;
}

static bool hc32l110_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t len)
{
	(void)len;
	// The Flash controller automatically erases the whole sector after one write operation
	target_mem_write32(flash->t, addr, 0);

	return hc32l110_check_flash_completion(flash->t, 2000);
}

static bool hc32l110_flash_write(
	target_flash_s *const flash, const target_addr_t dest, const void *const src, const size_t len)
{
	const uint32_t *const buffer = (const uint32_t *)src;
	for (size_t offset = 0; offset < len; offset += 4U) {
		uint32_t val = buffer[offset >> 2U];
		target_mem_write32(flash->t, dest + offset, val);
		hc32l110_check_flash_completion(flash->t, 2000);
	}

	return true;
}
