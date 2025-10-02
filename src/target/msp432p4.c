/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2017 newbrain <federico.zuccardimerli@gmail.com>
 * Copyright (C) 2022-2025 1BitSquared <info@1bitsquared.com>
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

/*
 * This file implements MSP432 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * References:
 * TI doc - SLAU356G
 *   MSP432P4xx Technical Reference Manual
 * TI doc - SLAS826G
 *   MSP432P401R, MSP432P401M SimpleLink Mixed-Signal Microcontrollers
 * TI doc - SLAA704
 *   Flash Operations on MSP432 MCUs
 * TI doc -
 *   MSP432® Peripheral Driver Library User's Guide
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortex.h"

/* TLV: Device info tag, address and expected value */
#define DEVINFO_TAG_ADDR  0x00201004U
#define DEVINFO_TAG_VALUE 0x0000000bU

/* TLV: Device info length, address and expected value */
#define DEVINFO_LEN_ADDR  0x00201008U
#define DEVINFO_LEN_VALUE 0x00000004U

/* TLV: Device ID, address and expected values */
#define DEVID_ADDR            0x0020100cU
#define DEVID_MSP432P401RIPZ  0x0000a000U
#define DEVID_MSP432P401MIPZ  0x0000a001U
#define DEVID_MSP432P401RIZXH 0x0000a002U
#define DEVID_MSP432P401MIZXH 0x0000a003U
#define DEVID_MSP432P401RIRGC 0x0000a004U
#define DEVID_MSP432P401MIRGC 0x0000a005U

/* TLV: Hardware revision, address and minimum expected value */
#define HWREV_ADDR      0x00201010U
#define HWREV_MIN_VALUE 0x00000043U

/* ROM Device Driver Table pointer addresses */
#define ROM_TABLE_BASE 0x02000800U

#define OFFS_FLASH_CTRL_TABLE        28U /* ROM_TABLE_BASE[7] */
#define OFFS_FLASH_CTRL_MASS_ERASE   32U /* ROM_FLASHCTLTABLE[8] */
#define OFFS_FLASH_CTRL_ERASE_SECTOR 36U /* ROM_FLASHCTLTABLE[9] */
#define OFFS_FLASH_CTRL_PROGRAM_MEM  40U /* ROM_FLASHCTLTABLE[10] */

/* Memory sizes and base addresses */
#define MAIN_FLASH_BASE 0x00000000U /* Beginning of Main Flash */
#define INFO_FLASH_BASE 0x00200000U /* Beginning of Info Flash */
#define INFO_BANK_SIZE  0x00002000U /* Size of 1 bank of Info Flash */
#define SECTOR_SIZE     0x1000U     /* Size of erase page: 4KB */

/* Flash protection registers */
#define INFO_BANK0_WEPROT 0x400110b0U /* Write/Erase protection Bank 0 Info */
#define MAIN_BANK0_WEPROT 0x400110b4U /* Write/Erase protection Bank 0 Main */
#define INFO_BANK1_WEPROT 0x400110c0U /* Write/Erase protection Bank 1 Info */
#define MAIN_BANK1_WEPROT 0x400110c4U /* Write/Erase protection Bank 1 Main */

/* Main Flash and SRAM size registers */
#define SYS_SRAM_SIZE  0xe0043010U /* Size of SRAM in SYSCTL */
#define SYS_FLASH_SIZE 0xe0043020U /* Size of main flash in SYSCTL */

/* RAM info */
#define SRAM_BASE       0x20000000U /* Beginning of SRAM */
#define SRAM_CODE_BASE  0x01000000U /* Beginning of SRAM, Code zone alias */
#define P401M_SRAM_SIZE 0x00008000U /* Size of SRAM, M: 32KB */
#define P401R_SRAM_SIZE 0x00010000U /* Size of SRAM, R: 64KB */

/* Flash write buffer and stack */
#define SRAM_STACK_OFFSET   0x00000200U /* A bit less than 512 stack room */
#define SRAM_STACK_PTR      (SRAM_BASE + SRAM_STACK_OFFSET)
#define SRAM_WRITE_BUFFER   SRAM_STACK_PTR /* Buffer right above stack */
#define SRAM_WRITE_BUF_SIZE 0x00000400U    /* Write 1024 bytes at a tima */

/* Watchdog */
#define WDT_A_WTDCTL 0x4000480cU /* Control register for watchdog */
#define WDT_A_HOLD   0x5a88U     /* Clears and halts the watchdog */

/* Support variables to call code in ROM */
typedef struct msp432_flash {
	target_flash_s f;
	target_addr_t flash_protect_register; /* Address of the WEPROT register*/
	target_addr_t flash_erase_sector_fn;  /* Erase flash sector routine in ROM*/
	target_addr_t flash_program_fn;       /* Flash programming routine in ROM */
} msp432_flash_s;

/* Optional commands handlers */
/* Erase all of main flash */
static bool msp432_cmd_erase_main(target_s *target, int argc, const char **argv);
/* Erase a single (4KB) sector */
static bool msp432_cmd_sector_erase(target_s *target, int argc, const char **argv);

