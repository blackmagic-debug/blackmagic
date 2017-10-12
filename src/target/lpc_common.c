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
	uint32_t result;
} __attribute__((aligned(4)));


struct lpc_flash *lpc_add_flash(target *t, target_addr addr, size_t length)
{
	struct lpc_flash *lf = calloc(1, sizeof(*lf));
	struct target_flash *f = &lf->f;
	f->start = addr;
	f->length = length;
	f->erase = lpc_flash_erase;
	f->write = lpc_flash_write;
	f->erased = 0xff;
	target_add_flash(t, f);
	return lf;
}

enum iap_status lpc_iap_call(struct lpc_flash *f, enum iap_cmd cmd, ...)
{
	target *t = f->f.t;
	struct flash_param param = {
		.opcode = ARM_THUMB_BREAKPOINT,
		.command = cmd,
	};

	/* Pet WDT before each IAP call, if it is on */
	if (f->wdt_kick)
		f->wdt_kick(t);

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
	regs[1] = f->iap_ram + offsetof(struct flash_param, result);
	regs[REG_MSP] = f->iap_msp;
	regs[REG_LR] = f->iap_ram | 1;
	regs[REG_PC] = f->iap_entry;
	target_regs_write(t, regs);

	/* start the target and wait for it to halt again */
	target_halt_resume(t, false);
	while (!target_halt_poll(t, NULL));

	/* copy back just the parameters structure */
	target_mem_read(t, &param, f->iap_ram, sizeof(param));
	return param.result;
}

static uint8_t lpc_sector_for_addr(struct lpc_flash *f, uint32_t addr)
{
	return f->base_sector + (addr - f->f.start) / f->f.blocksize;
}

int lpc_flash_erase(struct target_flash *tf, target_addr addr, size_t len)
{
	struct lpc_flash *f = (struct lpc_flash *)tf;
	uint32_t start = lpc_sector_for_addr(f, addr);
	uint32_t end = lpc_sector_for_addr(f, addr + len - 1);

	if (lpc_iap_call(f, IAP_CMD_PREPARE, start, end, f->bank))
		return -1;

	/* and now erase them */
	if (lpc_iap_call(f, IAP_CMD_ERASE, start, end, CPU_CLK_KHZ, f->bank))
		return -2;

	/* check erase ok */
	if (lpc_iap_call(f, IAP_CMD_BLANKCHECK, start, end, f->bank))
		return -3;

	return 0;
}

int lpc_flash_write(struct target_flash *tf,
                    target_addr dest, const void *src, size_t len)
{
	struct lpc_flash *f = (struct lpc_flash *)tf;
	/* prepare... */
	uint32_t sector = lpc_sector_for_addr(f, dest);
	if (lpc_iap_call(f, IAP_CMD_PREPARE, sector, sector, f->bank))
		return -1;

	/* Write payload to target ram */
	uint32_t bufaddr = ALIGN(f->iap_ram + sizeof(struct flash_param), 4);
	target_mem_write(f->f.t, bufaddr, src, len);

	/* set the destination address and program */
	if (lpc_iap_call(f, IAP_CMD_PROGRAM, dest, bufaddr, len, CPU_CLK_KHZ))
		return -2;

	return 0;
}

int lpc_flash_write_magic_vect(struct target_flash *f,
                               target_addr dest, const void *src, size_t len)
{
	if (dest == 0) {
		/* Fill in the magic vector to allow booting the flash */
		uint32_t *w = (uint32_t *)src;
		uint32_t sum = 0;

		for (unsigned i = 0; i < 7; i++)
			sum += w[i];
		w[7] = ~sum + 1;
	}
	return lpc_flash_write(f, dest, src, len);
}
