/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 hardesk <hardesk@gmail.com>
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

#include <assert.h>
#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "buffer_utils.h"
#include "jep106.h"
#include "cortex.h"

/*
	Code				0x0000.0000	0x1FFF.FFFF
	SRAM				0x2000.0000	0x3FFF.FFFF
	Peripheral			0x4000.0000 0x5FFF.FFFF		Aliased flash memory	0x4100.0000 0x41FF.FFFF
	BCR Configuration	41C0.0000h 41C0.005Bh		CRC 41C0.005Ch 41C0.005Fh
	BSL Configuration	41C0.0100h 41C0.0153h		CRC 41C0.0154h 41C0.0157h
	Subsystem			0x6000.0000 0x7FFF.FFFF
	System PPB			0xE000.0000 0xE00F.FFFF
*/

#define TI_DEVID_MSPM0C				0xBBA1 /* 110[34] */
#define TI_DEVID_MSPM0L				0xBB82 /* 110[56], 13[04][456] */
#define TI_DEVID_MSPM0L_1227_2228	0xBB9F /* [12]22[78]*/
#define TI_DEVID_MSPM0G				0xBB88 /* 310[567], 150[567], 350[567] */

#define MSPM0_SRAM_BASE         0x20000000U
#define MSPM0_FLASH_MAIN        0x00000000U
#define MSPM0_FLASH_NONMAIN     0x41c00000U /* One Sector, BANK0. Device boot configuration (BCR, BSL) */
#define MSPM0_FLASH_FACTORY     0x41c40000U	/* One Sector, BANK0. Non Modifiable */
#define MSPM0_FLASH_DATA        0x41d00000U
#define MSPM0_FLASH_SECTOR_SZ   1024

#define MSPM0_FLASHCTL_BASE         0x400cd000
#define MSPM0_FLASHCTL_CMDEXEC      (MSPM0_FLASHCTL_BASE + 0x1100)
#define MSPM0_FLASHCTL_CMDTYPE      (MSPM0_FLASHCTL_BASE + 0x1104)
#define MSPM0_FLASHCTL_CMDCTL       (MSPM0_FLASHCTL_BASE + 0x1108)
#define MSPM0_FLASHCTL_CMDADDR      (MSPM0_FLASHCTL_BASE + 0x1120)
#define MSPM0_FLASHCTL_BYTEN        (MSPM0_FLASHCTL_BASE + 0x1124)
#define MSPM0_FLASHCTL_CMDDATAIDX   (MSPM0_FLASHCTL_BASE + 0x112c)
#define MSPM0_FLASHCTL_STATCMD      (MSPM0_FLASHCTL_BASE + 0x13d0)

#define MSPM0_FLASHCTL_CMDDATA0     (MSPM0_FLASHCTL_BASE + 0x1130)
#define MSPM0_FLASHCTL_CMDDATA1     (MSPM0_FLASHCTL_BASE + 0x1134)
#define MSPM0_FLASHCTL_CMDDATA2     (MSPM0_FLASHCTL_BASE + 0x1138)
#define MSPM0_FLASHCTL_CMDDATA3     (MSPM0_FLASHCTL_BASE + 0x113c)
#define MSPM0_FLASHCTL_CMDWEPROTA   (MSPM0_FLASHCTL_BASE + 0x11d0)
#define MSPM0_FLASHCTL_CMDWEPROTB   (MSPM0_FLASHCTL_BASE + 0x11d4)
#define MSPM0_FLASHCTL_CMDWEPROTC   (MSPM0_FLASHCTL_BASE + 0x11d8)
#define MSPM0_FLASHCTL_CMDWEPROTNM  (MSPM0_FLASHCTL_BASE + 0x1210)

#define MSPM0_FLASH_CMDTYPE_NOOP        0U
#define MSPM0_FLASH_CMDTYPE_PROG        1U
#define MSPM0_FLASH_CMDTYPE_ERASE       2U
#define MSPM0_FLASH_CMDTYPE_RDVERIFY    3U
#define MSPM0_FLASH_CMDTYPE_BLVERIFY    6U

#define MSPM0_FLASH_CMDTYPE_SZ_1WORD    (0U << 4)
#define MSPM0_FLASH_CMDTYPE_SZ_2WORDS   (1U << 4)
#define MSPM0_FLASH_CMDTYPE_SZ_4WORDS   (2U << 4)
#define MSPM0_FLASH_CMDTYPE_SZ_8WORDS   (3U << 4)
#define MSPM0_FLASH_CMDTYPE_SZ_SECTOR   (4U << 4)
#define MSPM0_FLASH_CMDTYPE_SZ_BANK     (5U << 4)

#define MSPM0_FLASHCTL_STAT_DONE        0x01
#define MSPM0_FLASHCTL_STAT_CMDPASS     0x02

