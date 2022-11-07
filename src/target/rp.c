/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2021 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 * Copyright (C) 2022 James Turton
 * Includes extracts from pico-bootrom
 * Copyright (C) 2020 Raspberry Pi (Trading) Ltd
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* This file implements Raspberry Pico (RP2040) target specific functions
 * for detecting the device, providing the XML memory map and
 * Flash memory programming.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "sfdp.h"

#define RP_ID                 "Raspberry RP2040"
#define RP_MAX_TABLE_SIZE     0x80U
#define BOOTROM_MAGIC_ADDR    0x00000010U
#define BOOTROM_MAGIC         ('M' | ('u' << 8) | (0x01 << 16))
#define BOOTROM_MAGIC_MASK    0x00ffffffU
#define BOOTROM_VERSION_SHIFT 24U
#define RP_XIP_FLASH_BASE     0x10000000U
#define RP_SRAM_BASE          0x20000000U
#define RP_SRAM_SIZE          0x42000U

#define RP_GPIO_QSPI_BASE_ADDR            0x40018000U
#define RP_GPIO_QSPI_SCLK_CTRL            (RP_GPIO_QSPI_BASE_ADDR + 0x04U)
#define RP_GPIO_QSPI_CS_CTRL              (RP_GPIO_QSPI_BASE_ADDR + 0x0cU)
#define RP_GPIO_QSPI_SD0_CTRL             (RP_GPIO_QSPI_BASE_ADDR + 0x14U)
#define RP_GPIO_QSPI_SD1_CTRL             (RP_GPIO_QSPI_BASE_ADDR + 0x1cU)
#define RP_GPIO_QSPI_SD2_CTRL             (RP_GPIO_QSPI_BASE_ADDR + 0x24U)
#define RP_GPIO_QSPI_SD3_CTRL             (RP_GPIO_QSPI_BASE_ADDR + 0x2cU)
#define RP_GPIO_QSPI_CS_DRIVE_NORMAL      (0U << 8U)
#define RP_GPIO_QSPI_CS_DRIVE_INVERT      (1U << 8U)
#define RP_GPIO_QSPI_CS_DRIVE_LOW         (2U << 8U)
#define RP_GPIO_QSPI_CS_DRIVE_HIGH        (3U << 8U)
#define RP_GPIO_QSPI_CS_DRIVE_MASK        0x00000300U
#define RP_GPIO_QSPI_SD1_CTRL_INOVER_BITS 0x00030000U

#define RP_SSI_BASE_ADDR                       0x18000000U
#define RP_SSI_CTRL0                           (RP_SSI_BASE_ADDR + 0x00U)
#define RP_SSI_CTRL1                           (RP_SSI_BASE_ADDR + 0x04U)
#define RP_SSI_ENABLE                          (RP_SSI_BASE_ADDR + 0x08U)
#define RP_SSI_SER                             (RP_SSI_BASE_ADDR + 0x10U)
#define RP_SSI_BAUD                            (RP_SSI_BASE_ADDR + 0x14U)
#define RP_SSI_TXFLR                           (RP_SSI_BASE_ADDR + 0x20U)
#define RP_SSI_RXFLR                           (RP_SSI_BASE_ADDR + 0x24U)
#define RP_SSI_SR                              (RP_SSI_BASE_ADDR + 0x28U)
#define RP_SSI_ICR                             (RP_SSI_BASE_ADDR + 0x48U)
#define RP_SSI_DR0                             (RP_SSI_BASE_ADDR + 0x60U)
#define RP_SSI_XIP_SPI_CTRL0                   (RP_SSI_BASE_ADDR + 0xf4U)
#define RP_SSI_CTRL0_FRF_MASK                  0x00600000U
#define RP_SSI_CTRL0_FRF_SERIAL                (0U << 21U)
#define RP_SSI_CTRL0_FRF_DUAL                  (1U << 21U)
#define RP_SSI_CTRL0_FRF_QUAD                  (2U << 21U)
#define RP_SSI_CTRL0_TMOD_MASK                 0x00000300U
#define RP_SSI_CTRL0_TMOD_BIDI                 (0U << 8U)
#define RP_SSI_CTRL0_TMOD_TX_ONLY              (1U << 8U)
#define RP_SSI_CTRL0_TMOD_RX_ONLY              (2U << 8U)
#define RP_SSI_CTRL0_TMOD_EEPROM               (3U << 8U)
#define RP_SSI_CTRL0_DATA_BIT_MASK             0x001f0000U
#define RP_SSI_CTRL0_DATA_BIT_SHIFT            16U
#define RP_SSI_CTRL0_DATA_BITS(x)              (((x)-1U) << RP_SSI_CTRL0_DATA_BIT_SHIFT)
#define RP_SSI_CTRL0_MASK                      (RP_SSI_CTRL0_FRF_MASK | RP_SSI_CTRL0_TMOD_MASK | RP_SSI_CTRL0_DATA_BIT_MASK)
#define RP_SSI_ENABLE_SSI                      (1U << 0U)
#define RP_SSI_XIP_SPI_CTRL0_FORMAT_STD_SPI    (0U << 0U)
#define RP_SSI_XIP_SPI_CTRL0_FORMAT_SPLIT      (1U << 0U)
#define RP_SSI_XIP_SPI_CTRL0_FORMAT_FRF        (2U << 0U)
#define RP_SSI_XIP_SPI_CTRL0_ADDRESS_LENGTH(x) (((x)*2U) << 2U)
#define RP_SSI_XIP_SPI_CTRL0_INSTR_LENGTH_8b   (2U << 8U)
#define RP_SSI_XIP_SPI_CTRL0_WAIT_CYCLES(x)    (((x)*8U) << 11U)
#define RP_SSI_XIP_SPI_CTRL0_XIP_CMD_SHIFT     24U
#define RP_SSI_XIP_SPI_CTRL0_XIP_CMD(x)        ((x) << RP_SSI_XIP_SPI_CTRL0_XIP_CMD_SHIFT)
#define RP_SSI_XIP_SPI_CTRL0_TRANS_1C1A        (0U << 0U)
#define RP_SSI_XIP_SPI_CTRL0_TRANS_1C2A        (1U << 0U)
#define RP_SSI_XIP_SPI_CTRL0_TRANS_2C2A        (2U << 0U)