/* Optional commands structure*/
const command_s msp432_cmd_list[] = {
	{"erase", msp432_cmd_erase_main, "Erase main flash"},
	{"sector_erase", msp432_cmd_sector_erase, "Erase sector containing given address"},
	{NULL, NULL, NULL},
};

static bool msp432_sector_erase(const target_flash_s *target_flash, target_addr_t addr);
static bool msp432_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool msp432_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);

/* Call a function in the MSP432 ROM (or anywhere else...)*/
static void msp432_call_rom(target_s *target, uint32_t address, uint32_t *regs);

static void msp432_add_flash(target_s *target, uint32_t addr, size_t length, target_addr_t prot_reg)
{
	msp432_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *target_flash = &flash->f;
	target_flash->start = addr;
	target_flash->length = length;
	target_flash->blocksize = SECTOR_SIZE;
	target_flash->erase = msp432_flash_erase;
	target_flash->write = msp432_flash_write;
	target_flash->writesize = SRAM_WRITE_BUF_SIZE;
	target_flash->erased = 0xff;
	target_add_flash(target, target_flash);
	/* Initialize ROM call pointers. Silicon rev B is not supported */
	const uint32_t flash_ctrl_base = target_mem32_read32(target, ROM_TABLE_BASE + OFFS_FLASH_CTRL_TABLE);
	flash->flash_erase_sector_fn = target_mem32_read32(target, flash_ctrl_base + OFFS_FLASH_CTRL_ERASE_SECTOR);
	flash->flash_program_fn = target_mem32_read32(target, flash_ctrl_base + OFFS_FLASH_CTRL_PROGRAM_MEM);
	flash->flash_protect_register = prot_reg;
}

bool msp432p4_probe(target_s *target)
{
	/* Check for the right device info tag in the TLV ROM structure */
	if (target_mem32_read32(target, DEVINFO_TAG_ADDR) != DEVINFO_TAG_VALUE)
		return false;

	/* Check for the right device info length tag in the TLV ROM structure */
	if (target_mem32_read32(target, DEVINFO_LEN_ADDR) != DEVINFO_LEN_VALUE)
		return false;

	/* Check for the right HW revision: at least C, as no flash support for B */
	if (target_mem32_read32(target, HWREV_ADDR) < HWREV_MIN_VALUE) {
		DEBUG_INFO("MSP432 Version not handled\n");
		return false;
	}

	/* If we got till this point, we are most probably looking at a real TLV  */
	/* Device Information structure. Now check for the correct device         */
	switch (target_mem32_read32(target, DEVID_ADDR)) {
	case DEVID_MSP432P401RIPZ:
	case DEVID_MSP432P401RIZXH:
	case DEVID_MSP432P401RIRGC:
		/* R series: 256kB Flash, 64kB RAM */
		target->driver = "MSP432P401R 256KB Flash 64KB RAM";
		break;

	case DEVID_MSP432P401MIPZ:
	case DEVID_MSP432P401MIZXH:
	case DEVID_MSP432P401MIRGC:
		/* M series: 128kB Flash, 32kB RAM */
		target->driver = "MSP432P401M 128KB Flash 32KB RAM";
		break;

	default:
		/* Unknown device, not an MSP432 or not a real TLV */
		return false;
	}
	/* SRAM region, SRAM zone */
	target_add_ram32(target, SRAM_BASE, target_mem32_read32(target, SYS_SRAM_SIZE));
	/* Flash bank size */
	uint32_t banksize = target_mem32_read32(target, SYS_FLASH_SIZE) / 2U;
	/* Main Flash Bank 0 */
	msp432_add_flash(target, MAIN_FLASH_BASE, banksize, MAIN_BANK0_WEPROT);
	/* Main Flash Bank 1 */
	msp432_add_flash(target, MAIN_FLASH_BASE + banksize, banksize, MAIN_BANK1_WEPROT);
	/* Info Flash Bank 0 */
	msp432_add_flash(target, INFO_FLASH_BASE, INFO_BANK_SIZE, INFO_BANK0_WEPROT);
	/* Info Flash Bank 1 */
	msp432_add_flash(target, INFO_FLASH_BASE + INFO_BANK_SIZE, INFO_BANK_SIZE, INFO_BANK1_WEPROT);

	/* Connect the optional commands */
	target_add_commands(target, msp432_cmd_list, "MSP432P401x");

	/* All done */
	return true;
}

/* Protect or unprotect the sector containing address */
static inline uint32_t msp432_sector_unprotect(const msp432_flash_s *const flash, const target_addr_t addr)
{
	/* Read the old protection register */
	uint32_t old_mask = target_mem32_read32(flash->f.t, flash->flash_protect_register);
	/* Find the bit representing the sector and set it to 0  */
	uint32_t sec_mask = ~(1U << ((addr - flash->f.start) / SECTOR_SIZE));
	/* Clear the potection bit */
	sec_mask &= old_mask;
	target_mem32_write32(flash->f.t, flash->flash_protect_register, sec_mask);
	return old_mask;
}

