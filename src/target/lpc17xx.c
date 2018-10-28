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

#define IAP_PGM_CHUNKSIZE			4096

#define MIN_RAM_SIZE				8192 // LPC1751
#define RAM_USAGE_FOR_IAP_ROUTINES	32 // IAP routines use 32 bytes at top of ram

#define IAP_ENTRYPOINT				0x1FFF1FF1
#define IAP_RAM_BASE				0x10000000

#define ARM_CPUID					0xE000ED00
#define CORTEX_M3_CPUID				0x412FC230	// Cortex-M3 r2p0
#define CORTEX_M3_CPUID_MASK		0xFF00FFF0
#define MEMMAP						0x400FC040
#define LPC17xx_JTAG_IDCODE			0x4BA00477
#define LPC17xx_SWDP_IDCODE			0x2BA01477
#define FLASH_NUM_SECTOR			30

struct flash_param {
	uint16_t opcode;
	uint16_t pad0;
	uint32_t command;
	uint32_t words[4];
	uint32_t result[5]; // return code and maximum of 4 result parameters
} __attribute__((aligned(4)));

static void lpc17xx_extended_reset(target *t);
static bool lpc17xx_cmd_erase(target *t, int argc, const char *argv[]);
enum iap_status lpc17xx_iap_call(target *t, struct flash_param *param, enum iap_cmd cmd, ...);

const struct command_s lpc17xx_cmd_list[] = {
	{"erase_mass", lpc17xx_cmd_erase, "Erase entire flash memory"},
	{NULL, NULL, NULL}
};

void lpc17xx_add_flash(target *t, uint32_t addr, size_t len, size_t erasesize, unsigned int base_sector)
{
	struct lpc_flash *lf = lpc_add_flash(t, addr, len);
	lf->f.blocksize = erasesize;
	lf->base_sector = base_sector;
	lf->f.buf_size = IAP_PGM_CHUNKSIZE;
	lf->f.write = lpc_flash_write_magic_vect;
	lf->iap_entry = IAP_ENTRYPOINT;
	lf->iap_ram = IAP_RAM_BASE;
	lf->iap_msp = IAP_RAM_BASE + MIN_RAM_SIZE - RAM_USAGE_FOR_IAP_ROUTINES;
}

bool
lpc17xx_probe(target *t)
{
	/* Read the IDCODE register from the SW-DP */
	ADIv5_AP_t *ap = cortexm_ap(t);
	uint32_t ap_idcode = ap->dp->idcode;

	if (ap_idcode == LPC17xx_JTAG_IDCODE || ap_idcode == LPC17xx_SWDP_IDCODE) {
		/* LPC176x/5x family. See UM10360.pdf 33.7 JTAG TAP Identification*/
	} else {
		return false;
	}

	uint32_t cpuid = target_mem_read32(t, ARM_CPUID);
	if (((cpuid & CORTEX_M3_CPUID_MASK) == (CORTEX_M3_CPUID & CORTEX_M3_CPUID_MASK))) {
		/*
		 * Now that we're sure it's a Cortex-M3, we need to halt the
		 * target and make an IAP call to get the part number.
		 * There appears to have no other method of reading the part number.
		 */
		target_halt_request(t);

		/* Read the Part ID */
		struct flash_param param;
		lpc17xx_iap_call(t, &param, IAP_CMD_PARTID);
		target_halt_resume(t, 0);

		if (param.result[0]) {
			return false;
		}

		switch (param.result[1]) {
			case 0x26113F37: /* LPC1769 */
			case 0x26013F37: /* LPC1768 */
			case 0x26012837: /* LPC1767 */
			case 0x26013F33: /* LPC1766 */
			case 0x26013733: /* LPC1765 */
			case 0x26011922: /* LPC1764 */
			case 0x25113737: /* LPC1759 */
			case 0x25013F37: /* LPC1758 */
			case 0x25011723: /* LPC1756 */
			case 0x25011722: /* LPC1754 */
			case 0x25001121: /* LPC1752 */
			case 0x25001118: /* LPC1751 */
			case 0x25001110: /* LPC1751 (No CRP) */

				t->driver = "LPC17xx";
				t->extended_reset = lpc17xx_extended_reset;
				target_add_ram(t, 0x10000000, 0x8000);
				target_add_ram(t, 0x2007C000, 0x4000);
				target_add_ram(t, 0x20080000, 0x4000);
				lpc17xx_add_flash(t, 0x00000000, 0x10000, 0x1000, 0);
				lpc17xx_add_flash(t, 0x00010000, 0x70000, 0x8000, 16);
				target_add_commands(t, lpc17xx_cmd_list, "LPC17xx");

				return true;
		}
	}
	return false;
}

static bool
lpc17xx_cmd_erase(target *t, int argc, const char *argv[])
{
	(void)argc;
	(void)argv;
	struct flash_param param;

	if (lpc17xx_iap_call(t, &param, IAP_CMD_PREPARE, 0, FLASH_NUM_SECTOR-1)) {
		DEBUG("lpc17xx_cmd_erase: prepare failed %d\n", (unsigned int)param.result[0]);
		return false;
	}

	if (lpc17xx_iap_call(t, &param, IAP_CMD_ERASE, 0, FLASH_NUM_SECTOR-1, CPU_CLK_KHZ)) {
		DEBUG("lpc17xx_cmd_erase: erase failed %d\n", (unsigned int)param.result[0]);
		return false;
	}

	if (lpc17xx_iap_call(t, &param, IAP_CMD_BLANKCHECK, 0, FLASH_NUM_SECTOR-1)) {
		DEBUG("lpc17xx_cmd_erase: blankcheck failed %d\n", (unsigned int)param.result[0]);
		return false;
	}
	tc_printf(t, "Erase OK.\n");
	return true;
}

/**
 * Target has been reset, make sure to remap the boot ROM
 * from 0x00000000 leaving the user flash visible
 */
static void
lpc17xx_extended_reset(target *t)
{
	/* From §33.6 Debug memory re-mapping (Page 643) UM10360.pdf (Rev 2) */
	target_mem_write32(t, MEMMAP, 1);
}

enum iap_status
lpc17xx_iap_call(target *t, struct flash_param *param, enum iap_cmd cmd, ...) {
	param->opcode = ARM_THUMB_BREAKPOINT;
	param->command = cmd;

	/* fill out the remainder of the parameters */
	va_list ap;
	va_start(ap, cmd);
	for (int i = 0; i < 4; i++)
		param->words[i] = va_arg(ap, uint32_t);
	va_end(ap);

	/* copy the structure to RAM */
	target_mem_write(t, IAP_RAM_BASE, param, sizeof(struct flash_param));

	/* set up for the call to the IAP ROM */
	uint32_t regs[t->regs_size / sizeof(uint32_t)];
	target_regs_read(t, regs);
	regs[0] = IAP_RAM_BASE + offsetof(struct flash_param, command);
	regs[1] = IAP_RAM_BASE + offsetof(struct flash_param, result);
	regs[REG_MSP] = IAP_RAM_BASE + MIN_RAM_SIZE - RAM_USAGE_FOR_IAP_ROUTINES;
	regs[REG_LR] = IAP_RAM_BASE | 1;
	regs[REG_PC] = IAP_ENTRYPOINT;
	target_regs_write(t, regs);

	/* start the target and wait for it to halt again */
	target_halt_resume(t, false);
	while (!target_halt_poll(t, NULL));

	/* copy back just the parameters structure */
	target_mem_read(t, (void *)param, IAP_RAM_BASE, sizeof(struct flash_param));
	return param->result[0];
}