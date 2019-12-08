/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2017  newbrain.
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

/* This file implements MSP432 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * Refereces:
 * TI doc - SLAU356G
 *   MSP423P4xx Technical Reference Manual
 * TI doc - SLAS826G
 *   MSP432P401R, MSP432P401M SimpleLink Mixed-Signal Microcontrollers
 * TI doc - SLAA704
 *   Flash Operations on MSP432 MCUs
 * TI doc -
 *   MSP432® Peripheral Driver Library User's Guide
 *
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

/* TLV: Device info tag, address and expected value */
#define DEVINFO_TAG_ADDR 0x00201004u
#define DEVINFO_TAG_VALUE 0x0000000Bu

/* TLV: Device info length, address and expected value */
#define DEVINFO_LEN_ADDR 0x00201008u
#define DEVINFO_LEN_VALUE 0x00000004u

/* TLV: Device ID, address and expected values */
#define DEVID_ADDR 0x0020100Cu
#define DEVID_MSP432P401RIPZ 0x0000A000u
#define DEVID_MSP432P401MIPZ 0x0000A001u
#define DEVID_MSP432P401RIZXH 0x0000A002u
#define DEVID_MSP432P401MIZXH 0x0000A003u
#define DEVID_MSP432P401RIRGC 0x0000A004u
#define DEVID_MSP432P401MIRGC 0x0000A005u

/* TLV: Hardware revision, address and minimum expected value */
#define HWREV_ADDR 0x00201010u
#define HWREV_MIN_VALUE 0x00000043u

/* ROM Device Driver Table pointer addresses */
#define ROM_APITABLE 0x02000800u

#define OFS_FLASHCTLTABLE 28             /* ROM_APITABLE[7] */
#define OFS_FlashCtl_performMassErase 32 /* ROM_FLASHCTLTABLE[8] */
#define OFS_FlashCtl_eraseSector 36      /* ROM_FLASHCTLTABLE[9] */
#define OFS_FlashCtl_programMemory 40    /* ROM_FLASHCTLTABLE[10] */

/* Memory sizes and base addresses */
#define MAIN_FLASH_BASE 0x00000000u /* Beginning of Main Flash */
#define INFO_FLASH_BASE 0x00200000u /* Beginning of Info Flash */
#define INFO_BANK_SIZE 0x00002000u  /* Size of 1 bank of Info Flash */
#define SECTOR_SIZE 0x1000u         /* Size of erase page: 4KB */

/* Flash protection registers */
#define INFO_BANK0_WEPROT 0x400110B0u /* Write/Erase protection Bank 0 Info */
#define MAIN_BANK0_WEPROT 0x400110B4u /* Write/Erase protection Bank 0 Main */
#define INFO_BANK1_WEPROT 0x400110C0u /* Write/Erase protection Bank 1 Info */
#define MAIN_BANK1_WEPROT 0x400110C4u /* Write/Erase protection Bank 1 Main */

/* Main Flash and SRAM size registers */
#define SYS_SRAM_SIZE 0xE0043010u  /* Size of SRAM in SYSCTL */
#define SYS_FLASH_SIZE 0xE0043020u /* Size of main flash in SYSCTL */

/* RAM info */
#define SRAM_BASE 0x20000000u       /* Beginning of SRAM */
#define SRAM_CODE_BASE 0x01000000u  /* Beginning of SRAM, Code zone alias */
#define P401M_SRAM_SIZE 0x00008000u /* Size of SRAM, M: 32KB */
#define P401R_SRAM_SIZE 0x00010000u /* Size of SRAM, R: 64KB */

/* Flash write buffer and stack */
#define SRAM_STACK_OFFSET 0x00000200u /* A bit less than 512 stack room */
#define SRAM_STACK_PTR (SRAM_BASE + SRAM_STACK_OFFSET)
#define SRAM_WRITE_BUFFER SRAM_STACK_PTR /* Buffer right above stack */
#define SRAM_WRITE_BUF_SIZE 0x00000400u  /* Write 1024 bytes at a tima */