#define RP_PADS_QSPI_BASE_ADDR         0x40020000U
#define RP_PADS_QSPI_GPIO_SD0          (RP_PADS_QSPI_BASE_ADDR + 0x08U)
#define RP_PADS_QSPI_GPIO_SD1          (RP_PADS_QSPI_BASE_ADDR + 0x0cU)
#define RP_PADS_QSPI_GPIO_SD2          (RP_PADS_QSPI_BASE_ADDR + 0x10U)
#define RP_PADS_QSPI_GPIO_SD3          (RP_PADS_QSPI_BASE_ADDR + 0x14U)
#define RP_PADS_QSPI_GPIO_SD0_OD_BITS  0x00000080U
#define RP_PADS_QSPI_GPIO_SD0_PUE_BITS 0x00000008U
#define RP_PADS_QSPI_GPIO_SD0_PDE_BITS 0x00000004U

#define RP_XIP_BASE_ADDR   0x14000000U
#define RP_XIP_CTRL        (RP_XIP_BASE_ADDR + 0x00U)
#define RP_XIP_FLUSH       (RP_XIP_BASE_ADDR + 0x04U)
#define RP_XIP_CTRL_ENABLE 0x00000001U

#define RP_RESETS_BASE_ADDR            0x4000c000U
#define RP_RESETS_RESET                (RP_RESETS_BASE_ADDR + 0x00U)
#define RP_RESETS_RESET_DONE           (RP_RESETS_BASE_ADDR + 0x08U)
#define RP_RESETS_RESET_IO_QSPI_BITS   0x00000040U
#define RP_RESETS_RESET_PADS_QSPI_BITS 0x00000200U

#define BOOTROM_FUNC_TABLE_ADDR      0x00000014U
#define BOOTROM_FUNC_TABLE_TAG(x, y) ((uint8_t)(x) | ((uint8_t)(y) << 8U))

#define FLASHSIZE_4K_SECTOR      (4U * 1024U)
#define FLASHSIZE_32K_BLOCK      (32U * 1024U)
#define FLASHSIZE_64K_BLOCK      (64U * 1024U)
#define FLASHSIZE_32K_BLOCK_MASK ~(FLASHSIZE_32K_BLOCK - 1U)
#define FLASHSIZE_64K_BLOCK_MASK ~(FLASHSIZE_64K_BLOCK - 1U)
#define MAX_FLASH                (16U * 1024U * 1024U)
#define MAX_WRITE_CHUNK          0x1000

#define RP_SPI_OPCODE(x)            (x)
#define RP_SPI_OPCODE_MASK          0x00ffU
#define RP_SPI_INTER_SHIFT          8U
#define RP_SPI_INTER_LENGTH(x)      (((x)&7U) << RP_SPI_INTER_SHIFT)
#define RP_SPI_INTER_MASK           0x0700U
#define RP_SPI_FRAME_OPCODE_ONLY    (1 << 11U)
#define RP_SPI_FRAME_OPCODE_3B_ADDR (2 << 11U)
#define RP_SPI_FRAME_MASK           0x1800U

/* Instruction codes taken from Winbond W25Q16JV datasheet, as used on the
 * original Pico board from Raspberry Pi.
 * https://www.winbond.com/resource-files/w25q16jv%20spi%20revd%2008122016.pdf
 * All dev boards supported by Pico SDK V1.3.1 use SPI flash chips which support
 * these commands. Other custom boards using different SPI flash chips might
 * not support these commands
 */

#define SPI_FLASH_CMD_SECTOR_ERASE  0x20
#define FLASHCMD_BLOCK32K_ERASE     0x52
#define FLASHCMD_BLOCK64K_ERASE     0xd8
#define FLASHCMD_CHIP_ERASE         0x60
#define SPI_FLASH_CMD_READ_JEDEC_ID (RP_SPI_OPCODE(0x9fU) | RP_SPI_INTER_LENGTH(0) | RP_SPI_FRAME_OPCODE_ONLY)
#define SPI_FLASH_CMD_READ_SFDP     (RP_SPI_OPCODE(0x5aU) | RP_SPI_INTER_LENGTH(1) | RP_SPI_FRAME_OPCODE_3B_ADDR)

typedef struct rp_priv {
	uint16_t rom_debug_trampoline_begin;
	uint16_t rom_debug_trampoline_end;
	uint16_t rom_connect_internal_flash;
	uint16_t rom_flash_enter_xip;
	uint16_t rom_flash_exit_xip;
	uint16_t rom_flash_range_erase;
	uint16_t rom_flash_range_program;
	uint16_t rom_flash_flush_cache;
	uint16_t rom_reset_usb_boot;
	bool is_prepared;
	bool is_monitor;
	uint32_t regs[0x20]; /* Register playground*/
} rp_priv_s;

