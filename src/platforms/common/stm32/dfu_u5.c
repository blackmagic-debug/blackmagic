/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2025 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "general.h"
#include "usbdfu.h"

#include <libopencm3/stm32/flash.h>
#include <libopencm3/cm3/scb.h>

#define FLASH_BLOCK_SIZE 8192U
#define FLASH_PAGE_SHIFT 13U
#define FLASH_PAGE_MASK  0x7fU
#define FLASH_BANK_MASK  0x80U

#define SCB_VTOR_MASK 0xffffff80U
/*
 * Ignore both the bottom bit of the top most nibble, and all bits below the bottom of the 3rd -
 * this carves out both the NS/S bit (0x30000000 is the secure mirror of 0x20000000), and
 * any possible location of the stack pointer within the first 3 SRAMs in the system
 */
#define SRAM_MASK 0xeff00000U

static uint32_t last_erased_page = 0xffffffffU;

void dfu_check_and_do_sector_erase(uint32_t sector)
{
	sector &= ~(FLASH_BLOCK_SIZE - 1U);
	if (sector != last_erased_page) {
		const uint16_t page = (sector >> FLASH_PAGE_SHIFT);
		flash_erase_page((page & FLASH_BANK_MASK) ? FLASH_BANK_2 : FLASH_BANK_1, page & FLASH_PAGE_MASK);
		flash_wait_for_last_operation();
		last_erased_page = sector;
	}
}

void dfu_flash_program_buffer(const uint32_t address, const void *const buf, const size_t len)
{
	const uint8_t *const buffer = (const uint8_t *)buf;
	flash_program(address, buffer, len);

	/* Call the platform specific dfu event callback. */
	dfu_event();
}

/* A polling timeout, in miliseconds, for the ongoing programming/erase operation */
uint32_t dfu_poll_timeout(uint8_t cmd, uint32_t addr, uint16_t blocknum)
{
	/* We don't care about the address as that's not used here */
	(void)addr;
	/* DfuSe uses this as a special indicator to perform erases */
	if (blocknum == 0U && cmd == CMD_ERASE) {
		/*
		 * If we're doing an erase of a block, it'll take up to 3.4ms to erase 8KiB.
		 * Round up to the nearest milisecond.
		 */
		return 4U;
	}
	/*
	 * From dfucore.c, we receive up to 1KiB at a time to program, which is is 64 u128 blocks.
	 * DS13086 (STM32U585x) specifies the programming time for the Flash at 118µs a block
	 * (§5.3.11 Flash memory characteristics, Table 88. pg228).
	 * This works out to 7552µs, so round that up to the nearest whole milisecond.
	 */
	return 8U;
}

void dfu_protect(bool enable)
{
	/* For now, this function is a no-op and the bootloader is fully unprotected */
	(void)enable;
}

void dfu_jump_app_if_valid(void)
{
	const uint32_t stack_pointer = *((uint32_t *)app_address);
	/* Boot the application if it's valid */
	if ((stack_pointer & SRAM_MASK) == 0x20000000U) {
		/* Set vector table base address which must be aligned to the nearest 128 bytes */
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
