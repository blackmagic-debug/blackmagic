/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
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

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "target_probe.h"
#include "gdb_reg.h"
#include "exception.h"
#include "avr_pdi.h"

#include <assert.h>

#define IDCODE_XMEGA64A3U  0x9642U
#define IDCODE_XMEGA128A3U 0x9742U
#define IDCODE_XMEGA192A3U 0x9744U
#define IDCODE_XMEGA256A3U 0x9842U

#define ATXMEGA_DBG_BASE    0x00000000U
#define ATXMEGA_DBG_CTR     (ATXMEGA_DBG_BASE + 0x0U)
#define ATXMEGA_DBG_PC      (ATXMEGA_DBG_BASE + 0x4U)
#define ATXMEGA_DBG_CTRL    (ATXMEGA_DBG_BASE + 0xaU)
#define ATXMEGA_DBG_SPECIAL (ATXMEGA_DBG_BASE + 0xcU)

#define AVR_DBG_READ_REGS 0x11U
#define AVR_NUM_REGS      32

#define ATXMEGA_BRK_BASE     0x00000020U
#define ATXMEGA_BRK_COUNTER  0x00000028U
#define ATXMEGA_BRK_UNKNOWN1 0x00000040U
#define ATXMEGA_BRK_UNKNOWN2 0x00000046U
#define ATXMEGA_BRK_UNKNOWN3 0x00000048U

#define ATXMEGA_CPU_BASE 0x01000030U
/* Address of the low byte of the stack pointer */
#define ATXMEGA_CPU_SPL (ATXMEGA_CPU_BASE + 0xdU)
/* This is followed by the high byte and SREG */

#define ATXMEGA_NVM_BASE   0x010001c0U
#define ATXMEGA_NVM_DATA   (ATXMEGA_NVM_BASE + 0x4U)
#define ATXMEGA_NVM_CMD    (ATXMEGA_NVM_BASE + 0xaU)
#define ATXMEGA_NVM_STATUS (ATXMEGA_NVM_BASE + 0xfU)

#define ATXMEGA_NVM_CMD_NOP                0x00U
#define ATXMEGA_NVM_CMD_ERASE_FLASH_BUFFER 0x26U
#define ATXMEGA_NVM_CMD_WRITE_FLASH_BUFFER 0x23U
#define ATXMEGA_NVM_CMD_ERASE_FLASH_PAGE   0x2bU
#define ATXMEGA_NVM_CMD_WRITE_FLASH_PAGE   0x2eU
#define ATXMEGA_NVM_CMD_READ_NVM           0x43U

#define ATXMEGA_NVM_STATUS_BUSY  0x80U
#define ATXMEGA_NVM_STATUS_FBUSY 0x40U

/* Special-purpose register name strings */
static const char *const avr_spr_names[] = {
	"sreg",
	"sp",
	"pc",
};

/* Special-purpose register types */
static const gdb_reg_type_e avr_spr_types[] = {
	GDB_TYPE_UNSPECIFIED, /* sreg */
	GDB_TYPE_DATA_PTR,    /* sp */
	GDB_TYPE_CODE_PTR,    /* pc */
};

/* Special-purpose register bitsizes */
static const uint8_t avr_spr_bitsizes[] = {
	8,  /* sreg */
	16, /* sp */
	32, /* pc */
};

// clang-format off
static_assert(ARRAY_LENGTH(avr_spr_types) == ARRAY_LENGTH(avr_spr_names),
	"SPR array length mismatch! SPR type array should have the same length as SPR name array."
);

static_assert(ARRAY_LENGTH(avr_spr_bitsizes) == ARRAY_LENGTH(avr_spr_names),
	"SPR array length mismatch! SPR bitsize array should have the same length as SPR name array."
);
// clang-format on

static bool atxmega_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool atxmega_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool atxmega_flash_done(target_flash_s *flash);

static const char *atxmega_target_description(target_s *target);

static bool atxmega_check_error(target_s *target);
static void atxmega_halt_resume(target_s *target, bool step);

