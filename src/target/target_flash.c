/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
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

/* This file implements target flash interaction routines.
 * Provides functionality for buffered flash operations
 * It depends on target flash implementations
 */

#include "general.h"
#include "target_internal.h"

static bool flash_done(target_flash_s *flash);

target_flash_s *target_flash_for_addr(target_s *target, uint32_t addr)
{
	for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
		if (flash->start <= addr && addr < flash->start + flash->length)
			return flash;
	}
	return NULL;
}

static bool target_enter_flash_mode(target_s *target)
{
	if (target->flash_mode)
		return true;

	bool result = true;
	if (target->enter_flash_mode)
		result = target->enter_flash_mode(target);
	else
		/* Reset target on flash command */
		/* This saves us if we're interrupted in IRQ context */
		target_reset(target);

	if (result == true)
		target->flash_mode = true;
	return result;
}

static bool target_exit_flash_mode(target_s *target)
{
	if (!target->flash_mode)
		return true;

	bool result = true;
	if (target->exit_flash_mode)
		result = target->exit_flash_mode(target);
	else
		/* Reset target to known state when done flashing */
		target_reset(target);

	target->flash_mode = false;
	return result;
}

static bool flash_prepare(target_flash_s *flash, flash_operation_e operation)
{
	/* Check if we're already prepared for this operation */
	if (flash->operation == operation)
		return true;

	bool result = true;
	/* Terminate any ongoing Flash operation */
	if (flash->operation != FLASH_OPERATION_NONE)
		result = flash_done(flash);

	/* If that succeeded, set up the new operating state */
	if (result) {
		flash->operation = operation;
		/* Prepare flash for operation, unless we failed to terminate the previous one */
		if (flash->prepare)
			result = flash->prepare(flash);

		/* If the preparation step failed, revert back to the post-done state */
		if (!result)
			flash->operation = FLASH_OPERATION_NONE;
	}

	return result;
}

static bool flash_done(target_flash_s *flash)
{
	/* Check if we're already done */
	if (flash->operation == FLASH_OPERATION_NONE)
		return true;

	bool result = true;
	/* Terminate flash operation */
	if (flash->done)
		result = flash->done(flash);

	/* Free the operation buffer */
	if (flash->buf) {
		free(flash->buf);
		flash->buf = NULL;
	}

	/* Mark the Flash as idle again */
	flash->operation = FLASH_OPERATION_NONE;
	return result;
}

bool target_flash_erase(target_s *target, target_addr_t addr, size_t len)
{
	if (!target_enter_flash_mode(target))
		return false;

	target_flash_s *active_flash = target_flash_for_addr(target, addr);
	if (!active_flash)
		return false;

	bool result = true; /* Catch false returns with &= */
	while (len) {
		target_flash_s *flash = target_flash_for_addr(target, addr);
		if (!flash) {
			DEBUG_ERROR("Requested address is outside the valid range 0x%06" PRIx32 "\n", addr);
			return false;
		}

		/* Terminate flash operations if we're not in the same target flash */
		if (flash != active_flash) {
			result &= flash_done(active_flash);
			active_flash = flash;
		}

		/* Align the start address to the erase block size */
		const target_addr_t local_start_addr = addr & ~(flash->blocksize - 1U);

		/* Check if we can use mass erase, i.e. if the erase range covers the entire flash address space */
		const bool can_use_mass_erase =
			flash->mass_erase != NULL && local_start_addr == flash->start && addr + len >= flash->start + flash->length;

		/* Calculate the address at the end of the erase block */
		const target_addr_t local_end_addr =
			can_use_mass_erase ? flash->start + flash->length : local_start_addr + flash->blocksize;

		if (!flash_prepare(flash, can_use_mass_erase ? FLASH_OPERATION_MASS_ERASE : FLASH_OPERATION_ERASE))
			return false;

		DEBUG_TARGET("%s: %08" PRIx32 "+%" PRIu32 "\n", __func__, local_start_addr, local_end_addr - local_start_addr);
		/* Erase flash, either a single aligned block size or a full mass erase */
		result &= can_use_mass_erase ? flash->mass_erase(flash, NULL) :
									   flash->erase(flash, local_start_addr, flash->blocksize);
		if (!result) {
			DEBUG_ERROR("Erase failed at %" PRIx32 "\n", local_start_addr);
			break;
		}

		/* Update the remaining length and address, taking into account the alignment */
		len -= MIN(local_end_addr - addr, len);
		addr = local_end_addr;
	}
	/* Issue flash done on last operation */
	result &= flash_done(active_flash);
	return result;
}

