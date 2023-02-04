/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015 Gareth McMullin <gareth@blacksphere.co.nz>
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

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "lpc_common.h"

#include <stdarg.h>

typedef struct flash_param {
	uint16_t opcode;
	uint16_t pad0;
	uint32_t command;
	uint32_t words[4];
	uint32_t status;
	uint32_t result[4];
} __attribute__((aligned(4))) flash_param_s;

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

lpc_flash_s *lpc_add_flash(target_s *t, target_addr_t addr, size_t length)
{
	lpc_flash_s *lf = calloc(1, sizeof(*lf));
	target_flash_s *f;

	if (!lf) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return NULL;
	}

	f = &lf->f;
	f->start = addr;
	f->length = length;
	f->erase = lpc_flash_erase;
	f->write = lpc_flash_write;
	f->erased = 0xff;
	target_add_flash(t, f);
	return lf;
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

iap_status_e lpc_iap_call(lpc_flash_s *f, void *result, iap_cmd_e cmd, ...)
{
	target_s *t = f->f.t;
	flash_param_s param = {
		.opcode = ARM_THUMB_BREAKPOINT,
		.command = cmd,
		.status = 0xdeadbeef, // To help us see if the IAP didn't execute
	};

	/* Pet WDT before each IAP call, if it is on */
	if (f->wdt_kick)
		f->wdt_kick(t);

	/* Save IAP RAM to restore after IAP call */
	flash_param_s backup_param;
	target_mem_read(t, &backup_param, f->iap_ram, sizeof(backup_param));

	/* save registers to restore after IAP call */
	uint32_t backup_regs[t->regs_size / sizeof(uint32_t)];
	target_regs_read(t, backup_regs);

	/* Fill out the remainder of the parameters */
	va_list ap;
	va_start(ap, cmd);
	for (size_t i = 0; i < 4U; ++i)
		param.words[i] = va_arg(ap, uint32_t);
	va_end(ap);

	/* Copy the structure to RAM */
	target_mem_write(t, f->iap_ram, &param, sizeof(param));

	/* Set up for the call to the IAP ROM */
	uint32_t regs[t->regs_size / sizeof(uint32_t)];
	target_regs_read(t, regs);
	regs[0] = f->iap_ram + offsetof(flash_param_s, command);
	regs[1] = f->iap_ram + offsetof(flash_param_s, status);
	regs[REG_MSP] = f->iap_msp;
	regs[REG_LR] = f->iap_ram | 1U;
	regs[REG_PC] = f->iap_entry;
	target_regs_write(t, regs);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	const bool full_erase = cmd == IAP_CMD_ERASE && lpc_is_full_erase(f, param.words[0], param.words[1]);
	/* Start the target and wait for it to halt again */
	target_halt_resume(t, false);
	while (!target_halt_poll(t, NULL)) {
		if (full_erase)
			target_print_progress(&timeout);
	}

	/* Copy back just the parameters structure */
	target_mem_read(t, &param, f->iap_ram, sizeof(param));

	/* Restore the original data in RAM and registers */
	target_mem_write(t, f->iap_ram, &backup_param, sizeof(param));
	target_regs_write(t, backup_regs);

	/* If the user expected a result, set the result (16 bytes). */
	if (result != NULL)
		memcpy(result, param.result, sizeof(param.result));

#if defined(ENABLE_DEBUG)
	if (param.status != IAP_STATUS_CMD_SUCCESS) {
		if (param.status > (sizeof(iap_error) / sizeof(char *)))
			DEBUG_WARN("IAP cmd %d : %" PRIu32 "\n", cmd, param.status);
		else
			DEBUG_WARN("IAP cmd %d : %s\n", cmd, iap_error[param.status]);
		DEBUG_WARN("return parameters: %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32 "\n", param.result[0],
			param.result[1], param.result[2], param.result[3]);
	}
#endif
	return param.status;
}

#define LPX80X_SECTOR_SIZE 0x400U
#define LPX80X_PAGE_SIZE   0x40U

bool lpc_flash_erase(target_flash_s *tf, target_addr_t addr, size_t len)
{
	lpc_flash_s *f = (lpc_flash_s *)tf;
	const uint32_t start = lpc_sector_for_addr(f, addr);
	const uint32_t end = lpc_sector_for_addr(f, addr + len - 1U);
	uint32_t last_full_sector = end;

	if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, start, end, f->bank))
		return false;

	/* Only LPC80x has reserved pages!*/
	if (f->reserved_pages && addr + len >= tf->length - 0x400U)
		--last_full_sector;

	if (start <= last_full_sector) {
		/* Sector erase */
		if (lpc_iap_call(f, NULL, IAP_CMD_ERASE, start, last_full_sector, CPU_CLK_KHZ, f->bank))
			return false;

		/* Check erase ok */
		if (lpc_iap_call(f, NULL, IAP_CMD_BLANKCHECK, start, last_full_sector, f->bank))
			return false;
	}

	if (last_full_sector != end) {
		const uint32_t page_start = (addr + len - LPX80X_SECTOR_SIZE) / LPX80X_PAGE_SIZE;
		const uint32_t page_end = page_start + LPX80X_SECTOR_SIZE / LPX80X_PAGE_SIZE - 1U - f->reserved_pages;

		if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, end, end, f->bank))
			return false;

		if (lpc_iap_call(f, NULL, IAP_CMD_ERASE_PAGE, page_start, page_end, CPU_CLK_KHZ, f->bank))
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
	if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, sector, sector, f->bank)) {
		DEBUG_WARN("Prepare failed\n");
		return false;
	}
	const uint32_t bufaddr = ALIGN(f->iap_ram + sizeof(flash_param_s), 4);
	target_mem_write(f->f.t, bufaddr, src, len);
	/* Only LPC80x has reserved pages!*/
	if (!f->reserved_pages || dest + len <= tf->length - len) {
		/*
		 * Write payload to target ram,
		 * set the destination address and program
		 */
		if (lpc_iap_call(f, NULL, IAP_CMD_PROGRAM, dest, bufaddr, len, CPU_CLK_KHZ))
			return false;
	} else {
		/*
		 * On LPC80x, write top sector in pages.
		 * Silently ignore write to the 2 reserved pages at top!
		 */
		for (size_t offset = 0; offset < len - (0x40U * f->reserved_pages); offset += LPX80X_PAGE_SIZE) {
			if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, sector, sector, f->bank)) {
				DEBUG_WARN("Prepare failed\n");
				return false;
			}
			/* Set the destination address and program */
			if (lpc_iap_call(f, NULL, IAP_CMD_PROGRAM, dest + offset, bufaddr + offset, LPX80X_PAGE_SIZE, CPU_CLK_KHZ))
				return false;
		}
	}
	return true;
}

bool lpc_flash_write_magic_vect(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	if (dest == 0) {
		/* Fill in the magic vector to allow booting the flash */
		uint32_t *const w = (uint32_t *)src;
		uint32_t sum = 0;

		/* compute checksum of first 7 vectors */
		for (size_t i = 0; i < 7U; ++i)
			sum += w[i];
		/* two's complement is written to 8'th vector */
		w[7] = ~sum + 1U;
	}
	return lpc_flash_write(f, dest, src, len);
}
