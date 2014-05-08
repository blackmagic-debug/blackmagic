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

#include <libopencm3/stm32/f1/flash.h>
#include <libopencm3/cm3/scb.h>
#include "usbdfu.h"

#define FLASH_OBP_RDP 0x1FFFF800
#define FLASH_OBP_WRP10 0x1FFFF808

#define FLASH_OBP_RDP_KEY 0x5aa5

#if defined (STM32_CAN)
#	define FLASHBLOCKSIZE 2048
#else
#	define FLASHBLOCKSIZE 1024
#endif

static uint32_t last_erased_page = 0xffffffff;

void dfu_check_and_do_sector_erase(uint32_t sector)
{
	sector &= (~(FLASHBLOCKSIZE - 1));
	if (sector != last_erased_page) {
		flash_erase_page(sector);
		last_erased_page = sector;
	}
}

void dfu_flash_program_buffer(uint32_t baseaddr, void *buf, int len)
{
	for(int i = 0; i < len; i += 2)
		flash_program_half_word(baseaddr + i,
				*(uint16_t*)(buf+i));
}

uint32_t dfu_poll_timeout(uint8_t cmd, uint32_t addr, uint16_t blocknum)
{
	(void)cmd;
	(void)addr;
	(void)blocknum;
	return 100;
}

void dfu_protect(dfu_mode_t mode)
{
    if (mode == DFU_MODE) {
#ifdef DFU_SELF_PROTECT
	if ((FLASH_WRPR & 0x03) != 0x00) {
		flash_unlock();
		FLASH_CR = 0;
		flash_erase_option_bytes();
		flash_program_option_bytes(FLASH_OBP_RDP, FLASH_OBP_RDP_KEY);
		/* CL Device: Protect 2 bits with (2 * 2k pages each)*/
		/* MD Device: Protect 2 bits with (4 * 1k pages each)*/
		flash_program_option_bytes(FLASH_OBP_WRP10, 0x03FC);
	}
#endif
    }
    else if (mode == UPD_MODE) {
		flash_unlock();
		FLASH_CR = 0;
		flash_erase_option_bytes();
    }
}

void dfu_jump_app_if_valid(void)
{
	/* Boot the application if it's valid */
	if((*(volatile uint32_t*)app_address & 0x2FFE0000) == 0x20000000) {
		/* Set vector table base address */
		SCB_VTOR = app_address & 0x1FFFFF; /* Max 2 MByte Flash*/
		/* Initialise master stack pointer */
		asm volatile ("msr msp, %0"::"g"
				(*(volatile uint32_t*)app_address));
		/* Jump to application */
		(*(void(**)())(app_address + 4))();
	}
}

