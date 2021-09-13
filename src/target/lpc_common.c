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

struct flash_param {
	uint16_t opcode;
	uint16_t pad0;
	uint32_t command;
	uint32_t words[4];
	uint32_t status;
	uint32_t result[4];
} __attribute__((aligned(4)));

char *iap_error[] = {
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

static int lpc_flash_write(struct target_flash *tf,
						   target_addr dest, const void *src, size_t len);

struct lpc_flash *lpc_add_flash(target *t, target_addr addr, size_t length)
{
	struct lpc_flash *lf = calloc(1, sizeof(*lf));
	struct target_flash *f;

	if (!lf) {			/* calloc failed: heap exhaustion */
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

enum iap_status lpc_iap_call(struct lpc_flash *f, void *result, enum iap_cmd cmd, ...)
{
	target *t = f->f.t;
	struct flash_param param = {
		.opcode = ARM_THUMB_BREAKPOINT,
		.command = cmd,
	};

	/* Pet WDT before each IAP call, if it is on */
	if (f->wdt_kick)
		f->wdt_kick(t);

	/* save IAP RAM to restore after IAP call */
	struct flash_param backup_param;
	target_mem_read(t, &backup_param, f->iap_ram, sizeof(backup_param));

	/* save registers to restore after IAP call */
	uint32_t backup_regs[t->regs_size / sizeof(uint32_t)];
	target_regs_read(t, backup_regs);

	/* fill out the remainder of the parameters */
	va_list ap;
	va_start(ap, cmd);
	for (int i = 0; i < 4; i++)
		param.words[i] = va_arg(ap, uint32_t);
	va_end(ap);

	/* copy the structure to RAM */
	target_mem_write(t, f->iap_ram, &param, sizeof(param));

	/* set up for the call to the IAP ROM */
	uint32_t regs[t->regs_size / sizeof(uint32_t)];
	target_regs_read(t, regs);
	regs[0] = f->iap_ram + offsetof(struct flash_param, command);
	regs[1] = f->iap_ram + offsetof(struct flash_param, status);
	regs[REG_MSP] = f->iap_msp;
	regs[REG_LR] = f->iap_ram | 1;
	regs[REG_PC] = f->iap_entry;
	target_regs_write(t, regs);

	/* start the target and wait for it to halt again */
	target_halt_resume(t, false);
	while (!target_halt_poll(t, NULL));

	/* copy back just the parameters structure */
	target_mem_read(t, &param, f->iap_ram, sizeof(param));

	/* restore the original data in RAM and registers */
	target_mem_write(t, f->iap_ram, &backup_param, sizeof(param));
	target_regs_write(t, backup_regs);

	/* if the user expected a result, set the result (16 bytes). */
	if (result != NULL)
		memcpy(result, param.result, sizeof(param.result));

#if defined(ENABLE_DEBUG)
	if (param.status != IAP_STATUS_CMD_SUCCESS) {
		if (param.status > (sizeof(iap_error) / sizeof(char*)))
			DEBUG_WARN("IAP  cmd %d : %" PRId32 "\n", cmd, param.status);
		else
			DEBUG_WARN("IAP  cmd %d : %s\n", cmd, iap_error[param.status]);
		DEBUG_WARN("return parameters: %08" PRIx32 " %08" PRIx32 " %08" PRIx32
				   " %08" PRIx32 "\n", param.result[0],
				   param.result[1], param.result[2], param.result[3]);
	}
#endif
	return param.status;
}

static uint8_t lpc_sector_for_addr(struct lpc_flash *f, uint32_t addr)
{
	return f->base_sector + (addr - f->f.start) / f->f.blocksize;
}

#define LPX80X_SECTOR_SIZE 0x400
#define LPX80X_PAGE_SIZE    0x40

int lpc_flash_erase(struct target_flash *tf, target_addr addr, size_t len)
{
	struct lpc_flash *f = (struct lpc_flash *)tf;
	uint32_t start = lpc_sector_for_addr(f, addr);
	uint32_t end = lpc_sector_for_addr(f, addr + len - 1);
	uint32_t last_full_sector = end;

	if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, start, end, f->bank))
		return -1;

	/* Only LPC80x has reserved pages!*/
	if (f->reserved_pages && ((addr + len) >=  tf->length - 0x400) ) {
		last_full_sector -= 1;
	}
	if (start <= last_full_sector) {
		/* Sector erase */
		if (lpc_iap_call(f, NULL, IAP_CMD_ERASE, start, last_full_sector, CPU_CLK_KHZ, f->bank))
			return -2;

		/* check erase ok */
		if (lpc_iap_call(f, NULL, IAP_CMD_BLANKCHECK, start, last_full_sector, f->bank))
			return -3;
	}
	if (last_full_sector != end) {
		uint32_t page_start = (addr + len - LPX80X_SECTOR_SIZE) / LPX80X_PAGE_SIZE;
		uint32_t page_end = page_start +  LPX80X_SECTOR_SIZE/LPX80X_PAGE_SIZE - 1 - f->reserved_pages;
		if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, end, end, f->bank))
			return -1;

		if (lpc_iap_call(f, NULL, IAP_CMD_ERASE_PAGE, page_start, page_end, CPU_CLK_KHZ, f->bank))
			return -2;
		/* Blank check omitted!*/
	}
	return 0;
}

static int lpc_flash_write(struct target_flash *tf,
                    target_addr dest, const void *src, size_t len)
{
	struct lpc_flash *f = (struct lpc_flash *)tf;
	/* prepare... */
	uint32_t sector = lpc_sector_for_addr(f, dest);
	if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, sector, sector, f->bank)) {
		DEBUG_WARN("Prepare failed\n");
		return -1;
	}
	uint32_t bufaddr = ALIGN(f->iap_ram + sizeof(struct flash_param), 4);
	target_mem_write(f->f.t, bufaddr, src, len);
	/* Only LPC80x has reserved pages!*/
	if ((!f->reserved_pages) || ((dest + len) <= (tf->length - len))) {
		/* Write payload to target ram */
		/* set the destination address and program */
		if (lpc_iap_call(f, NULL, IAP_CMD_PROGRAM, dest, bufaddr, len, CPU_CLK_KHZ))
			return -2;
	} else {
		/* On LPC80x, write top sector in pages.
		 * Silently ignore write to the 2 reserved pages at top!*/
		len -= 0x40 * f->reserved_pages;
		while (len) {
			if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, sector, sector, f->bank)) {
				DEBUG_WARN("Prepare failed\n");
				return -1;
			}
			/* set the destination address and program */
			if (lpc_iap_call(f, NULL, IAP_CMD_PROGRAM, dest, bufaddr, LPX80X_PAGE_SIZE, CPU_CLK_KHZ))
				return -2;
			dest += LPX80X_PAGE_SIZE;
			bufaddr += LPX80X_PAGE_SIZE;
			len -= LPX80X_PAGE_SIZE;
		}
	}
	return 0;
}

int lpc_flash_write_magic_vect(struct target_flash *f,
                               target_addr dest, const void *src, size_t len)
{
	if (dest == 0) {
		/* Fill in the magic vector to allow booting the flash */
		uint32_t *w = (uint32_t *)src;
		uint32_t sum = 0;

		/* compute checksum of first 7 vectors */
		for (unsigned i = 0; i < 7; i++)
			sum += w[i];
		/* two's complement is written to 8'th vector */
		w[7] = ~sum + 1;
	}
	return lpc_flash_write(f, dest, src, len);
}
