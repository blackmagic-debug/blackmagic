/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Mike Smith <drziplok@me.com>
 * Copyright (C) 2016 Gareth McMullin <gareth@blacksphere.co.nz>
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
#include "jep106.h"
#include "lpc_common.h"

#define IAP_PGM_CHUNKSIZE 512U /* should fit in RAM on any device */

#define MIN_RAM_SIZE               1024U
#define RAM_USAGE_FOR_IAP_ROUTINES 32U /* IAP routines use 32 bytes at top of ram */

#define IAP_ENTRY_MOST 0x1fff1ff1U /* all except LPC802, LPC804 & LPC84x */
#define IAP_ENTRY_84x  0x0f001ff1U /* LPC802, LPC804 & LPC84x */
#define IAP_RAM_BASE   0x10000000U

#define LPC11XX_DEVICE_ID 0x400483f4U
#define LPC8XX_DEVICE_ID  0x400483f8U

#define LPC_RAM_BASE   0x10000000U
#define LPC_FLASH_BASE 0x00000000U

/*
 * CHIP    Ram Flash page sector   Rsvd pages  EEPROM
 * LPX80x   2k   16k   64   1024            2
 * LPC804   4k   32k   64   1024            2
 * LPC8N04  8k   32k   64   1024           32
 * LPC810   1k    4k   64   1024            0
 * LPC811   2k    8k   64   1024            0
 * LPC812   4k   16k   64   1024
 * LPC822   4k   16k   64   1024
 * LPC822   8k   32k   64   1024
 * LPC832   4k   16k   64   1024
 * LPC834   4k   32k   64   1024
 * LPC844   8k   64k   64   1024
 * LPC845  16k   64k   64   1024
 */

static bool lpc8xx_flash_mode(target_s *target);
static bool lpc11xx_read_uid(target_s *target, int argc, const char **argv);

const command_s lpc11xx_cmd_list[] = {
	{"readuid", lpc11xx_read_uid, "Read out the 16-byte UID."},
	{NULL, NULL, NULL},
};

static void lpc11xx_add_flash(target_s *target, const uint32_t addr, const size_t len, const size_t erase_block_len,
	const uint32_t iap_entry, const size_t reserved_pages)
{
	lpc_flash_s *const flash = lpc_add_flash(target, addr, len, IAP_PGM_CHUNKSIZE);
	flash->f.blocksize = erase_block_len;
	flash->f.write = lpc_flash_write_magic_vect;
	flash->iap_entry = iap_entry;
	flash->iap_ram = IAP_RAM_BASE;
	flash->iap_msp = IAP_RAM_BASE + MIN_RAM_SIZE - RAM_USAGE_FOR_IAP_ROUTINES;
	flash->reserved_pages = reserved_pages;
}

