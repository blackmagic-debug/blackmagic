/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
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

#include "ch32_flash.h"

/* 
 * FIXME: these defines are here to determine if these are actually required
 * if not remove them, else remove the defines
 */
#define ENABLE_CH32_FLASH_DELAYS   1U
#define ENABLE_CH32F1X_FLASH_MAGIC 1U

/* Generic CH32 flash routines */

bool ch32_flash_fast_mode_locked(target_s *const target, const uint32_t fpec_base)
{
	const uint32_t ctrl = target_mem_read32(target, STM32_FLASH_CR(fpec_base));
	return ctrl & CH32_FLASH_CR_FLOCK;
}

bool ch32_flash_fast_mode_unlock(target_s *const target, const uint32_t fpec_base)
{
	target_mem_write32(target, CH32_FLASH_MODEKEYR(fpec_base), STM32_FLASH_KEY1);
	target_mem_write32(target, CH32_FLASH_MODEKEYR(fpec_base), STM32_FLASH_KEY2);

#if ENABLE_CH32_FLASH_DELAYS
	platform_delay(1U); /* FIXME: The flash controller is timing sensitive? */
#endif

	/* Check that the fast mode is unlocked */
	if (ch32_flash_fast_mode_locked(target, fpec_base)) {
		DEBUG_ERROR("ch32 flash fast unlock failed\n");
		return false;
	}
	return true;
}

void ch32_flash_lock(target_s *const target, const uint32_t fpec_base)
{
	const uint32_t ctrl = target_mem_read32(target, STM32_FLASH_CR(fpec_base));
	/* Retain the EOPIE, ERRIE and OPTWRE bits, and set the LOCK and FLOCK bits */
	target_mem_write32(target, STM32_FLASH_CR(fpec_base),
		(ctrl & (STM32_FLASH_CR_EOPIE | STM32_FLASH_CR_ERRIE | STM32_FLASH_CR_OPTWRE)) | STM32_FLASH_CR_LOCK |
			CH32_FLASH_CR_FLOCK);

#if ENABLE_CH32_FLASH_DELAYS
	platform_delay(1U); /* FIXME: The flash controller is timing sensitive? */
#endif
}

static bool ch32_flash_prepare(target_flash_s *const flash)
{
	stm32_flash_s *const stm32_flash = (stm32_flash_s *)flash;
	target_s *const target = flash->t;

	/* Unlock the flash if required */
	if (stm32_flash_locked(target, stm32_flash->fpec_base, 0U) &&
		!stm32_flash_unlock(target, stm32_flash->fpec_base, 0U))
		return false;

	/* Ensure no operation is ongoing */
	if (target_mem_read32(target, STM32_FLASH_SR_BANK(stm32_flash->fpec_base, 0U)) & STM32_FLASH_SR_BSY) {
		DEBUG_ERROR("ch32 flash unexpectedly busy\n");
		return false; /* FIXME: should we re-lock here? */
	}

	/* Clear any previous operation status */
	stm32_flash_clear_status(target, stm32_flash->fpec_base, 0U);

	/* Set the instruction to the control register */
	uint32_t ctrl_instruction = 0U;
	switch (flash->operation) {
	case FLASH_OPERATION_WRITE:
		/* Set flash fast mode programming instruction */
		ctrl_instruction = CH32_FLASH_CR_FTPG;
		break;
	case FLASH_OPERATION_ERASE:
		/* Set flash fast mode page erase instruction */
		ctrl_instruction = CH32_FLASH_CR_FTER;
		break;
	case FLASH_OPERATION_MASS_ERASE:
		/* Set flash mass erase instruction */
		ctrl_instruction = STM32_FLASH_CR_MER;
		break;
	default:
		return false; /* Unsupported operation */
	}

	/* Unlock the fast mode extension if required by the requested instruction */
	if (flash->operation != FLASH_OPERATION_MASS_ERASE) {
		if (ch32_flash_fast_mode_locked(target, stm32_flash->fpec_base) &&
			!ch32_flash_fast_mode_unlock(target, stm32_flash->fpec_base))
			return false;
	}

	/*
	 * This will clear EOPIE, ERRIE and OPTWRE, but we don't care about them and expect them cleared
	 * after reset anyway, on CH32FV2x/V3x this also clears the EHMOD and SCKMOD, which follow the same logic
	 * as the former.
	 * 
	 * note that we don't read-modify-write the control register after this, meaning we set the instruction
	 * allways, this is to avoid the extra overhead of reading the register since we know what bits should be set.
	 */
	/* FIXME: on CH32FV2x/V3x we might want to check the default SYSCLK and if setting SCKMOD makes sense */
	target_mem_write32(target, STM32_FLASH_CR(stm32_flash->fpec_base), ctrl_instruction);

	return true;
}