/* Watchdog */
#define WDT_A_WTDCTL 0x4000480Cu /* Control register for watchdog */
#define WDT_A_HOLD 0x5A88u       /* Clears and halts the watchdog */

/* Support variables to call code in ROM */
struct msp432_flash
{
	struct target_flash f;
	target_addr flash_protect_register; /* Address of the WEPROT register*/
	target_addr FlashCtl_eraseSector;   /* Erase flash sector routine in ROM*/
	target_addr FlashCtl_programMemory; /* Flash programming routine in ROM */
};

/* Flash operations */
static bool msp432_sector_erase(struct target_flash *f, target_addr addr);
static int msp432_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static int msp432_flash_write(struct target_flash *f, target_addr dest,
			      const void *src, size_t len);

/* Utility functions */
/* Find the the target flash that conatins a specific address */
static struct target_flash *get_target_flash(target *t, target_addr addr);

/* Call a subroutine in the MSP432 ROM (or anywhere else...)*/
static void msp432_call_ROM(target *t, uint32_t address, uint32_t regs[]);

/* Protect or unprotect the sector containing address */
static inline uint32_t msp432_sector_unprotect(struct msp432_flash *mf, target_addr addr)
{
	/* Read the old protection register */
	uint32_t old_mask = target_mem_read32(mf->f.t, mf->flash_protect_register);
	/* Find the bit representing the sector and set it to 0  */
	uint32_t sec_mask = ~(1u << ((addr - mf->f.start) / SECTOR_SIZE));
	/* Clear the potection bit */
	sec_mask &= old_mask;
	target_mem_write32(mf->f.t, mf->flash_protect_register, sec_mask);
	return old_mask;
}

/* Optional commands handlers */
/* Erase all of main flash */
static bool msp432_cmd_erase_main(target *t, int argc, const char **argv);
/* Erase a single (4KB) sector */
static bool msp432_cmd_sector_erase(target *t, int argc, const char **argv);

/* Optional commands structure*/
const struct command_s msp432_cmd_list[] = {
	{"erase", (cmd_handler)msp432_cmd_erase_main, "Erase main flash"},
	{"sector_erase", (cmd_handler)msp432_cmd_sector_erase, "Erase sector containing given address"},
	{NULL, NULL, NULL}};

static void msp432_add_flash(target *t, uint32_t addr, size_t length, target_addr prot_reg)
{
	struct msp432_flash *mf = calloc(1, sizeof(*mf));
	struct target_flash *f;
	if (!mf) {			/* calloc failed: heap exhaustion */
		DEBUG("calloc: failed in %s\n", __func__);
		return;
	}

	f = &mf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = SECTOR_SIZE;
	f->erase = msp432_flash_erase;
	f->write = msp432_flash_write;
	f->buf_size = SRAM_WRITE_BUF_SIZE;
	f->erased = 0xff;
	target_add_flash(t, f);
	/* Initialize ROM call pointers. Silicon rev B is not supported */
	uint32_t flashctltable =
		target_mem_read32(t, ROM_APITABLE + OFS_FLASHCTLTABLE);
	mf->FlashCtl_eraseSector =
		target_mem_read32(t, flashctltable + OFS_FlashCtl_eraseSector);
	mf->FlashCtl_programMemory =
		target_mem_read32(t, flashctltable + OFS_FlashCtl_programMemory);
	mf->flash_protect_register = prot_reg;
}