static bool lpc11xx_detect(target_s *const target)
{
	/*
	 * Read the device ID register
	 *
	 * For LPC11xx & LPC11Cxx see UM10398 Rev. 12.4 §26.5.11 Table 387
	 * For LPC11Uxx see UM10462 Rev. 5.5 §20.13.11 Table 377
	 * Nota Bene: the DEVICE_ID register at address 0x400483f4 is not valid for:
	 *   1) the LPC11xx & LPC11Cxx "XL" series, see UM10398 Rev.12.4 §3.1
	 *   2) the LPC11U3x series, see UM10462 Rev.5.5 §3.1
	 * But see the comment for the LPC8xx series below.
	 */
	const uint32_t device_id = target_mem_read32(target, LPC11XX_DEVICE_ID);

	switch (device_id) {
	case 0x0a07102bU: /* LPC1110 - 4K Flash 1K SRAM */
	case 0x1a07102bU: /* LPC1110 - 4K Flash 1K SRAM */
	case 0x0a16d02bU: /* LPC1111/002 - 8K Flash 2K SRAM */
	case 0x1a16d02bU: /* LPC1111/002 - 8K Flash 2K SRAM */
	case 0x041e502bU: /* LPC1111/101 - 8K Flash 2K SRAM */
	case 0x2516d02bU: /* LPC1111/101/102 - 8K Flash 2K SRAM */
	case 0x0416502bU: /* LPC1111/201 - 8K Flash 4K SRAM */
	case 0x2516902bU: /* LPC1111/201/202 - 8K Flash 4K SRAM */
	case 0x0a23902bU: /* LPC1112/102 - 16K Flash 4K SRAM */
	case 0x1a23902bU: /* LPC1112/102 - 16K Flash 4K SRAM */
	case 0x042d502bU: /* LPC1112/101 - 16K Flash 2K SRAM */
	case 0x2524d02bU: /* LPC1112/101/102 - 16K Flash 2K SRAM */
	case 0x0425502bU: /* LPC1112/201 - 16K Flash 4K SRAM */
	case 0x2524902bU: /* LPC1112/201/202 - 16K Flash 4K SRAM */
	case 0x0434502bU: /* LPC1113/201 - 24K Flash 4K SRAM */
	case 0x2532902bU: /* LPC1113/201/202 - 24K Flash 4K SRAM */
	case 0x0434102bU: /* LPC1113/301 - 24K Flash 8K SRAM */
	case 0x2532102bU: /* LPC1113/301/302 - 24K Flash 8K SRAM */
	case 0x0a40902bU: /* LPC1114/102 - 32K Flash 4K SRAM */
	case 0x1a40902bU: /* LPC1114/102 - 32K Flash 4K SRAM */
	case 0x0444502bU: /* LPC1114/201 - 32K Flash 4K SRAM */
	case 0x2540902bU: /* LPC1114/201/202 - 32K Flash 4K SRAM */
	case 0x0444102bU: /* LPC1114/301 - 32K Flash 8K SRAM */
	case 0x2540102bU: /* LPC1114/301/302 & LPC11D14/302 - 32K Flash 8K SRAM */
	case 0x00050080U: /* LPC1115/303 - 64K Flash 8K SRAM (Redundant? see UM10398, XL has Device ID at different address) */
	case 0x1421102bU: /* LPC11c12/301 - 16K Flash 8K SRAM */
	case 0x1440102bU: /* LPC11c14/301 - 32K Flash 8K SRAM */
	case 0x1431102bU: /* LPC11c22/301 - 16K Flash 8K SRAM */
	case 0x1430102bU: /* LPC11c24/301 - 32K Flash 8K SRAM */
	case 0x095c802bU: /* LPC11u12x/201 - 16K Flash 4K SRAM */
	case 0x295c802bU: /* LPC11u12x/201 - 16K Flash 4K SRAM */
	case 0x097a802bU: /* LPC11u13/201 - 24K Flash 4K SRAM */
	case 0x297a802bU: /* LPC11u13/201 - 24K Flash 4K SRAM */
	case 0x0998802bU: /* LPC11u14/201 - 32K Flash 4K SRAM */
	case 0x2998802bU: /* LPC11u14/201 - 32K Flash 4K SRAM */
	case 0x2954402bU: /* LPC11u22/301 - 16K Flash 6K SRAM */
	case 0x2972402bU: /* LPC11u23/301 - 24K Flash 6K SRAM */
	case 0x2988402bU: /* LPC11u24x/301 - 32K Flash 6K SRAM */
	case 0x2980002bU: /* LPC11u24x/401 - 32K Flash 8K SRAM */
		target->driver = "LPC11xx";
		target_add_ram(target, LPC_RAM_BASE, 0x2000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x20000, 0x1000, IAP_ENTRY_MOST, 0);
		return true;

	case 0x0a24902bU:
	case 0x1a24902bU:
		target->driver = "LPC1112";
		target_add_ram(target, LPC_RAM_BASE, 0x1000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x10000, 0x1000, IAP_ENTRY_MOST, 0);
		return true;
	case 0x1000002bU: /* FX LPC11U6 32 kB SRAM/256 kB flash (max) */
		target->driver = "LPC11U6";
		target_add_ram(target, LPC_RAM_BASE, 0x8000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x40000, 0x1000, IAP_ENTRY_MOST, 0);
		return true;
	case 0x3000002bU:
	case 0x3d00002bU:
		target->driver = "LPC1343";
		target_add_ram(target, LPC_RAM_BASE, 0x2000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x8000, 0x1000, IAP_ENTRY_MOST, 0);
		return true;
	case 0x00008a04U: /* LPC8N04 (see UM11074 Rev.1.3 §4.5.19) */
		target->driver = "LPC8N04";
		target_add_ram(target, LPC_RAM_BASE, 0x2000);
		/*
		 * UM11074/ Flash controller/15.2: The two topmost sectors
		 * contain the initialization code and IAP firmware.
		 * Do not touch them!
		 */
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x7800, 0x400, IAP_ENTRY_MOST, 0);
		return true;
	}

	if (device_id && target->designer_code != JEP106_MANUFACTURER_SPECULAR)
		DEBUG_INFO("LPC11xx: Unknown Device ID 0x%08" PRIx32 "\n", device_id);
	return false;
}

