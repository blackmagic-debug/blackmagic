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

#define IAP_ENTRYPOINT	0x1fff1ff1
#define IAP_RAM_BASE	0x10000000

#define LPC11XX_DEVICE_ID  0x400483F4
#define LPC8XX_DEVICE_ID   0x400483F8

void lpc11xx_add_flash(target *t, uint32_t addr, size_t len, size_t erasesize)
{
	struct lpc_flash *lf = lpc_add_flash(t, addr, len);
	lf->f.blocksize = erasesize;
	lf->f.buf_size = IAP_PGM_CHUNKSIZE;
	lf->f.write = lpc_flash_write_magic_vect;
	lf->iap_entry = IAP_ENTRYPOINT;
	lf->iap_ram = IAP_RAM_BASE;
	lf->iap_msp = IAP_RAM_BASE + MIN_RAM_SIZE - RAM_USAGE_FOR_IAP_ROUTINES;
}

bool
lpc11xx_probe(target *t)
{
	uint32_t idcode;

	/* read the device ID register */
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
	case 0x2058002B:	/* lpc1115 */
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
		lpc11xx_add_flash(t, 0x00000000, 0x20000, 0x1000);
		return true;

	case 0x0A24902B:
	case 0x1A24902B:
		t->driver = "LPC1112";
		target_add_ram(t, 0x10000000, 0x1000);
		lpc11xx_add_flash(t, 0x00000000, 0x10000, 0x1000);
		return true;
	}

	idcode = target_mem_read32(t, LPC8XX_DEVICE_ID);
	switch (idcode) {
	case 0x00008100:  /* LPC810M021FN8 */
	case 0x00008110:  /* LPC811M001JDH16 */
	case 0x00008120:  /* LPC812M101JDH16 */
	case 0x00008121:  /* LPC812M101JD20 */
	case 0x00008122:  /* LPC812M101JDH20 / LPC812M101JTB16 */
		t->driver = "LPC81x";
		target_add_ram(t, 0x10000000, 0x1000);
		lpc11xx_add_flash(t, 0x00000000, 0x4000, 0x400);
		return true;
        case 0x00008221:  /* LPC822M101JHI33 */
        case 0x00008222:  /* LPC822M101JDH20 */
        case 0x00008241:  /* LPC824M201JHI33 */
        case 0x00008242:  /* LPC824M201JDH20 */
		t->driver = "LPC82x";
		target_add_ram(t, 0x10000000, 0x2000);
		lpc11xx_add_flash(t, 0x00000000, 0x8000, 0x400);
		return true;

	}

	return false;
}