bool msp432_probe(target *t)
{
	/* Check for the right device info tag in the TLV ROM structure */
	if (target_mem_read32(t, DEVINFO_TAG_ADDR) != DEVINFO_TAG_VALUE)
		return false;

	/* Check for the right device info length tag in the TLV ROM structure */
	if (target_mem_read32(t, DEVINFO_LEN_ADDR) != DEVINFO_LEN_VALUE)
		return false;

	/* Check for the right HW revision: at least C, as no flash support for B */
	if (target_mem_read32(t, HWREV_ADDR) < HWREV_MIN_VALUE)
		return false;

	/* If we got till this point, we are most probably looking at a real TLV  */
	/* Device Information structure. Now check for the correct device         */
	switch (target_mem_read32(t, DEVID_ADDR)) {
	case DEVID_MSP432P401RIPZ:
	case DEVID_MSP432P401RIZXH:
	case DEVID_MSP432P401RIRGC:
		/* R series: 256kB Flash, 64kB RAM */
		t->driver = "MSP432P401R 256KB Flash 64KB RAM";
		break;

	case DEVID_MSP432P401MIPZ:
	case DEVID_MSP432P401MIZXH:
	case DEVID_MSP432P401MIRGC:
		/* M series: 128kB Flash, 32kB RAM */
		t->driver = "MSP432P401M 128KB Flash 32KB RAM";
		break;

	default:
		/* Unknown device, not an MSP432 or not a real TLV */
		return false;
	}
	/* SRAM region, SRAM zone */
	target_add_ram(t, SRAM_BASE, target_mem_read32(t, SYS_SRAM_SIZE));
	/* Flash bank size */
	uint32_t banksize = target_mem_read32(t, SYS_FLASH_SIZE) / 2;
	/* Main Flash Bank 0 */
	msp432_add_flash(t, MAIN_FLASH_BASE, banksize, MAIN_BANK0_WEPROT);
	/* Main Flash Bank 1 */
	msp432_add_flash(t, MAIN_FLASH_BASE + banksize, banksize, MAIN_BANK1_WEPROT);
	/* Info Flash Bank 0 */
	msp432_add_flash(t, INFO_FLASH_BASE, INFO_BANK_SIZE, INFO_BANK0_WEPROT);
	/* Info Flash Bank 1 */
	msp432_add_flash(t, INFO_FLASH_BASE + INFO_BANK_SIZE, INFO_BANK_SIZE, INFO_BANK1_WEPROT);

	/* Connect the optional commands */
	target_add_commands(t, msp432_cmd_list, "MSP432P401x");

	/* All done */
	return true;
}

/* Flash operations */
/* Erase a single sector at addr calling the ROM routine*/
static bool msp432_sector_erase(struct target_flash *f, target_addr addr)
{
	target *t = f->t;
	struct msp432_flash *mf = (struct msp432_flash *)f;

	/* Unprotect sector */
	uint32_t old_prot = msp432_sector_unprotect(mf, addr);
	DEBUG("Flash protect: 0x%08"PRIX32"\n", target_mem_read32(t, mf->flash_protect_register));

	/* Prepare input data */
	uint32_t regs[t->regs_size / sizeof(uint32_t)]; // Use of VLA
	target_regs_read(t, regs);
	regs[0] = addr; // Address of sector to erase in R0

	DEBUG("Erasing sector at 0x%08"PRIX32"\n", addr);

	/* Call ROM */
	msp432_call_ROM(t, mf->FlashCtl_eraseSector, regs);

	// Result value in R0 is true for success
	DEBUG("ROM return value: %"PRIu32"\n", regs[0]);

	/* Restore original protection */
	target_mem_write32(t, mf->flash_protect_register, old_prot);

	return !regs[0];
}

/* Erase from addr for len bytes */
static int msp432_flash_erase(struct target_flash *f, target_addr addr, size_t len)
{
	int ret = 0;
	while (len) {
		ret |= msp432_sector_erase(f, addr);

		/* update len and addr */
		len -= f->blocksize;
		if (len > f->blocksize)
			len -= f->blocksize;
		else
			len = 0;
	}

	return ret;
}

