/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
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

/* This file implements RISC-V CH32Vx target specific functions */

#include "general.h"
#include "target.h"
#include "target_internal.h"

/* IDCODE register */
#define CH32VX_IDCODE               0x1ffff704U
#define CH32VX_IDCODE_MASK          0x0ffffff0f
#define CH32VX_IDCODE_FAMILY_OFFSET 20U
#define CH32VX_IDCODE_FAMILY_MASK   (0xfffU << CH32VX_IDCODE_FAMILY_OFFSET)

#define CH32V203_IDCODE_FAMILY 0x203U
#define CH32V208_IDCODE_FAMILY 0x208U
#define CH32V305_IDCODE_FAMILY 0x305U
#define CH32V303_IDCODE_FAMILY 0x303U
#define CH32V307_IDCODE_FAMILY 0x307U

bool ch32vx_probe(target_s *const target)
{
	const uint32_t idcode = target_mem32_read32(target, CH32VX_IDCODE);

	switch (idcode & CH32VX_IDCODE_MASK) {
	case 0x30330504U: /* CH32V303CBT6 */
	case 0x30320504U: /* CH32V303RBT6 */
	case 0x30310504U: /* CH32V303RCT6 */
	case 0x30300504U: /* CH32V303VCT6 */
	case 0x30520508U: /* CH32V305FBP6 */
	case 0x30500508U: /* CH32V305RBT6 */
	case 0x30730508U: /* CH32V307WCU6 */
	case 0x30720508U: /* CH32V307FBP6 */
	case 0x30710508U: /* CH32V307RCT6 */
	case 0x30700508U: /* CH32V307VCT6 */
		break;
	default:
		return false;
		break;
	}

	const uint16_t family = (idcode & CH32VX_IDCODE_FAMILY_MASK) >> CH32VX_IDCODE_FAMILY_OFFSET;
	switch (family) {
	case CH32V303_IDCODE_FAMILY:
		target->driver = "CH32V303";
		break;
	case CH32V305_IDCODE_FAMILY:
		target->driver = "CH32V305";
		break;
	case CH32V307_IDCODE_FAMILY:
		target->driver = "CH32V307";
		break;
	default:
		return false;
		break;
	}

	target->part_id = idcode;

	return true;
}