typedef struct mspm0_flash {
	target_flash_s target_flash;
	uint32_t banks;
} mspm0_flash_s;

static bool mspm0_flash_erase(target_flash_s *flash, target_addr_t addr, size_t length);
static bool mspm0_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t length);
static bool mspm0_mass_erase(target_s *target);

static bool mspm0_dump_factory_config(target_s *const target, const int argc, const char **const argv);
static bool mspm0_dump_bcr_config(target_s *const target, const int argc, const char **const argv);

static command_s mspm0_cmds_list[] = {
	{"dump_factory", mspm0_dump_factory_config, "Display FACTORY registers"},
	{"dump_bcr", mspm0_dump_bcr_config, "Display NONMAIN (BCR/BSL) registers"},
	/* {"write_nm", mspm0_write_non_main, "Write data to NONMAIN (configuration) flash. write_nm <address> <value>"}, */
	{NULL, NULL, NULL}
};

typedef struct conf_register {
	uint16_t offset:12;
	uint16_t words:4;
	char const* id;
} conf_register_s;

static conf_register_s mspm0_factory_regs[] =
{
	{ 0x00, 1, "TRACEID" },
	{ 0x04, 1, "DEVICEID" },
	{ 0x08, 1, "USERID" },
	{ 0x0c, 1, "BSLPIN_UART" },
	{ 0x10, 1, "BSLPIN_I2C" },
	{ 0x14, 1, "BSLPIN_INVOKE" },
	{ 0x18, 1, "SRAMFLASH" },
	{ 0x3c, 1, "TEMP_SENSE0" },
	{ 0x7c, 1, "BOOTCRC" },
	{ 0, 0, NULL}
};

static conf_register_s mspm0_bcr_regs[] =
{
	{ 0x00, 1, "BCRCONFIGID" },
	{ 0x04, 1, "BOOTCFG0" },
	{ 0x08, 1, "BOOTCFG1" },
	{ 0x0c, 4, "PWDDEBUGLOCK" },
	{ 0x1c, 4, "BOOTCFG2" },
	{ 0x20, 1, "BOOTCFG3" },
	{ 0x24, 4, "PWDMASSERASE" },
	{ 0x34, 4, "PWDFACTORYRESET" },
	{ 0x44, 1, "FLASHSWP0" },
	{ 0x48, 1, "FLASHSWP1" },
	{ 0x4c, 1, "BOOTCFG4" },
	{ 0x50, 1, "APPCRCSTART" },
	{ 0x54, 1, "APPCRCLENGTH" },
	{ 0x58, 1, "APPCRC" },
	{ 0x5c, 1, "BOOTCRC" },
	{ 0x100, 1, "BSLCONFIGID" },
	{ 0x104, 1, "BSLPINCFG0" },
	{ 0x108, 1, "BSLPINCFG1" },
	{ 0x10c, 1, "BSLCONFIG0" },
	{ 0x110, 8, "BSLPW" },
	{ 0x130, 1, "BSLPLUGINCFG" },
	{ 0x134, 4, "BSLPLUGINHOOK" },
	{ 0x144, 1, "PATCHHOOKID" },
	{ 0x148, 1, "SBLADDRESS" },
	{ 0x14c, 1, "BSLAPPVER" },
	{ 0x150, 1, "BSLCONFIG1" },
	{ 0x154, 1, "BSLCRC" },
	{ 0, 0, NULL }
};

static void mspm0_add_flash(
	target_s *const target, const uint32_t base, const size_t length, size_t banks)
{
	mspm0_flash_s *const flash = calloc(1, sizeof(*flash));
	if (flash == NULL) {
		DEBUG_WARN("%s: calloc failed for %" PRIu32 " bytes\n", __func__, (uint32_t)length);
		return;
	}

	flash->banks = banks;
	target_flash_s *target_flash = &flash->target_flash;
	target_flash->start = base;
	target_flash->length = length;
	target_flash->blocksize = MSPM0_FLASH_SECTOR_SZ;
	target_flash->writesize = 8;
	target_flash->erase = mspm0_flash_erase;
	target_flash->write = mspm0_flash_write;
	target_flash->erased = 0xff;
	target_add_flash(target, target_flash);
}

#define BIT_LMASK(T, bits) ((T)-1) >> (sizeof(T)*__CHAR_BIT__ - (bits))
#define BITS(h, l, x) (uint32_t)((x >> l) & BIT_LMASK(uint32_t,h-l+1))

