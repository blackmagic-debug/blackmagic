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

#include <libopencm3/stm32/flash.h>
#include <libopencm3/cm3/scb.h>

#define FLASH_OBP_RDP   0x1ffff800U
#define FLASH_OBP_WRP10 0x1ffff808U

#define FLASH_OBP_RDP_KEY 0x5aa5U

#if defined(STM32_CAN)
#define FLASHBLOCKSIZE 2048U
#else
#define FLASHBLOCKSIZE 1024U
#endif

static uint32_t last_erased_page = 0xffffffffU;

void dfu_check_and_do_sector_erase(uint32_t sector)
{
	sector &= ~(FLASHBLOCKSIZE - 1U);
	if (sector != last_erased_page) {
		flash_erase_page(sector);
		last_erased_page = sector;
	}
}

void dfu_flash_program_buffer(const uint32_t baseaddr, const void *const buf, const size_t len)
{
	const uint16_t *const buffer = (const uint16_t *)buf;
	for (size_t i = 0; i < len; i += 2U)
		flash_program_half_word(baseaddr + i, buffer[i >> 1U]);

	/* Call the platform specific dfu event callback. */
	dfu_event();
}

uint32_t dfu_poll_timeout(uint8_t cmd, uint32_t addr, uint16_t blocknum)
{
	(void)cmd;
	(void)addr;
	(void)blocknum;
	return 100;
}

void dfu_protect(bool enable)
{
#ifdef DFU_SELF_PROTECT
	if (enable) {
		if (FLASH_WRPR & 0x03U) {
			flash_unlock();
			FLASH_CR = 0;
			flash_erase_option_bytes();
			flash_program_option_bytes(FLASH_OBP_RDP, FLASH_OBP_RDP_KEY);
			/* CL Device: Protect 2 bits with (2 * 2k pages each) */
			/* MD Device: Protect 2 bits with (4 * 1k pages each) */
			flash_program_option_bytes(FLASH_OBP_WRP10, 0x03fc);
		}
	}
#else
	(void)enable;
#endif
	/*
	 * There is no way we can update the bootloader with a program running
	 * on the same device when the bootloader pages are write
	 * protected or the device is read protected!
	 *
	 * Erasing option bytes to remove write protection will make the
	 * device read protected. Read protection means that the first pages
	 * get write protected again (PM0075, 2.4.1 Read protection.)
	 *
	 * Removing read protection after option erase results in device mass
	 * erase, crashing the update (PM0075, 2.4.2, Unprotection, Case 1).
     */
#if 0
    else if (mode == UPD_MODE && (FLASH_WRPR & 0x03U) != 0x03U) {
		flash_unlock();
		FLASH_CR = 0;
		flash_erase_option_bytes();
		flash_program_option_bytes(FLASH_OBP_RDP, FLASH_OBP_RDP_KEY);
    }
#endif
}

void dfu_jump_app_if_valid(void)
{
	const uint32_t stack_pointer = *((uint32_t *)app_address);
	/* Boot the application if it's valid */
	if ((stack_pointer & 0x2ffe0000U) == 0x20000000U) {
		/*
		 * Set vector table base address
		 * Max 2MiB Flash
		 * XXX: Does this not want to be a direct assignment of `app_address`? This seems wrong.
		 */
		SCB_VTOR = app_address & 0x001fffffU;
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
