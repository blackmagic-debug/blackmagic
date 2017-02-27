/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2017  Black Sphere Technologies Ltd.
 * Written by Gordon Smith <gordonhj.smith@gmail.com>
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

#define SR_ERROR_MASK 0xF2

void __attribute__((naked))
stm32f4_flash_write_x8_stub(uint32_t *dest, uint32_t *src, uint32_t size)
{
	uint8_t *b_dest, *b_src;
	b_dest = (void *)dest;
	b_src = (void *)src;
	for (int i = 0; i < size; i += 1) {
		FLASH_CR = FLASH_CR_PROGRAM_X8 | FLASH_CR_PG;
		*b_dest++ = *b_src++;
		__asm("dsb");
		while (FLASH_SR & FLASH_SR_BSY)
			;
	}

	if (FLASH_SR & SR_ERROR_MASK)
		stub_exit(1);

	stub_exit(0);
}