static void atxmega_regs_read(target_s *target, void *data);
static void atxmega_mem_read(target_s *target, void *dest, target_addr_t src, size_t len);

static bool atxmega_ensure_nvm_idle(const avr_pdi_s *pdi);
static bool atxmega_config_breakpoints(const avr_pdi_s *pdi, bool step);

void avr_add_flash(target_s *const target, const uint32_t start, const size_t length, const uint16_t block_size)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = start;
	flash->length = length;
	flash->blocksize = block_size;
	flash->erase = atxmega_flash_erase;
	flash->write = atxmega_flash_write;
	flash->done = atxmega_flash_done;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

bool atxmega_probe(target_s *const target)
{
	uint32_t application_flash = 0;
	uint32_t application_table_flash = 0;
	uint32_t bootloader_flash = 0;
	uint16_t flash_block_size = 0;
	uint32_t sram = 0;

	switch (target->part_id) {
	case IDCODE_XMEGA64A3U:
		/*
		 * The 64A3U has:
		 * 60kiB of normal Flash
		 * 4kiB of application table Flash
		 * 4kiB of bootloader Flash
		 * 16kiB of internal SRAM
		 */
		application_flash = 0xf000U;
		application_table_flash = 0x1000U;
		bootloader_flash = 0x1000U;
		flash_block_size = 128;
		target->core = "ATXMega64A3U";
		break;
	case IDCODE_XMEGA128A3U:
		/*
		 * The 128A3U has:
		 * 120kiB of normal Flash
		 * 8kiB of application table Flash
		 * 8kiB of bootloader Flash
		 * 16kiB of internal SRAM
		 */
		application_flash = 0x1e000U;
		application_table_flash = 0x2000U;
		bootloader_flash = 0x2000U;
		flash_block_size = 256;
		target->core = "ATXMega128A3U";
		break;
	case IDCODE_XMEGA192A3U:
		/*
		 * The 192A3U has:
		 * 184kiB of normal Flash
		 * 8kiB of application table Flash
		 * 8kiB of bootloader Flash
		 * 16kiB of internal SRAM
		 */
		application_flash = 0x2e000U;
		application_table_flash = 0x2000U;
		bootloader_flash = 0x2000U;
		flash_block_size = 256;
		target->core = "ATXMega192A3U";
		break;
	case IDCODE_XMEGA256A3U:
		/*
		 * The 256A3U has:
		 * 248kiB of normal Flash
		 * 8kiB of application table Flash
		 * 8kiB of bootloader Flash
		 * 16kiB of internal SRAM
		 */
		application_flash = 0x3e000U;
		application_table_flash = 0x2000U;
		bootloader_flash = 0x2000U;
		flash_block_size = 256;
		sram = 0x800U;
		target->core = "ATXMega256A3U";
		break;
	default:
		return false;
	}

	target->regs_description = atxmega_target_description;
	target->check_error = atxmega_check_error;
	target->halt_resume = atxmega_halt_resume;

	target->regs_read = atxmega_regs_read;
	target->mem_read = atxmega_mem_read;

	/*
	 * RAM is actually at 0x01002000 in the 24-bit linearised PDI address space however, because GDB/GCC,
	 * internally we have to map at 0x00800000 to get a suitable mapping for the host
	 */
	target_add_ram(target, 0x00802000U, sram);
	uint32_t flash_base_address = 0x00000000;
	avr_add_flash(target, flash_base_address, application_flash, flash_block_size);
	flash_base_address += application_flash;
	avr_add_flash(target, flash_base_address, application_table_flash, flash_block_size);
	flash_base_address += application_table_flash;
	avr_add_flash(target, flash_base_address, bootloader_flash, flash_block_size);

	avr_pdi_s *const pdi = avr_pdi_struct(target);
	pdi->ensure_nvm_idle = atxmega_ensure_nvm_idle;

	/* This is unfortunately hard-coded as we don't currently have a way to "learn" this from the target. */
	pdi->breakpoints_available = 2;
	return true;
}

