/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015 Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "lpc_common.h"

#include <stdarg.h>

typedef struct iap_config {
	uint32_t command;
	uint32_t params[4];
} iap_config_s;

typedef struct __attribute__((aligned(4))) iap_frame {
	/* The start of an IAP stack frame is the opcode we set as the return point. */
	uint16_t opcode;
	/* There's then a hidden alignment field here, followed by the IAP call setup */
	iap_config_s config;
	iap_result_s result;
} iap_frame_s;

#if defined(ENABLE_DEBUG)
static const char *const iap_error[] = {
	"CMD_SUCCESS",
	"Invalid command",
	"Unaligned src address",
	"Dst address not on boundary",
	"Src not mapped",
	"Dst not mapped",
	"Invalid byte count",
	"Invalid sector",
	"Sector not blank",
	"Sector not prepared",
	"Compare error",
	"Flash interface busy",
	"Invalid or missing parameter",
	"Address not on boundary",
	"Address not mapped",
	"Checksum error",
	"16",
	"17",
	"18",
	"19",
	"20",
	"21",
	"22",
	"FRO not powered",
	"Flash not powered",
	"25",
	"26",
	"Flash clock disabled",
	"Reinvoke error",
	"Invalid image",
	"30",
	"31",
	"Flash erase failed",
	"Page is invalid",
};
#endif

static bool lpc_flash_write(target_flash_s *tf, target_addr_t dest, const void *src, size_t len);

lpc_flash_s *lpc_add_flash(
	target_s *const target, const target_addr_t addr, const size_t length, const size_t write_size)
{
	lpc_flash_s *const lpc_flash = calloc(1, sizeof(*lpc_flash));
	if (!lpc_flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return NULL;
	}

	target_flash_s *const flash = &lpc_flash->f;
	flash->start = addr;
	flash->length = length;
	flash->erase = lpc_flash_erase;
	flash->write = lpc_flash_write;
	flash->erased = 0xff;
	flash->writesize = write_size;
	target_add_flash(target, flash);
	return lpc_flash;
}

static uint8_t lpc_sector_for_addr(lpc_flash_s *f, uint32_t addr)
{
	return f->base_sector + (addr - f->f.start) / f->f.blocksize;
}

static inline bool lpc_is_full_erase(lpc_flash_s *f, const uint32_t begin, const uint32_t end)
{
	const target_addr_t addr = f->f.start;
	const size_t len = f->f.length;
	return begin == lpc_sector_for_addr(f, addr) && end == lpc_sector_for_addr(f, addr + len - 1U);
}

void lpc_save_state(target_s *const target, const uint32_t iap_ram, iap_frame_s *const frame, uint32_t *const regs)
{
	/* Save IAP RAM to restore after IAP call */
	target_mem_read(target, frame, iap_ram, sizeof(iap_frame_s));
	/* Save registers to restore after IAP call */
	target_regs_read(target, regs);
}

void lpc_restore_state(
	target_s *const target, const uint32_t iap_ram, const iap_frame_s *const frame, const uint32_t *const regs)
{
	target_mem_write(target, iap_ram, frame, sizeof(iap_frame_s));
	target_regs_write(target, regs);
}

static size_t lpc_iap_params(const iap_cmd_e cmd)
{
	switch (cmd) {
	case IAP_CMD_PREPARE:
	case IAP_CMD_BLANKCHECK:
		return 3U;
	case IAP_CMD_ERASE:
	case IAP_CMD_ERASE_PAGE:
	case IAP_CMD_PROGRAM:
		return 4U;
	case IAP_CMD_SET_ACTIVE_BANK:
		return 2U;
	default:
		return 0U;
	}
}

