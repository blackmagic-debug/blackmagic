/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2017  Black Sphere Technologies Ltd.
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

/* Non-Volatile Memory Controller (NVMC) Registers */
#define NVMC           ((volatile uint32_t *)0x4001E000)
#define NVMC_READY     NVMC[0x100]

void __attribute__((naked))
nrf51_flash_write_stub(volatile uint32_t *dest, uint32_t *src, uint32_t size)
{
	for (int i; i < size; i += 4) {
		*dest++ = *src++;
		while (!(NVMC_READY & 1))
			;
	}

	stub_exit(0);
}
