// SPDX-License-Identifier: BSD-3-Clause
/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 ArcaneNibble, jediminer543
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
#include "cortexm.h"
#include "adiv5.h"

/*
 * Memory map
 */
/* 250KB + 2KB，CodeFlash + DataFlash of FlashROM */
#define CH579_FLASH_BASE_ADDR  0x00000000U
#define CH579_FLASH_SIZE       0x3f000U
#define CH579_FLASH_BLOCK_SIZE 512U
#define CH579_FLASH_WRITE_SIZE 4U
/* 32KB，SRAM */
#define CH579_SRAM_BASE_ADDR 0x20000000U
#define CH579_SRAM_SIZE      0x8000U

/*
 * Registers
 */

/* System Control registers */
#define CH579_R8_CHIP_ID 0x40001041U

/*
 * Flash functions
 */
static bool ch579_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool ch579_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool ch579_flash_prepare(target_flash_s *flash);
static bool ch579_flash_done(target_flash_s *flash);

bool ch579_probe(target_s *target)
{
	uint8_t chip_id = target_mem32_read8(target, CH579_R8_CHIP_ID);
	if (chip_id != 0x79) {
		DEBUG_ERROR("Not CH579! 0x%02" PRIx8 "\n", chip_id);
		return false;
	}

	target->driver = "CH579";

	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	flash->start = CH579_FLASH_BASE_ADDR;
	flash->length = CH579_FLASH_SIZE;
	flash->blocksize = CH579_FLASH_BLOCK_SIZE;
	flash->writesize = CH579_FLASH_WRITE_SIZE;
	flash->erase = ch579_flash_erase;
	flash->write = ch579_flash_write;
	flash->prepare = ch579_flash_prepare;
	flash->done = ch579_flash_done;
	flash->erased = 0xffU;
	target_add_flash(target, flash);

	target_add_ram32(target, CH579_SRAM_BASE_ADDR, CH579_SRAM_SIZE);
	return true;
}

static bool ch579_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len)
{
	DEBUG_INFO("ch579 flash erase\n");
	return false;
}

static bool ch579_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	DEBUG_INFO("ch579 flash write\n");
	return false;
}

static bool ch579_flash_prepare(target_flash_s *flash)
{
	DEBUG_INFO("ch579 flash prepare\n");
	return false;
}

static bool ch579_flash_done(target_flash_s *flash)
{
	DEBUG_INFO("ch579 flash done\n");
	return false;
}