/* Program flash */
static int msp432_flash_write(struct target_flash *f, target_addr dest,
			      const void *src, size_t len)
{
	struct msp432_flash *mf = (struct msp432_flash *)f;
	target *t = f->t;

	/* Prepare RAM buffer in target */
	target_mem_write(t, SRAM_WRITE_BUFFER, src, len);

	/* Unprotect sector, len is always < SECTOR_SIZE */
	uint32_t old_prot = msp432_sector_unprotect(mf, dest);

	DEBUG("Flash protect: 0x%08"PRIX32"\n", target_mem_read32(t, mf->flash_protect_register));

	/* Prepare input data */
	uint32_t regs[t->regs_size / sizeof(uint32_t)]; // Use of VLA
	target_regs_read(t, regs);
	regs[0] = SRAM_WRITE_BUFFER; // Address of buffer to be flashed in R0
	regs[1] = dest;              // Flash address to be write to in R1
	regs[2] = len;               // Size of buffer to be flashed in R2

	DEBUG("Writing 0x%04" PRIX32 " bytes at 0x%08zu\n", dest, len);
	/* Call ROM */
	msp432_call_ROM(t, mf->FlashCtl_programMemory, regs);

	/* Restore original protection */
	target_mem_write32(t, mf->flash_protect_register, old_prot);

	DEBUG("ROM return value: %"PRIu32"\n", regs[0]);
	// Result value in R0 is true for success
	return !regs[0];
}

/* Optional commands handlers */
static bool msp432_cmd_erase_main(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* The mass erase routine in ROM will also erase the Info Flash. */
	/* Usually, this is not wanted, so go sector by sector...        */

	uint32_t banksize = target_mem_read32(t, SYS_FLASH_SIZE) / 2;
	DEBUG("Bank Size: 0x%08"PRIX32"\n", banksize);

	/* Erase first bank */
	struct target_flash *f = get_target_flash(t, MAIN_FLASH_BASE);
	bool ret = msp432_flash_erase(f, MAIN_FLASH_BASE, banksize);

	/* Erase second bank */
	f = get_target_flash(t, MAIN_FLASH_BASE + banksize);
	ret |= msp432_flash_erase(f, MAIN_FLASH_BASE + banksize, banksize);

	return ret;
}

static bool msp432_cmd_sector_erase(target *t, int argc, const char **argv)
{
	if (argc < 2)
		tc_printf(t, "usage: monitor sector_erase <addr>\n");

	uint32_t addr = strtoul(argv[1], NULL, 0);

	/* Find the flash structure (for the rigth protect register) */
	struct target_flash *f = get_target_flash(t, addr);

	if (f)
		return msp432_sector_erase(f, addr);
	tc_printf(t, "Invalid sector address\n");
	return false;
}

/* Returns flash bank containing addr, or NULL if not found */
static struct target_flash *get_target_flash(target *t, target_addr addr)
{
	struct target_flash *f = t->flash;
	while (f) {
		if ((f->start <= addr) && (addr < f->start + f->length))
			break;
		f = f->next;
	}
	return f;
}

/* MSP432 ROM routine invocation */
static void msp432_call_ROM(target *t, uint32_t address, uint32_t regs[])
{
	/* Kill watchdog */
	target_mem_write16(t, WDT_A_WTDCTL, WDT_A_HOLD);

	/* Breakpoint at the beginning of CODE SRAM alias area */
	target_mem_write16(t, SRAM_CODE_BASE, ARM_THUMB_BREAKPOINT);

	/* Prepare registers */
	regs[REG_MSP] = SRAM_STACK_PTR;    /* Stack space */
	regs[REG_LR] = SRAM_CODE_BASE | 1; /* Return to beginning of SRAM CODE alias */
	regs[REG_PC] = address;            /* Start at given address */
	target_regs_write(t, regs);

	/* Call ROM */
	/* start the target and wait for it to halt again */
	target_halt_resume(t, false);
	while (!target_halt_poll(t, NULL));

	// Read registers to get result
	target_regs_read(t, regs);
}