/* Flash operations */
/* Erase a single sector at addr calling the ROM routine*/
static bool msp432_sector_erase(const target_flash_s *const target_flash, const target_addr_t addr)
{
	target_s *target = target_flash->t;
	const msp432_flash_s *const flash = (const msp432_flash_s *)target_flash;

	/* Unprotect sector */
	uint32_t old_prot = msp432_sector_unprotect(flash, addr);
	DEBUG_WARN("Flash protect: 0x%08" PRIX32 "\n", target_mem32_read32(target, flash->flash_protect_register));

	/* Prepare input data */
	uint32_t regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];
	target_regs_read(target, regs);
	regs[0] = addr; // Address of sector to erase in R0

	DEBUG_TARGET("Erasing sector at 0x%08" PRIX32 "\n", addr);

	/* Call ROM */
	msp432_call_rom(target, flash->flash_erase_sector_fn, regs);

	// Result value in R0 is true for success
	DEBUG_TARGET("ROM return value: %" PRIu32 "\n", regs[0]);

	/* Restore original protection */
	target_mem32_write32(target, flash->flash_protect_register, old_prot);
	return regs[0] != 0;
}

/* Erase from addr for len bytes */
static bool msp432_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t len)
{
	(void)len;
	return msp432_sector_erase(flash, addr);
}

/* Program flash */
static bool msp432_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	msp432_flash_s *mf = (msp432_flash_s *)flash;
	target_s *target = flash->t;

	/* Prepare RAM buffer in target */
	target_mem32_write(target, SRAM_WRITE_BUFFER, src, len);

	/* Unprotect sector, len is always < SECTOR_SIZE */
	uint32_t old_prot = msp432_sector_unprotect(mf, dest);

	DEBUG_WARN("Flash protect: 0x%08" PRIX32 "\n", target_mem32_read32(target, mf->flash_protect_register));

	/* Prepare input data */
	uint32_t *regs = alloca(target->regs_size / sizeof(uint32_t)); // Use of VLA
	target_regs_read(target, regs);
	regs[0] = SRAM_WRITE_BUFFER; // Address of buffer to be flashed in R0
	regs[1] = dest;              // Flash address to be write to in R1
	regs[2] = len;               // Size of buffer to be flashed in R2

	DEBUG_TARGET("Writing 0x%04" PRIX32 " bytes at 0x%08" PRIx32 "\n", dest, (uint32_t)len);
	/* Call ROM */
	msp432_call_rom(target, mf->flash_program_fn, regs);

	/* Restore original protection */
	target_mem32_write32(target, mf->flash_protect_register, old_prot);

	DEBUG_TARGET("ROM return value: %" PRIu32 "\n", regs[0]);

	// Result value in R0 is true for success
	return regs[0] != 0;
}

/* Optional commands handlers */
static bool msp432_cmd_erase_main(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* The mass erase routine in ROM will also erase the Info Flash. */
	/* Usually, this is not wanted, so go sector by sector...        */

	uint32_t banksize = target_mem32_read32(target, SYS_FLASH_SIZE) / 2U;
	DEBUG_TARGET("Bank Size: 0x%08" PRIX32 "\n", banksize);

	bool result = true;

	/* Erase first bank */
	target_flash_s *flash = target_flash_for_addr(target, MAIN_FLASH_BASE);
	result &= msp432_flash_erase(flash, MAIN_FLASH_BASE, banksize);

	/* Erase second bank */
	flash = target_flash_for_addr(target, MAIN_FLASH_BASE + banksize);
	result &= msp432_flash_erase(flash, MAIN_FLASH_BASE + banksize, banksize);

	return result;
}

static bool msp432_cmd_sector_erase(target_s *target, int argc, const char **argv)
{
	if (argc < 2)
		tc_printf(target, "usage: monitor sector_erase <addr>\n");

	uint32_t addr = strtoul(argv[1], NULL, 0);

	/* Find the flash structure (for the right protect register) */
	target_flash_s *flash = target_flash_for_addr(target, addr);

	if (flash)
		return msp432_sector_erase(flash, addr);
	tc_printf(target, "Invalid sector address\n");
	return false;
}

/* MSP432 ROM routine invocation */
static void msp432_call_rom(target_s *target, uint32_t address, uint32_t *regs)
{
	/* Kill watchdog */
	target_mem32_write16(target, WDT_A_WTDCTL, WDT_A_HOLD);

	/* Breakpoint at the beginning of CODE SRAM alias area */
	target_mem32_write16(target, SRAM_CODE_BASE, CORTEX_THUMB_BREAKPOINT);

	/* Prepare registers */
	regs[CORTEX_REG_MSP] = SRAM_STACK_PTR;     /* Stack space */
	regs[CORTEX_REG_LR] = SRAM_CODE_BASE | 1U; /* Return to beginning of SRAM CODE alias */
	regs[CORTEX_REG_PC] = address;             /* Start at given address */
	target_regs_write(target, regs);

	/* Start the target and wait for it to halt again, which calls the routine setup above */
	target_halt_resume(target, false);
	while (!target_halt_poll(target, NULL))
		continue;

	// Read registers to get result
	target_regs_read(target, regs);
}
