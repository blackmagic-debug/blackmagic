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

#include <string.h>
#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "lpc_common.h"

#define IAP_PGM_CHUNKSIZE 512U /* Should fit in RAM on any device */

#define MIN_RAM_SIZE               1024U
#define RAM_USAGE_FOR_IAP_ROUTINES 32U /* IAP routines use 32 bytes at top of ram */

#define IAP_ENTRYPOINT 0x03000205U
#define IAP_RAM_BASE   0x02000000U

#define LPC15XX_DEVICE_ID 0x400743f8U

static bool lpc15xx_read_uid(target_s *target, int argc, const char *argv[])
{
	(void)argc;
	(void)argv;
	struct lpc_flash *flash = (struct lpc_flash *)target->flash;
	iap_result_s result = {0};
	if (lpc_iap_call(flash, &result, IAP_CMD_READUID))
		return false;
	uint8_t uid[16U] = {0};
	memcpy(&uid, result.values, sizeof(uid));
	tc_printf(target, "UID: 0x");
	for (uint32_t i = 0; i < sizeof(uid); ++i)
		tc_printf(target, "%02x", uid[i]);
	tc_printf(target, "\n");
	return true;
}

const command_s lpc15xx_cmd_list[] = {
	{"readuid", lpc15xx_read_uid, "Read out the 16-byte UID."},
	{NULL, NULL, NULL},
};

static void lpc15xx_add_flash(target_s *target, uint32_t addr, size_t len, size_t erasesize)
{
	struct lpc_flash *flash = lpc_add_flash(target, addr, len, IAP_PGM_CHUNKSIZE);
	flash->f.blocksize = erasesize;
	flash->f.write = lpc_flash_write_magic_vect;
	flash->iap_entry = IAP_ENTRYPOINT;
	flash->iap_ram = IAP_RAM_BASE;
	flash->iap_msp = IAP_RAM_BASE + MIN_RAM_SIZE - RAM_USAGE_FOR_IAP_ROUTINES;
}

bool lpc15xx_probe(target_s *t)
{
	/* read the device ID register */
	const uint32_t device_id = target_mem_read32(t, LPC15XX_DEVICE_ID);

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

	t->driver = "LPC15xx";
	target_add_ram(t, 0x02000000, ram_size);
	lpc15xx_add_flash(t, 0x00000000, 0x40000, 0x1000);
	target_add_commands(t, lpc15xx_cmd_list, "LPC15xx");
	return true;
}
