/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2013 Gareth McMullin <gareth@blacksphere.co.nz>
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

#include "platform.h"

#if defined(STM32F2)
#	include <libopencm3/stm32/f2/flash.h>
#elif defined(STM32F4)
#	include <libopencm3/stm32/f4/flash.h>
#endif
#include <libopencm3/cm3/scb.h>

#include "usbdfu.h"

static uint32_t sector_addr[] = {0x8000000, 0x8004000, 0x8008000, 0x800c000,
                            0x8010000, 0x8020000, 0x8040000, 0x8060000,
                            0x8080000, 0x80a0000, 0x80c0000, 0x80e0000,
                            0x8100000, 0};
static uint16_t sector_erase_time[12]= {500, 500, 500, 500,
                            1100,
                            2600, 2600, 2600, 2600, 2600, 2600, 2600};
static uint8_t sector_num = 0xff;

/* Find the sector number for a given address*/
static void get_sector_num(uint32_t addr)
{
	int i = 0;
	while(sector_addr[i+1]) {
		if (addr < sector_addr[i+1])
			break;
		i++;
		}
	if (!sector_addr[i])
		return;
	sector_num = i;
}

void dfu_check_and_do_sector_erase(uint32_t addr)
{
	if(addr == sector_addr[sector_num]) {
		flash_erase_sector((sector_num & 0x1f)<<3, FLASH_PROGRAM_X32);
	}
}

void dfu_flash_program_buffer(uint32_t baseaddr, void *buf, int len)
{
	for(int i = 0; i < len; i += 4)
		flash_program_word(baseaddr + i,
			*(uint32_t*)(buf+i),
			FLASH_PROGRAM_X32);
}

uint32_t dfu_poll_timeout(uint8_t cmd, uint32_t addr, uint16_t blocknum)
{
	/* Erase for big pages on STM2/4 needs "long" time
	   Try not to hit USB timeouts*/
	if ((blocknum == 0) && (cmd == CMD_ERASE)) {
		get_sector_num(addr);
		if(addr == sector_addr[sector_num])
			return sector_erase_time[sector_num];
	}

	/* Programming 256 word with 100 us(max) per word*/
	return 26;
}

void dfu_protect_enable(void)
{
#ifdef DFU_SELF_PROTECT
	if ((FLASH_OPTCR & 0x10000) != 0) {
		flash_program_option_bytes(FLASH_OPTCR & ~0x10000);
		flash_lock_option_bytes();
	}
#endif
}

void dfu_jump_app_if_valid(void)
{
	/* Boot the application if it's valid */
	/* Vector table may be anywhere in 128 kByte RAM
	   CCM not handled*/
	if((*(volatile uint32_t*)APP_ADDRESS & 0x2FFC0000) == 0x20000000) {
		/* Set vector table base address */
		SCB_VTOR = APP_ADDRESS & 0x1FFFFF; /* Max 2 MByte Flash*/
		/* Initialise master stack pointer */
		asm volatile ("msr msp, %0"::"g"
				(*(volatile uint32_t*)APP_ADDRESS));
		/* Jump to application */
		(*(void(**)())(APP_ADDRESS + 4))();
	}
}

