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

/*
 * This file implements KE04 target specific functions providing
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
#define RAM_BASE_ADDR   0x20000000U
#define FLASH_BASE_ADDR 0x00000000U

/* ID register and related constants */
#define SIM_SRSID         0x40048000U
#define SRSID_KE04_MASK   0xff00U
#define SRSID_KE04_FAMILY 0x0400U
#define SRSID_PIN_MASK    0x000fU
#define SRSID_PIN__8      0x0000U
#define SRSID_PIN_16      0x0001U
#define SRSID_PIN_20      0x0002U
#define SRSID_PIN_24      0x0003U
#define SRSID_PIN_32      0x0004U
#define SRSID_PIN_44      0x0005U
#define SRSID_PIN_48      0x0006U
#define SRSID_PIN_64      0x0007U
#define SRSID_PIN_80      0x0008U
#define SRSID_PIN100      0x000aU

/* Flash Memory Module registers */
#define FTMRE_BASE    0x40020000U
#define FTMRE_FCCOBIX (FTMRE_BASE + 0x01U)
#define FTMRE_FSEC    (FTMRE_BASE + 0x02U)
#define FTMRE_FCLKDIV (FTMRE_BASE + 0x03U)
#define FTMRE_FSTAT   (FTMRE_BASE + 0x05U)
#define FTMRE_FCNFG   (FTMRE_BASE + 0x07U)
#define FTMRE_FCCOB   (FTMRE_BASE + 0x08U)
#define FTMRE_FCCOBLO (FTMRE_BASE + 0x08U)
#define FTMRE_FCCOBHI (FTMRE_BASE + 0x09U)
#define FTMRE_FPROT   (FTMRE_BASE + 0x0bU)
#define FTMRE_FOPT    (FTMRE_BASE + 0x0fU)

/* FTMRE_FSTAT flags */
#define FTMRE_FSTAT_CCIF    0x80U
#define FTMRE_FSTAT_ACCERR  0x20U
#define FTMRE_FSTAT_FPVIOL  0x10U
#define FTMRE_FSTAT_MGBUSY  0x08U
#define FTMRE_FSTAT_MGSTAT1 0x02U
#define FTMRE_FSTAT_MGSTAT0 0x01U

/* Flash Memory Module commands */
#define CMD_PROGRAM_FLASH_32           0x00U /* Special placeholder */
#define CMD_ERASE_VERIFY_ALL_BLOCKS    0x01U /* Unused */
#define CMD_ERASE_VERIFY_BLOCK         0x02U /* Unused */
#define CMD_ERASE_VERIFY_SECTION       0x03U /* Unused */
#define CMD_READ_ONCE                  0x04U /* Unused */
#define CMD_PROGRAM_FLASH              0x06U /* Used   */
#define CMD_PROGRAM_ONCE               0x07U /* Unused */
#define CMD_ERASE_ALL_BLOCKS           0x08U /* Unused */
#define CMD_ERASE_FLASH_BLOCK          0x09U /* Unused */
#define CMD_ERASE_FLASH_SECTOR         0x0aU /* Used   */
#define CMD_UNSECURE_FLASH             0x0bU /* Used   */
#define CMD_VERIFY_BACKDOOR_ACCESS_KEY 0x0cU /* Unused */
#define CMD_SET_USER_MARGIN_LEVEL      0x0dU /* Unused */
#define CMD_SET_FACTORY_MARGIN_LEVEL   0x0eU /* Unused */

/* Flash Memory Module write and erase sizes */
#define KE04_WRITE_LEN   8U
#define KE04_SECTOR_SIZE 0x200U

/* Security byte */
#define FLASH_SECURITY_BYTE_ADDRESS   0x0000040eU
#define FLASH_SECURITY_BYTE_UNSECURED 0xfeU
#define FLASH_SECURITY_WORD_ADDRESS   0x0000040cU
#define FLASH_SECURITY_WORD_UNSECURED 0xfffeffffU

/* Length in 16bit words of flash commands */
static const uint8_t cmd_lens[] = {4, 1, 2, 3, 6, 0, 6, 6, 1, 2, 2, 1, 5, 3, 3};

/* Flash routines */
static bool ke04_command(target_s *t, uint8_t cmd, uint32_t addr, const void *data);
static bool ke04_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool ke04_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len);
static bool ke04_flash_done(target_flash_s *f);
static bool ke04_mass_erase(target_s *t);

/* Target specific commands */
static bool kinetis_cmd_unsafe(target_s *t, int argc, const char **argv);
static bool ke04_cmd_sector_erase(target_s *t, int argc, const char **argv);

