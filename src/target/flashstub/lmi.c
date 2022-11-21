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
#include <stdint.h>
#include "stub.h"

#define LMI_FLASH_BASE ((volatile uint32_t *)0x400fd000U)
#define LMI_FLASH_FMA  LMI_FLASH_BASE[0]
#define LMI_FLASH_FMD  LMI_FLASH_BASE[1]
#define LMI_FLASH_FMC  LMI_FLASH_BASE[2]

#define LMI_FLASH_FMC_WRITE  (1U << 0U)
#define LMI_FLASH_FMC_ERASE  (1U << 1U)
#define LMI_FLASH_FMC_MERASE (1U << 2U)
#define LMI_FLASH_FMC_COMT   (1U << 3U)
#define LMI_FLASH_FMC_WRKEY  0xa4420000U

void __attribute__((naked))
stm32f1_flash_write_stub(const uint32_t *const dest, const uint32_t *const src, const uint32_t size)
{
	for (uint32_t i; i < (size / 4U); ++i) {
		LMI_FLASH_FMA = (uintptr_t)(dest + i);
		LMI_FLASH_FMD = src[i];
		LMI_FLASH_FMC = LMI_FLASH_FMC_WRKEY | LMI_FLASH_FMC_WRITE;
		while (LMI_FLASH_FMC & LMI_FLASH_FMC_WRITE)
			continue;
	}

	stub_exit(0);
}
