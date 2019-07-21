/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * Copyright (C) 2018  newbrain
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

/* This file implements KE04 target specific functions providing
 * the XML memory map and Flash memory programming.
 *
 * An additional command to manually erase a single sector is also provided
 *
 * While very similar to other Kinetis, the differences in the Flash Module
 * registers and the security byte warrant a separate set of routines.
 *
 * According to Freescale document MKE04P24M48SF0RM and MKE04P80M48SF0RM:
 *    KE04 Sub-Family Reference Manual
 *
 * And document MKE04P24M48SF0 and MKE04P80M48SF0:
 *    KE04 Sub-Family Data Sheet
 */

#include "command.h"
#include "general.h"
#include "target.h"
#include "target_internal.h"

/* KE04 registers and constants */

/* Memory base addresses */
#define RAM_BASE_ADDR   0x20000000u
#define FLASH_BASE_ADDR 0x00000000u

/* ID register and related constants */
#define SIM_SRSID         0x40048000u
#define SRSID_KE04_MASK   0xFF00u
#define SRSID_KE04_FAMILY 0x0400u
#define SRSID_PIN_MASK    0x000Fu
#define SRSID_PIN__8      0x0000u
#define SRSID_PIN_16      0x0001u
#define SRSID_PIN_20      0x0002u
#define SRSID_PIN_24      0x0003u
#define SRSID_PIN_32      0x0004u
#define SRSID_PIN_44      0x0005u
#define SRSID_PIN_48      0x0006u
#define SRSID_PIN_64      0x0007u
#define SRSID_PIN_80      0x0008u
#define SRSID_PIN100      0x000Au

/* Flash Memory Module registers */
#define FTMRE_BASE  0x40020000u
#define FTMRE_FCCOBIX (FTMRE_BASE + 0x01u)
#define FTMRE_FSEC    (FTMRE_BASE + 0x02u)
#define FTMRE_FCLKDIV (FTMRE_BASE + 0x03u)
#define FTMRE_FSTAT   (FTMRE_BASE + 0x05u)
#define FTMRE_FCNFG   (FTMRE_BASE + 0x07u)
#define FTMRE_FCCOB   (FTMRE_BASE + 0x08u)
#define FTMRE_FCCOBLO (FTMRE_BASE + 0x08u)
#define FTMRE_FCCOBHI (FTMRE_BASE + 0x09u)
#define FTMRE_FPROT   (FTMRE_BASE + 0x0Bu)
#define FTMRE_FOPT    (FTMRE_BASE + 0x0Fu)

/* FTMRE_FSTAT flags */
#define FTMRE_FSTAT_CCIF    0x80u
#define FTMRE_FSTAT_ACCERR  0x20u
#define FTMRE_FSTAT_FPVIOL  0x10u
#define FTMRE_FSTAT_MGBUSY  0x08u
#define FTMRE_FSTAT_MGSTAT1 0x02u
#define FTMRE_FSTAT_MGSTAT0 0x01u

/* Flash Memory Module commands */
#define CMD_PROGRAM_FLASH_32           0x00u /* Special placeholder */
#define CMD_ERASE_VERIFY_ALL_BLOCKS    0x01u /* Unused */
#define CMD_ERASE_VERIFY_BLOCK         0x02u /* Unused */
#define CMD_ERASE_VERIFY_SECTION       0x03u /* Unused */
#define CMD_READ_ONCE                  0x04u /* Unused */
#define CMD_PROGRAM_FLASH              0x06u /* Used   */
#define CMD_PROGRAM_ONCE               0x07u /* Unused */
#define CMD_ERASE_ALL_BLOCKS           0x08u /* Unused */
#define CMD_ERASE_FLASH_BLOCK          0x09u /* Unused */
#define CMD_ERASE_FLASH_SECTOR         0x0Au /* Used   */
#define CMD_UNSECURE_FLASH             0x0Bu /* Used   */
#define CMD_VERIFY_BACKDOOR_ACCESS_KEY 0x0Cu /* Unused */
#define CMD_SET_USER_MARGIN_LEVEL      0x0Du /* Unused */
#define CMD_SET_FACTORY_MARGIN_LEVEL   0x0Eu /* Unused */

/* Flash Memory Module write and erase sizes */
#define KE04_WRITE_LEN   8
#define KE04_SECTOR_SIZE 0x200u

/* Security byte */
#define FLASH_SECURITY_BYTE_ADDRESS   0x0000040Eu
#define FLASH_SECURITY_BYTE_UNSECURED 0xFEu
#define FLASH_SECURITY_WORD_ADDRESS   0x0000040Cu
#define FLASH_SECURITY_WORD_UNSECURED 0xFFFEFFFFu


/* Length in 16bit words of flash commands */
static const uint8_t cmdLen[] = {
    4, 1, 2, 3, 6, 0, 6, 6, 1, 2, 2, 1, 5, 3, 3};

/* Flash routines */
static bool ke04_command(target *t, uint8_t cmd, uint32_t addr, const uint8_t data[8]);
static int ke04_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static int ke04_flash_write(struct target_flash *f,
			    target_addr dest, const void *src, size_t len);

