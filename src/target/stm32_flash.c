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

#include "stm32_flash.h"
#include "cortexm.h"

/* Code flash routines */

bool stm32_flash_locked(target_s *const target, const uint32_t fpec_base, const uint8_t bank)
{
	const uint32_t ctrl = target_mem_read32(target, STM32_FLASH_CR_BANK(fpec_base, bank));
	return ctrl & STM32_FLASH_CR_LOCK;
}

bool stm32_flash_unlock(target_s *const target, const uint32_t fpec_base, const uint8_t bank)
{
	target_mem_write32(target, STM32_FLASH_KEYR_BANK(fpec_base, bank), STM32_FLASH_KEY1);
	target_mem_write32(target, STM32_FLASH_KEYR_BANK(fpec_base, bank), STM32_FLASH_KEY2);

	/* Check that the bank is unlocked */
	if (stm32_flash_locked(target, fpec_base, bank)) {
		DEBUG_ERROR("stm32 flash unlock failed\n");
		return false;
	}
	return true;
}

void stm32_flash_lock(target_s *const target, const uint32_t fpec_base, const uint8_t bank)
{
	const uint32_t ctrl = target_mem_read32(target, STM32_FLASH_CR_BANK(fpec_base, bank));
	/* Retain the EOPIE, ERRIE and OPTWRE bits, and set the LOCK bit */
	target_mem_write32(target, STM32_FLASH_CR_BANK(fpec_base, bank),
		(ctrl & (STM32_FLASH_CR_EOPIE | STM32_FLASH_CR_ERRIE | STM32_FLASH_CR_OPTWRE)) | STM32_FLASH_CR_LOCK);
}

void stm32_flash_clear_status(target_s *const target, const uint32_t fpec_base, const uint8_t bank)
{
	/* EOP, WRPRTERR and PGERR are reset by writing 1 */
	target_mem_write32(target, STM32_FLASH_SR_BANK(fpec_base, bank),
		STM32_FLASH_SR_EOP | STM32_FLASH_SR_PGERR | STM32_FLASH_SR_WRPRTERR);
}

bool stm32_flash_busy_wait(
	target_s *const target, const uint32_t fpec_base, const uint8_t bank, platform_timeout_s *const print_progess)
{
	/* Read FLASH_SR to poll for BSY bit */
	uint32_t status = STM32_FLASH_SR_BSY;
	/*
	 * Note that checking EOP here is only legal ifevery operation is preceded by a call to
	 * stm32_flash_clear_status, without it the flag could be set from a previous operation.
	 * 
	 * See §3.4 Flash status register (FLASH_SR) in PM0068/PM0075 Programming manual
	 */
	while (!(status & STM32_FLASH_SR_EOP) && (status & STM32_FLASH_SR_BSY)) {
		status = target_mem_read32(target, STM32_FLASH_SR_BANK(fpec_base, bank));
		if (target_check_error(target)) {
			DEBUG_ERROR("Lost communications with target");
			return false;
		}
		if (print_progess)
			target_print_progress(print_progess);
	};

	/* Check for errors */
	const uint32_t error = status & (STM32_FLASH_SR_PGERR | STM32_FLASH_SR_WRPRTERR);
	if (error)
		DEBUG_ERROR("stm32 flash error 0x%" PRIx32 "\n", error);
	return error == 0U;
}

static bool stm32_flash_prepare(target_flash_s *const flash)
{
	stm32_flash_s *const stm32_flash = (stm32_flash_s *)flash;
	target_s *const target = flash->t;

	/* Unlock the flash bank if required */
	if (stm32_flash_locked(target, stm32_flash->fpec_base, stm32_flash->bank) &&
		!stm32_flash_unlock(target, stm32_flash->fpec_base, stm32_flash->bank))
		return false;

	/* Ensure no operation is ongoing */
	if (target_mem_read32(target, STM32_FLASH_SR_BANK(stm32_flash->fpec_base, stm32_flash->bank)) &
		STM32_FLASH_SR_BSY) {
		DEBUG_ERROR("stm32 flash unexpectedly busy\n");
		return false; /* FIXME: should we re-lock here? */
	}

	/* Clear any previous operation status */
	stm32_flash_clear_status(target, stm32_flash->fpec_base, stm32_flash->bank);

	/* Set the instruction to the control register */
	uint32_t ctrl_instruction = 0U;
	switch (flash->operation) {
	case FLASH_OPERATION_WRITE:
		/* Set flash programming instruction */
		ctrl_instruction = STM32_FLASH_CR_PG;
		break;
	case FLASH_OPERATION_ERASE:
		/* Set flash page erase instruction */
		ctrl_instruction = STM32_FLASH_CR_PER_SER;
		break;
	case FLASH_OPERATION_MASS_ERASE:
		/* Set flash bank mass erase instruction */
		ctrl_instruction = STM32_FLASH_CR_MER;
		break;
	default:
		return false; /* Unsupported operation */
	}
	/*
	 * This will clear EOPIE, ERRIE and OPTWRE, but we don't care about them and expect them cleared anyway
	 * note that we don't read-modify-write the control register after this, meaning we need to set the instruction
	 * allways, this is to avoid the extra overhead of reading the register since we know what bits should be set.
	 */
	target_mem_write32(target, STM32_FLASH_CR_BANK(stm32_flash->fpec_base, stm32_flash->bank), ctrl_instruction);

	return true;
}

