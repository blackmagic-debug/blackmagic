/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2014 Allen Ibara <aibara>
 * Copyright (C) 2015 Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2020 Eivind Bergem <eivindbergem>
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

#include <string.h>
#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "lpc_common.h"

#define LPC546XX_CHIPID 0x40000ff8U

#define IAP_ENTRYPOINT_LOCATION 0x03000204U

#define LPC546XX_ETBAHB_SRAM_BASE 0x20000000U

/* Only SRAM0 bank is enabled after reset */
#define LPC546XX_ETBAHB_SRAM_SIZE (64U * 1024U)

#define LPC546XX_WDT_MODE       0x4000c000U
#define LPC546XX_WDT_CNT        0x4000c004U
#define LPC546XX_WDT_FEED       0x4000c008U
#define LPC546XX_WDT_PERIOD_MAX 0xffffffU
#define LPC546XX_WDT_PROTECT    (1U << 4U)

#define LPC546XX_MAINCLKSELA 0x40000280U
#define LPC546XX_MAINCLKSELB 0x40000284U
#define LPC546XX_AHBCLKDIV   0x40000380U
#define LPC546XX_FLASHCFG    0x40000400U

#define IAP_RAM_SIZE LPC546XX_ETBAHB_SRAM_SIZE
#define IAP_RAM_BASE LPC546XX_ETBAHB_SRAM_BASE

#define IAP_PGM_CHUNKSIZE 4096U

static bool lpc546xx_cmd_read_partid(target_s *target, int argc, const char **argv);
static bool lpc546xx_cmd_reset_attach(target_s *target, int argc, const char **argv);
static bool lpc546xx_cmd_reset(target_s *target, int argc, const char **argv);
static bool lpc546xx_cmd_write_sector(target_s *target, int argc, const char **argv);

const command_s lpc546xx_cmd_list[] = {
	{"read_partid", lpc546xx_cmd_read_partid, "Read out the 32-bit part ID using IAP."},
	{"read_uid", lpc_cmd_read_uid, "Read out the 16-byte UID."},
	{"reset_attach", lpc546xx_cmd_reset_attach,
		"Reset target. Reset debug registers. Re-attach debugger. This restores "
		"the chip to the very start of program execution, after the ROM bootloader."},
	{"reset", lpc546xx_cmd_reset, "Reset target"},
	{"write_sector", lpc546xx_cmd_write_sector,
		"Write incrementing data 8-bit values across a previously erased sector"},
	{NULL, NULL, NULL},
};

static void lpc546xx_reset_attach(target_s *target);
static bool lpc546xx_flash_init(target_s *target);
static bool lpc546xx_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static void lpc546xx_wdt_set_period(target_s *target);
static void lpc546xx_wdt_kick(target_s *target);

typedef struct lpc546xx_device {
	uint32_t chipid;
	const char *designator;
	uint16_t flash_kbytes;
	uint16_t sram123_kbytes;
} lpc546xx_device_s;

/*
 * Reference: "LPC546XX Product data sheet" revision 2.6, 2018
 * Part type number encoding: LPC546xxJyyy, where yyy is flash size, KiB
 */
static const lpc546xx_device_s lpc546xx_devices_lut[] = {
	{0x7f954605U, "LPC546xxJ256", 256, 32},
	{0x7f954606U, "LPC546xxJ256", 256, 32},
	{0x7f954607U, "LPC546xxJ256", 256, 32},
	{0x7f954616U, "LPC546xxJ256", 256, 32},
	{0xfff54605U, "LPC546xxJ512", 512, 96},
	{0xfff54606U, "LPC546xxJ512", 512, 96},
	{0xfff54607U, "LPC546xxJ512", 512, 96},
	{0xfff54608U, "LPC546xxJ512", 512, 96},
	{0xfff54616U, "LPC546xxJ512", 512, 96},
	{0xfff54618U, "LPC546xxJ512", 512, 96},
	{0xfff54628U, "LPC546xxJ512", 512, 96},
};

/* Look up device parameters */
static const lpc546xx_device_s *lpc546xx_get_device(const uint32_t chipid)
{
	/* Linear search through chips */
	for (size_t i = 0; i < ARRAY_LENGTH(lpc546xx_devices_lut); i++) {
		if (lpc546xx_devices_lut[i].chipid == chipid)
			return lpc546xx_devices_lut + i;
	}

	/* Unknown chip */
	return NULL;
}

static void lpc546xx_add_flash(target_s *const target, const target_addr32_t iap_entry, const uint8_t base_sector,
	const target_addr32_t addr, const size_t len, const size_t erasesize)
{
	lpc_flash_s *const flash = lpc_add_flash(target, addr, len, IAP_PGM_CHUNKSIZE);
	flash->target_flash.blocksize = erasesize;
	flash->target_flash.erase = lpc546xx_flash_erase;
	/* LPC546xx devices require the checksum value written into the vector table in sector 0 */
	flash->target_flash.write = lpc_flash_write_magic_vect;
	flash->bank = 0;
	flash->base_sector = base_sector;
	flash->iap_entry = iap_entry;
	flash->iap_ram = IAP_RAM_BASE;
	flash->iap_msp = IAP_RAM_BASE + IAP_RAM_SIZE;
	flash->wdt_kick = lpc546xx_wdt_kick;
}

