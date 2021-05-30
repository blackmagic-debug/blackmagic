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

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "lpc_common.h"

#define IAP_PGM_CHUNKSIZE	512	/* should fit in RAM on any device */

#define MIN_RAM_SIZE            1024
#define RAM_USAGE_FOR_IAP_ROUTINES	32	/* IAP routines use 32 bytes at top of ram */

#define IAP_ENTRY_MOST	0x1fff1ff1	/* all except LPC802, LPC804 & LPC84x */
#define IAP_ENTRY_84x	0x0f001ff1  /* LPC802, LPC804 & LPC84x */
#define IAP_RAM_BASE	0x10000000

#define LPC11XX_DEVICE_ID  0x400483F4
#define LPC8XX_DEVICE_ID   0x400483F8

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

static bool lpc11xx_read_uid(target *t, int argc, const char *argv[])
{
	(void)argc;
	(void)argv;
	struct lpc_flash *f = (struct lpc_flash *)t->flash;
	uint8_t uid[16];
	if (lpc_iap_call(f, uid, IAP_CMD_READUID))
		return false;
	tc_printf(t, "UID: 0x");
	for (uint32_t i = 0; i < sizeof(uid); ++i)
		tc_printf(t, "%02x", uid[i]);
	tc_printf(t, "\n");
	return true;
}

const struct command_s lpc11xx_cmd_list[] = {
	{"readuid", lpc11xx_read_uid, "Read out the 16-byte UID."},
	{NULL, NULL, NULL}
};

void lpc11xx_add_flash(target *t, uint32_t addr, size_t len, size_t erasesize, uint32_t iap_entry, uint8_t reserved_pages)
{
	struct lpc_flash *lf = lpc_add_flash(t, addr, len);
	lf->f.blocksize = erasesize;
	lf->f.buf_size = IAP_PGM_CHUNKSIZE;
	lf->f.write = lpc_flash_write_magic_vect;
	lf->iap_entry = iap_entry;
	lf->iap_ram = IAP_RAM_BASE;
	lf->iap_msp = IAP_RAM_BASE + MIN_RAM_SIZE - RAM_USAGE_FOR_IAP_ROUTINES;
	lf->reserved_pages = reserved_pages;
}