typedef struct rp_flash {
	target_flash_s f;
	uint32_t page_size;
	uint8_t sector_erase_opcode;
} rp_flash_s;

static bool rp_cmd_erase_sector(target *t, int argc, const char **argv);
static bool rp_cmd_reset_usb_boot(target *t, int argc, const char **argv);

const struct command_s rp_cmd_list[] = {
	{"erase_sector", rp_cmd_erase_sector, "Erase a sector: [start address] length"},
	{"reset_usb_boot", rp_cmd_reset_usb_boot, "Reboot the device into BOOTSEL mode"},
	{NULL, NULL, NULL},
};

static bool rp_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool rp_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len);

static bool rp_read_rom_func_table(target *t);
static bool rp_attach(target *t);
static bool rp_flash_prepare(target *t);
static bool rp_flash_resume(target *t);
static void rp_spi_read(target *t, uint16_t command, target_addr_t address, void *buffer, size_t length);
static uint32_t rp_get_flash_length(target *t);
static bool rp_mass_erase(target *t);

// Our own implementation of bootloader functions for handling flash chip
static void rp_flash_exit_xip(target *const t);
static void rp_flash_enter_xip(target *const t);
#if 0
static void rp_flash_connect_internal(target *const t);
static void rp_flash_flush_cache(target *const t);
#endif

static void rp_spi_read_sfdp(target *const t, const uint32_t address, void *const buffer, const size_t length)
{
	rp_spi_read(t, SPI_FLASH_CMD_READ_SFDP, address, buffer, length);
}

static void rp_add_flash(target *t)
{
	rp_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	rp_flash_exit_xip(t);

	spi_parameters_s spi_parameters;
	if (!sfdp_read_parameters(t, &spi_parameters, rp_spi_read_sfdp)) {
		/* SFDP readout failed, so make some assumptions and hope for the best. */
		spi_parameters.page_size = 256U;
		spi_parameters.sector_size = 4096U;
		spi_parameters.capacity = rp_get_flash_length(t);
		spi_parameters.sector_erase_opcode = SPI_FLASH_CMD_SECTOR_ERASE;
	}

	rp_flash_enter_xip(t);

	DEBUG_INFO("Flash size: %uMiB\n", spi_parameters.capacity / (1024U * 1024U));

	target_flash_s *const f = &flash->f;
	f->start = RP_XIP_FLASH_BASE;
	f->length = spi_parameters.capacity;
	f->blocksize = spi_parameters.sector_size;
	f->erase = rp_flash_erase;
	f->write = rp_flash_write;
	f->writesize = MAX_WRITE_CHUNK; /* Max buffer size used otherwise */
	f->erased = 0xffU;
	target_add_flash(t, f);

	flash->page_size = spi_parameters.page_size;
	flash->sector_erase_opcode = spi_parameters.sector_erase_opcode;
}

bool rp_probe(target *t)
{
	/* Check bootrom magic*/
	uint32_t boot_magic = target_mem_read32(t, BOOTROM_MAGIC_ADDR);
	if ((boot_magic & BOOTROM_MAGIC_MASK) != BOOTROM_MAGIC) {
		DEBUG_WARN("Wrong Bootmagic %08" PRIx32 " found!\n", boot_magic);
		return false;
	}

#if defined(ENABLE_DEBUG)
	if ((boot_magic >> BOOTROM_VERSION_SHIFT) == 1)
		DEBUG_WARN("Old Bootrom Version 1!\n");
#endif

	rp_priv_s *priv_storage = calloc(1, sizeof(rp_priv_s));
	if (!priv_storage) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return false;
	}
	priv_storage->is_prepared = false;
	priv_storage->is_monitor = false;
	t->target_storage = (void *)priv_storage;

	t->mass_erase = rp_mass_erase;
	t->driver = RP_ID;
	t->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
	t->attach = rp_attach;
	t->enter_flash_mode = rp_flash_prepare;
	t->exit_flash_mode = rp_flash_resume;
	target_add_commands(t, rp_cmd_list, RP_ID);
	return true;
}

static bool rp_attach(target *t)
{
	if (!cortexm_attach(t) || !rp_read_rom_func_table(t))
		return false;

	/* Free previously loaded memory map */
	target_mem_map_free(t);
	rp_add_flash(t);
	target_add_ram(t, RP_SRAM_BASE, RP_SRAM_SIZE);

	return true;
}

/*
 * Parse out the ROM function table for routines we need.
 * Entries in the table are in pairs of 16-bit integers:
 *  * A two character tag for the routine (see section 2.8.3 of the datasheet)
 *  * The 16-bit pointer associated with that routine
 */
