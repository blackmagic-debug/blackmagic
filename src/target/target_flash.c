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

		const target_addr_t local_start_addr = addr & ~(flash->blocksize - 1U);
		const target_addr_t local_end_addr = local_start_addr + flash->blocksize;

		if (!flash_prepare(flash, FLASH_OPERATION_ERASE))
			return false;

		result &= flash->erase(flash, local_start_addr, flash->blocksize);
		if (!result) {
			DEBUG_ERROR("Erase failed at %" PRIx32 "\n", local_start_addr);
			break;
		}

		len -= MIN(local_end_addr - addr, len);
		addr = local_end_addr;
	}
	/* Issue flash done on last operation */
	result &= flash_done(active_flash);
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

static bool flash_buffered_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
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
		src += local_length;
		len -= local_length;
	}
	return result;
}

bool target_flash_complete(target_s *target)
{
	if (!target->flash_mode)
		return false;

	bool result = true; /* Catch false returns with &= */
	for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
		result &= flash_buffered_flush(flash);
		result &= flash_done(flash);
	}

	target_exit_flash_mode(target);
	return result;
}
