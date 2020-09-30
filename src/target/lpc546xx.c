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

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "lpc_common.h"

#define LPC546XX_CHIPID 0x40000FF8

#define IAP_ENTRYPOINT_LOCATION	0x03000204

#define LPC546XX_ETBAHB_SRAM_BASE 0x20000000
#define LPC546XX_ETBAHB_SRAM_SIZE (160*1024)

#define LPC546XX_WDT_MODE 0x4000C000
#define LPC546XX_WDT_CNT  0x4000C004
#define LPC546XX_WDT_FEED 0x4000C008
#define LPC546XX_WDT_PERIOD_MAX 0xFFFFFF
#define LPC546XX_WDT_PROTECT (1 << 4)

#define IAP_RAM_SIZE	LPC546XX_ETBAHB_SRAM_SIZE
#define IAP_RAM_BASE	LPC546XX_ETBAHB_SRAM_BASE

#define IAP_PGM_CHUNKSIZE	4096

#define FLASH_NUM_SECTOR	15

static bool lpc546xx_cmd_erase(target *t, int argc, const char *argv[]);
static bool lpc546xx_cmd_reset(target *t, int argc, const char *argv[]);
static int lpc546xx_flash_init(target *t);
static int lpc546xx_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static void lpc546xx_set_internal_clock(target *t);
static void lpc546xx_wdt_set_period(target *t);
static void lpc546xx_wdt_pet(target *t);

const struct command_s lpc546xx_cmd_list[] = {
	{"erase_mass", lpc546xx_cmd_erase, "Erase entire flash memory"},
	{"reset", lpc546xx_cmd_reset, "Reset target"},
	{NULL, NULL, NULL}
};

void lpc546xx_add_flash(target *t, uint32_t iap_entry,
			uint8_t base_sector, uint32_t addr,
			size_t len, size_t erasesize)
{
	struct lpc_flash *lf = lpc_add_flash(t, addr, len);
	lf->f.erase = lpc546xx_flash_erase;
	lf->f.blocksize = erasesize;
	lf->f.buf_size = IAP_PGM_CHUNKSIZE;
	lf->bank = 0;
	lf->base_sector = base_sector;
	lf->iap_entry = iap_entry;
	lf->iap_ram = IAP_RAM_BASE;
	lf->iap_msp = IAP_RAM_BASE + IAP_RAM_SIZE;
	lf->wdt_kick = lpc546xx_wdt_pet;
}

bool lpc546xx_probe(target *t)
{
	uint32_t chipid;
	uint32_t iap_entry;
	uint32_t flash_size;

	chipid = target_mem_read32(t, LPC546XX_CHIPID);

	switch(chipid) {
	case 0x7F954605:
		t->driver = "LPC54605J256";
		flash_size = 0x40000;
		break;
	case 0x7F954606:
		t->driver = "LPC54606J256";
		flash_size = 0x40000;
		break;
	case 0x7F954607:
		t->driver = "LPC54607J256";
		flash_size = 0x40000;
		break;
	case 0x7F954616:
		t->driver = "LPC54616J256";
		flash_size = 0x40000;
		break;
	case 0xFFF54605:
		t->driver = "LPC54605J512";
		flash_size = 0x80000;
		break;
	case 0xFFF54606:
		t->driver = "LPC54606J512";
		flash_size = 0x80000;
		break;
	case 0xFFF54607:
		t->driver = "LPC54607J512";
		flash_size = 0x80000;
		break;
	case 0xFFF54608:
		t->driver = "LPC54608J512";
		flash_size = 0x80000;
		break;
	case 0xFFF54616:
		t->driver = "LPC54616J512";
		flash_size = 0x80000;
		break;
	case 0xFFF54618:
		t->driver = "LPC54618J512";
		flash_size = 0x80000;
		break;
	case 0xFFF54628:
		t->driver = "LPC54628J512";
		flash_size = 0x80000;
		break;
	default:
		return false;
	}

	iap_entry = target_mem_read32(t,
				      IAP_ENTRYPOINT_LOCATION);
	lpc546xx_add_flash(t, iap_entry, 0, 0x0,
			   flash_size, 0x8000);
	target_add_ram(t, 0x20000000, 0x28000);
	target_add_commands(t, lpc546xx_cmd_list, "Lpc546xx");
	t->target_options |= CORTEXM_TOPT_INHIBIT_SRST;

	return false;
}

/* Reset all major systems _except_ debug */
static bool lpc546xx_cmd_reset(target *t, int argc, const char *argv[])
{
	(void)argc;
	(void)argv;

	/* Cortex-M4 Application Interrupt and Reset Control Register */
	static const uint32_t AIRCR = 0xE000ED0C;
	/* Magic value key */
	static const uint32_t reset_val = 0x05FA0004;

	/* System reset on target */
	target_mem_write(t, AIRCR, &reset_val, sizeof(reset_val));

	return true;
}

static bool lpc546xx_cmd_erase(target *t, int argc, const char *argv[])
{
	(void)argc;
	(void)argv;

	lpc546xx_flash_init(t);
	struct lpc_flash *f = (struct lpc_flash *)t->flash;

	if (lpc_iap_call(f, NULL, IAP_CMD_PREPARE, 0, FLASH_NUM_SECTOR-1))
		return false;

	if (lpc_iap_call(f, NULL, IAP_CMD_ERASE, 0, FLASH_NUM_SECTOR-1, CPU_CLK_KHZ))
		return false;

	tc_printf(t, "Erase OK.\n");

	return true;
}

static int lpc546xx_flash_init(target *t)
{
	/* Deal with WDT */
	lpc546xx_wdt_set_period(t);

	/* /\* Force internal clock *\/ */
	lpc546xx_set_internal_clock(t);

	/* Initialize flash IAP */
	struct lpc_flash *f = (struct lpc_flash *)t->flash;
	if (lpc_iap_call(f, NULL, IAP_CMD_INIT))
		return -1;

	return 0;
}

static int lpc546xx_flash_erase(struct target_flash *tf, target_addr addr, size_t len)
{
	if (lpc546xx_flash_init(tf->t))
		return -1;

	return lpc_flash_erase(tf, addr, len);
}

static void lpc546xx_set_internal_clock(target *t)
{
	/* Switch to 12 Mhz FRO */
	target_mem_write32(t, 0x40000000 + 0x248, 0);
}

static void lpc546xx_wdt_set_period(target *t)
{
	/* Check if WDT is on */
	uint32_t wdt_mode = target_mem_read32(t, LPC546XX_WDT_MODE);

	/* If WDT on, we can't disable it, but we may be able to set a long period */
	if (wdt_mode && !(wdt_mode & LPC546XX_WDT_PROTECT))
		target_mem_write32(t, LPC546XX_WDT_CNT, LPC546XX_WDT_PERIOD_MAX);
}

static void lpc546xx_wdt_pet(target *t)
{
	/* Check if WDT is on */
	uint32_t wdt_mode = target_mem_read32(t, LPC546XX_WDT_MODE);

	/* If WDT on, pet */
	if (wdt_mode) {
		target_mem_write32(t, LPC546XX_WDT_FEED, 0xAA);
		target_mem_write32(t, LPC546XX_WDT_FEED, 0xFF);
	}
}
