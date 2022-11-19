/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012 Gareth McMullin <gareth@blacksphere.co.nz>
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
#include "adiv5.h"

#define IAP_PGM_CHUNKSIZE 4096U

#define MIN_RAM_SIZE               8192U // LPC1751
#define RAM_USAGE_FOR_IAP_ROUTINES 32U   // IAP routines use 32 bytes at top of ram

#define IAP_ENTRYPOINT 0x1fff1ff1U
#define IAP_RAM_BASE   0x10000000U

#define MEMMAP           0x400fc040U
#define FLASH_NUM_SECTOR 30U

typedef struct flash_param {
	uint16_t opcode;
	uint16_t pad0;
	uint32_t command;
	uint32_t words[4];
	uint32_t result[5]; // Return code and maximum of 4 result parameters
} __attribute__((aligned(4))) flash_param_s;

static void lpc17xx_extended_reset(target_s *t);
static bool lpc17xx_mass_erase(target_s *t);
enum iap_status lpc17xx_iap_call(target_s *t, flash_param_s *param, enum iap_cmd cmd, ...);

static void lpc17xx_add_flash(target_s *t, uint32_t addr, size_t len, size_t erasesize, unsigned int base_sector)
{
	struct lpc_flash *lf = lpc_add_flash(t, addr, len);
	lf->f.blocksize = erasesize;
	lf->base_sector = base_sector;
	lf->f.writesize = IAP_PGM_CHUNKSIZE;
	lf->f.write = lpc_flash_write_magic_vect;
	lf->iap_entry = IAP_ENTRYPOINT;
	lf->iap_ram = IAP_RAM_BASE;
	lf->iap_msp = IAP_RAM_BASE + MIN_RAM_SIZE - RAM_USAGE_FOR_IAP_ROUTINES;
}

bool lpc17xx_probe(target_s *t)
{
	if ((t->cpuid & CPUID_PARTNO_MASK) == CORTEX_M3) {
		/*
		 * Now that we're sure it's a Cortex-M3, we need to halt the
		 * target and make an IAP call to get the part number.
		 * There appears to have no other method of reading the part number.
		 */
		target_halt_request(t);

		/* Read the Part ID */
		flash_param_s param;
		lpc17xx_iap_call(t, &param, IAP_CMD_PARTID);
		target_halt_resume(t, 0);

		if (param.result[0])
			return false;

		switch (param.result[1]) {
		case 0x26113f37U: /* LPC1769 */
		case 0x26013f37U: /* LPC1768 */
		case 0x26012837U: /* LPC1767 */
		case 0x26013f33U: /* LPC1766 */
		case 0x26013733U: /* LPC1765 */
		case 0x26011922U: /* LPC1764 */
		case 0x25113737U: /* LPC1759 */
		case 0x25013f37U: /* LPC1758 */
		case 0x25011723U: /* LPC1756 */
		case 0x25011722U: /* LPC1754 */
		case 0x25001121U: /* LPC1752 */
		case 0x25001118U: /* LPC1751 */
		case 0x25001110U: /* LPC1751 (No CRP) */
			t->mass_erase = lpc17xx_mass_erase;
			t->driver = "LPC17xx";
			t->extended_reset = lpc17xx_extended_reset;
			target_add_ram(t, 0x10000000U, 0x8000U);
			target_add_ram(t, 0x2007c000U, 0x4000U);
			target_add_ram(t, 0x20080000U, 0x4000U);
			lpc17xx_add_flash(t, 0x00000000U, 0x10000U, 0x1000U, 0);
			lpc17xx_add_flash(t, 0x00010000U, 0x70000U, 0x8000U, 16);
			return true;
		}
	}
	return false;
}

static bool lpc17xx_mass_erase(target_s *t)
{
	flash_param_s param;

	if (lpc17xx_iap_call(t, &param, IAP_CMD_PREPARE, 0, FLASH_NUM_SECTOR - 1U)) {
		DEBUG_WARN("lpc17xx_cmd_erase: prepare failed %" PRIu32 "\n", param.result[0]);
		return false;
	}

	if (lpc17xx_iap_call(t, &param, IAP_CMD_ERASE, 0, FLASH_NUM_SECTOR - 1U, CPU_CLK_KHZ)) {
		DEBUG_WARN("lpc17xx_cmd_erase: erase failed %" PRIu32 "\n", param.result[0]);
		return false;
	}

	if (lpc17xx_iap_call(t, &param, IAP_CMD_BLANKCHECK, 0, FLASH_NUM_SECTOR - 1U)) {
		DEBUG_WARN("lpc17xx_cmd_erase: blankcheck failed %" PRIu32 "\n", param.result[0]);
		return false;
	}

	tc_printf(t, "Erase OK.\n");
	return true;
}

/**
 * Target has been reset, make sure to remap the boot ROM
 * from 0x00000000 leaving the user flash visible
 */
static void lpc17xx_extended_reset(target_s *t)
{
	/* From §33.6 Debug memory re-mapping (Page 643) UM10360.pdf (Rev 2) */
	target_mem_write32(t, MEMMAP, 1);
}

enum iap_status lpc17xx_iap_call(target_s *t, flash_param_s *param, enum iap_cmd cmd, ...)
{
	param->opcode = ARM_THUMB_BREAKPOINT;
	param->command = cmd;

	/* Fill out the remainder of the parameters */
	va_list ap;
	va_start(ap, cmd);
	for (int i = 0; i < 4; i++)
		param->words[i] = va_arg(ap, uint32_t);
	va_end(ap);

	/* Copy the structure to RAM */
	target_mem_write(t, IAP_RAM_BASE, param, sizeof(flash_param_s));

	/* Set up for the call to the IAP ROM */
	uint32_t regs[t->regs_size / sizeof(uint32_t)];
	target_regs_read(t, regs);
	regs[0] = IAP_RAM_BASE + offsetof(flash_param_s, command);
	regs[1] = IAP_RAM_BASE + offsetof(flash_param_s, result);
	regs[REG_MSP] = IAP_RAM_BASE + MIN_RAM_SIZE - RAM_USAGE_FOR_IAP_ROUTINES;
	regs[REG_LR] = IAP_RAM_BASE | 1;
	regs[REG_PC] = IAP_ENTRYPOINT;
	target_regs_write(t, regs);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	/* Start the target and wait for it to halt again */
	target_halt_resume(t, false);
	while (!target_halt_poll(t, NULL)) {
		if (cmd == IAP_CMD_ERASE)
			target_print_progress(&timeout);
	}

	/* Copy back just the parameters structure */
	target_mem_read(t, (void *)param, IAP_RAM_BASE, sizeof(flash_param_s));
	return param->result[0];
}