static int ke04_flash_done(struct target_flash *f);

/* Target specific commands */
static bool kinetis_cmd_unsafe(target *t, int argc, char *argv[]);
static bool ke04_cmd_sector_erase(target *t, int argc, char *argv[]);
static bool ke04_cmd_mass_erase(target *t, int argc, char *argv[]);
static bool unsafe_enabled;

const struct command_s ke_cmd_list[] = {
	{"unsafe", (cmd_handler)kinetis_cmd_unsafe, "Allow programming security byte (enable|disable)"},
	{"sector_erase", (cmd_handler)ke04_cmd_sector_erase, "Erase sector containing given address"},
	{"mass_erase", (cmd_handler)ke04_cmd_mass_erase, "Erase the whole flash"},
	{NULL, NULL, NULL}
};

static bool ke04_cmd_sector_erase(target *t, int argc, char *argv[])
{
	if (argc < 2)
		tc_printf(t, "usage: monitor sector_erase <addr>\n");

	char *eos;
	struct target_flash *f = t->flash;
	uint32_t addr = strtoul(argv[1], &eos, 0);

	/* check that addr is a valid number and inside the flash range */
	if ((*eos != 0) || (addr < f->start) || (addr >= f->start+f->length)) {
		/* Address not in range */
		tc_printf(t, "Invalid sector address\n");
		return false;
	}

	/* Erase and verify the given sector */
	ke04_command(t, CMD_ERASE_FLASH_SECTOR, addr, NULL);
	/* Adjust security byte if needed */
	ke04_flash_done(f);
	return true;
}

static bool ke04_cmd_mass_erase(target *t, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	/* Erase and verify the whole flash */
	ke04_command(t, CMD_ERASE_ALL_BLOCKS, 0, NULL);
	/* Adjust security byte if needed */
	ke04_flash_done(t->flash);
	return true;
}

static bool kinetis_cmd_unsafe(target *t, int argc, char *argv[])
{
	if (argc == 1) {
		tc_printf(t, "Allow programming security byte: %s\n",
			  unsafe_enabled ? "enabled" : "disabled");
	} else {
		parse_enable_or_disable(argv[1], &unsafe_enabled);
	}
	return true;
}

bool ke04_probe(target *t)
{
	uint32_t ramsize;
	uint32_t flashsize;
	volatile uint32_t dummy;

	/* Read the higher 16bits of System Reset Status and ID Register */
	uint32_t srsid = target_mem_read32(t, SIM_SRSID) >> 16;

	/* Is this a Kinetis KE04 family MCU? */
	if ((srsid & SRSID_KE04_MASK) != SRSID_KE04_FAMILY)
		return false;

	/* KE04Z8 only comes in 16, 20, and 24 pins */
	switch (srsid & SRSID_PIN_MASK) {
	case SRSID_PIN_16:
	case SRSID_PIN_20:
	case SRSID_PIN_24:
		/* We have a KE04Z8 */
		t->driver = "Kinetis KE04Z8Vxxxx";
		flashsize = 0x2000u; /* 8 kilobytes */
		ramsize   = 0x400u;  /* 1 kilobyte  */
		break;

	/* KE04Z64 and KE04Z128 only come in 44, 64, and 80 pins */
	case SRSID_PIN_44:
	case SRSID_PIN_64:
	case SRSID_PIN_80:
		/* We have either a KE04Z64 or 128 */
		/* Try to read a flash address not available in a Z64 */
		dummy = target_mem_read32(t, 0x00010000u);
		(void)dummy;
		if (target_check_error(t)) {
			/* Read failed: we have a 64 */
			t->driver = "Kinetis KE04Z64Vxxxx";
			flashsize = 0x10000u; /* 64 kilobytes */
			ramsize   = 0x2000u;  /*  8 kilobytes */
		} else {
			/* Read succeeded: we have a 128 */
			t->driver = "Kinetis KE04Z128Vxxxx";
			flashsize = 0x20000u; /* 128 kilobytes */
			ramsize   = 0x4000u;  /*  16 kilobytes */
		}
		break;

	/* Unknown number of pins, not a supported KE04 */
	default:
		return false;
	}

	/* Add low (1/4) and high (3/4) RAM */
	ramsize /= 4;                       /* Amount before RAM_BASE_ADDR */
	target_add_ram(t, RAM_BASE_ADDR - ramsize, ramsize); /* Lower RAM  */
	ramsize *= 3;                       /* Amount after RAM_BASE_ADDR  */
	target_add_ram(t, RAM_BASE_ADDR, ramsize);           /* Higher RAM */

	/* Add flash, all KE04 have same write and erase size */
	struct target_flash *f = calloc(1, sizeof(*f));
	if (!f) {			/* calloc failed: heap exhaustion */
		DEBUG("calloc: failed in %s\n", __func__);
		return false;
	}

	f->start     = FLASH_BASE_ADDR;
	f->length    = flashsize;
	f->blocksize = KE04_SECTOR_SIZE;
	f->erase     = ke04_flash_erase;
	f->write     = ke04_flash_write;
	f->done      = ke04_flash_done;
	f->erased    = 0xFFu;
	target_add_flash(t, f);

	/* Add target specific commands */
	unsafe_enabled = false;
	target_add_commands(t, ke_cmd_list, t->driver);

	return true;
}