static bool atxmega_ensure_nvm_idle(const avr_pdi_s *const pdi)
{
	return avr_pdi_write(pdi, PDI_DATA_8, ATXMEGA_NVM_CMD, ATXMEGA_NVM_CMD_NOP) &&
		avr_pdi_write(pdi, PDI_DATA_8, ATXMEGA_NVM_DATA, 0xffU);
}

static bool atxmega_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t len)
{
	const avr_pdi_s *const pdi = avr_pdi_struct(flash->t);
	for (size_t i = 0; i < len; i += flash->blocksize) {
		if (!avr_pdi_write(pdi, PDI_DATA_8, ATXMEGA_NVM_CMD, ATXMEGA_NVM_CMD_ERASE_FLASH_PAGE) ||
			!avr_pdi_write(pdi, PDI_DATA_8, (addr + i) | PDI_FLASH_OFFSET, 0x55U))
			return false;

		uint8_t status = 0;
		while (avr_pdi_read8(pdi, ATXMEGA_NVM_STATUS, &status) &&
			(status & (ATXMEGA_NVM_STATUS_BUSY | ATXMEGA_NVM_STATUS_FBUSY)) ==
				(ATXMEGA_NVM_STATUS_BUSY | ATXMEGA_NVM_STATUS_FBUSY))
			continue;

		/* Check if the read status failed */
		if (status & (ATXMEGA_NVM_STATUS_BUSY | ATXMEGA_NVM_STATUS_FBUSY))
			return false;
	}
	return true;
}

static bool atxmega_flash_write(
	target_flash_s *const flash, target_addr_t dest, const void *const src, const size_t len)
{
	const avr_pdi_s *const pdi = avr_pdi_struct(flash->t);
	const uint8_t *const buffer = (const uint8_t *)src;
	for (size_t i = 0; i < len; i += flash->blocksize) {
		const size_t amount = MIN(flash->blocksize, len - i);
		const uint32_t addr = (dest + i) | PDI_FLASH_OFFSET;
		if (!avr_pdi_write(pdi, PDI_DATA_8, ATXMEGA_NVM_CMD, ATXMEGA_NVM_CMD_WRITE_FLASH_BUFFER) ||
			!avr_pdi_write_ind(pdi, addr, PDI_MODE_IND_INCPTR, buffer + i, amount) ||
			!avr_pdi_write(pdi, PDI_DATA_8, ATXMEGA_NVM_CMD, ATXMEGA_NVM_CMD_WRITE_FLASH_PAGE) ||
			!avr_pdi_write(pdi, PDI_DATA_8, addr, 0xffU))
			return false;

		uint8_t status = 0;
		while (avr_pdi_read8(pdi, ATXMEGA_NVM_STATUS, &status) &&
			(status & (ATXMEGA_NVM_STATUS_BUSY | ATXMEGA_NVM_STATUS_FBUSY)) ==
				(ATXMEGA_NVM_STATUS_BUSY | ATXMEGA_NVM_STATUS_FBUSY))
			continue;

		/* Check if the read status failed */
		if (status & (ATXMEGA_NVM_STATUS_BUSY | ATXMEGA_NVM_STATUS_FBUSY))
			return false;
	}
	return true;
}

static bool atxmega_flash_done(target_flash_s *const flash)
{
	const avr_pdi_s *const pdi = avr_pdi_struct(flash->t);
	return atxmega_ensure_nvm_idle(pdi);
}