static bool lpc8xx_detect(target_s *const target)
{
	/*
	 * For LPC802, see UM11045 Rev. 1.4 §6.6.29 Table 84
	 * For LPC804, see UM11065 Rev. 1.0 §6.6.31 Table 87
	 * For LPC81x, see UM10601 Rev. 1.6 §4.6.33 Table 50
	 * For LPC82x, see UM10800 Rev. 1.2 §5.6.34 Table 55
	 * For LPC83x, see UM11021 Rev. 1.1 §5.6.34 Table 53
	 * For LPC84x, see UM11029 Rev. 1.4 §8.6.49 Table 174
	 *
	 * Not documented, but the DEVICE_ID register at address 0x400483f8
	 * for the LPC8xx series is also valid for the LPC11xx "XL" and the
	 * LPC11U3x variants.
	 */
	const uint32_t device_id = target_mem_read32(target, LPC8XX_DEVICE_ID);
	target->enter_flash_mode = lpc8xx_flash_mode;
	target->exit_flash_mode = lpc8xx_flash_mode;

	switch (device_id) {
	case 0x00008021U: /* LPC802M001JDH20 - 16K Flash 2K SRAM */
	case 0x00008022U: /* LPC802M011JDH20 */
	case 0x00008023U: /* LPC802M001JDH16 */
	case 0x00008024U: /* LPC802M001JHI33 */
		target->driver = "LPC802";
		target_add_ram(target, LPC_RAM_BASE, 0x800);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x4000, 0x400, IAP_ENTRY_84x, 2);
		return true;
	case 0x00008040U: /* LPC804M101JBD64 - 32K Flash 4K SRAM */
	case 0x00008041U: /* LPC804M101JDH20 */
	case 0x00008042U: /* LPC804M101JDH24 */
	case 0x00008043U: /* LPC804M111JDH24 */
	case 0x00008044U: /* LPC804M101JHI33 */
		target->driver = "LPC804";
		target_add_ram(target, LPC_RAM_BASE, 0x1000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x8000, 0x400, IAP_ENTRY_84x, 2);
		return true;
	case 0x00008100U: /* LPC810M021FN8 - 4K Flash 1K SRAM */
	case 0x00008110U: /* LPC811M001JDH16 - 8K Flash 2K SRAM */
	case 0x00008120U: /* LPC812M101JDH16 - 16K Flash 4K SRAM */
	case 0x00008121U: /* LPC812M101JD20 - 16K Flash 4K SRAM */
	case 0x00008122U: /* LPC812M101JDH20 / LPC812M101JTB16 - 16K Flash 4K SRAM */
		target->driver = "LPC81x";
		target_add_ram(target, LPC_RAM_BASE, 0x1000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x4000, 0x400, IAP_ENTRY_MOST, 0);
		return true;
	case 0x00008221U: /* LPC822M101JHI33 - 16K Flash 4K SRAM */
	case 0x00008222U: /* LPC822M101JDH20 */
	case 0x00008241U: /* LPC824M201JHI33 - 32K Flash 8K SRAM */
	case 0x00008242U: /* LPC824M201JDH20 */
		target->driver = "LPC82x";
		target_add_ram(target, LPC_RAM_BASE, 0x2000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x8000, 0x400, IAP_ENTRY_MOST, 0);
		return true;
	case 0x00008322U: /* LPC832M101FDH20 - 16K Flash 4K SRAM */
		target->driver = "LPC832";
		target_add_ram(target, LPC_RAM_BASE, 0x1000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x4000, 0x400, IAP_ENTRY_MOST, 0);
		return true;
	case 0x00008341U: /* LPC834M101FHI33 - 32K Flash 4K SRAM */
		target->driver = "LPC834";
		target_add_ram(target, LPC_RAM_BASE, 0x1000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x8000, 0x400, IAP_ENTRY_MOST, 0);
		return true;
	case 0x00008441U: /* LPC844M201JBD64 - 64K Flash 8K SRAM */
	case 0x00008442U: /* LPC844M201JBD48 */
	case 0x00008443U: /* LPC844M201JHI48, note UM11029 Rev.1.4 table 29 is wrong, see table 174 (in same manual) */
	case 0x00008444U: /* LPC844M201JHI33 */
		target->driver = "LPC844";
		target_add_ram(target, LPC_RAM_BASE, 0x2000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x10000, 0x400, IAP_ENTRY_84x, 0);
		return true;
	case 0x00008451U: /* LPC845M301JBD64 - 64K Flash 16K SRAM */
	case 0x00008452U: /* LPC845M301JBD48 */
	case 0x00008453U: /* LPC845M301JHI48 */
	case 0x00008454U: /* LPC845M301JHI33 */
		target->driver = "LPC845";
		target_add_ram(target, LPC_RAM_BASE, 0x4000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x10000, 0x400, IAP_ENTRY_84x, 0);
		return true;
	case 0x0003d440U: /* LPC11U34/311 - 40K Flash 8K SRAM */
	case 0x0001cc40U: /* LPC11U34/421 - 48K Flash 8K SRAM */
	case 0x0001bc40U: /* LPC11U35/401 - 64K Flash 8K SRAM */
	case 0x0000bc40U: /* LPC11U35/501 - 64K Flash 8K SRAM */
	case 0x00019c40U: /* LPC11U36/401 - 96K Flash 8K SRAM */
	case 0x00017c40U: /* LPC11U37FBD48/401 - 128K Flash 8K SRAM */
	case 0x00007c44U: /* LPC11U37HFBD64/401 */
	case 0x00007c40U: /* LPC11U37FBD64/501 */
		target->driver = "LPC11U3x";
		target_add_ram(target, LPC_RAM_BASE, 0x2000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x20000, 0x1000, IAP_ENTRY_MOST, 0);
		return true;
	case 0x00010013U: /* LPC1111/103 - 8K Flash 2K SRAM */
	case 0x00010012U: /* LPC1111/203 - 8K Flash 4K SRAM */
	case 0x00020023U: /* LPC1112/103 - 16K Flash 2K SRAM */
	case 0x00020022U: /* LPC1112/203 - 16K Flash 4K SRAM */
	case 0x00030030U: /* LPC1113/303 - 24K Flash 8K SRAM */
	case 0x00030032U: /* LPC1113/203 - 24K Flash 4K SRAM */
	case 0x00040040U: /* LPC1114/303 - 32K Flash 8K SRAM */
	case 0x00040042U: /* LPC1114/203 - 32K Flash 4K SRAM */
	case 0x00040060U: /* LPC1114/323 - 48K Flash 8K SRAM */
	case 0x00040070U: /* LPC1114/333 - 56K Flash 8K SRAM */
	case 0x00050080U: /* LPC1115/303 - 64K Flash 8K SRAM */
		target->driver = "LPC11xx-XL";
		target_add_ram(target, LPC_RAM_BASE, 0x2000);
		lpc11xx_add_flash(target, LPC_FLASH_BASE, 0x20000, 0x1000, IAP_ENTRY_MOST, 0);
		return true;
	case 0x00140040U: /* LPC1124/303 - 32K Flash 8K SRAM */
	case 0x00150080U: /* LPC1125/303 - 64K Flash 8K SRAM */
		target->driver = "LPC112x";
		target_add_ram(target, LPC_RAM_BASE, 0x2000U);
		lpc11xx_add_flash(
			target, LPC_FLASH_BASE, device_id == 0x00140040U ? 0x8000U : 0x10000U, 0x1000, IAP_ENTRY_MOST, 0);
		return true;
	}

	if (device_id)
		DEBUG_INFO("LPC8xx: Unknown Device ID 0x%08" PRIx32 "\n", device_id);
	return false;
}

bool lpc11xx_probe(target_s *const target)
{
	const bool result = lpc11xx_detect(target) || lpc8xx_detect(target);
	if (result)
		target_add_commands(target, lpc11xx_cmd_list, target->driver);
	return result;
}

static bool lpc8xx_flash_mode(target_s *const target)
{
	(void)target;
	return true;
}

static bool lpc11xx_read_uid(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	lpc_flash_s *flash = (lpc_flash_s *)target->flash;
	iap_result_s result = {0};
	if (lpc_iap_call(flash, &result, IAP_CMD_READUID))
		return false;
	uint8_t uid[16U] = {0};
	memcpy(&uid, result.values, sizeof(uid));
	tc_printf(target, "UID: 0x");
	for (size_t i = 0; i < sizeof(uid); ++i)
		tc_printf(target, "%02x", uid[i]);
	tc_printf(target, "\n");
	return true;
}
