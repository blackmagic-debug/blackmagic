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
#include "libopencm3/stm32/flash.h"
#include "stub.h"

#define SR_ERROR_MASK 0x14

void __attribute__((naked))
stm32f1_flash_write_stub(uint16_t *dest, uint16_t *src, uint32_t size)
{
	for (int i; i < size; i += 2) {
		FLASH_CR = FLASH_CR_PG;
		*dest++ = *src++;
		while (FLASH_SR & FLASH_SR_BSY)
			;
	}

	if (FLASH_SR & SR_ERROR_MASK)
		stub_exit(1);

	stub_exit(0);
}