bool mspm0_probe(target_s *const target)
{
	uint32_t const deviceid = target_mem32_read32(target, MSPM0_FLASH_FACTORY + 0x4);

	uint32_t manufacturer = BITS(11, 1, deviceid);
	if (manufacturer != JEP106_MANUFACTURER_TEXAS)
		 return false;

	uint32_t partnum = BITS(27, 12, deviceid);
	if (partnum != TI_DEVID_MSPM0C && partnum != TI_DEVID_MSPM0L &&
		partnum != TI_DEVID_MSPM0L_1227_2228 && partnum != TI_DEVID_MSPM0G)
		return false;

	uint32_t const userid __attribute__((unused)) = target_mem32_read32(target, MSPM0_FLASH_FACTORY + 0x8);
	DEBUG_TARGET("%s: DEVICEID Manufacturer %" PRIx32 " Partnum %" PRIx32 " Version %" PRIu32
				   ", USERID Part %" PRIu32 " Variant %" PRIu32  " Ver %" PRIu32 ".%" PRIu32 "\n",
		__func__,
		manufacturer, partnum, BITS(31, 28, deviceid),
		BITS(15, 0, userid), BITS(23, 16, userid), BITS(30, 28, userid), BITS(27, 24, userid)
	);

	target->driver = "MSPM0";
	target->target_options |= TOPT_INHIBIT_NRST;
	target->mass_erase = mspm0_mass_erase;

	uint32_t const sramflash = target_mem32_read32(target, MSPM0_FLASH_FACTORY + 0x18);
	const uint32_t mainflash_size = BITS(11, 0, sramflash) * 1024;
	const uint32_t main_num_banks = BITS(13, 12, sramflash);
	const uint32_t sram_size = BITS(25, 16, sramflash) * 1024;
	const uint32_t dataflash_size = BITS(31, 26, sramflash) * 1024;

	target_add_ram32(target, MSPM0_SRAM_BASE, sram_size);
	mspm0_add_flash(target, MSPM0_FLASH_MAIN, mainflash_size, main_num_banks);
	if (dataflash_size != 0)
		mspm0_add_flash(target, MSPM0_FLASH_DATA, dataflash_size, 1);

	target_add_commands(target, mspm0_cmds_list, "MSPM0");

	return true;
}

static void mspm0_dump_regs(target_s *const target, const conf_register_s* regs, uint32_t base)
{
	for (conf_register_s const* r=regs; r->id; r++) {
		tc_printf(target, "%15s: ", r->id);
		for (int q=0; q<r->words; q++) {
			uint32_t value = target_mem32_read32(target, base + r->offset + q*4);
			tc_printf(target, "0x%08" PRIx32 "%s", value, q==r->words-1 ? "\n" : " ");
		}
	}
}

static bool mspm0_dump_factory_config(target_s *const target, const int argc, const char **const argv)
{
	(void)argc; (void)argv;
	mspm0_dump_regs(target, mspm0_factory_regs, MSPM0_FLASH_FACTORY);
	return true;
}

static bool mspm0_dump_bcr_config(target_s *const target, const int argc, const char **const argv)
{
	(void)argc; (void)argv;
	mspm0_dump_regs(target, mspm0_bcr_regs, MSPM0_FLASH_NONMAIN);
	return true;
}


/* Wait for flash command to finish and return the status word or -1 if timout */
static uint32_t mspm0_flash_wait_done(target_s *const target)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);

	uint32_t statcmd;
	while (!((statcmd = target_mem32_read32(target, MSPM0_FLASHCTL_STATCMD)) & MSPM0_FLASHCTL_STAT_DONE)) {
		if (platform_timeout_is_expired(&timeout))
			return -1;
	}
	return statcmd;
}

static void mspm0_flash_unprotect(target_flash_s *const target_flash)
{
	target_mem32_write32(target_flash->t, MSPM0_FLASHCTL_CMDWEPROTA, 0);
	target_mem32_write32(target_flash->t, MSPM0_FLASHCTL_CMDWEPROTB, 0);
	target_mem32_write32(target_flash->t, MSPM0_FLASHCTL_CMDWEPROTC, 0);
}

static void mspm0_flash_unprotect_sector(target_flash_s *const target_flash, const target_addr_t addr)
{
	mspm0_flash_s *mspm0_flash = (mspm0_flash_s *)target_flash;
	unsigned sector = (addr - target_flash->start) / MSPM0_FLASH_SECTOR_SZ;

	if (sector < 32) {
		uint32_t mask = ~(1u << sector);
		target_mem32_write32(target_flash->t, MSPM0_FLASHCTL_CMDWEPROTA, mask);
	} else if (sector < 256) {
		/* Depending whether main flash is multibank or not, the first 4 bits of PROTB either
		start at 32k or overlay over PROTA responsible range. */
		unsigned start_protb_sector = mspm0_flash->banks <= 1 ? 32 : 0;
		uint32_t mask = ~(1u << ((sector - start_protb_sector) >> 3));
		target_mem32_write32(target_flash->t, MSPM0_FLASHCTL_CMDWEPROTB, mask);
	} else {
		uint32_t mask = ~(1u << ((sector - 256) >> 3));
		target_mem32_write32(target_flash->t, MSPM0_FLASHCTL_CMDWEPROTC, mask);
	}
}