iap_status_e lpc_iap_call(lpc_flash_s *const flash, iap_result_s *const result, iap_cmd_e cmd, ...)
{
	target_s *const target = flash->f.t;

	/* Poke the WDT before each IAP call, if it is on */
	if (flash->wdt_kick)
		flash->wdt_kick(target);

	/* Save IAP RAM and target regsiters to restore after IAP call */
	iap_frame_s saved_frame;
	/*
	 * Note, we allocate space for the float regs even if the CPU doesn't implement them.
	 * The Cortex register IO routines will avoid touching the unused slots and this avoids a VLA.
	 */
	uint32_t saved_regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];
	lpc_save_state(target, flash->iap_ram, &saved_frame, saved_regs);

	/* Set up our IAP frame with the break opcode and command to run */
	iap_frame_s frame = {
		.opcode = CORTEX_THUMB_BREAKPOINT,
		.config = {.command = cmd},
	};

	/* Fill out the remainder of the parameters */
	const size_t params_count = lpc_iap_params(cmd);
	va_list params;
	va_start(params, cmd);
	for (size_t i = 0; i < params_count; ++i)
		frame.config.params[i] = va_arg(params, uint32_t);
	va_end(params);
	for (size_t i = params_count; i < 4; ++i)
		frame.config.params[i] = 0U;

	/* Set the result code to something notable to help with checking if the call ran */
	frame.result.return_code = cmd;

	DEBUG_INFO("%s: cmd %d (%x), params: %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32 "\n", __func__, cmd, cmd,
		frame.config.params[0], frame.config.params[1], frame.config.params[2], frame.config.params[3]);

	/* Copy the structure to RAM */
	target_mem_write(target, flash->iap_ram, &frame, sizeof(iap_frame_s));
	const uint32_t iap_results_addr = flash->iap_ram + offsetof(iap_frame_s, result);

	/* Set up for the call to the IAP ROM */
	uint32_t regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];
	memset(regs, 0, target->regs_size);
	/* Point r0 to the start of the config block */
	regs[0] = flash->iap_ram + offsetof(iap_frame_s, config);
	/* And r1 to the next block memory after for the results */
	regs[1] = iap_results_addr;
	/* Set the top of stack to the location of the RAM block the target uses */
	regs[CORTEX_REG_MSP] = flash->iap_msp;
	/* Point the return address to our breakpoint opcode (thumb mode) */
	regs[CORTEX_REG_LR] = flash->iap_ram | 1U;
	/* And set the program counter to the IAP ROM entrypoint */
	regs[CORTEX_REG_PC] = flash->iap_entry;
	/* Finally set up xPSR to indicate a suitable instruction mode, no fault */
	regs[CORTEX_REG_XPSR] = (flash->iap_entry & 1U) ? CORTEXM_XPSR_THUMB : 0U;
	target_regs_write(target, regs);

	/* Figure out if we're about to execute a mass erase or not */
	const bool full_erase =
		cmd == IAP_CMD_ERASE && lpc_is_full_erase(flash, frame.config.params[0], frame.config.params[1]);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	/* Start the target and wait for it to halt again */
	target_halt_resume(target, false);
	while (!target_halt_poll(target, NULL)) {
		if (full_erase)
			target_print_progress(&timeout);
		/* If after 500ms we've been unable to complete a PartID command, error out */
		else if (cmd == IAP_CMD_PARTID && platform_timeout_is_expired(&timeout)) {
			target_halt_request(target);
			/* Restore the original data in RAM and registers */
			lpc_restore_state(target, flash->iap_ram, &saved_frame, saved_regs);
			return IAP_STATUS_INVALID_COMMAND;
		}
	}

	/* Check if a fault occured while executing the call */
	uint32_t status = 0;
	target_reg_read(target, CORTEX_REG_XPSR, &status, sizeof(status));
	if (status & CORTEXM_XPSR_EXCEPTION_MASK) {
		/*
		 * Read back the program counter to determine the fault address
		 * (cortexm_fault_unwind puts the frame back in registers for us)
		 */
		uint32_t fault_address = 0;
		target_reg_read(target, CORTEX_REG_PC, &fault_address, sizeof(fault_address));
		/* Set the thumb bit in the address appropriately */
		if (status & CORTEXM_XPSR_THUMB)
			fault_address |= 1U;

		/* If the fault is not because of our break instruction at the end of the IAP sequence */
		if (fault_address != (flash->iap_ram | 1U)) {
			DEBUG_WARN("%s: Failure due to fault (%" PRIu32 ")\n", __func__, status & CORTEXM_XPSR_EXCEPTION_MASK);
			DEBUG_WARN("\t-> Fault at %08" PRIx32 "\n", fault_address);
			lpc_restore_state(target, flash->iap_ram, &saved_frame, saved_regs);
			return IAP_STATUS_INVALID_COMMAND;
		}
	}

	/* Copy back just the results */
	iap_result_s results = {0};
	target_mem_read(target, &results, iap_results_addr, sizeof(iap_result_s));

	/* Restore the original data in RAM and registers */
	lpc_restore_state(target, flash->iap_ram, &saved_frame, saved_regs);

	/* If the user expected a result, set the result (16 bytes). */
	if (result != NULL)
		*result = results;

/* This guard block deals with the fact iap_error is only defined when ENABLE_DEBUG is */
#if defined(ENABLE_DEBUG)
	if (results.return_code < ARRAY_LENGTH(iap_error))
		DEBUG_INFO("%s: result %s, ", __func__, iap_error[results.return_code]);
	else
		DEBUG_INFO("%s: result %" PRIu32 ", ", __func__, results.return_code);