static inline bool flash_manual_mass_erase(target_flash_s *const flash, platform_timeout_s *const print_progess)
{
	for (target_addr_t addr = flash->start; addr < flash->start + flash->length; addr += flash->blocksize) {
		if (!flash->erase(flash, addr, flash->blocksize))
			return false;
		target_print_progress(print_progess);
	}
	return true;
}

/* Run specialized target mass erase if available, otherwise erase all flash' */
bool target_flash_mass_erase(target_s *const target)
{
	if (!target_enter_flash_mode(target))
		return false;

	/* Setup progress printout */
	platform_timeout_s print_progess;
	platform_timeout_set(&print_progess, 500U);

	bool result = false;
	if (target->mass_erase) {
		DEBUG_TARGET("Running specialized target mass erase\n");

		/* Run specialized target mass erase */
		result = target->mass_erase(target, &print_progess);
	} else {
		DEBUG_WARN("No specialized target mass erase available, erasing all flash\n");

		/* Erase all target flash */
		for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
			/* If the flash has a mass erase function, use it */
			const bool can_use_mass_erase = flash->mass_erase != NULL;

			if (can_use_mass_erase)
				DEBUG_TARGET("Running specialized flash mass erase for flash 0x%08" PRIx32 "\n", flash->start);
			else
				DEBUG_WARN("No specialized flash mass erase available for 0x%08" PRIx32 "\n", flash->start);

			result = flash_prepare(flash, can_use_mass_erase ? FLASH_OPERATION_MASS_ERASE : FLASH_OPERATION_ERASE);
			if (!result) {
				DEBUG_ERROR("Failed to prepare flash 0x%08" PRIx32 " for mass erase\n", flash->start);
				break;
			}

			result = can_use_mass_erase ? flash->mass_erase(flash, &print_progess) :
										  flash_manual_mass_erase(flash, &print_progess);
			result &= flash_done(flash); /* Don't overwrite previous result, AND with it instead */
			if (!result) {
				DEBUG_ERROR("Failed to mass erase flash 0x%08" PRIx32 "\n", flash->start);
				break;
			}
		}
	}

	target_exit_flash_mode(target);
	return result;
}

bool flash_buffer_alloc(target_flash_s *flash)
{
	/* Allocate buffer */
	flash->buf = malloc(flash->writebufsize);
	if (!flash->buf) { /* malloc failed: heap exhaustion */
		DEBUG_ERROR("malloc: failed in %s\n", __func__);
		return false;
	}
	flash->buf_addr_base = UINT32_MAX;
	flash->buf_addr_low = UINT32_MAX;
	flash->buf_addr_high = 0;
	return true;
}

static bool flash_buffered_flush(target_flash_s *flash)
{
	bool result = true; /* Catch false returns with &= */
	if (flash->buf && flash->buf_addr_base != UINT32_MAX && flash->buf_addr_low != UINT32_MAX &&
		flash->buf_addr_low < flash->buf_addr_high) {
		/* Write buffer to flash */

		if (!flash_prepare(flash, FLASH_OPERATION_WRITE))
			return false;

		const target_addr_t aligned_addr = flash->buf_addr_low & ~(flash->writesize - 1U);
		const uint8_t *src = flash->buf + (aligned_addr - flash->buf_addr_base);
		const uint32_t length = flash->buf_addr_high - aligned_addr;

		for (size_t offset = 0; offset < length; offset += flash->writesize)
			result &= flash->write(flash, aligned_addr + offset, src + offset, flash->writesize);

		flash->buf_addr_base = UINT32_MAX;
		flash->buf_addr_low = UINT32_MAX;
		flash->buf_addr_high = 0;
	}

	return result;
}