static bool mspm0_flash_erase(target_flash_s *const target_flash, const target_addr_t addr,
	const size_t length __attribute__((unused)))
{
	assert(length == target_flash->blocksize);

	target_s *const target = target_flash->t;

	DEBUG_INFO("%s: Erasing flash addr %08" PRIx32 " length %08" PRIx32"\n",
		__func__, addr, (uint32_t)length);

	mspm0_flash_unprotect_sector(target_flash, addr);
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDTYPE, MSPM0_FLASH_CMDTYPE_SZ_SECTOR | MSPM0_FLASH_CMDTYPE_ERASE);
	target_mem32_write32(target, MSPM0_FLASHCTL_BYTEN, 0xffffffff);
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDCTL, 0);
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDADDR, addr);
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDEXEC, 1);

	uint32_t statcmd = mspm0_flash_wait_done(target);
	if (statcmd == (uint32_t)-1 || !(statcmd & MSPM0_FLASHCTL_STAT_CMDPASS))
		DEBUG_TARGET("%s: Failed to erase flash, status %08" PRIx32 " addr %08" PRIx32 " length %08" PRIx32 "\n",
			__func__, statcmd, addr, (uint32_t)length);

	return statcmd & MSPM0_FLASHCTL_STAT_CMDPASS;
}

static bool mspm0_flash_write( target_flash_s *const target_flash, target_addr_t dest,
	const void *const src, const size_t length __attribute__((unused)) )
{
	assert(length == target_flash->writesize);

	target_s *const target = target_flash->t;

	mspm0_flash_unprotect_sector(target_flash, dest);
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDTYPE, MSPM0_FLASH_CMDTYPE_PROG | MSPM0_FLASH_CMDTYPE_SZ_1WORD);
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDCTL, 0);
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDADDR, dest);
	target_mem32_write32(target, MSPM0_FLASHCTL_BYTEN, 0xffffffff);
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDDATA0, read_le4((const uint8_t *)src, 0));
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDDATA1, read_le4((const uint8_t *)src, 4));
	target_mem32_write32(target, MSPM0_FLASHCTL_CMDEXEC, 1);

	uint32_t statcmd = mspm0_flash_wait_done(target);
	if (statcmd == (uint32_t)-1 || !(statcmd & MSPM0_FLASHCTL_STAT_CMDPASS))
		DEBUG_TARGET("%s: Failed to write to flash, status %08" PRIx32 " addr %08" PRIx32 " length %08" PRIx32 "\n",
			__func__, statcmd, dest, (uint32_t)length);

	return statcmd & MSPM0_FLASHCTL_STAT_CMDPASS;
}

static bool mspm0_mass_erase(target_s *const target)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);

	bool success = true;
	for (mspm0_flash_s* flash = (mspm0_flash_s*)target->flash; flash && success;
		flash = (mspm0_flash_s*)flash->target_flash.next) {

		/* Assume banks are of same size */
		uint32_t bank_size = flash->target_flash.length / flash->banks;
		for (size_t bank=0; bank<flash->banks; ++bank) {

			uint32_t bank_offset = bank * bank_size;
			uint32_t bank_address = flash->target_flash.start + bank_offset;
			DEBUG_INFO("%s: Mass erase flash bank starting %08" PRIx32 " length %08" PRIx32 "\n",
				__func__, bank_address, bank_size);

			mspm0_flash_unprotect(&flash->target_flash);
			target_mem32_write32(target, MSPM0_FLASHCTL_CMDTYPE, MSPM0_FLASH_CMDTYPE_SZ_BANK | MSPM0_FLASH_CMDTYPE_ERASE);
			target_mem32_write32(target, MSPM0_FLASHCTL_CMDCTL, 0);
			target_mem32_write32(target, MSPM0_FLASHCTL_CMDADDR, bank_address);
			target_mem32_write32(target, MSPM0_FLASHCTL_CMDEXEC, 1);

			uint32_t statcmd;
			while (!((statcmd = target_mem32_read32(target, MSPM0_FLASHCTL_STATCMD)) & MSPM0_FLASHCTL_STAT_DONE))
				target_print_progress(&timeout);

			if (!(statcmd & MSPM0_FLASHCTL_STAT_CMDPASS))
				DEBUG_TARGET("%s: Failed to mass erase flash, status %08" PRIx32 " start %08" PRIx32 " length %08" PRIx32 "\n",
					__func__, statcmd, bank_address, (uint32_t)bank_size);

			success &= (statcmd & MSPM0_FLASHCTL_STAT_CMDPASS) == MSPM0_FLASHCTL_STAT_CMDPASS;
		}
	}

	return success;
}
