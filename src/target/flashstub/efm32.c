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

#define EFM32_MSC_WRITECTRL(msc)		*((volatile uint32_t *)(msc+0x008))
#define EFM32_MSC_WRITECMD(msc)	    	*((volatile uint32_t *)(msc+0x00c))
#define EFM32_MSC_ADDRB(msc)			*((volatile uint32_t *)(msc+0x010))
#define EFM32_MSC_WDATA(msc)			*((volatile uint32_t *)(msc+0x018))
#define EFM32_MSC_STATUS(msc)			*((volatile uint32_t *)(msc+0x01c))
#define EFM32_MSC_LOCK(msc)				*((volatile uint32_t *)(msc+(msc == 0x400c0000?0x3c:0x40)))
#define EFM32_MSC_MASSLOCK(msc)	    	*((volatile uint32_t *)(msc+0x054))

#define EFM32_MSC_LOCK_LOCKKEY          0x1b71

#define EFM32_MSC_WRITECMD_LADDRIM      (1<<0)
#define EFM32_MSC_WRITECMD_ERASEPAGE    (1<<1)
#define EFM32_MSC_WRITECMD_WRITEEND     (1<<2)
#define EFM32_MSC_WRITECMD_WRITEONCE    (1<<3)
#define EFM32_MSC_WRITECMD_WRITETRIG    (1<<4)
#define EFM32_MSC_WRITECMD_ERASEABORT   (1<<5)

#define EFM32_MSC_STATUS_BUSY           (1<<0)
#define EFM32_MSC_STATUS_LOCKED         (1<<1)
#define EFM32_MSC_STATUS_INVADDR        (1<<2)
#define EFM32_MSC_STATUS_WDATAREADY     (1<<3)
#define EFM32_MSC_STATUS_WORDTIMEOUT	(1<<4)

void __attribute__((naked))
efm32_flash_write_stub(uint32_t *dest, uint32_t *src, uint32_t size, uint32_t msc)
{
	uint32_t i;

	EFM32_MSC_LOCK(msc) = EFM32_MSC_LOCK_LOCKKEY;
	EFM32_MSC_WRITECTRL(msc) = 1;

	for (i = 0; i < size/4; i++) {
		EFM32_MSC_ADDRB(msc) = (uint32_t)&dest[i];
		EFM32_MSC_WRITECMD(msc) = EFM32_MSC_WRITECMD_LADDRIM;

		/* Wait for WDATAREADY */
		while ((EFM32_MSC_STATUS(msc) & EFM32_MSC_STATUS_WDATAREADY) == 0);

		EFM32_MSC_WDATA(msc) = src[i];
		EFM32_MSC_WRITECMD(msc) = EFM32_MSC_WRITECMD_WRITEONCE;

		/* Wait for BUSY */
		while ((EFM32_MSC_STATUS(msc) & EFM32_MSC_STATUS_BUSY));
	}

	stub_exit(0);
}