bool
lpc11xx_probe(target *t)
{
	uint32_t idcode;

	/* read the device ID register */
	/* See UM10462 Rev. 5.5 Chapter 20.13.11 Table 377 */
	idcode = target_mem_read32(t, LPC11XX_DEVICE_ID);
	switch (idcode) {
	case 0x041E502B:
	case 0x2516D02B:
	case 0x0416502B:
	case 0x2516902B:	/* lpc1111 */
	case 0x2524D02B:
	case 0x0425502B:
	case 0x2524902B:
	case 0x1421102B:	/* lpc1112 */
	case 0x0434502B:
	case 0x2532902B:
	case 0x0434102B:
	case 0x2532102B:	/* lpc1113 */
	case 0x0444502B:
	case 0x2540902B:
	case 0x0444102B:
	case 0x2540102B:
	case 0x1440102B:	/* lpc1114 */
	case 0x0A40902B:
	case 0x1A40902B:
	case 0x00050080:	/* lpc1115 and lpc1115L (not the XL version. See UM10398 Rev12.4 Chapter 3.1  ) */
	case 0x1431102B:	/* lpc11c22 */
	case 0x1430102B:	/* lpc11c24 */
	case 0x095C802B:	/* lpc11u12x/201 */
	case 0x295C802B:
	case 0x097A802B:	/* lpc11u13/201 */
	case 0x297A802B:
	case 0x0998802B:	/* lpc11u14x/201 */
	case 0x2998802B:
	case 0x2972402B:	/* lpc11u23/301 */
	case 0x2988402B:	/* lpc11u24x/301 */
	case 0x2980002B:	/* lpc11u24x/401 */
		t->driver = "LPC11xx";
		target_add_ram(t, 0x10000000, 0x2000);
		lpc11xx_add_flash(t, 0x00000000, 0x20000, 0x1000, IAP_ENTRY_MOST, 0);
		target_add_commands(t, lpc11xx_cmd_list, "LPC11xx");
		return true;

	case 0x0A24902B:
	case 0x1A24902B:
		t->driver = "LPC1112";
		target_add_ram(t, 0x10000000, 0x1000);
		lpc11xx_add_flash(t, 0x00000000, 0x10000, 0x1000, IAP_ENTRY_MOST, 0);
		return true;
    case 0x1000002b: // FX LPC11U6 32 kB SRAM/256 kB flash (max)
		t->driver = "LPC11U6";
		target_add_ram(t, 0x10000000, 0x8000);
		lpc11xx_add_flash(t, 0x00000000, 0x40000, 0x1000, IAP_ENTRY_MOST, 0);
		return true;
	case 0x3000002B:
	case 0x3D00002B:
		t->driver = "LPC1343";
		target_add_ram(t, 0x10000000, 0x2000);
		lpc11xx_add_flash(t, 0x00000000, 0x8000, 0x1000, IAP_ENTRY_MOST, 0);
		return true;
	case 0x00008A04:  /* LPC8N04 (see UM11074 Rev.1.3 section 4.5.19) */
		t->driver = "LPC8N04";
		target_add_ram(t, 0x10000000, 0x2000);
		/* UM11074/ Flash controller/15.2: The two topmost sectors
		 * contain the initialization code and IAP firmware.
		 * Do not touch the! */
		lpc11xx_add_flash(t, 0x00000000, 0x7800, 0x400, IAP_ENTRY_MOST, 0);
		target_add_commands(t, lpc11xx_cmd_list, "LPC8N04");
		return true;
	}
	if ((t->t_designer != AP_DESIGNER_SPECULAR) && idcode) {
		DEBUG_INFO("LPC11xx: Unknown IDCODE 0x%08" PRIx32 "\n", idcode);
	}
	idcode = target_mem_read32(t, LPC8XX_DEVICE_ID);
	switch (idcode) {
	case 0x00008021:  /* 802M001JDH20 */
	case 0x00008022:  /* 802M011JDH20 */
	case 0x00008023:  /* 802M001JDH16 */
	case 0x00008024:  /* 802M001JHI33 */
	  t->driver = "LPC802";
	  target_add_ram(t, 0x10000000, 0x800);
	  lpc11xx_add_flash(t, 0x00000000, 0x4000, 0x400, IAP_ENTRY_84x, 2);
	  target_add_commands(t, lpc11xx_cmd_list, "LPC802");
	  return true;
	case 0x00008040:  /* 804M101JBD64 */
	case 0x00008041:  /* 804M101JDH20 */
	case 0x00008042:  /* 804M101JDH24 */
	case 0x00008043:  /* 804M111JDH24 */
	case 0x00008044:  /* 804M101JHI33 */
	  t->driver = "LPC804";
	  target_add_ram(t, 0x10000000, 0x1000);
	  lpc11xx_add_flash(t, 0x00000000, 0x8000, 0x400, IAP_ENTRY_84x, 2);
	  target_add_commands(t, lpc11xx_cmd_list, "LPC804");
	  return true;
	case 0x00008100:  /* LPC810M021FN8 */
	case 0x00008110:  /* LPC811M001JDH16 */
	case 0x00008120:  /* LPC812M101JDH16 */
	case 0x00008121:  /* LPC812M101JD20 */
	case 0x00008122:  /* LPC812M101JDH20 / LPC812M101JTB16 */
		t->driver = "LPC81x";
		target_add_ram(t, 0x10000000, 0x1000);
		lpc11xx_add_flash(t, 0x00000000, 0x4000, 0x400, IAP_ENTRY_MOST, 0);
		target_add_commands(t, lpc11xx_cmd_list, "LPC81x");
		return true;
	case 0x00008221:  /* LPC822M101JHI33 */
	case 0x00008222:  /* LPC822M101JDH20 */
	case 0x00008241:  /* LPC824M201JHI33 */
	case 0x00008242:  /* LPC824M201JDH20 */
		t->driver = "LPC82x";
		target_add_ram(t, 0x10000000, 0x2000);
		lpc11xx_add_flash(t, 0x00000000, 0x8000, 0x400, IAP_ENTRY_MOST, 0);
		target_add_commands(t, lpc11xx_cmd_list, "LPC82x");
		return true;
	case 0x00008322:  /* LPC832M101FDH20 */
		t->driver = "LPC832";
		target_add_ram(t, 0x10000000, 0x1000);
		lpc11xx_add_flash(t, 0x00000000, 0x4000, 0x400, IAP_ENTRY_MOST, 0);
		target_add_commands(t, lpc11xx_cmd_list, "LPC832");
		return true;
	case 0x00008341:  /* LPC8341201FHI33 */
		t->driver = "LPC834";
		target_add_ram(t, 0x10000000, 0x1000);
		lpc11xx_add_flash(t, 0x00000000, 0x8000, 0x400, IAP_ENTRY_MOST, 0);
		target_add_commands(t, lpc11xx_cmd_list, "LPC834");
		return true;
	case 0x00008441:
	case 0x00008442:
	case 0x00008443: /* UM11029 Rev.1.4 list 8442 */
	case 0x00008444:
		t->driver = "LPC844";
		target_add_ram(t, 0x10000000, 0x2000);
		lpc11xx_add_flash(t, 0x00000000, 0x10000, 0x400, IAP_ENTRY_84x, 0);
		return true;
	case 0x00008451:
	case 0x00008452:
	case 0x00008453:
	case 0x00008454:
		t->driver = "LPC845";
		target_add_ram(t, 0x10000000, 0x4000);
		lpc11xx_add_flash(t, 0x00000000, 0x10000, 0x400, IAP_ENTRY_84x, 0);
		return true;
	case 0x0003D440:	/* LPC11U34/311  */
	case 0x0001cc40:	/* LPC11U34/421  */
	case 0x0001BC40:	/* LPC11U35/401  */
	case 0x0000BC40:	/* LPC11U35/501  */
	case 0x00019C40:	/* LPC11U36/401  */
	case 0x00017C40:	/* LPC11U37FBD48/401  */
	case 0x00007C44:	/* LPC11U37HFBD64/401  */
	case 0x00007C40:	/* LPC11U37FBD64/501  */
		t->driver = "LPC11U3x";
		target_add_ram(t, 0x10000000, 0x2000);
		lpc11xx_add_flash(t, 0x00000000, 0x20000, 0x1000, IAP_ENTRY_MOST, 0);
		return true;
	case 0x00040070:	/* LPC1114/333 */
	case 0x00050080:	/* lpc1115XL */
		t->driver = "LPC1100XL";
		target_add_ram(t, 0x10000000, 0x2000);
		lpc11xx_add_flash(t, 0x00000000, 0x20000, 0x1000, IAP_ENTRY_MOST, 0);
		return true;
	}
	if (idcode) {
		DEBUG_INFO("LPC8xx: Unknown IDCODE 0x%08" PRIx32 "\n", idcode);
	}

	return false;
}