/*
 * This function creates the target description XML string for an ATXMega6 part.
 * This is done this way to decrease string duplication and thus code size, making it
 * unfortunately much less readable than the string literal it is equivilent to.
 *
 * This string it creates is the XML-equivalent to the following:
 * "<?xml version=\"1.0\"?>"
 * "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
 * "<target>"
 * "	<architecture>avr:106</architecture>"
 * "	<feature name=\"org.gnu.gdb.avr.cpu\">"
 * "		<reg name=\"r0\" bitsize=\"8\" regnum=\"0\"/>"
 * "		<reg name=\"r1\" bitsize=\"8\"/>"
 * "		<reg name=\"r2\" bitsize=\"8\"/>"
 * "		<reg name=\"r3\" bitsize=\"8\"/>"
 * "		<reg name=\"r4\" bitsize=\"8\"/>"
 * "		<reg name=\"r5\" bitsize=\"8\"/>"
 * "		<reg name=\"r6\" bitsize=\"8\"/>"
 * "		<reg name=\"r7\" bitsize=\"8\"/>"
 * "		<reg name=\"r8\" bitsize=\"8\"/>"
 * "		<reg name=\"r9\" bitsize=\"8\"/>"
 * "		<reg name=\"r10\" bitsize=\"8\"/>"
 * "		<reg name=\"r11\" bitsize=\"8\"/>"
 * "		<reg name=\"r12\" bitsize=\"8\"/>"
 * "		<reg name=\"r13\" bitsize=\"8\"/>"
 * "		<reg name=\"r14\" bitsize=\"8\"/>"
 * "		<reg name=\"r15\" bitsize=\"8\"/>"
 * "		<reg name=\"r16\" bitsize=\"8\"/>"
 * "		<reg name=\"r17\" bitsize=\"8\"/>"
 * "		<reg name=\"r18\" bitsize=\"8\"/>"
 * "		<reg name=\"r19\" bitsize=\"8\"/>"
 * "		<reg name=\"r20\" bitsize=\"8\"/>"
 * "		<reg name=\"r21\" bitsize=\"8\"/>"
 * "		<reg name=\"r22\" bitsize=\"8\"/>"
 * "		<reg name=\"r23\" bitsize=\"8\"/>"
 * "		<reg name=\"r24\" bitsize=\"8\"/>"
 * "		<reg name=\"r25\" bitsize=\"8\"/>"
 * "		<reg name=\"r26\" bitsize=\"8\"/>"
 * "		<reg name=\"r27\" bitsize=\"8\"/>"
 * "		<reg name=\"r28\" bitsize=\"8\"/>"
 * "		<reg name=\"r29\" bitsize=\"8\"/>"
 * "		<reg name=\"r30\" bitsize=\"8\"/>"
 * "		<reg name=\"r31\" bitsize=\"8\"/>"
 * "		<reg name=\"sreg\" bitsize=\"8\"/>"
 * "		<reg name=\"sp\" bitsize=\"16\" type=\"data_ptr\"/>"
 * "		<reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>"
 * "	</feature>"
 * "</target>"
 */
static size_t atxmega_build_target_description(char *buffer, size_t max_len)
{
	size_t print_size = max_len;
	/* Start with the "preamble" chunks, which are mostly common across targets save for 2 words. */
	int offset = snprintf(buffer, print_size, "%s target %savr:106%s <feature name=\"org.gnu.gdb.avr.cpu\">",
		gdb_xml_preamble_first, gdb_xml_preamble_second, gdb_xml_preamble_third);

	/* Then build the general purpose register descriptions which have names r0 through r31 and the same bitsize */
	for (uint8_t i = 0; i < 32; ++i) {
		if (max_len != 0)
			print_size = max_len - (size_t)offset;
		offset += snprintf(
			buffer + offset, print_size, "<reg name=\"r%u\" bitsize=\"8\"%s/>", i, i == 0 ? " regnum=\"0\"" : "");
	}

	/* Then finally build the special-purpose register descriptions using the arrays at top of file. */
	for (size_t i = 0; i < ARRAY_LENGTH(avr_spr_names); ++i) {
		if (max_len != 0)
			print_size = max_len - (size_t)offset;

		const char *const name = avr_spr_names[i];
		const uint8_t bitsize = avr_spr_bitsizes[i];
		const gdb_reg_type_e type = avr_spr_types[i];

		offset += snprintf(buffer + offset, print_size, "<reg name=\"%s\" bitsize=\"%u\"%s/>", name, bitsize,
			gdb_reg_type_strings[type]);
	}

	/* Add the closing tags required */
	if (max_len != 0)
		print_size = max_len - (size_t)offset;

	offset += snprintf(buffer + offset, print_size, "</feature></target>");
	/* offset is now the total length of the string created, discard the sign and return it. */
	return (size_t)offset;
}

