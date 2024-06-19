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

/*
 * This file implements support for CH579 devices, providing
 * ram and flash memory maps and Flash programming routines.
 *
 * This may support other chips from WCH but this has not been
 * tested.
 *
 * References:
 * - CH579 Datasheet: https://www.wch-ic.com/downloads/CH579DS1_PDF.html
 * - Special Function Register list is found in eval board zip
 *   - Can be downloaded at: https://www.wch.cn/downloads/CH579EVT_ZIP.html
 *   - Path: EVT/EXAM/SRC/StdPeriphDriver/inc/CH579SFR.h
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "adiv5.h"
#include "buffer_utils.h"

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

/* FlashROM registers */
#define CH579_R32_FLASH_DATA   0x40001800U
#define CH579_R32_FLASH_ADDR   0x40001804U
#define CH579_R8_FLASH_COMMAND 0x40001808U
#define CH579_R8_FLASH_PROTECT 0x40001809U
#define CH579_R16_FLASH_STATUS 0x4000180aU

/*
 * Constants
 */
/* ADDR_OK */
#define CH579_CONST_ADDR_OK 0x40U

/* FlashROM Commands */
#define CH579_CONST_ROM_CMD_ERASE   0xa6U
#define CH579_CONST_ROM_CMD_PROGRAM 0x9aU
/* Undocumented FlashROM Commands */
#define CH579_CONST_ROM_CMD_ERASE_INFO   0xa5U
#define CH579_CONST_ROM_CMD_PROGRAM_INFO 0x99U

/* Flash Protect base value; upper bits must be set*/
#define CH579_RB_ROM_WE_MUST_10 0b10000000U
/* Flash Protect Bitmasks */
#define CH579_RB_ROM_CODE_WE 1U << 3U
#define CH579_RB_ROM_DATA_WE 1U << 2U
/* Flash Protect Standard value */
#define CH579_RB_ROM_WRITE_ENABLE  CH579_RB_ROM_WE_MUST_10 | CH579_RB_ROM_CODE_WE | CH579_RB_ROM_DATA_WE
#define CH579_RB_ROM_WRITE_DISABLE CH579_RB_ROM_WE_MUST_10

/* Flash addresses */
#define CH579_FLASH_CONFIG_ADDR 0x00040010U
/* Undocumented Flash addresses */
#define CH579_FLASH_INFO_ADDR 0x00040000U

/* Flash config address info */
#define CH579_FLASH_CONFIG_FLAG_CFG_RESET_EN 1U << 3U
#define CH579_FLASH_CONFIG_FLAG_CFG_DEBUG_EN 1U << 4U
#define CH579_FLASH_CONFIG_FLAG_CFG_BOOT_EN  1U << 6U
#define CH579_FLASH_CONFIG_FLAG_CFG_ROM_READ 1U << 7U

/*
 * Flash functions
 */
static bool ch579_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool ch579_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool ch579_flash_prepare(target_flash_s *flash);
static bool ch579_flash_done(target_flash_s *flash);
/*
 * Monitor functions
 */
static bool ch579_cmd_erase_info_dangerous(target_s *target, int argc, const char **argv);
static bool ch579_cmd_write_info_dangerous(target_s *target, int argc, const char **argv);
static bool ch579_cmd_disable_bootloader(target_s *target, int argc, const char **argv);

const command_s ch579_cmd_list[] = {
	{"void_warranty_erase_infoflash", ch579_cmd_erase_info_dangerous, "Erase info flash sector"},
	{"void_warranty_write_infoflash", ch579_cmd_write_info_dangerous, "Write to info flash: [address] [value]"},
	{"disable_bootloader", ch579_cmd_disable_bootloader, "Disables ISP bootloader"},
	{NULL, NULL, NULL},
};

bool ch579_probe(target_s *target)
{
	uint8_t chip_id = target_mem32_read8(target, CH579_R8_CHIP_ID);
	if (chip_id != 0x79) {
		DEBUG_ERROR("Not CH579! 0x%02x\n", chip_id);
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
	target_add_commands(target, ch579_cmd_list, target->driver);
	return true;
}

/* Helper function to wait for flash */
static bool ch579_wait_flash(target_s *const target, platform_timeout_s *const timeout)
{
	uint16_t status = target_mem32_read16(target, CH579_R16_FLASH_STATUS);
	/*
	 * XXX it isn't 100% certain how this is supposed to be done.
	 * When self-programming, the CPU core is halted until the programming is finished.
	 * It isn't clear whether or not anything like that happens when accessing over SWD.
	 * No bit is documented as being an "in progress" bit.
	 * The bootloader checks for this exact value to detect success as
	 * all of the (documented) bits that indicate error are zero.
	 */
	while ((status & 0xff) != CH579_CONST_ADDR_OK) {
		DEBUG_TARGET("ch579 wait %04" PRIx16 "\n", status);
		if (target_check_error(target))
			return false;
		if (timeout)
			target_print_progress(timeout);
		status = target_mem32_read16(target, CH579_R16_FLASH_STATUS);
	}
	return true;
}

static bool ch579_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len)
{
	(void)len;
	target_mem32_write32(flash->t, CH579_R32_FLASH_ADDR, addr);
	target_mem32_write8(flash->t, CH579_R8_FLASH_COMMAND, CH579_CONST_ROM_CMD_ERASE);

	return ch579_wait_flash(flash->t, NULL);
}

