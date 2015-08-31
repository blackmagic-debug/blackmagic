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
#include "stub.h"
#include <stdint.h>

/* No STM32L4 definitions in libopencm3 yet */
#define FLASH_SR ((volatile uint32_t *) 0x40022010)
#define FLASH_SR_EOP		(1 << 0)
#define SR_ERROR_MASK		0xC3FA
#define FLASH_SR_BSY		(1 << 16)

#define FLASH_CR ((volatile uint32_t *) 0x40022014)
#define FLASH_CR_PG			(1 << 0)
#define FLASH_CR_EOPIE		(1 << 24)
#define FLASH_CR_ERRIE		(1 << 25)
#define FLASH_SR_EOP		(1 << 0)

void __attribute__((naked))
stm32l4_flash_write_stub(uint32_t *dest, uint32_t *src, uint32_t size)
{
	if ((size & 7) || ((uint32_t)dest & 7))
		stub_exit(1);
	for (int i = 0; i < size; i += 8) {
		*FLASH_CR =  FLASH_CR_EOPIE | FLASH_CR_ERRIE | FLASH_CR_PG;
		*dest++ = *src++;
		*dest++ = *src++;
		__asm("dsb");
		while (*FLASH_SR & FLASH_SR_BSY)
			;
		if ((*FLASH_SR & SR_ERROR_MASK) || !(*FLASH_SR & FLASH_SR_EOP))
			stub_exit(1);
		*FLASH_SR |= FLASH_SR_EOP;
	}
	*FLASH_CR = 0;
	stub_exit(0);
}
