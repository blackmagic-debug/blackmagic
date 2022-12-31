/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
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
#include "target.h"
#include "target_internal.h"
#include "target_probe.h"
#include "avr_pdi.h"

#define IDCODE_XMEGA64A3U  0x9642U
#define IDCODE_XMEGA128A3U 0x9742U
#define IDCODE_XMEGA192A3U 0x9744U
#define IDCODE_XMEGA256A3U 0x9842U

void avr_add_flash(target_s *const target, const uint32_t start, const size_t length, const uint16_t block_size)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = start;
	flash->length = length;
	flash->blocksize = block_size;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

bool atxmega_probe(target_s *const target)
{
	uint32_t application_flash = 0;
	uint32_t application_table_flash = 0;
	uint32_t bootloader_flash = 0;
	uint16_t flash_block_size = 0;
	uint32_t sram = 0;

	switch (target->part_id) {
	case IDCODE_XMEGA64A3U:
		/*
		 * The 64A3U has:
		 * 60kiB of normal Flash
		 * 4kiB of application table Flash
		 * 4kiB of bootloader Flash
		 * 16kiB of internal SRAM
		 */
		application_flash = 0xf000U;
		application_table_flash = 0x1000U;
		bootloader_flash = 0x1000U;
		flash_block_size = 128;
		target->core = "ATXMega64A3U";
		break;
	case IDCODE_XMEGA128A3U:
		/*
		 * The 128A3U has:
		 * 120kiB of normal Flash
		 * 8kiB of application table Flash
		 * 8kiB of bootloader Flash
		 * 16kiB of internal SRAM
		 */
		application_flash = 0x1e000U;
		application_table_flash = 0x2000U;
		bootloader_flash = 0x2000U;
		flash_block_size = 256;
		target->core = "ATXMega128A3U";
		break;
	case IDCODE_XMEGA192A3U:
		/*
		 * The 192A3U has:
		 * 184kiB of normal Flash
		 * 8kiB of application table Flash
		 * 8kiB of bootloader Flash
		 * 16kiB of internal SRAM
		 */
		application_flash = 0x2e000U;
		application_table_flash = 0x2000U;
		bootloader_flash = 0x2000U;
		flash_block_size = 256;
		target->core = "ATXMega192A3U";
		break;
	case IDCODE_XMEGA256A3U:
		/*
		 * The 256A3U has:
		 * 248kiB of normal Flash
		 * 8kiB of application table Flash
		 * 8kiB of bootloader Flash
		 * 16kiB of internal SRAM
		 */
		application_flash = 0x3e000U;
		application_table_flash = 0x2000U;
		bootloader_flash = 0x2000U;
		flash_block_size = 256;
		sram = 0x800U;
		target->core = "ATXMega256A3U";
		break;
	default:
		return false;
	}

	/*
	 * RAM is actually at 0x01002000 in the 24-bit linearised PDI address space however, because GDB/GCC,
	 * internally we have to map at 0x00800000 to get a suitable mapping for the host
	 */
	target_add_ram32(target, 0x00802000U, sram);
	uint32_t flash_base_address = 0x00000000;
	avr_add_flash(target, flash_base_address, application_flash, flash_block_size);
	flash_base_address += application_flash;
	avr_add_flash(target, flash_base_address, application_table_flash, flash_block_size);
	flash_base_address += application_table_flash;
	avr_add_flash(target, flash_base_address, bootloader_flash, flash_block_size);
	return true;
}