static const char *atxmega_target_description(target_s *const target)
{
	(void)target;
	const size_t description_length = atxmega_build_target_description(NULL, 0) + 1U;
	char *const description = malloc(description_length);
	if (description)
		atxmega_build_target_description(description, description_length);
	return description;
}

static bool atxmega_check_error(target_s *const target)
{
	const avr_pdi_s *const pdi = avr_pdi_struct(target);
	return pdi->error_state != pdi_ok;
}

static void atxmega_mem_read(target_s *const target, void *const dest, const target_addr_t src, const size_t len)
{
	avr_pdi_s *const pdi = avr_pdi_struct(target);
	const target_addr_t translated_src = src + PDI_FLASH_OFFSET;
	if (target_flash_for_addr(target, src)) {
		if (!avr_pdi_write(pdi, PDI_DATA_8, ATXMEGA_NVM_CMD, ATXMEGA_NVM_CMD_READ_NVM) ||
			!avr_pdi_read_ind(pdi, translated_src, PDI_MODE_IND_INCPTR, dest, len) || !atxmega_ensure_nvm_idle(pdi))
			pdi->error_state = pdi_failure;
	} else if (translated_src < PDI_FLASH_OFFSET) {
		if (!avr_pdi_read_ind(pdi, translated_src, PDI_MODE_IND_INCPTR, dest, len))
			pdi->error_state = pdi_failure;
	}
}

static void atxmega_regs_read(target_s *const target, void *const data)
{
	avr_pdi_s *const pdi = avr_pdi_struct(target);
	avr_regs_s *regs = (avr_regs_s *)data;
	uint8_t status[3];
	uint32_t program_counter = 0;
	if (!avr_pdi_read32(pdi, ATXMEGA_DBG_PC, &program_counter) ||
		!avr_pdi_read_ind(pdi, ATXMEGA_CPU_SPL, PDI_MODE_IND_INCPTR, status, 3) ||
		!avr_pdi_write(pdi, PDI_DATA_32, ATXMEGA_DBG_PC, 0) ||
		!avr_pdi_write(pdi, PDI_DATA_32, ATXMEGA_DBG_CTR, AVR_NUM_REGS) ||
		!avr_pdi_write(pdi, PDI_DATA_8, ATXMEGA_DBG_CTRL, AVR_DBG_READ_REGS) ||
		!avr_pdi_reg_write(pdi, PDI_REG_R4, 1) ||
		!avr_pdi_read_ind(pdi, ATXMEGA_DBG_SPECIAL, PDI_MODE_IND_PTR, regs->general, 32) ||
		avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U)
		raise_exception(EXCEPTION_ERROR, "Error reading registers");
	/* Store the newly read program counter */
	pdi->program_counter = program_counter - 1U;
	/*
	 * These aren't in the reads above because regs is a packed struct, which results in compiler errors.
	 * Additionally, the program counter is stored in words and points to the next instruction to be executed
	 * so we have to adjust it by 1 and make it bytes.
	 */
	regs->pc = pdi->program_counter << 1U;
	regs->sp = status[0] | ((uint16_t)status[1] << 8);
	regs->sreg = status[2];
}