static bool rp_read_rom_func_table(target *const t)
{
	rp_priv_s *const priv = (rp_priv_s *)t->target_storage;
	/* We have to do a 32-bit read here but the pointer contained is only 16-bit. */
	const uint16_t table_offset = target_mem_read32(t, BOOTROM_FUNC_TABLE_ADDR) & 0x0000ffffU;
	uint16_t table[RP_MAX_TABLE_SIZE];
	if (target_mem_read(t, table, table_offset, RP_MAX_TABLE_SIZE))
		return false;

	size_t check = 0;
	for (size_t i = 0; i < RP_MAX_TABLE_SIZE; i += 2) {
		const uint16_t tag = table[i];
		const uint16_t addr = table[i + 1];
		switch (tag) {
		case BOOTROM_FUNC_TABLE_TAG('D', 'T'):
			priv->rom_debug_trampoline_begin = addr;
			break;
		case BOOTROM_FUNC_TABLE_TAG('D', 'E'):
			priv->rom_debug_trampoline_end = addr;
			break;
		case BOOTROM_FUNC_TABLE_TAG('I', 'F'):
			priv->rom_connect_internal_flash = addr;
			break;
		case BOOTROM_FUNC_TABLE_TAG('C', 'X'):
			priv->rom_flash_enter_xip = addr;
			break;
		case BOOTROM_FUNC_TABLE_TAG('E', 'X'):
			priv->rom_flash_exit_xip = addr;
			break;
		case BOOTROM_FUNC_TABLE_TAG('R', 'E'):
			priv->rom_flash_range_erase = addr;
			break;
		case BOOTROM_FUNC_TABLE_TAG('R', 'P'):
			priv->rom_flash_range_program = addr;
			break;
		case BOOTROM_FUNC_TABLE_TAG('F', 'C'):
			priv->rom_flash_flush_cache = addr;
			break;
		case BOOTROM_FUNC_TABLE_TAG('U', 'B'):
			priv->rom_reset_usb_boot = addr;
			break;
		default:
			continue;
		}
		++check;
	}
	DEBUG_TARGET("RP ROM routines connect %04x debug_trampoline %04x end %04x\n", priv->rom_connect_internal_flash,
		priv->rom_debug_trampoline_begin, priv->rom_debug_trampoline_end);
	return check == 9;
}

/* RP ROM functions calls
 *
 * timout == 0: Do not wait for poll, use for rom_reset_usb_boot()
 * timeout > 500 (ms) : display spinner
 */
static bool rp_rom_call(target *t, uint32_t *regs, uint32_t cmd, uint32_t timeout)
{
	rp_priv_s *ps = (rp_priv_s *)t->target_storage;
	regs[7] = cmd;
	regs[REG_LR] = ps->rom_debug_trampoline_end;
	regs[REG_PC] = ps->rom_debug_trampoline_begin;
	regs[REG_MSP] = 0x20042000;
	regs[REG_XPSR] = CORTEXM_XPSR_THUMB;
	uint32_t dbg_regs[t->regs_size / sizeof(uint32_t)];
	target_regs_write(t, regs);
	/* start the target and wait for it to halt again */
	target_halt_resume(t, false);
	if (!timeout)
		return false;
	DEBUG_INFO("Call cmd %04" PRIx32 "\n", cmd);
	platform_timeout operation_timeout;
	platform_timeout_set(&operation_timeout, timeout);
	platform_timeout wait_timeout;
	platform_timeout_set(&wait_timeout, 500);
	while (!target_halt_poll(t, NULL)) {
		if (ps->is_monitor)
			target_print_progress(&wait_timeout);
		if (platform_timeout_is_expired(&operation_timeout)) {
			DEBUG_WARN("RP Run timout %" PRIu32 "ms reached: ", timeout);
			break;
		}
	}
	/* Debug */
	target_regs_read(t, dbg_regs);
	const bool result = (dbg_regs[REG_PC] & ~1U) == (ps->rom_debug_trampoline_end & ~1U);
	if (!result) {
		DEBUG_WARN("rp_rom_call cmd %04" PRIx32 " failed, PC %08" PRIx32 "\n", cmd, dbg_regs[REG_PC]);
	}
	return result;
}

static bool rp_flash_prepare(target *t)
{
	rp_priv_s *ps = (rp_priv_s *)t->target_storage;
	bool result = true; /* catch false returns with &= */
	if (!ps->is_prepared) {
		DEBUG_INFO("rp_flash_prepare\n");
		/* connect*/
		result &= rp_rom_call(t, ps->regs, ps->rom_connect_internal_flash, 100);
		/* exit_xip */
		result &= rp_rom_call(t, ps->regs, ps->rom_flash_exit_xip, 100);
		ps->is_prepared = true;
	}
	return result;
}

static bool rp_flash_resume(target *t)
{
	rp_priv_s *ps = (rp_priv_s *)t->target_storage;
	bool result = true; /* catch false returns with &= */
	if (ps->is_prepared) {
		DEBUG_INFO("rp_flash_resume\n");
		/* flush */
		result &= rp_rom_call(t, ps->regs, ps->rom_flash_flush_cache, 100);
		/* enter_cmd_xip */
		result &= rp_rom_call(t, ps->regs, ps->rom_flash_enter_xip, 100);
		ps->is_prepared = false;
	}
	return result;
}

/*
 * 4k sector erase    45/  400 ms
 * 32k block erase   120/ 1600 ms
 * 64k block erase   150/ 2000 ms
 * chip erase       5000/25000 ms
 * page programm       0.4/  3 ms
 */