static bool stm32_flash_done(target_flash_s *const flash)
{
	stm32_flash_s *const stm32_flash = (stm32_flash_s *)flash;
	target_s *const target = flash->t;

	/* Lock the flash bank */
	stm32_flash_lock(target, stm32_flash->fpec_base, stm32_flash->bank);

	return true;
}

static bool stm32_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t length)
{
	(void)length;

	stm32_flash_s *const stm32_flash = (stm32_flash_s *)flash;
	target_s *const target = flash->t;

	/* See §2.3.4 Flash memory erase in PM0068/PM0075 Programming manual */

	/* Clear any previous operation status */
	stm32_flash_clear_status(target, stm32_flash->fpec_base, stm32_flash->bank);

	/* Write page address to address register */
	target_mem_write32(target, STM32_FLASH_AR_BANK(stm32_flash->fpec_base, stm32_flash->bank), addr);

	/* Start flash page erase instruction */
	target_mem_write32(target, STM32_FLASH_CR_BANK(stm32_flash->fpec_base, stm32_flash->bank),
		STM32_FLASH_CR_STRT | STM32_FLASH_CR_PER_SER);

	/* Wait for completion or an error, return false on error */
	return stm32_flash_busy_wait(target, stm32_flash->fpec_base, stm32_flash->bank, NULL);
}

static bool stm32_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	(void)len;

	stm32_flash_s *const stm32_flash = (stm32_flash_s *)flash;
	target_s *const target = flash->t;

	/* See §2.3.3 Main Flash memory programming in PM0068/PM0075 Programming manual */

	/* Clear any previous operation status */
	stm32_flash_clear_status(target, stm32_flash->fpec_base, stm32_flash->bank);

	/* 
	 * The operatio is started on a half-word write into a main Flash memory address.
	 * Any attempt to write data that are not half-word long will result in a bus error response from the FPEC.
	 */
	target_mem_write16(
		target, dest, *(const uint16_t *)src); // target_mem_write_aligned(target, dest, src, len, ALIGN_HALFWORD);

	return stm32_flash_busy_wait(target, stm32_flash->fpec_base, stm32_flash->bank, NULL);
}

bool stm32_flash_mass_erase(target_flash_s *const flash, platform_timeout_s *const print_progess)
{
	stm32_flash_s *const stm32_flash = (stm32_flash_s *)flash;
	target_s *const target = flash->t;

	/* Start flash bank mass erase instruction */
	target_mem_write32(target, STM32_FLASH_CR_BANK(stm32_flash->fpec_base, stm32_flash->bank),
		STM32_FLASH_CR_STRT | STM32_FLASH_CR_MER);

	/* Wait for completion or an error, return false on error */
	return stm32_flash_busy_wait(target, stm32_flash->fpec_base, stm32_flash->bank, print_progess);
}

static void stm32_add_flash_bank(target_s *const target, target_flash_s *const flash, const target_addr_t addr,
	const size_t length, const size_t block_size)
{
	flash->start = addr;
	flash->length = length;
	flash->blocksize = block_size;
	flash->writesize = 2U; /* The smallest write size is 16 bits, in the interest of speed we might want to bump this */
	flash->erased = 0xffU;
	flash->erase = stm32_flash_erase;
	flash->mass_erase = stm32_flash_mass_erase;
	flash->write = stm32_flash_write;
	flash->prepare = stm32_flash_prepare;
	flash->done = stm32_flash_done;

	target_add_flash(target, flash);
}

