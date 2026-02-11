/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2026 1BitSquared <info@1bitsquared.com>
 * Written by hardesk <hardesk17@gmail.com>
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
#include <stdint.h>
#include "stub.h"

#define MSPM0_FLASH_MAIN      0x00000000U
#define MSPM0_FLASH_SECTOR_SZ 1024U

#define MSPM0_FLASHCTL_BASE             0x400cd000U
#define MSPM0_FLASHCTL_CMDEXEC          *((volatile uint32_t *)(MSPM0_FLASHCTL_BASE + 0x1100U))
#define MSPM0_FLASHCTL_CMDTYPE          *((volatile uint32_t *)(MSPM0_FLASHCTL_BASE + 0x1104U))
#define MSPM0_FLASHCTL_CMDCTL           *((volatile uint32_t *)(MSPM0_FLASHCTL_BASE + 0x1108U))
#define MSPM0_FLASHCTL_CMDADDR          *((volatile uint32_t *)(MSPM0_FLASHCTL_BASE + 0x1120U))
#define MSPM0_FLASHCTL_BYTEN            *((volatile uint32_t *)(MSPM0_FLASHCTL_BASE + 0x1124U))
#define MSPM0_FLASHCTL_STATCMD          *((volatile uint32_t *)(MSPM0_FLASHCTL_BASE + 0x13d0U))
#define MSPM0_FLASHCTL_CMDDATA0         *((volatile uint32_t *)(MSPM0_FLASHCTL_BASE + 0x1130U))
#define MSPM0_FLASHCTL_CMDDATA1         *((volatile uint32_t *)(MSPM0_FLASHCTL_BASE + 0x1134U))
#define MSPM0_FLASHCTL_CMDWEPROTA       *((volatile uint32_t *)(MSPM0_FLASHCTL_BASE + 0x11d0U))
#define MSPM0_FLASHCTL_CMDWEPROTB       *((volatile uint32_t *)(MSPM0_FLASHCTL_BASE + 0x11d4U))
#define MSPM0_FLASHCTL_CMDWEPROTC       *((volatile uint32_t *)(MSPM0_FLASHCTL_BASE + 0x11d8U))
#define MSPM0_FLASHCTL_CMDTYPE_PROG     1U
#define MSPM0_FLASHCTL_CMDTYPE_SZ_1WORD (0U << 4U)
#define MSPM0_FLASHCTL_CMDEXEC_EXEC     1U
#define MSPM0_FLASHCTL_STATCMD_DONE     0x01U
#define MSPM0_FLASHCTL_STATCMD_CMDPASS  0x02U

void mspm0_flash_write_stub(const uint32_t *const dest, const uint32_t *const src, const uint32_t size)
{
	for (uint32_t i = 0U; i < size / 4; i += 2) {
		uint32_t addr = (uint32_t)(dest + i);
		uint32_t sector = (addr - MSPM0_FLASH_MAIN) / MSPM0_FLASH_SECTOR_SZ;

		if (sector < 32U)
			MSPM0_FLASHCTL_CMDWEPROTA = ~(1U << sector);
		else if (sector < 256U)
			MSPM0_FLASHCTL_CMDWEPROTB = 0U;
		else
			MSPM0_FLASHCTL_CMDWEPROTC = 0U;

		MSPM0_FLASHCTL_CMDCTL = 0U;
		MSPM0_FLASHCTL_BYTEN = 0xffffffffU;
		MSPM0_FLASHCTL_CMDTYPE = MSPM0_FLASHCTL_CMDTYPE_PROG | MSPM0_FLASHCTL_CMDTYPE_SZ_1WORD;

		MSPM0_FLASHCTL_CMDADDR = addr;
		MSPM0_FLASHCTL_CMDDATA0 = src[i];
		MSPM0_FLASHCTL_CMDDATA1 = src[i + 1U];
		MSPM0_FLASHCTL_CMDEXEC = MSPM0_FLASHCTL_CMDEXEC_EXEC;
		while (!(MSPM0_FLASHCTL_STATCMD & MSPM0_FLASHCTL_STATCMD_DONE))
			continue;
		if (!(MSPM0_FLASHCTL_STATCMD & MSPM0_FLASHCTL_STATCMD_CMDPASS))
			stub_exit(0);
	}

	stub_exit(1);
}