static bool rp_flash_erase(target_flash_s *f, target_addr_t addr, size_t len)
{
	DEBUG_INFO("Erase addr 0x%08" PRIx32 " len 0x%" PRIx32 "\n", addr, (uint32_t)len);
	target *t = f->t;

	if (addr & (f->blocksize - 1)) {
		DEBUG_WARN("Unaligned erase\n");
		return false;
	}
	if ((addr < f->start) || (addr >= f->start + f->length)) {
		DEBUG_WARN("Address is invalid\n");
		return false;
	}
	addr -= f->start;
	len = ALIGN(len, f->blocksize);
	len = MIN(len, f->length - addr);
	rp_priv_s *ps = (rp_priv_s *)t->target_storage;
	const bool full_erase = addr == f->start && len == f->length;
	platform_timeout timeout;
	platform_timeout_set(&timeout, 500);

	/* erase */
	bool result = false;
	while (len) {
		if (len >= FLASHSIZE_64K_BLOCK) {
			const uint32_t chunk = len & FLASHSIZE_64K_BLOCK_MASK;
			ps->regs[0] = addr;
			ps->regs[1] = chunk;
			ps->regs[2] = FLASHSIZE_64K_BLOCK;
			ps->regs[3] = FLASHCMD_BLOCK64K_ERASE;
			DEBUG_WARN("64k_ERASE addr 0x%08" PRIx32 " len 0x%" PRIx32 "\n", addr, chunk);
			result = rp_rom_call(t, ps->regs, ps->rom_flash_range_erase, 25100);
			len -= chunk;
			addr += chunk;
		} else if (len >= FLASHSIZE_32K_BLOCK) {
			const uint32_t chunk = len & FLASHSIZE_32K_BLOCK_MASK;
			ps->regs[0] = addr;
			ps->regs[1] = chunk;
			ps->regs[2] = FLASHSIZE_32K_BLOCK;
			ps->regs[3] = FLASHCMD_BLOCK32K_ERASE;
			DEBUG_WARN("32k_ERASE addr 0x%08" PRIx32 " len 0x%" PRIx32 "\n", addr, chunk);
			result = rp_rom_call(t, ps->regs, ps->rom_flash_range_erase, 1700);
			len -= chunk;
			addr += chunk;
		} else {
			rp_flash_s *flash = (rp_flash_s *)f;
			ps->regs[0] = addr;
			ps->regs[1] = len;
			ps->regs[2] = f->blocksize;
			ps->regs[3] = flash->sector_erase_opcode;
			DEBUG_WARN("Sector_ERASE addr 0x%08" PRIx32 " len 0x%" PRIx32 "\n", addr, (uint32_t)len);
			result = rp_rom_call(t, ps->regs, ps->rom_flash_range_erase, 410);
			len = 0;
		}
		if (!result) {
			DEBUG_WARN("Erase failed!\n");
			break;
		}
		if (full_erase)
			target_print_progress(&timeout);
	}
	DEBUG_INFO("Erase done!\n");
	return result;
}

static bool rp_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	DEBUG_INFO("RP Write 0x%08" PRIx32 " len 0x%" PRIx32 "\n", dest, (uint32_t)len);
	target *t = f->t;
	if ((dest & 0xff) || (len & 0xff)) {
		DEBUG_WARN("Unaligned write\n");
		return false;
	}
	dest -= f->start;
	rp_priv_s *ps = (rp_priv_s *)t->target_storage;
	/* Write payload to target ram */
	bool result = true;
	while (len) {
		uint32_t chunksize = (len <= MAX_WRITE_CHUNK) ? len : MAX_WRITE_CHUNK;
		target_mem_write(t, RP_SRAM_BASE, src, chunksize);
		/* Programm range */
		ps->regs[0] = dest;
		ps->regs[1] = RP_SRAM_BASE;
		ps->regs[2] = chunksize;
		/* Loading takes 3 ms per 256 byte page
		 * however it takes much longer if the XOSC is not enabled
		 * so lets give ourselves a little bit more time (x10)
		 */
		result = rp_rom_call(t, ps->regs, ps->rom_flash_range_program, (3 * chunksize * 10) >> 8);
		if (!result) {
			DEBUG_WARN("Write failed!\n");
			break;
		}
		len -= chunksize;
		src += chunksize;
		dest += chunksize;
	}
	DEBUG_INFO("Write done!\n");
	return result;
}

static bool rp_mass_erase(target *t)
{
	rp_priv_s *ps = (rp_priv_s *)t->target_storage;
	ps->is_monitor = true;
	bool result = true; /* catch false returns with &= */
	result &= rp_flash_prepare(t);
	result &= rp_flash_erase(t->flash, t->flash->start, t->flash->length);
	result &= rp_flash_resume(t);
	ps->is_monitor = false;
	return result;
}

static void rp_spi_chip_select(target *const t, const uint32_t state)
{
	const uint32_t value = target_mem_read32(t, RP_GPIO_QSPI_CS_CTRL);
	target_mem_write32(t, RP_GPIO_QSPI_CS_CTRL, (value & ~RP_GPIO_QSPI_CS_DRIVE_MASK) | state);
}

