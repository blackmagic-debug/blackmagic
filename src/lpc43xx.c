/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012 Gareth McMullin <gareth@blacksphere.co.nz>
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
#include "adiv5.h"
#include "target.h"

#define LPC43XX_CHIPID	0x40043200
#define ARM_CPUID	0xE000ED00

bool lpc43xx_probe(struct target_s *target)
{
	uint32_t chipid, cpuid;

	chipid = adiv5_ap_mem_read(adiv5_target_ap(target), LPC43XX_CHIPID);
	cpuid = adiv5_ap_mem_read(adiv5_target_ap(target), ARM_CPUID);

	switch(chipid) {
	case 0x4906002B:	/* Parts with on-chip flash */
	case 0x5906002B:	/* Flashless parts */
	case 0x6906002B:
		switch (cpuid & 0xFF00FFF0) {
		case 0x4100C240:
			target->driver = "LPC43xx Cortex-M4";
			break;
		case 0x4100C200:
			target->driver = "LPC43xx Cortex-M0";
			break;
		default:
			target->driver = "LPC43xx <Unknown>";
		}
		return true;
	}

	return false;
}

