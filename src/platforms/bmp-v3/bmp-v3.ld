/*
 * This file is part of the libopenstm32 project.
 *
 * Copyright (C) 2010 Thomas Otto <tommi@viadmin.org>
 * Copyright (C) 2024-2025 1BitSquared <info@1bitsquared.com>
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

/* Define memory regions. */
MEMORY
{
	rom (rx) : ORIGIN = 0x08000000, LENGTH = 2M
	/*
	 * We actually have 786KiB available, but we want to be in the main 512KiB area
	 * that optionally runs ECC. We're not using ECC, however this is one large 8x64KiB
	 * block that is contiguous, marked NS, and behaves the way we expect it to.
	 *
	 * NB: SRAM on this part is mapped as:
	 * SRAM1: 0x20000000 - 0x20030000
	 * SRAM2: 0x20030000 - 0x20080000
	 * SRAM3: 0x20080000 - 0x200c0000
	 *
	 * SRAM2 is erased when the system is reset, all other SRAMs retain their contents.
	 */
	ram (rwx) : ORIGIN = 0x20080000, LENGTH = 512K
}

/* Include the platform common linker script. */
INCLUDE ../common/blackmagic.ld