static void rp_spi_read(
	target *const t, const uint16_t command, const target_addr_t address, void *const buffer, const size_t length)
{
	/* Ensure the controller is in the correct serial SPI mode and select the Flash */
	const uint32_t ssi_enabled = target_mem_read32(t, RP_SSI_ENABLE);
	target_mem_write32(t, RP_SSI_ENABLE, 0);
	const uint32_t ctrl0 = target_mem_read32(t, RP_SSI_CTRL0);
	const uint32_t ctrl1 = target_mem_read32(t, RP_SSI_CTRL1);
	const uint32_t xpi_ctrl0 = target_mem_read32(t, RP_SSI_XIP_SPI_CTRL0);
	target_mem_write32(t, RP_SSI_CTRL0,
		(ctrl0 & RP_SSI_CTRL0_MASK) | RP_SSI_CTRL0_FRF_SERIAL | RP_SSI_CTRL0_TMOD_BIDI | RP_SSI_CTRL0_DATA_BITS(8));
	target_mem_write32(t, RP_SSI_XIP_SPI_CTRL0,
		RP_SSI_XIP_SPI_CTRL0_FORMAT_FRF | RP_SSI_XIP_SPI_CTRL0_ADDRESS_LENGTH(0) |
			RP_SSI_XIP_SPI_CTRL0_INSTR_LENGTH_8b | RP_SSI_XIP_SPI_CTRL0_WAIT_CYCLES(0));
	target_mem_write32(t, RP_SSI_CTRL1, length);
	target_mem_write32(t, RP_SSI_ENABLE, RP_SSI_ENABLE_SSI);
	rp_spi_chip_select(t, RP_GPIO_QSPI_CS_DRIVE_LOW);

	/* Set up the instruction */
	const uint8_t opcode = command & RP_SPI_OPCODE_MASK;
	target_mem_write32(t, RP_SSI_DR0, opcode);
	target_mem_read32(t, RP_SSI_DR0);

	const uint16_t addr_mode = command & RP_SPI_FRAME_MASK;
	if (addr_mode == RP_SPI_FRAME_OPCODE_3B_ADDR) {
		/* For each byte sent here, we have to manually clean up from the controller with a read */
		target_mem_write32(t, RP_SSI_DR0, (address >> 16U) & 0xffU);
		target_mem_read32(t, RP_SSI_DR0);
		target_mem_write32(t, RP_SSI_DR0, (address >> 8U) & 0xffU);
		target_mem_read32(t, RP_SSI_DR0);
		target_mem_write32(t, RP_SSI_DR0, address & 0xffU);
		target_mem_read32(t, RP_SSI_DR0);
	}

	const size_t inter_length = (command & RP_SPI_INTER_MASK) >> RP_SPI_INTER_SHIFT;
	for (size_t i = 0; i < inter_length; ++i) {
		/* For each byte sent here, we have to manually clean up from the controller with a read */
		target_mem_write32(t, RP_SSI_DR0, 0);
		target_mem_read32(t, RP_SSI_DR0);
	}

	/* Now read back the data that elicited */
	uint8_t *const data = (uint8_t *const)buffer;
	for (size_t i = 0; i < length; ++i) {
		/* Do a write to read */
		target_mem_write32(t, RP_SSI_DR0, 0);
		data[i] = target_mem_read32(t, RP_SSI_DR0) & 0xffU;
	}

	/* Deselect the Flash and put things back to how they were */
	rp_spi_chip_select(t, RP_GPIO_QSPI_CS_DRIVE_HIGH);
	target_mem_write32(t, RP_SSI_ENABLE, 0);
	target_mem_write32(t, RP_SSI_CTRL1, ctrl1);
	target_mem_write32(t, RP_SSI_CTRL0, ctrl0);
	target_mem_write32(t, RP_SSI_XIP_SPI_CTRL0, xpi_ctrl0);
	target_mem_write32(t, RP_SSI_ENABLE, ssi_enabled);
}

#if 0
// Connect the XIP controller to the flash pads
static void rp_flash_connect_internal(target *const t)
{
	// Use hard reset to force IO and pad controls to known state (don't touch
	// IO_BANK0 as that does not affect XIP signals)
	const uint32_t reset = target_mem_read32(t, RP_RESETS_RESET);
	const uint32_t io_pads_bits = RP_RESETS_RESET_IO_QSPI_BITS | RP_RESETS_RESET_PADS_QSPI_BITS;
	target_mem_write32(t, RP_RESETS_RESET, reset | io_pads_bits);
	target_mem_write32(t, RP_RESETS_RESET, reset);
	const uint32_t reset_done = target_mem_read32(t, RP_RESETS_RESET_DONE);
	while (~reset_done & io_pads_bits)
		continue;

	// Then mux XIP block onto internal QSPI flash pads
	target_mem_write32(t, RP_GPIO_QSPI_SCLK_CTRL, 0);
	target_mem_write32(t, RP_GPIO_QSPI_CS_CTRL, 0);
	target_mem_write32(t, RP_GPIO_QSPI_SD0_CTRL, 0);
	target_mem_write32(t, RP_GPIO_QSPI_SD1_CTRL, 0);
	target_mem_write32(t, RP_GPIO_QSPI_SD2_CTRL, 0);
	target_mem_write32(t, RP_GPIO_QSPI_SD3_CTRL, 0);
}
#endif

// Set up the SSI controller for standard SPI mode,i.e. for every byte sent we get one back
// This is only called by flash_exit_xip(), not by any of the other functions.
// This makes it possible for the debugger or user code to edit SPI settings
// e.g. baud rate, CPOL/CPHA.
static void rp_flash_init_spi(target *const t)
{
	// Disable SSI for further config
	target_mem_write32(t, RP_SSI_ENABLE, 0);
	// Clear sticky errors (clear-on-read)
	target_mem_read32(t, RP_SSI_SR);
	target_mem_read32(t, RP_SSI_ICR);
	// Hopefully-conservative baud rate for boot and programming
	target_mem_write32(t, RP_SSI_BAUD, 6);
	target_mem_write32(t, RP_SSI_CTRL0,
		RP_SSI_CTRL0_FRF_SERIAL |       // Standard 1-bit SPI serial frames
			RP_SSI_CTRL0_DATA_BITS(8) | // 8 clocks per data frame
			RP_SSI_CTRL0_TMOD_BIDI      // TX and RX FIFOs are both used for every byte
	);
	// Slave selected when transfers in progress
	target_mem_write32(t, RP_SSI_SER, 1);
	// Re-enable
	target_mem_write32(t, RP_SSI_ENABLE, 1);
}

// Also allow any unbounded loops to check whether the above abort condition
// was asserted, and terminate early
static int rp_flash_was_aborted(target *const t)
{
	return target_mem_read32(t, RP_GPIO_QSPI_SD1_CTRL) & RP_GPIO_QSPI_SD1_CTRL_INOVER_BITS;
}