static bool flash_buffered_write(target_flash_s *flash, target_addr_t dest, const uint8_t *src, size_t len)
{
	bool result = true; /* Catch false returns with &= */
	while (len) {
		const target_addr_t base_addr = dest & ~(flash->writebufsize - 1U);

		/* Check for base address change */
		if (base_addr != flash->buf_addr_base) {
			result &= flash_buffered_flush(flash);

			/* Setup buffer */
			flash->buf_addr_base = base_addr;
			memset(flash->buf, flash->erased, flash->writebufsize);
		}

		const size_t offset = dest % flash->writebufsize;
		const size_t local_len = MIN(flash->writebufsize - offset, len);

		/* Copy chunk into sector buffer */
		memcpy(flash->buf + offset, src, local_len);

		/* This allows for writes smaller than writebufsize when flushing in the future */
		flash->buf_addr_low = MIN(flash->buf_addr_low, dest);
		flash->buf_addr_high = MAX(flash->buf_addr_high, dest + local_len);

		dest += local_len;
		src += local_len;
		len -= local_len;
	}
	return result;
}

bool target_flash_write(target_s *target, target_addr_t dest, const void *src, size_t len)
{
	if (!target_enter_flash_mode(target))
		return false;

	bool result = true; /* Catch false returns with &= */
	target_flash_s *active_flash = NULL;

	for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
		if (flash->start <= dest && dest < flash->start + flash->length)
			active_flash = flash;
		else if (flash->buf) {
			result &= flash_buffered_flush(flash);
			result &= flash_done(flash);
		}
	}
	if (!active_flash || !result)
		return false;

	while (len) {
		target_flash_s *flash = target_flash_for_addr(target, dest);
		if (!flash)
			return false;

		/* Terminate flash operations if we're not in the same target flash */
		if (flash != active_flash) {
			result &= flash_buffered_flush(active_flash);
			result &= flash_done(active_flash);
			active_flash = flash;
		}
		if (!flash->buf)
			result &= flash_buffer_alloc(flash);

		/* Early exit if any of the flushing and cleanup steps above failed */
		if (!result)
			return false;

		const target_addr_t local_end_addr = MIN(dest + len, flash->start + flash->length);
		const target_addr_t local_length = local_end_addr - dest;

		result &= flash_buffered_write(flash, dest, src, local_length);
		if (!result) {
			DEBUG_ERROR("Write failed at %" PRIx32 "\n", dest);
			return false;
		}

		dest = local_end_addr;
		src = (const uint8_t *)src + local_length;
		len -= local_length;
	}
	return result;
}

bool target_flash_complete(target_s *target)
{
	if (!target || !target->flash_mode)
		return false;

	bool result = true; /* Catch false returns with &= */
	for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
		result &= flash_buffered_flush(flash);
		result &= flash_done(flash);
	}

	target_exit_flash_mode(target);
	return result;
}

static bool flash_blank_check(target_flash_s *flash, target_addr_t src, size_t len, target_addr_t *mismatch)
{
	bool result = true; /* Catch false returns with &= */
	target_s *target = flash->t;
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);

	for (size_t offset = 0U; offset < len; offset += flash->writebufsize) {
		/* Fetch chunk into sector buffer */
		target_mem32_read(target, flash->buf, src + offset, flash->writebufsize);

		/* Compare bytewise with erased value */
		const uint8_t erased = flash->erased;
		for (size_t i = 0; i < flash->writebufsize; i++) {
			if (flash->buf[i] != erased) {
				*mismatch = src + i;
				return false;
			}
		}
		target_print_progress(&timeout);
	}
	return result;
}

bool target_flash_blank_check(target_s *target)
{
	if (!target->flash)
		return false;

	bool result = true;
	target_addr_t mismatch = 0;

	for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
		if (!flash->buf && !flash_buffer_alloc(flash))
			return false;

		const target_addr_t local_end = flash->start + flash->length;
		for (target_addr_t local_start = flash->start; local_start < local_end; local_start += flash->blocksize) {
			result = flash_blank_check(flash, local_start, flash->blocksize, &mismatch);
			if (!result)
				tc_printf(target, "Has data at 0x%08" PRIx32 "\n", mismatch);
			else
				tc_printf(target, "Blank 0x%08" PRIx32 "+%" PRIu32 "\n", local_start, (uint32_t)flash->blocksize);
		}
		/* Free the operation buffer */
		if (flash->buf) {
			free(flash->buf);
			flash->buf = NULL;
		}
	}

	return result;
}
