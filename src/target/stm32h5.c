/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
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

/*
 * This file implements support for STM32H5xx series devices, providing
 * memory maps and Flash programming routines.
 *
 * References:
 * RM0481 - STM32H563, H573 and H562 ARM®-based 32-bit MCUs, Rev. 1
 *   https://www.st.com/resource/en/reference_manual/rm0481-stm32h563h573-and-stm32h562-armbased-32bit-mcus-stmicroelectronics.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"

/* Memory map constants */
#define STM32H5_FLASH_BANK1_BASE 0x08000000U
#define STM32H5_FLASH_BANK2_BASE 0x08100000U
#define STM32H5_FLASH_BANK_SIZE  0x00100000U
#define STM32H5_SRAM1_BASE       0x0a000000U
#define STM32H5_SRAM1_SIZE       0x00040000U
#define STM32H5_SRAM2_BASE       0x0a040000U
#define STM32H5_SRAM2_SIZE       0x00010000U
#define STM32H5_SRAM3_BASE       0x0a050000U
#define STM32H5_SRAM3_SIZE       0x00050000U
/* NB: Take all base addresses and add 0x04000000U to find their TrustZone addresses */

#define STM32H5_FLASH_BASE        0x40022000
#define STM32H5_SECTORS_PER_BANK  128U
#define STM32H5_FLASH_SECTOR_SIZE 0x2000U

/* Taken from DP_TARGETIDR in §58.3.3 of RM0481 rev 1, pg2958 */
#define ID_STM32H5xx 0x4840U

static void stm32h5_add_flash(
	target_s *const target, const uint32_t base_addr, const size_t length, const size_t block_size)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = base_addr;
	flash->length = length;
	flash->blocksize = block_size;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

bool stm32h5_probe(target_s *const target)
{
	if (target->part_id != ID_STM32H5xx)
		return false;

	target->driver = "STM32H5";

	/*
	 * Build the RAM map.
	 * This uses the addresses and sizes found in §2.3.2, Figure 2, pg113 of RM0481 Rev. 1
	 */
	target_add_ram(target, STM32H5_SRAM1_BASE, STM32H5_SRAM1_SIZE);
	target_add_ram(target, STM32H5_SRAM2_BASE, STM32H5_SRAM2_SIZE);
	target_add_ram(target, STM32H5_SRAM3_BASE, STM32H5_SRAM3_SIZE);

	/* Build the Flash map */
	stm32h5_add_flash(target, STM32H5_FLASH_BANK1_BASE, STM32H5_FLASH_BANK_SIZE, STM32H5_FLASH_SECTOR_SIZE);
	stm32h5_add_flash(target, STM32H5_FLASH_BANK2_BASE, STM32H5_FLASH_BANK_SIZE, STM32H5_FLASH_SECTOR_SIZE);

	return true;
}