static bool atxmega_config_breakpoints(const avr_pdi_s *const pdi, const bool step)
{
	uint8_t breakpoint_count = 0U;
	if (step) {
		/* If we are single stepping, clear all enabled breakpoints */
		for (uint8_t idx = 0; idx < pdi->breakpoints_available; ++idx) {
			if (!avr_pdi_write(pdi, PDI_DATA_32, ATXMEGA_BRK_BASE + (idx * 4U), 0U))
				return false;
		}
	} else {
		/* We are not single stepping, so configure the breakpoints as defined in the PDI structure */
		for (uint8_t idx = 0; idx < pdi->breakpoints_available; ++idx) {
			const uint32_t breakpoint = pdi->breakpoints[idx];
			/* If the breakpoint is enabled, increment breakpoint_count */
			if (breakpoint & AVR_BREAKPOINT_ENABLED)
				++breakpoint_count;
			/* Try to write the address of the breakpoint */
			/* XXX: Need to first collect all the breakpoints on the stack, then write all of them used first */
			if (!avr_pdi_write(pdi, PDI_DATA_32, ATXMEGA_BRK_BASE + (idx * 4U), breakpoint & AVR_BREAKPOINT_MASK))
				return false;
		}
	}
	/* Tell the breakpoint unit how many breakpoints are enabled */
	return avr_pdi_write(pdi, PDI_DATA_8, ATXMEGA_BRK_UNKNOWN1, 0) &&
		avr_pdi_write(pdi, PDI_DATA_8, ATXMEGA_BRK_UNKNOWN2, 0) &&
		avr_pdi_write(pdi, PDI_DATA_16, ATXMEGA_BRK_COUNTER, (uint16_t)breakpoint_count << 8U) &&
		avr_pdi_write(pdi, PDI_DATA_8, ATXMEGA_BRK_UNKNOWN3, 0);
}

static void atxmega_halt_resume(target_s *const target, const bool step)
{
	avr_pdi_s *const pdi = avr_pdi_struct(target);
	if (step) {
		const uint32_t current_pc = pdi->program_counter;
		const uint32_t next_pc = current_pc + 1U;
		/*
		 * To do a single step, we run the following steps:
		 * Write the debug control register to 4, which puts the processor in a temporary breakpoint mode
		 * Write the debug counter register with the address to stop execution on
		 * Write the program counter with the address to resume execution on
		 */
		/* Check that we are in administrative halt */
		if (avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U ||
			/* Try to configure (clear) the breakpoints */
			!atxmega_config_breakpoints(pdi, step) || avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U ||
			/* Configure the debug controller */
			!avr_pdi_write(pdi, PDI_DATA_8, ATXMEGA_DBG_CTRL, 4U) ||
			!avr_pdi_write(pdi, PDI_DATA_32, ATXMEGA_DBG_CTR, next_pc) ||
			!avr_pdi_write(pdi, PDI_DATA_32, ATXMEGA_DBG_PC, current_pc) ||
			avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U ||
			/* And try to execute the request*/
			!avr_pdi_reg_write(pdi, PDI_REG_R4, 1U))
			raise_exception(EXCEPTION_ERROR, "Error stepping device, device in incorrect state");
		/* Then spin waiting to see the processor stop back in administrative halt */
		while (avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U)
			continue;
		pdi->halt_reason = TARGET_HALT_STEPPING;
	} else {
		/*
		 * To resume the processor we go through the following specific steps:
		 * Write the program counter to ensure we start where we expect
		 * Then we release the externally (PDI) applied reset
		 * We then poke the debug control register to indicate debug-supervised run
		 * Ensure that PDI is still in debug mode (r4 = 1)
		 * Read r3 to see that the processor is resuming
		 */
		/* Check that we are in administrative halt */
		if (avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U ||
			/* Try to configure the breakpoints */
			!atxmega_config_breakpoints(pdi, step) || avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U ||
			/* Set the program counter and release reset */
			!avr_pdi_write(pdi, PDI_DATA_32, ATXMEGA_DBG_PC, pdi->program_counter) ||
			!avr_pdi_reg_write(pdi, PDI_REG_RESET, 0U) ||
			/* Configure the debug controller */
			!avr_pdi_write(pdi, PDI_DATA_8, ATXMEGA_DBG_CTRL, 0U) || avr_pdi_reg_read(pdi, PDI_REG_R3) != 0x04U ||
			/* And try to execute the request */
			!avr_pdi_reg_write(pdi, PDI_REG_R4, 1U))
			raise_exception(EXCEPTION_ERROR, "Error resuming device, device in incorrect state");
		pdi->halt_reason = TARGET_HALT_RUNNING;
	}
}
