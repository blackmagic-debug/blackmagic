/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Richard Meadows
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

#define EFM32_MSC_WRITECTRL(msc) *((volatile uint32_t *)((msc) + 0x008U))
#define EFM32_MSC_WRITECMD(msc)  *((volatile uint32_t *)((msc) + 0x00cU))
#define EFM32_MSC_ADDRB(msc)     *((volatile uint32_t *)((msc) + 0x010U))
#define EFM32_MSC_WDATA(msc)     *((volatile uint32_t *)((msc) + 0x018U))
#define EFM32_MSC_STATUS(msc)    *((volatile uint32_t *)((msc) + 0x01cU))
#define EFM32_MSC_LOCK(msc)      *((volatile uint32_t *)((msc) + ((msc) == 0x400c0000U ? 0x3cU : 0x40U)))
#define EFM32_MSC_MASSLOCK(msc)  *((volatile uint32_t *)((msc) + 0x054U))

#define EFM32_MSC_LOCK_LOCKKEY 0x1b71U

#define EFM32_MSC_WRITECMD_LADDRIM    (1U << 0U)
#define EFM32_MSC_WRITECMD_ERASEPAGE  (1U << 1U)
#define EFM32_MSC_WRITECMD_WRITEEND   (1U << 2U)
#define EFM32_MSC_WRITECMD_WRITEONCE  (1U << 3U)
#define EFM32_MSC_WRITECMD_WRITETRIG  (1U << 4U)
#define EFM32_MSC_WRITECMD_ERASEABORT (1U << 5U)

#define EFM32_MSC_STATUS_BUSY        (1U << 0U)
#define EFM32_MSC_STATUS_LOCKED      (1U << 1U)
#define EFM32_MSC_STATUS_INVADDR     (1U << 2U)
#define EFM32_MSC_STATUS_WDATAREADY  (1U << 3U)
#define EFM32_MSC_STATUS_WORDTIMEOUT (1U << 4U)

void __attribute__((naked))
efm32_flash_write_stub(const uint32_t *const dest, const uint32_t *const src, uint32_t size, const uint32_t msc_addr)
{
	const uintptr_t msc = msc_addr;
	EFM32_MSC_LOCK(msc) = EFM32_MSC_LOCK_LOCKKEY;
	EFM32_MSC_WRITECTRL(msc) = 1;

	for (uint32_t i = 0; i < size / 4U; i++) {
		EFM32_MSC_ADDRB(msc) = (uintptr_t)(dest + i);
		EFM32_MSC_WRITECMD(msc) = EFM32_MSC_WRITECMD_LADDRIM;

		/* Wait for WDATAREADY */
		while (!(EFM32_MSC_STATUS(msc) & EFM32_MSC_STATUS_WDATAREADY))
			continue;

		EFM32_MSC_WDATA(msc) = src[i];
		EFM32_MSC_WRITECMD(msc) = EFM32_MSC_WRITECMD_WRITEONCE;

		/* Wait for BUSY */
		while ((EFM32_MSC_STATUS(msc) & EFM32_MSC_STATUS_BUSY))
			continue;
	}

	stub_exit(0);
}
