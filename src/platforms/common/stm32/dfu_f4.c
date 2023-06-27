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

#include "general.h"
#include "usbdfu.h"

#include <assert.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/cm3/scb.h>

static uint32_t sector_addr[] = {
	0x8000000,
	0x8004000,
	0x8008000,
	0x800c000,
	0x8010000,
	0x8020000,
	0x8040000,
	0x8060000,
	0x8080000,
	0x80a0000,
	0x80c0000,
	0x80e0000,
	0x8100000,
	0,
};

static uint16_t sector_erase_time[] = {500, 500, 500, 500, 1100, 2600, 2600, 2600, 2600, 2600, 2600, 2600, 2600};
static uint8_t sector_num = 0xff;

static_assert(ARRAY_LENGTH(sector_erase_time) == ARRAY_LENGTH(sector_addr) - 1U,
	"Number of sectors must equal number of erase time values");

/* Find the sector number for a given address */
static void get_sector_num(uint32_t addr)
{
	if (addr < sector_addr[0])
		return;
	size_t i;
	for (i = 1; sector_addr[i]; ++i) {
		if (addr < sector_addr[i])
			break;
	}

	if (!sector_addr[i])
		return;
	--i;
	sector_num = i & 0x1fU;
}

void dfu_check_and_do_sector_erase(uint32_t addr)
{
	if (addr == sector_addr[sector_num])
		flash_erase_sector(sector_num, FLASH_CR_PROGRAM_X32);
}

void dfu_flash_program_buffer(const uint32_t baseaddr, const void *const buf, const size_t len)
{
	const uint32_t *const buffer = (const uint32_t *)buf;
	for (size_t i = 0; i < len; i += 4U)
		flash_program_word(baseaddr + i, buffer[i >> 2U]);
}

uint32_t dfu_poll_timeout(uint8_t cmd, uint32_t addr, uint16_t blocknum)
{
	/* Erase for big pages on STM2/4 needs "long" time
	   Try not to hit USB timeouts*/
	if (blocknum == 0 && cmd == CMD_ERASE) {
		get_sector_num(addr);
		if (addr == sector_addr[sector_num])
			return sector_erase_time[sector_num];
	}

	/* Programming 256 word with 100 us(max) per word*/
	return 26U;
}

void dfu_protect(bool enable)
{
#ifdef DFU_SELF_PROTECT
	if (enable) {
		if (FLASH_OPTCR != 0) {
			flash_program_option_bytes(FLASH_OPTCR & ~0x10000U);
			flash_lock_option_bytes();
		}
	}
#else
	(void)enable;
#endif
}

#if defined(STM32F7) /* Set vector table base address */
#define SCB_VTOR_MASK 0xffffff00U
#define RAM_MASK      0x2ff00000U
#else
#define SCB_VTOR_MASK 0x001fffffU
#define RAM_MASK      0x2ffc0000U
#endif

void dfu_jump_app_if_valid(void)
{
	const uint32_t stack_pointer = *((uint32_t *)app_address);
	/* Boot the application if it's valid */
	if ((stack_pointer & RAM_MASK) == 0x20000000U) {
		/*
		 * Vector table may be anywhere in the main 128kiB RAM,
		 * however use of CCM is not handled
		 *
		 * Set vector table base address
		 * XXX: Does this not want to be a direct assignment of `app_address`? This seems wrong.
		 */
		SCB_VTOR = app_address & SCB_VTOR_MASK;
		/* clang-format off */
		__asm__(
			"msr msp, %1\n"     /* Load the system stack register with the new stack pointer */
			"ldr pc, [%0, 4]\n" /* Jump to application */
			: : "l"(app_address), "l"(stack_pointer) : "r0"
		);
		/* clang-format on */

		while (true)
			continue;
	}
}