static bool
ke04_command(target *t, uint8_t cmd, uint32_t addr, const uint8_t data[8])
{
	uint8_t fstat;

	/* Set FCLKDIV to 0x17 for 24MHz (default at reset) */
	uint8_t fclkdiv = target_mem_read8(t, FTMRE_FCLKDIV);
	if( (fclkdiv & 0x1Fu) != 0x17u ) {
		/* Wait for CCIF to be high */
		do {
			fstat = target_mem_read8(t, FTMRE_FSTAT);
		} while (!(fstat & FTMRE_FSTAT_CCIF));
		/* Write correct value */
		target_mem_write8(t, FTMRE_FCLKDIV, 0x17u);
	}

	/* clear errors unconditionally, so we can start a new operation */
	target_mem_write8(t,FTMRE_FSTAT,(FTMRE_FSTAT_ACCERR | FTMRE_FSTAT_FPVIOL));

	/* Wait for CCIF to be high */
	do {
		fstat = target_mem_read8(t, FTMRE_FSTAT);
	} while (!(fstat & FTMRE_FSTAT_CCIF));

	/* Write the flash command and the needed parameters */
	uint8_t fccobix = 0;
	/* Trim address, probably not needed */
	addr &= 0x00FFFFFF;
	uint8_t cmdL = cmdLen[cmd];
	/* Special case: single 32bits word flashing */
	if(cmd == CMD_PROGRAM_FLASH_32)
		cmd = CMD_PROGRAM_FLASH;
	uint16_t cmdV = (cmd << 8) | (addr >> 16);
	/* Write command to FCCOB array */
	target_mem_write8(t, FTMRE_FCCOBIX, fccobix++);
	target_mem_write16(t, FTMRE_FCCOB, cmdV);

	/* Write first argument (low partof address) */
	if (cmdL >= 1) {
		target_mem_write8(t, FTMRE_FCCOBIX, fccobix++);
		target_mem_write16(t, FTMRE_FCCOB, addr & 0xFFFFu);
	}

	/* Write one or two 32 bit words of data */
	uint8_t dataix = 0;
	for ( ; fccobix < cmdL; fccobix++) {
		target_mem_write8(t, FTMRE_FCCOBIX, fccobix);
		target_mem_write16(t, FTMRE_FCCOB, *(uint16_t *)&data[dataix]);
		dataix += 2;
	}

	/* Enable execution by clearing CCIF */
	target_mem_write8(t, FTMRE_FSTAT, FTMRE_FSTAT_CCIF);

	/* Wait for execution to complete */
	do {
		fstat = target_mem_read8(t, FTMRE_FSTAT);
		/* Check ACCERR and FPVIOL are zero in FSTAT */
		if (fstat & (FTMRE_FSTAT_ACCERR | FTMRE_FSTAT_FPVIOL)) {
			return false;
		}
	} while (!(fstat & FTMRE_FSTAT_CCIF));

	return true;
}

static int ke04_flash_erase(struct target_flash *f, target_addr addr, size_t len)
{
	while (len) {
		if (ke04_command(f->t, CMD_ERASE_FLASH_SECTOR, addr, NULL)) {
			/* Different targets have different flash erase sizes */
			len  -= f->blocksize;
			addr += f->blocksize;
		} else {
			return 1;
		}
	}
	return 0;
}

static int ke04_flash_write(struct target_flash *f,
                              target_addr dest, const void *src, size_t len)
{
	/* Ensure we don't write something horrible over the security byte */
	if (!unsafe_enabled &&
	    (dest <= FLASH_SECURITY_BYTE_ADDRESS) &&
	    ((dest + len) > FLASH_SECURITY_BYTE_ADDRESS)) {
		((uint8_t*)src)[FLASH_SECURITY_BYTE_ADDRESS - dest] =
		    FLASH_SECURITY_BYTE_UNSECURED;
	}

	while (len) {
		if (ke04_command(f->t, CMD_PROGRAM_FLASH, dest, src)) {
			len  -= KE04_WRITE_LEN;
			dest += KE04_WRITE_LEN;
			src  += KE04_WRITE_LEN;
		} else {
			return 1;
		}
	}
	return 0;
}

static int ke04_flash_done(struct target_flash *f)
{
	if (unsafe_enabled)
		return 0;

	if (target_mem_read8(f->t, FLASH_SECURITY_BYTE_ADDRESS) ==
	    FLASH_SECURITY_BYTE_UNSECURED)
		return 0;

	/* Load the security byte from its field */
	/* Note: Cumulative programming is not allowed according to the RM */
	uint32_t val = target_mem_read32(f->t, FLASH_SECURITY_WORD_ADDRESS);
	val = (val & 0xff00ffff) | (FLASH_SECURITY_BYTE_UNSECURED << 16);
	ke04_command(f->t, CMD_PROGRAM_FLASH_32,
			 FLASH_SECURITY_WORD_ADDRESS, (uint8_t *)&val);

	return 0;
}