bool lpc546xx_probe(target_s *const target)
{
	const uint32_t chipid = target_mem32_read32(target, LPC546XX_CHIPID);

	DEBUG_INFO("LPC546xx: Part ID 0x%08" PRIx32 "\n", chipid);
	const lpc546xx_device_s *const device = lpc546xx_get_device(chipid);
	if (!device)
		return false;

	const uint32_t flash_size = (uint32_t)device->flash_kbytes * 1024U;
	target->driver = device->designator;
	/*
	 * All parts have 64kB SRAM0 (and 32kB SRAMX)
	 * J256 parts only have 32kB SRAM1
	 * J512 parts also have 32kB SRAM2 & 32kB SRAM3 (total 96kB "upper" SRAM123)
	 */
	const uint32_t sram123_size = (uint32_t)device->sram123_kbytes * 1024U;

	lpc546xx_add_flash(target, IAP_ENTRYPOINT_LOCATION, 0, 0x0, flash_size, 0x8000);

	/*
	 * Note: upper 96kiB is only usable after enabling the appropriate control
	 * register bits, see LPC546xx User Manual: §7.5.19 AHB Clock Control register 0
	 */
	const uint32_t sram0_size = UINT32_C(64) * 1024U;
	target_add_ram32(target, 0x20000000, sram0_size);
	target_add_ram32(target, 0x20010000, sram123_size);
	target_add_ram32(target, 0x04000000, 0x8000U); /* SRAMX */
	target_add_commands(target, lpc546xx_cmd_list, "LPC546xx");
	target->target_options |= TOPT_INHIBIT_NRST;
	return true;
}

static void lpc546xx_reset_attach(target_s *const target)
{
	/*
	 * To reset the LPC546xx into a usable state, we need to reset and let it
	 * step once, then attach the debug probe again. Otherwise the ROM bootloader
	 * is mapped to address 0x0, we can't perform flash operations on sector 0,
	 * and reading memory from sector 0 will return the contents of the ROM
	 * bootloader, not the flash
	 */
	target_reset(target);
	target_halt_resume(target, false);
	cortexm_attach(target);
}

static bool lpc546xx_cmd_read_partid(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;
	lpc_flash_s *const flash = (lpc_flash_s *)target->flash;
	iap_result_s result;
	if (lpc_iap_call(flash, &result, IAP_CMD_PARTID))
		return false;
	tc_printf(target, "PART ID: 0x%08" PRIx32 "\n", result.values[0]);
	return true;
}

/* Reset everything, including debug; single step past the ROM bootloader so the system is in a sane state */
static bool lpc546xx_cmd_reset_attach(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;

	lpc546xx_reset_attach(target);

	return true;
}

/* XXX: Why does this command exist at all? Thsi should already be being provided by other layers before this one */
/* Reset all major systems _except_ debug. Note that this will leave the system with the ROM bootloader mapped to 0x0 */
static bool lpc546xx_cmd_reset(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;

	/* System reset on target */
	target_mem32_write32(target, CORTEXM_AIRCR, CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_SYSRESETREQ);
	return true;
}

static bool lpc546xx_cmd_write_sector(target_s *const target, const int argc, const char **const argv)
{
	if (argc > 1) {
		const uint32_t sector_size = target->flash->blocksize;
		uint32_t sector_addr = strtoul(argv[1], NULL, 0);
		sector_addr *= sector_size;

		if (!lpc546xx_flash_erase(target->flash, sector_addr, 1U))
			return false;

		uint8_t *buf = calloc(1, sector_size);
		if (!buf) { /* calloc failed: heap exhaustion */
			DEBUG_ERROR("calloc: failed in %s\n", __func__);
			return false;
		}
		for (uint32_t i = 0; i < sector_size; i++)
			buf[i] = i & 0xffU;

		const bool result = lpc_flash_write_magic_vect(target->flash, sector_addr, buf, sector_size);
		free(buf);
		return result;
	}
	return true;
}

static bool lpc546xx_flash_init(target_s *const target)
{
	/*
	 * Reset the chip. It's unfortunate but we need to make sure the ROM
	 * bootloader is no longer mapped to 0x0 or flash blank check won't work after
	 * erasing that sector. Additionally, the ROM itself may increase the
	 * main clock frequency during its own operation, so we need to force
	 * it back to the 12MHz FRO to guarantee correct flash timing for the IAP API
	 */
	lpc546xx_reset_attach(target);

	/* Deal with WDT */
	lpc546xx_wdt_set_period(target);

	target_mem32_write32(target, LPC546XX_MAINCLKSELA, 0);  // 12MHz FRO
	target_mem32_write32(target, LPC546XX_MAINCLKSELB, 0);  // Use MAINCLKSELA
	target_mem32_write32(target, LPC546XX_AHBCLKDIV, 0);    // Divide by 1
	target_mem32_write32(target, LPC546XX_FLASHCFG, 0x1aU); // Recommended default
	return true;
}

static bool lpc546xx_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t len)
{
	if (!lpc546xx_flash_init(flash->t))
		return false;
	return lpc_flash_erase(flash, addr, len);
}

static void lpc546xx_wdt_set_period(target_s *const target)
{
	/* Check if WDT is on */
	uint32_t wdt_mode = target_mem32_read32(target, LPC546XX_WDT_MODE);

	/* If WDT on, we can't disable it, but we may be able to set a long period */
	if (wdt_mode && !(wdt_mode & LPC546XX_WDT_PROTECT))
		target_mem32_write32(target, LPC546XX_WDT_CNT, LPC546XX_WDT_PERIOD_MAX);
}

static void lpc546xx_wdt_kick(target_s *const target)
{
	/* Check if WDT is on */
	uint32_t wdt_mode = target_mem32_read32(target, LPC546XX_WDT_MODE);

	/* If WDT on, poke it to reset it */
	if (wdt_mode) {
		target_mem32_write32(target, LPC546XX_WDT_FEED, 0xaa);
		target_mem32_write32(target, LPC546XX_WDT_FEED, 0xff);
	}
}