// Put bytes from one buffer, and get bytes into another buffer.
// These can be the same buffer.
// If tx is NULL then send zeroes.
// If rx is NULL then all read data will be dropped.
//
// If rx_skip is nonzero, this many bytes will first be consumed from the FIFO,
// before reading a further count bytes into *rx.
// E.g. if you have written a command+address just before calling this function.
static void rp_flash_put_get(
	target *const t, const uint8_t *const tx, uint8_t *const rx, const size_t count, size_t rx_skip)
{
	// Make sure there is never more data in flight than the depth of the RX
	// FIFO. Otherwise, when we are interrupted for long periods, hardware
	// will overflow the RX FIFO.
	static const size_t max_in_flight = 16 - 2; // account for data internal to SSI
	size_t tx_count = 0;
	size_t rx_count = 0;
	while (tx_count < count || rx_count < count || rx_skip) {
		// NB order of reads, for pessimism rather than optimism
		const uint32_t tx_level = target_mem_read32(t, RP_SSI_TXFLR);
		const uint32_t rx_level = target_mem_read32(t, RP_SSI_RXFLR);
		bool idle = true; // Expect this to be folded into control flow, not register
		if (tx_count < count && tx_level + rx_level < max_in_flight) {
			target_mem_write32(t, RP_SSI_DR0, (uint32_t)(tx ? tx[tx_count] : 0));
			++tx_count;
			idle = false;
		}
		if (rx_level) {
			const uint8_t data = target_mem_read32(t, RP_SSI_DR0);
			if (rx_skip)
				--rx_skip;
			else {
				if (rx)
					rx[rx_count] = data;
				++rx_count;
			}
			idle = false;
		}
		// APB load costs 4 cycles, so only do it on idle loops (our budget is 48 cyc/byte)
		if (idle && rp_flash_was_aborted(t))
			break;
	}
	rp_spi_chip_select(t, RP_GPIO_QSPI_CS_DRIVE_HIGH);
}

// Sequence:
// 1. CSn = 1, IO = 4'h0 (via pulldown to avoid contention), x32 clocks
// 2. CSn = 0, IO = 4'hf (via pullup to avoid contention), x32 clocks
// 3. CSn = 1 (brief deassertion)
// 4. CSn = 0, MOSI = 1'b1 driven, x16 clocks
//
// Part 4 is the sequence suggested in W25X10CL datasheet.
// Parts 1 and 2 are to improve compatibility with Micron parts
static void rp_flash_exit_xip(target *const t)
{
	uint8_t buf[2];
	memset(buf, 0xffU, sizeof(buf));

	rp_flash_init_spi(t);

	uint32_t padctrl_save = target_mem_read32(t, RP_PADS_QSPI_GPIO_SD0);
	uint32_t padctrl_tmp =
		(padctrl_save &
			~(RP_PADS_QSPI_GPIO_SD0_OD_BITS | RP_PADS_QSPI_GPIO_SD0_PUE_BITS | RP_PADS_QSPI_GPIO_SD0_PDE_BITS)) |
		RP_PADS_QSPI_GPIO_SD0_OD_BITS | RP_PADS_QSPI_GPIO_SD0_PDE_BITS;

	// First two 32-clock sequences
	// CSn is held high for the first 32 clocks, then asserted low for next 32
	rp_spi_chip_select(t, RP_GPIO_QSPI_CS_DRIVE_HIGH);
	for (size_t i = 0; i < 2; ++i) {
		// This gives 4 16-bit offset store instructions. Anything else seems to
		// produce a large island of constants
		target_mem_write32(t, RP_PADS_QSPI_GPIO_SD0, padctrl_tmp);
		target_mem_write32(t, RP_PADS_QSPI_GPIO_SD1, padctrl_tmp);
		target_mem_write32(t, RP_PADS_QSPI_GPIO_SD2, padctrl_tmp);
		target_mem_write32(t, RP_PADS_QSPI_GPIO_SD3, padctrl_tmp);

		// Brief delay (~6000 cyc) for pulls to take effect
		platform_delay(10);

		rp_flash_put_get(t, NULL, NULL, 4, 0);

		padctrl_tmp = (padctrl_tmp & ~RP_PADS_QSPI_GPIO_SD0_PDE_BITS) | RP_PADS_QSPI_GPIO_SD0_PUE_BITS;

		rp_spi_chip_select(t, RP_GPIO_QSPI_CS_DRIVE_LOW);
	}

	// Restore IO/pad controls, and send 0xff, 0xff. Put pullup on IO2/IO3 as
	// these may be used as WPn/HOLDn at this point, and we are now starting
	// to issue serial commands.

	target_mem_write32(t, RP_PADS_QSPI_GPIO_SD0, padctrl_save);
	target_mem_write32(t, RP_PADS_QSPI_GPIO_SD1, padctrl_save);
	padctrl_save = (padctrl_save & ~RP_PADS_QSPI_GPIO_SD0_PDE_BITS) | RP_PADS_QSPI_GPIO_SD0_PUE_BITS;
	target_mem_write32(t, RP_PADS_QSPI_GPIO_SD2, padctrl_save);
	target_mem_write32(t, RP_PADS_QSPI_GPIO_SD3, padctrl_save);

	rp_spi_chip_select(t, RP_GPIO_QSPI_CS_DRIVE_LOW);
	rp_flash_put_get(t, buf, NULL, 2, 0);

	target_mem_write32(t, RP_GPIO_QSPI_CS_CTRL, 0);
}