static bool ch32_flash_done(target_flash_s *const flash)
{
	stm32_flash_s *const stm32_flash = (stm32_flash_s *)flash;
	target_s *const target = flash->t;

	/* Lock the flash */
	ch32_flash_lock(target, stm32_flash->fpec_base);

	return true;
}

/* CH32F1x flash routines */

#if ENABLE_CH32F1X_FLASH_MAGIC
static inline void ch32f1x_flash_magic(target_s *const target, const uint32_t fpec_base, const uint32_t page_addr)
{
	/* We don't know what this does or if we actually need it, but it is done on the standard peripheral lib */
	const uint32_t magic = target_mem_read32(target, page_addr ^ CH32F1X_FLASH_MAGIC_XOR);
	target_mem_write32(target, CH32F1X_FLASH_MAGIC(fpec_base), magic);
}
#endif

static bool ch32f1x_flash_fast_mode_buffer_clear(target_s *const target, const uint32_t fpec_base)
{
	/* Clear any previous operation status */
	stm32_flash_clear_status(target, fpec_base, 0U);

	/* Clear the internal buffer */
	target_mem_write32(target, STM32_FLASH_CR(fpec_base), CH32_FLASH_CR_FTPG | CH32F1X_FLASH_CR_BUFRST);
	const bool result = stm32_flash_busy_wait(target, fpec_base, 0U, NULL);

#if ENABLE_CH32_FLASH_DELAYS
	platform_delay(2U); /* FIXME: The flash controller is timing sensitive? */
#endif

	return result;
}

static bool ch32f1x_flash_fast_mode_buffer_load(
	target_s *const target, const uint32_t fpec_base, const uint32_t dest, const void *const src)
{
	/*
	 * The fast mode buffer is 128 bytes long, it is loaded in 8x16 byte chunks.
	 * The 16 byte chunks should be written continuously in 32 bit words to the destination address.
	 * The 8 chunks should be written consecutively.
	 */
	for (size_t offset = 0; offset < 128U; offset += 16U) {
		/* Clear any previous operation status */
		stm32_flash_clear_status(target, fpec_base, 0U);

		/* Continuously write 16 bytes of data to the specified address in 32 bit writes */
		target_mem_write(target, dest + offset, (const uint8_t *)src + offset, 16U);

		/* Start buffer load instruction */
		target_mem_write32(target, STM32_FLASH_CR(fpec_base), CH32_FLASH_CR_FTPG | CH32F1X_FLASH_CR_BUFRST);
		if (!stm32_flash_busy_wait(target, fpec_base, 0U, NULL))
			return false;

#if ENABLE_CH32F1X_FLASH_MAGIC
		/* Unknown magic sequence */
		ch32f1x_flash_magic(target, fpec_base, dest + offset);
#endif
	}
	return true;
}

static bool ch32f1x_flash_fast_mode_erase(target_flash_s *const flash, const target_addr_t addr, const size_t length)
{
	(void)length;

	stm32_flash_s *const stm32_flash = (stm32_flash_s *)flash;
	target_s *const target = flash->t;

	/* See §24.4.7 Main Memory Fast Erasure in CH32xRM */

	/* Clear any previous operation status */
	stm32_flash_clear_status(target, stm32_flash->fpec_base, 0U);

	/* Write page address to address register */
	target_mem_write32(target, STM32_FLASH_AR(stm32_flash->fpec_base), addr);

	/* Start fast flash page erase instruction */
	target_mem_write32(target, STM32_FLASH_CR(stm32_flash->fpec_base), STM32_FLASH_CR_STRT | CH32_FLASH_CR_FTER);

	/* Wait for completion or an error, return false on error */
	const bool result = stm32_flash_busy_wait(target, stm32_flash->fpec_base, 0U, NULL);

#if ENABLE_CH32F1X_FLASH_MAGIC
	/* Unknown magic sequence */
	ch32f1x_flash_magic(target, stm32_flash->fpec_base, addr);
#endif

	return result;
}

/* CH32F1x use a "buffer" for fast programming */
static bool ch32f1x_flash_fast_mode_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	(void)len;

	stm32_flash_s *const stm32_flash = (stm32_flash_s *)flash;
	target_s *const target = flash->t;

	/* See §24.4.6 Main Memory Fast Programming in CH32xRM */

	/* Clear the internal buffer */
	ch32f1x_flash_fast_mode_buffer_clear(target, stm32_flash->fpec_base);

	/* Load the internal buffer with the 128 byte page */
	ch32f1x_flash_fast_mode_buffer_load(target, stm32_flash->fpec_base, dest, src);

	/* Clear any previous operation status */
	stm32_flash_clear_status(target, stm32_flash->fpec_base, 0U);

	/* Write page address to address register */
	target_mem_write32(target, STM32_FLASH_AR(stm32_flash->fpec_base), dest);

	/* Start fast mode flash programming instruction */
	target_mem_write32(target, STM32_FLASH_CR(stm32_flash->fpec_base), STM32_FLASH_CR_STRT | CH32_FLASH_CR_FTPG);

	/* Wait for completion or an error, return false on error */
	const bool result = stm32_flash_busy_wait(target, stm32_flash->fpec_base, 0U, NULL);

