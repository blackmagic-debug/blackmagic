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

#define LMI_FLASH_BASE       ((volatile uint32_t *)0x400FD000)
#define LMI_FLASH_FMA        LMI_FLASH_BASE[0]
#define LMI_FLASH_FMD        LMI_FLASH_BASE[1]
#define LMI_FLASH_FMC        LMI_FLASH_BASE[2]

#define LMI_FLASH_FMC_WRITE  (1 << 0)
#define LMI_FLASH_FMC_ERASE  (1 << 1)
#define LMI_FLASH_FMC_MERASE (1 << 2)
#define LMI_FLASH_FMC_COMT   (1 << 3)
#define LMI_FLASH_FMC_WRKEY  0xA4420000

void __attribute__((naked))
stm32f1_flash_write_stub(uint32_t *dest, uint32_t *src, uint32_t size)
{
	size /= 4;
	for (int i; i < size; i++) {
		LMI_FLASH_FMA = (uint32_t)&dest[i];
		LMI_FLASH_FMD = src[i];
		LMI_FLASH_FMC = LMI_FLASH_FMC_WRKEY | LMI_FLASH_FMC_WRITE;
		while (LMI_FLASH_FMC & LMI_FLASH_FMC_WRITE)
			;
	}

	stub_exit(0);
}


