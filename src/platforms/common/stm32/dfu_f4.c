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

/*
 * References, ST datasheets:
 *
 * DS8626 - STM32F405xx/407xx Rev 9 pg108, Table 40. Flash memory programming
 *   https://www.st.com/resource/en/datasheet/stm32f407vg.pdf
 *   (f4discovery:stm32f407vg, 128/1024 KiB;
 *    hydrabus:stm32f405rg, 128/1024 KiB)
 *
 * DS9716 - STM32F401xB/xC Rev 11 pg85, Table 45. Flash memory programming
 *   https://www.st.com/resource/en/datasheet/stm32f401cb.pdf
 *   (blackpill-f4:stm32f401cc, 64/256 KiB)
 *
 * DS10086 - STM32F401xD/xE Rev 3 pg86, Table 45. Flash memory programming
 *   https://www.st.com/resource/en/datasheet/stm32f401re.pdf
 *   (blackpill-f4:stm32f401ce, 96/512 KiB;
 *    96b_carbon:stm32f401re, 96/512 KiB)
 *
 * DS10314 - STM32F411xC/xE Rev 7 pg92, Table 45. Flash memory programming
 *   https://www.st.com/resource/en/datasheet/stm32f411ce.pdf
 *   (blackpill-f4:stm32f411ce, 128/512 KiB)
 *
 * DS11853 - STM32F722xx/723xx Rev 9 pg138, Table 53. Flash memory programming
 *   https://www.st.com/resource/en/datasheet/stm32f723ie.pdf
 *   (stlinkv3:stm32f723ie, 256/512 KiB; and F7 has slightly smaller timings than F4 family)
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

/* Sector erase times in milliseconds, typ, for x32 parallelism at 2.7-3.6v */
typedef enum erase_times_f4 {
	ERASE_TIME_16KB = 250,   /*  500 * 0.5 */
	ERASE_TIME_64KB = 550,   /* 1100 * 0.5 */
	ERASE_TIME_128KB = 1000, /* 2000 * 0.5 */
} erase_times_f4_e;

static erase_times_f4_e sector_erase_time[] = {
	ERASE_TIME_16KB,
	ERASE_TIME_16KB,
	ERASE_TIME_16KB,
	ERASE_TIME_16KB,
	ERASE_TIME_64KB,
	ERASE_TIME_128KB,
	ERASE_TIME_128KB,
	ERASE_TIME_128KB,
	ERASE_TIME_128KB,
	ERASE_TIME_128KB,
	ERASE_TIME_128KB,
	ERASE_TIME_128KB,
	ERASE_TIME_128KB,
};

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

	/* Call the platform specific dfu event callback. */
	dfu_event();
}

uint32_t dfu_poll_timeout(uint8_t cmd, uint32_t addr, uint16_t blocknum)
{
	/*
	 * Sector erase for big pages of STM32 F2/F4/F7 needs "long" time,
	 * up to 1-2 seconds. Try not to hit USB timeouts.
	 */
	if (blocknum == 0 && cmd == CMD_ERASE) {
		get_sector_num(addr);
		if (addr == sector_addr[sector_num])
			return sector_erase_time[sector_num];
	}

	/* Programming 256 words (32-bit) with 16 us(typ), 100 us(max) per word */
	return 16U * 1024U / 4U / 1000U;
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