#endif
	DEBUG_INFO("return values: %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32 "\n", results.values[0],
		results.values[1], results.values[2], results.values[3]);

	return results.return_code;
}

#define LPX80X_SECTOR_SIZE 0x400U
#define LPX80X_PAGE_SIZE   0x40U

bool lpc_flash_erase(target_flash_s *tf, target_addr_t addr, size_t len)
{
	lpc_flash_s *f = (lpc_flash_s *)tf;
	const uint32_t start = lpc_sector_for_addr(f, addr);
	const uint32_t end = lpc_sector_for_addr(f, addr + len - 1U);
	uint32_t last_full_sector = end;

	if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, start, end, f->bank) != IAP_STATUS_CMD_SUCCESS)
		return false;

	/* Only LPC80x has reserved pages!*/
	if (f->reserved_pages && addr + len >= tf->length - 0x400U)
		--last_full_sector;

	if (start <= last_full_sector) {
		/* Sector erase */
		if (lpc_iap_call(f, NULL, IAP_CMD_ERASE, start, last_full_sector, CPU_CLK_KHZ, f->bank) !=
			IAP_STATUS_CMD_SUCCESS)
			return false;

		/* Check erase ok */
		if (lpc_iap_call(f, NULL, IAP_CMD_BLANKCHECK, start, last_full_sector, f->bank) != IAP_STATUS_CMD_SUCCESS)
			return false;
	}

	if (last_full_sector != end) {
		const uint32_t page_start = (addr + len - LPX80X_SECTOR_SIZE) / LPX80X_PAGE_SIZE;
		const uint32_t page_end = page_start + LPX80X_SECTOR_SIZE / LPX80X_PAGE_SIZE - 1U - f->reserved_pages;

		if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, end, end, f->bank) != IAP_STATUS_CMD_SUCCESS)
			return false;

		if (lpc_iap_call(f, NULL, IAP_CMD_ERASE_PAGE, page_start, page_end, CPU_CLK_KHZ, f->bank) !=
			IAP_STATUS_CMD_SUCCESS)
			return false;
		/* Blank check omitted!*/
	}
	return true;
}

static bool lpc_flash_write(target_flash_s *tf, target_addr_t dest, const void *src, size_t len)
{
	lpc_flash_s *f = (lpc_flash_s *)tf;
	/* Prepare... */
	const uint32_t sector = lpc_sector_for_addr(f, dest);
	if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, sector, sector, f->bank) != IAP_STATUS_CMD_SUCCESS) {
		DEBUG_ERROR("Prepare failed\n");
		return false;
	}
	const uint32_t bufaddr = ALIGN(f->iap_ram + sizeof(iap_frame_s), 4U);
	target_mem_write(f->f.t, bufaddr, src, len);
	/* Only LPC80x has reserved pages!*/
	if (!f->reserved_pages || dest + len <= tf->length - len) {
		/*
		 * Write payload to target ram,
		 * set the destination address and program
		 */
		if (lpc_iap_call(f, NULL, IAP_CMD_PROGRAM, dest, bufaddr, len, CPU_CLK_KHZ) != IAP_STATUS_CMD_SUCCESS)
			return false;
	} else {
		/*
		 * On LPC80x, write top sector in pages.
		 * Silently ignore write to the 2 reserved pages at top!
		 */
		for (size_t offset = 0; offset < len - (0x40U * (size_t)f->reserved_pages); offset += LPX80X_PAGE_SIZE) {
			if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, sector, sector, f->bank) != IAP_STATUS_CMD_SUCCESS) {
				DEBUG_ERROR("Prepare failed\n");
				return false;
			}
			/* Set the destination address and program */
			if (lpc_iap_call(f, NULL, IAP_CMD_PROGRAM, dest + offset, bufaddr + offset, LPX80X_PAGE_SIZE,
					CPU_CLK_KHZ) != IAP_STATUS_CMD_SUCCESS)
				return false;
		}
	}
	return true;
}

bool lpc_flash_write_magic_vect(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	if (dest == 0) {
		/* Fill in the magic vector to allow booting the flash */
		uint32_t *const vectors = (uint32_t *)src;
		uint32_t sum = 0;

		/* compute checksum of first 7 vectors */
		for (size_t i = 0; i < 7U; ++i)
			sum += vectors[i];
		/* two's complement is written to 8'th vector */
		vectors[7] = ~sum + 1U;
	}
	return lpc_flash_write(f, dest, src, len);
}