#if 0
// This is a hook for steps to be taken in between programming the flash and
// doing cached XIP reads from the flash. Called by the bootrom before
// entering flash second stage, and called by the debugger after flash
// programming.
static void rp_flash_flush_cache(target *const t)
{
	target_mem_write32(t, RP_XIP_FLUSH, 1);
	// Read blocks until flush completion
	target_mem_read32(t, RP_XIP_FLUSH);
	// Enable the cache
	const uint32_t ctrl = target_mem_read32(t, RP_XIP_CTRL);
	target_mem_write32(t, RP_XIP_CTRL, ctrl | RP_XIP_CTRL_ENABLE);
	rp_spi_chip_select(t, RP_GPIO_QSPI_CS_DRIVE_NORMAL);
}
#endif

// Put the SSI into a mode where XIP accesses translate to standard
// serial 03h read commands. The flash remains in its default serial command
// state, so will still respond to other commands.
static void rp_flash_enter_xip(target *const t)
{
	target_mem_write32(t, RP_SSI_ENABLE, 0);
	target_mem_write32(t, RP_SSI_CTRL0,
		RP_SSI_CTRL0_FRF_SERIAL |        // Standard 1-bit SPI serial frames
			RP_SSI_CTRL0_DATA_BITS(32) | // 32 clocks per data frame
			RP_SSI_CTRL0_TMOD_EEPROM     // Send instr + addr, receive data
	);
	target_mem_write32(t, RP_SSI_XIP_SPI_CTRL0,
		RP_SSI_XIP_SPI_CTRL0_XIP_CMD(0x03) |            // Standard 03h read
			RP_SSI_XIP_SPI_CTRL0_INSTR_LENGTH_8b |      // 8-bit instruction prefix
			RP_SSI_XIP_SPI_CTRL0_ADDRESS_LENGTH(0x03) | // 24-bit addressing for 03h commands
			RP_SSI_XIP_SPI_CTRL0_TRANS_1C1A             // Command and address both in serial format
	);
	target_mem_write32(t, RP_SSI_ENABLE, RP_SSI_ENABLE_SSI);
}

static uint32_t rp_get_flash_length(target *const t)
{
	// Read the JEDEC ID and try to decode it
	spi_flash_id_s flash_id;
	rp_spi_read(t, SPI_FLASH_CMD_READ_JEDEC_ID, 0, &flash_id, sizeof(flash_id));

	DEBUG_INFO("Flash device ID: %02x %02x %02x\n", flash_id.manufacturer, flash_id.type, flash_id.capacity);
	if (flash_id.capacity >= 8 && flash_id.capacity <= 34)
		return 1 << flash_id.capacity;

	// Guess maximum flash size
	return MAX_FLASH;
}

static bool rp_cmd_erase_sector(target *t, int argc, const char **argv)
{
	uint32_t start = t->flash->start;
	uint32_t length;

	if (argc == 3) {
		start = strtoul(argv[1], NULL, 0);
		length = strtoul(argv[2], NULL, 0);
	} else if (argc == 2)
		length = strtoul(argv[1], NULL, 0);
	else
		return false;

	rp_priv_s *ps = (rp_priv_s *)t->target_storage;
	ps->is_monitor = true;
	bool result = true; /* catch false returns with &= */
	result &= rp_flash_prepare(t);
	result &= rp_flash_erase(t->flash, start, length);
	result &= rp_flash_resume(t);
	ps->is_monitor = false;
	return result;
}

static bool rp_cmd_reset_usb_boot(target *t, int argc, const char **argv)
{
	rp_priv_s *ps = (rp_priv_s *)t->target_storage;
	ps->regs[0] = 0;
	ps->regs[1] = 0;
	if (argc > 1)
		ps->regs[0] = strtoul(argv[1], NULL, 0);
	if (argc > 2)
		ps->regs[1] = strtoul(argv[2], NULL, 0);
	rp_rom_call(t, ps->regs, ps->rom_reset_usb_boot, 0);
	return true;
}

static bool rp_rescue_do_reset(target *t)
{
	ADIv5_AP_t *ap = (ADIv5_AP_t *)t->priv;
	uint32_t ctrlstat = ap->dp->low_access(ap->dp, ADIV5_LOW_READ, ADIV5_DP_CTRLSTAT, 0);
	ap->dp->low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_DP_CTRLSTAT, ctrlstat | ADIV5_DP_CTRLSTAT_CDBGPWRUPREQ);
	platform_timeout timeout;
	platform_timeout_set(&timeout, 100);
	while (true) {
		ctrlstat = ap->dp->low_access(ap->dp, ADIV5_LOW_READ, ADIV5_DP_CTRLSTAT, 0);
		if (!(ctrlstat & ADIV5_DP_CTRLSTAT_CDBGRSTACK)) {
			DEBUG_INFO("RP RESCUE succeeded.\n");
			break;
		}
		if (platform_timeout_is_expired(&timeout)) {
			DEBUG_INFO("RP RESCUE failed\n");
			break;
		}
	}
	return false;
}

/* The RP Pico rescue DP provides no AP, so we need special handling
 *
 * Attach to this DP will do the reset, but will fail to attach!
 */
bool rp_rescue_probe(ADIv5_AP_t *ap)
{
	target *t = target_new();
	if (!t) {
		return false;
	}

	adiv5_ap_ref(ap);
	t->attach = (void *)rp_rescue_do_reset;
	t->priv = ap;
	t->priv_free = (void *)adiv5_ap_unref;
	t->driver = "Raspberry RP2040 Rescue (Attach to reset!)";

	return true;
}
