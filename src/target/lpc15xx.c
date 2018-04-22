/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Mike Smith <drziplok@me.com>
 * Copyright (C) 2016 Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2016 David Lawrence <dlaw@markforged.com>
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

#define IAP_ENTRYPOINT	0x03000205
#define IAP_RAM_BASE	0x02000000

#define LPC15XX_DEVICE_ID  0x400743F8

void lpc15xx_add_flash(target *t, uint32_t addr, size_t len, size_t erasesize)
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
lpc15xx_probe(target *t)
{
	uint32_t idcode;
	uint32_t ram_size = 0;

	/* read the device ID register */
	idcode = target_mem_read32(t, LPC15XX_DEVICE_ID);
	switch (idcode) {
	case 0x00001549:
	case 0x00001519:
		ram_size = 0x9000;
		break;
	case 0x00001548:
	case 0x00001518:
		ram_size = 0x5000;
		break;
	case 0x00001547:
	case 0x00001517:
		ram_size = 0x3000;
		break;
	}
	if (ram_size) {
		t->driver = "LPC15xx";
		target_add_ram(t, 0x02000000, ram_size);
		lpc15xx_add_flash(t, 0x00000000, 0x40000, 0x1000);
		return true;
	}

	return false;
}