#if ENABLE_CH32F1X_FLASH_MAGIC
	/* Unknown magic sequence */
	ch32f1x_flash_magic(target, stm32_flash->fpec_base, dest);
#endif

	return result;
}

void ch32f1x_add_flash(target_s *const target, const target_addr_t addr, const size_t length)
{
	stm32_flash_s *const stm32_flash = calloc(1U, sizeof(*stm32_flash));
	if (!stm32_flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}
	target_flash_s *const flash = &stm32_flash->flash;

	stm32_flash->fpec_base = CH32_FPEC_BASE;

	flash->start = addr;
	flash->length = length;
	flash->blocksize = CH32F1X_FAST_MODE_PAGE_SIZE;
	flash->writesize = CH32F1X_FAST_MODE_PAGE_SIZE;
	flash->erased = 0xffU;
	flash->erase = ch32f1x_flash_fast_mode_erase;
	flash->mass_erase = stm32_flash_mass_erase;
	flash->write = ch32f1x_flash_fast_mode_write;
	flash->prepare = ch32_flash_prepare;
	flash->done = ch32_flash_done;

	target_add_flash(target, flash);
}

/* CH32FV2x/V3x flash routines */

/* FIXME: this function is very close to ch32f1x_flash_fast_mode_erase, the only exception is the magic sequence, optimize */
static bool ch32fv2x_v3x_flash_fast_mode_erase(
	target_flash_s *const flash, const target_addr_t addr, const size_t length)
{
	(void)length;

	stm32_flash_s *const stm32_flash = (stm32_flash_s *)flash;
	target_s *const target = flash->t;

	/* See §32.5.7 Main Memory Fast Erasure in CH32FV2x_V3xRM */

	/* Clear any previous operation status */
	stm32_flash_clear_status(target, stm32_flash->fpec_base, 0U);

	/* Write page address to address register */
	target_mem_write32(target, STM32_FLASH_AR(stm32_flash->fpec_base), addr);

	/* Start fast flash page erase instruction */
	target_mem_write32(target, STM32_FLASH_CR(stm32_flash->fpec_base), STM32_FLASH_CR_STRT | CH32_FLASH_CR_FTER);

	/* Wait for completion or an error, return false on error */
	return stm32_flash_busy_wait(target, stm32_flash->fpec_base, 0U, NULL);
}

/* CH32FV2x/V3x don't use a "buffer" for fast programming */
static bool ch32fv2x_v3x_flash_fast_mode_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	(void)len;

	stm32_flash_s *const stm32_flash = (stm32_flash_s *)flash;
	target_s *const target = flash->t;

	/* See §32.5.6 Main Memory Fast Programming in CH32FV2x_V3xRM */

	/* Clear any previous operation status */
	stm32_flash_clear_status(target, stm32_flash->fpec_base, 0U);

	/* Write data to the FLASH address in 32 bit writes, 64 times */
	for (size_t offset = 0; offset < 64U; offset++) {
		target_mem_write32(target, dest + (offset << 2U), *((const uint32_t *)src + offset));
		if (!stm32_flash_busy_wait(target, stm32_flash->fpec_base, 0U, NULL))
			return false;
	}

	/* Start fast mode flash programming instruction */
	target_mem_write32(
		target, STM32_FLASH_CR(stm32_flash->fpec_base), CH32FV2X_V3X_FLASH_CR_PGSTRT | CH32_FLASH_CR_FTPG);

	/* Wait for completion or an error, return false on error */
	return stm32_flash_busy_wait(target, stm32_flash->fpec_base, 0U, NULL);
}

void ch32fv2x_v3x_add_flash(target_s *const target, const target_addr_t addr, const size_t length)
{
	stm32_flash_s *const stm32_flash = calloc(1U, sizeof(*stm32_flash));
	if (!stm32_flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}
	target_flash_s *const flash = &stm32_flash->flash;

	stm32_flash->fpec_base = CH32_FPEC_BASE;

	flash->start = addr;
	flash->length = length;
	flash->blocksize = CH32FV2X_V3X_FAST_MODE_PAGE_SIZE;
	flash->writesize = CH32FV2X_V3X_FAST_MODE_PAGE_SIZE;
	flash->erased = 0xffU;
	flash->erase = ch32fv2x_v3x_flash_fast_mode_erase;
	flash->mass_erase = stm32_flash_mass_erase;
	flash->write = ch32fv2x_v3x_flash_fast_mode_write;
	flash->prepare = ch32_flash_prepare;
	flash->done = ch32_flash_done;

	target_add_flash(target, flash);
}
