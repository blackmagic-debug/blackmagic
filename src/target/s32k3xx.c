/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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
 * This file implements S32K3xx target specific functions providing
 * the XML memory map and Flash memory programming.
 */

#include <assert.h>

#include "command.h"
#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "adiv5.h"
#include "cortexm.h"

#define SIUL2_MIDR1 0x40290004U

bool s32k3xx_probe(target_s *const target)
{
	uint32_t midr1 = target_mem_read32(target, SIUL2_MIDR1);
	char product_letter = (midr1 >> 26U) & 0x3fU;
	uint32_t part_no = (midr1 >> 16U) & 0x3ffU;

	if (product_letter != 0xbU)
		return false;

	switch (part_no) {
    default:
      return false;
  }

	return true;
}