void stm32_add_flash(target_s *target, target_addr_t addr, size_t length, uint32_t fpec_base, size_t block_size)
{
	stm32_flash_s *const stm32_flash = calloc(1U, sizeof(*stm32_flash));
	if (!stm32_flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}
	stm32_flash->fpec_base = fpec_base;

	stm32_add_flash_bank(target, &stm32_flash->flash, addr, length, block_size);
}

void stm32_add_banked_flash(target_s *target, target_addr_t addr, size_t length, target_addr_t bank_split_addr,
	uint32_t fpec_base, size_t block_size)
{
	/* Allocate two sequential flash structures, one for each bank */
	stm32_flash_s *const stm32_flash = calloc(2U, sizeof(*stm32_flash));
	if (!stm32_flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	/* Add the two flash banks */
	for (size_t bank = 0; bank < 2U; bank++) {
		stm32_flash[bank].fpec_base = fpec_base;
		stm32_flash[bank].bank = bank;

		const target_addr_t bank_addr = bank == 0U ? addr : bank_split_addr;
		const size_t bank_length = bank == 0U ? bank_split_addr - addr : length - (bank_split_addr - addr);

		stm32_add_flash_bank(target, &stm32_flash[bank].flash, bank_addr, bank_length, block_size);
	}
}

/* Option byte flash routines */

bool stm32_option_bytes_locked(target_s *const target, const uint32_t fpec_base)
{
	const uint32_t ctrl = target_mem_read32(target, STM32_FLASH_CR(fpec_base));
	return !(ctrl & STM32_FLASH_CR_OPTWRE);
}

bool stm32_option_bytes_unlock(target_s *const target, const uint32_t fpec_base)
{
	target_mem_write32(target, STM32_FLASH_OPTKEYR(fpec_base), STM32_FLASH_KEY1);
	target_mem_write32(target, STM32_FLASH_OPTKEYR(fpec_base), STM32_FLASH_KEY2);

	/* Check that the bank is unlocked */
	if (stm32_option_bytes_locked(target, fpec_base)) {
		DEBUG_ERROR("stm32 option bytes unlock failed\n");
		return false;
	}
	return true;
}

static bool stm32_option_bytes_erase(target_s *const target, const uint32_t fpec_base)
{
	/* Clear any previous operation status */
	stm32_flash_clear_status(target, fpec_base, 0U);

	/* Set option byte erase instruction */
	target_mem_write32(target, STM32_FLASH_CR(fpec_base), STM32_FLASH_CR_OPTWRE | STM32_FLASH_CR_OPTER);

	/* Start option byte erase instruction */
	target_mem_write32(
		target, STM32_FLASH_CR(fpec_base), STM32_FLASH_CR_OPTWRE | STM32_FLASH_CR_OPTER | STM32_FLASH_CR_STRT);

	/* Wait for completion or an error, return false on error */
	return stm32_flash_busy_wait(target, fpec_base, 0U, NULL);
}

static bool stm32_option_bytes_write(
	target_s *const target, const uint32_t fpec_base, const size_t offset, const uint16_t value)
{
	if (value == 0xffffU)
		return true;

	/* Clear any previous operation status */
	stm32_flash_clear_status(target, fpec_base, 0U);

	/* Set option byte programming instruction */
	target_mem_write32(target, STM32_FLASH_CR(fpec_base), STM32_FLASH_CR_OPTWRE | STM32_FLASH_CR_OPTPG);

	const uint32_t addr = STM32_FLASH_OPT_ADDR + (offset * 2U);

	/*
	 * Write changed values, taking into account if we can use 32- or have to use 16-bit writes.
	 * GD32E230 is a special case as target_mem_write16 does not work
	 */
	const bool write16_broken = target->part_id == 0x410U && (target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M23;
	if (write16_broken)
		target_mem_write32(target, addr, 0xffff0000U | value);
	else
		target_mem_write16(target, addr, value);

	/* Wait for completion or an error, return false on error */
	const bool result = stm32_flash_busy_wait(target, fpec_base, 0U, NULL);
	if (offset != 0U || result)
		return result;
	/*
	 * FIXME: I don't follow the explanation here, clarify
	 * In the case that the write failed and we're handling option byte 0 (RDP),
	 * check if we got a status of "Program Error" in FLASH_SR, indicating the target
	 * refused to erase the read protection option bytes (and turn it into a truthy return).
	 */
	const bool programming_error = !!(target_mem_read32(target, STM32_FLASH_SR(fpec_base)) & STM32_FLASH_SR_PGERR);
	return programming_error;
}

static bool stm32_option_bytes_read_modify_write(
	target_s *const target, const uint32_t fpec_base, const uint32_t addr, const uint16_t value)
{
	const uint32_t index = (addr - STM32_FLASH_OPT_ADDR) >> 1U;
	/* If index would be negative, the high most bit is set, so we get a giant positive number. */
	if (index > 7U)
		return false;

	uint16_t option_bytes[8U];

	/* Retrieve old values */
	target_mem_read(target, option_bytes, STM32_FLASH_OPT_ADDR, sizeof(option_bytes));

	if (option_bytes[index] == value)
		return true;

	/* Check for erased value */
	if (option_bytes[index] != 0xffffU && !stm32_option_bytes_erase(target, fpec_base))
		return false;

	option_bytes[index] = value;

	/* Write the modified option bytes */
	for (size_t byte_index = 0U; byte_index < 8U; ++byte_index) {
		if (!stm32_option_bytes_write(target, fpec_base, byte_index, option_bytes[byte_index]))
			return false;
	}

	return true;
}

static inline uint16_t stm32_option_bytes_read_protect_key(const target_s *const target)
{
	/* FIXME: there must be a better way */
	switch (target->part_id) {
	case 0x422U: /* STM32F30x */
	case 0x432U: /* STM32F37x */
	case 0x438U: /* STM32F303x6/8 and STM32F328 */
	case 0x440U: /* STM32F0 */
	case 0x446U: /* STM32F303xD/E and STM32F398xE */
	case 0x445U: /* STM32F04 RM0091 Rev.7, STM32F070x6 RM0360 Rev. 4*/
	case 0x448U: /* STM32F07 RM0091 Rev.7, STM32F070xb RM0360 Rev. 4*/
	case 0x442U: /* STM32F09 RM0091 Rev.7, STM32F030xc RM0360 Rev. 4*/
		return STM32F3X_FLASH_RDPRT;
	}
	return STM32F10X_FLASH_RDPRT;
}

bool stm32_option_bytes_cmd(target_s *const target, int argc, const char **argv)
{
	const uint32_t fpec_base = ((stm32_flash_s *)target->flash)->fpec_base; /* Get the FPEC base from a target flash */

	/* Fast-exit if the Flash is not readable and the user didn't ask us to erase the option bytes */
	const bool erase_requested = argc == 2 && strcmp(argv[1], "erase") == 0;
	if (!erase_requested) {
		const bool read_protected =
			!!(target_mem_read32(target, STM32_FLASH_OBR(fpec_base)) & STM32F10X_FLASH_OBR_RDPRT);

		if (read_protected) {
			tc_printf(target, "Device is Read Protected\nUse `monitor option erase` to unprotect and erase device\n");
			return true;
		}
	}

	/* Unlock the flash if required */
	if (stm32_flash_locked(target, fpec_base, 0U) && !stm32_flash_unlock(target, fpec_base, 0U))
		return false;

	/* Unlock the option bytes if required */
	if (stm32_option_bytes_locked(target, fpec_base) && !stm32_option_bytes_unlock(target, fpec_base))
		return false;

	if (erase_requested) {
		/* When the user asks us to erase the option bytes, kick of an erase */
		if (!stm32_option_bytes_erase(target, fpec_base))
			return false;

		/* Write the option bytes Flash readable key */
		if (!stm32_option_bytes_write(target, fpec_base, 0U, stm32_option_bytes_read_protect_key(target)))
			return false;
	} else if (argc == 3) {
		/* If 3 arguments are given, assume the second is an address, and the third a value */
		const uint32_t addr = strtoul(argv[1], NULL, 0);
		const uint32_t value = strtoul(argv[2], NULL, 0);

		/* Try and program the new option value to the requested option byte */
		if (!stm32_option_bytes_read_modify_write(target, fpec_base, addr, value))
			return false;
	} else
		tc_printf(target, "usage: monitor option erase\nusage: monitor option <addr> <value>\n");

	/* When all gets said and done, display the current option bytes values */
	for (size_t offset = 0U; offset < 16U; offset += 4U) {
		const uint32_t addr = STM32_FLASH_OPT_ADDR + offset;
		const uint32_t val = target_mem_read32(target, addr);
		tc_printf(target, "0x%08X: 0x%04X\n", addr, val & 0xffffU);
		tc_printf(target, "0x%08X: 0x%04X\n", addr + 2U, val >> 16U);
	}

	return true;
}