const command_s ke_cmd_list[] = {
	{"unsafe", kinetis_cmd_unsafe, "Allow programming security byte (enable|disable)"},
	{"sector_erase", ke04_cmd_sector_erase, "Erase sector containing given address"},
	{NULL, NULL, NULL},
};

static bool ke04_cmd_sector_erase(target_s *t, int argc, const char **argv)
{
	if (argc < 2)
		tc_printf(t, "usage: monitor sector_erase <addr>\n");

	target_flash_s *f = t->flash;
	char *eos = NULL;
	uint32_t addr = strtoul(argv[1], &eos, 0);

	/* Check that addr is a valid number and inside the flash range */
	if ((eos && eos[0] != '\0') || addr < f->start || addr >= f->start + f->length) {
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

static bool kinetis_cmd_unsafe(target_s *t, int argc, const char **argv)
{
	if (argc == 1)
		tc_printf(t, "Allow programming security byte: %s\n", t->unsafe_enabled ? "enabled" : "disabled");
	else
		parse_enable_or_disable(argv[1], &t->unsafe_enabled);
	return true;
}

bool ke04_probe(target_s *t)
{
	/* Read the higher 16bits of System Reset Status and ID Register */
	const uint16_t srsid = target_mem_read32(t, SIM_SRSID) >> 16U;

	/* Is this a Kinetis KE04 family MCU? */
	if ((srsid & SRSID_KE04_MASK) != SRSID_KE04_FAMILY)
		return false;

	uint32_t ramsize = 0;
	uint32_t flashsize = 0;

	/* KE04Z8 only comes in 16, 20, and 24 pins */
	switch (srsid & SRSID_PIN_MASK) {
	case SRSID_PIN_16:
	case SRSID_PIN_20:
	case SRSID_PIN_24:
		/* We have a KE04Z8 */
		t->driver = "Kinetis KE04Z8Vxxxx";
		flashsize = 0x2000U; /* 8 kilobytes */
		ramsize = 0x400U;    /* 1 kilobyte  */
		break;

	/* KE04Z64 and KE04Z128 only come in 44, 64, and 80 pins */
	case SRSID_PIN_44:
	case SRSID_PIN_64:
	case SRSID_PIN_80: {
		/* We have either a KE04Z64 or 128 */
		/* Try to read a flash address not available in a Z64 */
		volatile uint32_t __attribute__((unused)) dummy = target_mem_read32(t, 0x00010000U);
		if (target_check_error(t)) {
			/* Read failed: we have a 64 */
			t->driver = "Kinetis KE04Z64Vxxxx";
			flashsize = 0x10000U; /* 64 kilobytes */
			ramsize = 0x2000U;    /*  8 kilobytes */
		} else {
			/* Read succeeded: we have a 128 */
			t->driver = "Kinetis KE04Z128Vxxxx";
			flashsize = 0x20000U; /* 128 kilobytes */
			ramsize = 0x4000U;    /*  16 kilobytes */
		}
		break;
	}

	/* Unknown number of pins, not a supported KE04 */
	default:
		return false;
	}

	t->mass_erase = ke04_mass_erase;
	/* Add low (1/4) and high (3/4) RAM */
	ramsize /= 4U;                                       /* Amount before RAM_BASE_ADDR */
	target_add_ram(t, RAM_BASE_ADDR - ramsize, ramsize); /* Lower RAM  */
	ramsize *= 3U;                                       /* Amount after RAM_BASE_ADDR  */
	target_add_ram(t, RAM_BASE_ADDR, ramsize);           /* Higher RAM */

	/* Add flash, all KE04 have same write and erase size */
	target_flash_s *f = calloc(1, sizeof(*f));
	if (!f) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}

	f->start = FLASH_BASE_ADDR;
	f->length = flashsize;
	f->blocksize = KE04_SECTOR_SIZE;
	f->erase = ke04_flash_erase;
	f->write = ke04_flash_write;
	f->done = ke04_flash_done;
	f->erased = 0xffU;
	target_add_flash(t, f);

	/* Add target specific commands */
	target_add_commands(t, ke_cmd_list, t->driver);
	return true;
}

static bool ke04_mass_erase(target_s *t)
{
	/* Erase and verify the whole flash */
	ke04_command(t, CMD_ERASE_ALL_BLOCKS, 0, NULL);
	/* Adjust security byte if needed */
	ke04_flash_done(t->flash);
	return true;
}

static bool ke04_wait_complete(target_s *t)
{
	uint8_t fstat = 0;
	/* Wait for CCIF to be high */
	while (!(fstat & FTMRE_FSTAT_CCIF)) {
		fstat = target_mem_read8(t, FTMRE_FSTAT);
		if (target_check_error(t))
			return false;
	}
	return true;
}

static bool ke04_command(target_s *t, uint8_t cmd, uint32_t addr, const void *const data)
{
	/* Set FCLKDIV to 0x17 for 24MHz (default at reset) */
	uint8_t fclkdiv = target_mem_read8(t, FTMRE_FCLKDIV);
	if ((fclkdiv & 0x1fU) != 0x17U) {
		if (!ke04_wait_complete(t))
			return false;
		/* Write correct value */
		target_mem_write8(t, FTMRE_FCLKDIV, 0x17U);
	}

	/* clear errors unconditionally, so we can start a new operation */
	target_mem_write8(t, FTMRE_FSTAT, FTMRE_FSTAT_ACCERR | FTMRE_FSTAT_FPVIOL);
	if (!ke04_wait_complete(t))
		return false;

	/* Write the flash command and the needed parameters */
	uint8_t fccob_idx = 0;
	/* Trim address, probably not needed */
	addr &= 0x00ffffffU;
	const uint8_t cmd_len = cmd_lens[cmd];
	/* Special case: single 32bits word flashing */
	if (cmd == CMD_PROGRAM_FLASH_32)
		cmd = CMD_PROGRAM_FLASH;
	const uint16_t fccob_cmd = (cmd << 8U) | (addr >> 16U);
	/* Write command to FCCOB array */
	target_mem_write8(t, FTMRE_FCCOBIX, fccob_idx++);
	target_mem_write16(t, FTMRE_FCCOB, fccob_cmd);

	/* Write first argument (low partof address) */
	if (cmd_len >= 1) {
		target_mem_write8(t, FTMRE_FCCOBIX, fccob_idx++);
		target_mem_write16(t, FTMRE_FCCOB, addr & 0xffffU);
	}

	/* Write one or two 32 bit words of data */
	const uint16_t *const cmd_data = (const uint16_t *)data;
	for (uint8_t offset = 0; fccob_idx < cmd_len; ++fccob_idx, ++offset) {
		target_mem_write8(t, FTMRE_FCCOBIX, fccob_idx);
		target_mem_write16(t, FTMRE_FCCOB, cmd_data[offset]);
	}

	/* Enable execution by clearing CCIF */
	target_mem_write8(t, FTMRE_FSTAT, FTMRE_FSTAT_CCIF);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	/* Wait for execution to complete */
	uint8_t fstat = 0;
	while (!(fstat & FTMRE_FSTAT_CCIF)) {
		fstat = target_mem_read8(t, FTMRE_FSTAT);
		/* Check ACCERR and FPVIOL are zero in FSTAT */
		if (fstat & (FTMRE_FSTAT_ACCERR | FTMRE_FSTAT_FPVIOL))
			return false;

		if (cmd == CMD_ERASE_ALL_BLOCKS)
			target_print_progress(&timeout);
	}
	return true;
}

static bool ke04_flash_erase(target_flash_s *const f, const target_addr_t addr, const size_t len)
{
	for (size_t offset = 0; offset < len; offset += f->blocksize) {
		if (!ke04_command(f->t, CMD_ERASE_FLASH_SECTOR, addr + offset, NULL))
			return false;
	}
	return true;
}

static bool ke04_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	/* Ensure we don't write something horrible over the security byte */
	target_s *t = f->t;
	const uint8_t *data = src;
	if (!t->unsafe_enabled && dest <= FLASH_SECURITY_BYTE_ADDRESS && dest + len > FLASH_SECURITY_BYTE_ADDRESS)
		((uint8_t *)data)[FLASH_SECURITY_BYTE_ADDRESS - dest] = FLASH_SECURITY_BYTE_UNSECURED;

	for (size_t offset = 0; offset < len; offset += KE04_WRITE_LEN) {
		if (!ke04_command(f->t, CMD_PROGRAM_FLASH, dest + offset, data + offset))
			return false;
	}
	return true;
}

static bool ke04_flash_done(target_flash_s *f)
{
	target_s *t = f->t;
	if (t->unsafe_enabled || target_mem_read8(f->t, FLASH_SECURITY_BYTE_ADDRESS) == FLASH_SECURITY_BYTE_UNSECURED)
		return true;

	/* Load the security byte from its field */
	/* Note: Cumulative programming is not allowed according to the RM */
	uint32_t vals[2] = {target_mem_read32(f->t, FLASH_SECURITY_WORD_ADDRESS), 0};
	vals[0] = (vals[0] & 0xff00ffffU) | (FLASH_SECURITY_BYTE_UNSECURED << 16U);
	return ke04_command(f->t, CMD_PROGRAM_FLASH_32, FLASH_SECURITY_WORD_ADDRESS, &vals);
}
