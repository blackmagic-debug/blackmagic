/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Mike Smith <drziplok@me.com>
 * Copyright (C) 2016 Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2016 David Lawrence <dlaw@markforged.com>
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
 * This file implements support for LPC15xx series devices, providing
 * memory maps and Flash programming routines.
 *
 * References and details about the IAP variant used here:
 * LPC15xx 32-bit ARM Cortex-M3 microcontroller, Product data sheet, Rev. 1.1
 *   https://www.nxp.com/docs/en/data-sheet/LPC15XX.pdf
 * and (behind their login wall):
 * UM10736 - LPC15xx User manual, Rev. 1.2
 *   https://www.nxp.com/webapp/Download?colCode=UM10736&location=null
 */

#include <string.h>
#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "lpc_common.h"

#define LPC15xx_SRAM_SIZE_MIN 1024U
#define LPC15xx_SRAM_IAP_SIZE 32U /* IAP routines use 32 bytes at top of ram */

#define LPC15xx_IAP_ENTRYPOINT_LOCATION 0x03000205U
#define LPC15xx_IAP_RAM_BASE            0x02000000U

#define LPC15xx_IAP_PGM_CHUNKSIZE 512U /* Should fit in RAM on any device */

#define LPC15xx_DEVICE_ID 0x400743f8U

static void lpc15xx_add_flash(target_s *const target, const uint32_t addr, const size_t len, const size_t erasesize)
{
	struct lpc_flash *flash = lpc_add_flash(target, addr, len, LPC15xx_IAP_PGM_CHUNKSIZE);
	flash->target_flash.blocksize = erasesize;
	flash->target_flash.write = lpc_flash_write_magic_vect;
}

bool lpc15xx_probe(target_s *const target)
{
	/* Read the device ID register */
	const uint32_t device_id = target_mem32_read32(target, LPC15xx_DEVICE_ID);

	uint32_t ram_size = 0;
	switch (device_id) {
	case 0x00001549U:
	case 0x00001519U:
		ram_size = 0x9000U;
		break;
	case 0x00001548U:
	case 0x00001518U:
		ram_size = 0x5000U;
		break;
	case 0x00001547U:
	case 0x00001517U:
		ram_size = 0x3000U;
		break;
	default:
		return false;
	}

	/* Allocate the private structure needed for lpc_iap_call() to work */
	lpc_priv_s *const priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	target->target_storage = priv;

	/* Set the structure up for this target */
	priv->iap_params = lpc_iap_params;
	priv->iap_entry = LPC15xx_IAP_ENTRYPOINT_LOCATION;
	priv->iap_ram = LPC15xx_IAP_RAM_BASE;
	priv->iap_msp = LPC15xx_IAP_RAM_BASE + LPC15xx_SRAM_SIZE_MIN - LPC15xx_SRAM_IAP_SIZE;

	/* Register Flash and RAM maps + target-specific commands */
	target->driver = "LPC15xx";
	target_add_ram32(target, 0x02000000, ram_size);
	lpc15xx_add_flash(target, 0x00000000, 0x40000, 0x1000);
	lpc_add_commands(target);
	return true;
}