static bool ch579_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	(void)len;
	target_mem32_write32(flash->t, CH579_R32_FLASH_ADDR, dest);
	target_mem32_write32(flash->t, CH579_R32_FLASH_DATA, read_le4((const uint8_t *)src, 0));
	target_mem32_write8(flash->t, CH579_R8_FLASH_COMMAND, CH579_CONST_ROM_CMD_PROGRAM);

	return ch579_wait_flash(flash->t, NULL);
}

static bool ch579_flash_prepare(target_flash_s *flash)
{
	/* Just enable both write flags now, so that code/data flash can be treated as contiguous */
	target_mem32_write8(flash->t, CH579_R8_FLASH_PROTECT, CH579_RB_ROM_WRITE_ENABLE);
	return true;
}

static bool ch579_flash_done(target_flash_s *flash)
{
	DEBUG_TARGET("ch579 flash done\n");
	target_mem32_write8(flash->t, CH579_R8_FLASH_PROTECT, CH579_RB_ROM_WRITE_DISABLE);
	return true;
}

/*
 *Monitor commands
 */

/* Dangerous as this disables debugging; may disable bootloader; will likely lock you out of the chip */
static bool ch579_cmd_erase_info_dangerous(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	target_mem32_write8(target, CH579_R8_FLASH_PROTECT, CH579_RB_ROM_WRITE_ENABLE);
	target_mem32_write32(target, CH579_R32_FLASH_ADDR, CH579_FLASH_INFO_ADDR);
	target_mem32_write8(target, CH579_R8_FLASH_COMMAND, CH579_CONST_ROM_CMD_ERASE_INFO);
	const bool okay = ch579_wait_flash(target, NULL);
	target_mem32_write8(target, CH579_R8_FLASH_PROTECT, CH579_RB_ROM_WRITE_DISABLE);
	return okay;
}

/* Dangerous as is able to lock oneself out of programming */
static bool ch579_cmd_write_info_dangerous(target_s *target, int argc, const char **argv)
{
	uint32_t addr;
	uint32_t val;

	if (argc == 3) {
		addr = strtoul(argv[1], NULL, 0);
		val = strtoul(argv[2], NULL, 0);
	} else
		return false;

	target_mem32_write8(target, CH579_R8_FLASH_PROTECT, CH579_RB_ROM_WRITE_ENABLE);
	target_mem32_write32(target, CH579_R32_FLASH_ADDR, addr);
	target_mem32_write32(target, CH579_R32_FLASH_DATA, val);
	target_mem32_write8(target, CH579_R8_FLASH_COMMAND, CH579_CONST_ROM_CMD_PROGRAM_INFO);
	const bool okay = ch579_wait_flash(target, NULL);
	target_mem32_write8(target, CH579_R8_FLASH_PROTECT, CH579_RB_ROM_WRITE_DISABLE);
	return okay;
}

/* This is much safer as it only clears a bit in flash from 1->0 */
static bool ch579_cmd_disable_bootloader(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	target_mem32_write8(target, CH579_R8_FLASH_PROTECT, CH579_RB_ROM_WRITE_ENABLE);
	target_mem32_write32(target, CH579_R32_FLASH_ADDR, CH579_FLASH_CONFIG_ADDR);
	target_mem32_write32(target, CH579_R32_FLASH_DATA, 0xffffffff & ~CH579_FLASH_CONFIG_FLAG_CFG_BOOT_EN);
	target_mem32_write8(target, CH579_R8_FLASH_COMMAND, CH579_CONST_ROM_CMD_PROGRAM_INFO);
	const bool okay = ch579_wait_flash(target, NULL);
	target_mem32_write8(target, CH579_R8_FLASH_PROTECT, CH579_RB_ROM_WRITE_DISABLE);
	return okay;
}
