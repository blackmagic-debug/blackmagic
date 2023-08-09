/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2021 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Significantly rewritten in 2022 by Rachel Mant <git@dragonmux.network>
 * Copyright (C) 2022 James Turton
 * Includes extracts from pico-bootrom
 * Copyright (C) 2020 Raspberry Pi (Trading) Ltd
 * Modified by Rachel Mant <git@dragonmux.network>
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

/*
 * This file implements Raspberry Pico (RP2040) target specific functions
 * for detecting the device, providing the XML memory map and
 * Flash memory programming.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "spi.h"
#include "sfdp.h"

#define RP_MAX_TABLE_SIZE     0x80U
#define BOOTROM_MAGIC_ADDR    0x00000010U
#define BOOTROM_MAGIC         ((uint32_t)'M' | ((uint32_t)'u' << 8U) | (1U << 16U))
#define BOOTROM_MAGIC_MASK    0x00ffffffU
#define BOOTROM_VERSION_SHIFT 24U
#define RP_XIP_FLASH_BASE     0x10000000U
#define RP_SRAM_BASE          0x20000000U
#define RP_SRAM_SIZE          0x42000U

#define RP_REG_ACCESS_NORMAL              0x0000U
#define RP_REG_ACCESS_WRITE_XOR           0x1000U
#define RP_REG_ACCESS_WRITE_ATOMIC_BITSET 0x2000U
#define RP_REG_ACCESS_WRITE_ATOMIC_BITCLR 0x3000U

#define RP_CLOCKS_BASE_ADDR     0x40008000U
#define RP_CLOCKS_WAKE_EN0      (RP_CLOCKS_BASE_ADDR + 0xa0U)
#define RP_CLOCKS_WAKE_EN1      (RP_CLOCKS_BASE_ADDR + 0xa4U)
#define RP_CLOCKS_WAKE_EN0_MASK 0xff0c0f19U
#define RP_CLOCKS_WAKE_EN1_MASK 0x00002007U

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
#define RP_GPIO_QSPI_SCLK_POR             0x0000001fU

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

#define RP_PADS_QSPI_BASE_ADDR           0x40020000U
#define RP_PADS_QSPI_GPIO_SCLK           (RP_PADS_QSPI_BASE_ADDR + 0x04U)
#define RP_PADS_QSPI_GPIO_SD0            (RP_PADS_QSPI_BASE_ADDR + 0x08U)
#define RP_PADS_QSPI_GPIO_SD1            (RP_PADS_QSPI_BASE_ADDR + 0x0cU)
#define RP_PADS_QSPI_GPIO_SD2            (RP_PADS_QSPI_BASE_ADDR + 0x10U)
#define RP_PADS_QSPI_GPIO_SD3            (RP_PADS_QSPI_BASE_ADDR + 0x14U)
#define RP_PADS_QSPI_GPIO_SCLK_FAST_SLEW 0x00000001U
#define RP_PADS_QSPI_GPIO_SCLK_8mA_DRIVE 0x00000020U
#define RP_PADS_QSPI_GPIO_SCLK_IE        0x00000040U
#define RP_PADS_QSPI_GPIO_SD0_OD_BITS    0x00000080U
#define RP_PADS_QSPI_GPIO_SD0_PUE_BITS   0x00000008U
#define RP_PADS_QSPI_GPIO_SD0_PDE_BITS   0x00000004U

#define RP_XIP_BASE_ADDR   0x14000000U
#define RP_XIP_CTRL        (RP_XIP_BASE_ADDR + 0x00U)
#define RP_XIP_FLUSH       (RP_XIP_BASE_ADDR + 0x04U)
#define RP_XIP_STAT        (RP_XIP_BASE_ADDR + 0x08U)
#define RP_XIP_CTRL_ENABLE 0x00000001U
#define RP_XIP_STAT_POR    0x00000002U

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
#define MAX_WRITE_CHUNK          0x1000U

typedef struct rp_priv {
	uint16_t rom_reset_usb_boot;
	uint32_t ssi_enabled;
	uint32_t ctrl0;
	uint32_t ctrl1;
	uint32_t xpi_ctrl0;
} rp_priv_s;

static bool rp_cmd_erase_sector(target_s *target, int argc, const char **argv);
static bool rp_cmd_reset_usb_boot(target_s *target, int argc, const char **argv);

const command_s rp_cmd_list[] = {
	{"erase_sector", rp_cmd_erase_sector, "Erase a sector: [start address] length"},
	{"reset_usb_boot", rp_cmd_reset_usb_boot, "Reboot the device into BOOTSEL mode"},
	{NULL, NULL, NULL},
};

static bool rp_read_rom_func_table(target_s *target);
static bool rp_attach(target_s *target);
static void rp_spi_config(target_s *target);
static void rp_spi_restore(target_s *target);
static bool rp_flash_prepare(target_s *target);
static bool rp_flash_resume(target_s *target);
static void rp_spi_read(target_s *target, uint16_t command, target_addr_t address, void *buffer, size_t length);
static void rp_spi_write(target_s *target, uint16_t command, target_addr_t address, const void *buffer, size_t length);
static void rp_spi_run_command(target_s *target, uint16_t command, target_addr_t address);
static uint32_t rp_get_flash_length(target_s *target);

static bool rp_flash_in_por_state(target_s *target);
// Our own implementation of bootloader functions for handling flash chip
static void rp_flash_exit_xip(target_s *target);
static void rp_flash_enter_xip(target_s *target);
static void rp_flash_connect_internal(target_s *target);
static void rp_flash_flush_cache(target_s *target);

static void rp_add_flash(target_s *target)
{
	const bool por_state = rp_flash_in_por_state(target);
	DEBUG_INFO("RP2040 Flash controller %sin POR state, reconfiguring\n", por_state ? "" : "not ");
	if (por_state)
		rp_flash_connect_internal(target);
	rp_flash_exit_xip(target);
	rp_spi_config(target);

	bmp_spi_add_flash(
		target, RP_XIP_FLASH_BASE, rp_get_flash_length(target), rp_spi_read, rp_spi_write, rp_spi_run_command);

	rp_spi_restore(target);
	if (por_state)
		rp_flash_flush_cache(target);
	rp_flash_enter_xip(target);
}

bool rp_probe(target_s *target)
{
	/* Check bootrom magic*/
	uint32_t boot_magic = target_mem_read32(target, BOOTROM_MAGIC_ADDR);
	if ((boot_magic & BOOTROM_MAGIC_MASK) != BOOTROM_MAGIC) {
		DEBUG_ERROR("Wrong Bootmagic %08" PRIx32 " found!\n", boot_magic);
		return false;
	}

#if defined(ENABLE_DEBUG)
	if ((boot_magic >> BOOTROM_VERSION_SHIFT) == 1)
		DEBUG_WARN("Old Bootrom Version 1!\n");
#endif

	rp_priv_s *priv_storage = calloc(1, sizeof(rp_priv_s));
	if (!priv_storage) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	target->target_storage = (void *)priv_storage;

	target->mass_erase = bmp_spi_mass_erase;
	target->driver = "Raspberry RP2040";
	target->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
	target->attach = rp_attach;
	target->enter_flash_mode = rp_flash_prepare;
	target->exit_flash_mode = rp_flash_resume;
	target_add_commands(target, rp_cmd_list, target->driver);
	return true;
}

static bool rp_attach(target_s *target)
{
	if (!cortexm_attach(target) || !rp_read_rom_func_table(target))
		return false;

	/* Free previously loaded memory map */
	target_mem_map_free(target);
	rp_add_flash(target);
	target_add_ram(target, RP_SRAM_BASE, RP_SRAM_SIZE);

	return true;
}

/*
 * Parse out the ROM function table for routines we need.
 * Entries in the table are in pairs of 16-bit integers:
 *  * A two character tag for the routine (see section 2.8.3 of the datasheet)
 *  * The 16-bit pointer associated with that routine
 */
static bool rp_read_rom_func_table(target_s *const target)
{
	rp_priv_s *const priv = (rp_priv_s *)target->target_storage;
	/* We have to do a 32-bit read here but the pointer contained is only 16-bit. */
	const uint16_t table_offset = target_mem_read32(target, BOOTROM_FUNC_TABLE_ADDR) & 0x0000ffffU;
	uint16_t table[RP_MAX_TABLE_SIZE];
	if (target_mem_read(target, table, table_offset, RP_MAX_TABLE_SIZE))
		return false;

	for (size_t i = 0; i < RP_MAX_TABLE_SIZE; i += 2U) {
		const uint16_t tag = table[i];
		const uint16_t addr = table[i + 1U];
		if (tag == BOOTROM_FUNC_TABLE_TAG('U', 'B')) {
			priv->rom_reset_usb_boot = addr;
			return true;
		}
	}
	return false;
}

static void rp_spi_config(target_s *const target)
{
	rp_priv_s *const priv = (rp_priv_s *)target->target_storage;
	/* Ensure the controller is in the correct serial SPI mode */
	priv->ssi_enabled = target_mem_read32(target, RP_SSI_ENABLE);
	target_mem_write32(target, RP_SSI_ENABLE, 0);
	priv->ctrl0 = target_mem_read32(target, RP_SSI_CTRL0);
	priv->ctrl1 = target_mem_read32(target, RP_SSI_CTRL1);
	priv->xpi_ctrl0 = target_mem_read32(target, RP_SSI_XIP_SPI_CTRL0);
	target_mem_write32(target, RP_SSI_CTRL0,
		(priv->ctrl0 & RP_SSI_CTRL0_MASK) | RP_SSI_CTRL0_FRF_SERIAL | RP_SSI_CTRL0_TMOD_BIDI |
			RP_SSI_CTRL0_DATA_BITS(8));
	target_mem_write32(target, RP_SSI_XIP_SPI_CTRL0,
		RP_SSI_XIP_SPI_CTRL0_FORMAT_FRF | RP_SSI_XIP_SPI_CTRL0_ADDRESS_LENGTH(0) |
			RP_SSI_XIP_SPI_CTRL0_INSTR_LENGTH_8b | RP_SSI_XIP_SPI_CTRL0_WAIT_CYCLES(0));
	target_mem_write32(target, RP_SSI_ENABLE, RP_SSI_ENABLE_SSI);
}

static void rp_spi_restore(target_s *const target)
{
	const rp_priv_s *const priv = (rp_priv_s *)target->target_storage;
	/* Put things back to how they were */
	target_mem_write32(target, RP_SSI_ENABLE, 0);
	target_mem_write32(target, RP_SSI_CTRL1, priv->ctrl1);
	target_mem_write32(target, RP_SSI_CTRL0, priv->ctrl0);
	target_mem_write32(target, RP_SSI_XIP_SPI_CTRL0, priv->xpi_ctrl0);
	target_mem_write32(target, RP_SSI_ENABLE, priv->ssi_enabled);
}

static bool rp_flash_prepare(target_s *const target)
{
	DEBUG_TARGET("%s\n", __func__);
	/* Suspend the cache, come out of XIP */
	rp_flash_connect_internal(target);
	rp_flash_exit_xip(target);
	/* Configure the SPI controller for our use */
	rp_spi_config(target);
	return true;
}

static bool rp_flash_resume(target_s *const target)
{
	DEBUG_TARGET("%s\n", __func__);
	/* Put the SPI controller back how it was when we entered Flash mode */
	rp_spi_restore(target);
	/* Flush the cache and resume XIP */
	rp_flash_flush_cache(target);
	rp_flash_enter_xip(target);
	target_mem_write32(target, CORTEXM_AIRCR, CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_SYSRESETREQ);
	return true;
}

static void rp_spi_chip_select(target_s *const target, const uint32_t state)
{
	const uint32_t value = target_mem_read32(target, RP_GPIO_QSPI_CS_CTRL);
	target_mem_write32(target, RP_GPIO_QSPI_CS_CTRL, (value & ~RP_GPIO_QSPI_CS_DRIVE_MASK) | state);
}

static uint8_t rp_spi_xfer_data(target_s *const target, const uint8_t data)
{
	target_mem_write32(target, RP_SSI_DR0, data);
	return target_mem_read32(target, RP_SSI_DR0) & 0xffU;
}

static void rp_spi_setup_xfer(
	target_s *const target, const uint16_t command, const target_addr_t address, const size_t length)
{
	/* Configure the controller, and select the Flash */
	target_mem_write32(target, RP_SSI_CTRL1, length);
	rp_spi_chip_select(target, RP_GPIO_QSPI_CS_DRIVE_LOW);

	/* Set up the instruction */
	const uint8_t opcode = command & SPI_FLASH_OPCODE_MASK;
	rp_spi_xfer_data(target, opcode);

	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR) {
		/* For each byte sent here, we have to manually clean up from the controller with a read */
		rp_spi_xfer_data(target, (address >> 16U) & 0xffU);
		rp_spi_xfer_data(target, (address >> 8U) & 0xffU);
		rp_spi_xfer_data(target, address & 0xffU);
	}

	const size_t inter_length = (command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT;
	for (size_t i = 0; i < inter_length; ++i)
		/* For each byte sent here, we have to manually clean up from the controller with a read */
		rp_spi_xfer_data(target, 0);
}

static void rp_spi_read(target_s *const target, const uint16_t command, const target_addr_t address, void *const buffer,
	const size_t length)
{
	/* Setup the transaction */
	rp_spi_setup_xfer(target, command, address, length);
	/* Now read back the data that elicited */
	uint8_t *const data = (uint8_t *const)buffer;
	for (size_t i = 0; i < length; ++i)
		/* Do a write to read */
		data[i] = rp_spi_xfer_data(target, 0);
	/* Deselect the Flash */
	rp_spi_chip_select(target, RP_GPIO_QSPI_CS_DRIVE_HIGH);
}

static void rp_spi_write(target_s *const target, const uint16_t command, const target_addr_t address,
	const void *const buffer, const size_t length)
{
	/* Setup the transaction */
	rp_spi_setup_xfer(target, command, address, length);
	/* Now write out back the data requested */
	uint8_t *const data = (uint8_t *const)buffer;
	for (size_t i = 0; i < length; ++i)
		/* Do a write to read */
		rp_spi_xfer_data(target, data[i]);
	/* Deselect the Flash */
	rp_spi_chip_select(target, RP_GPIO_QSPI_CS_DRIVE_HIGH);
}

static void rp_spi_run_command(target_s *const target, const uint16_t command, const target_addr_t address)
{
	rp_spi_write(target, command, address, NULL, 0);
}

/* Checks if the QSPI and XIP controllers are in their POR state */
static bool rp_flash_in_por_state(target_s *const target)
{
	const uint32_t clk_enables0 = target_mem_read32(target, RP_CLOCKS_WAKE_EN0);
	const uint32_t clk_enables1 = target_mem_read32(target, RP_CLOCKS_WAKE_EN1);
	if ((clk_enables0 & RP_CLOCKS_WAKE_EN0_MASK) != RP_CLOCKS_WAKE_EN0_MASK ||
		(clk_enables1 & RP_CLOCKS_WAKE_EN1_MASK) != RP_CLOCKS_WAKE_EN1_MASK) {
		/* If the right clocks aren't enabled, enable all of them just like the boot ROM does. */
		target_mem_write32(target, RP_CLOCKS_WAKE_EN0, 0xffffffffU);
		target_mem_write32(target, RP_CLOCKS_WAKE_EN1, 0xffffffffU);
		return true;
	}
	const uint32_t pad_sclk_state = target_mem_read32(target, RP_PADS_QSPI_GPIO_SCLK);
	/* If input is enabled on the SPI clock pin, we're not configured. */
	if (pad_sclk_state & RP_PADS_QSPI_GPIO_SCLK_IE)
		return true;
	const uint32_t xip_state = target_mem_read32(target, RP_XIP_STAT);
	const uint32_t qspi_sclk_state = target_mem_read32(target, RP_GPIO_QSPI_SCLK_CTRL);
	const uint32_t ssi_state = target_mem_read32(target, RP_SSI_ENABLE);
	/* Check the XIP, QSPI and SSI controllers for their POR states, indicating we need to configure them */
	return xip_state == RP_XIP_STAT_POR && qspi_sclk_state == RP_GPIO_QSPI_SCLK_POR && ssi_state == 0U;
}

// Connect the XIP controller to the flash pads
static void rp_flash_connect_internal(target_s *const target)
{
	// Use hard reset to force IO and pad controls to known state (don't touch
	// IO_BANK0 as that does not affect XIP signals)
	const uint32_t io_pads_bits = RP_RESETS_RESET_IO_QSPI_BITS | RP_RESETS_RESET_PADS_QSPI_BITS;
	target_mem_write32(target, RP_RESETS_RESET | RP_REG_ACCESS_WRITE_ATOMIC_BITSET, io_pads_bits); // Assert the resets
	target_mem_write32(target, RP_RESETS_RESET | RP_REG_ACCESS_WRITE_ATOMIC_BITCLR, io_pads_bits); // Then deassert them
	uint32_t reset_done = 0;
	while ((reset_done & io_pads_bits) != io_pads_bits) // Wait until the reset done signals for both come good
		reset_done = target_mem_read32(target, RP_RESETS_RESET_DONE);

	// Then mux XIP block onto internal QSPI flash pads
	target_mem_write32(target, RP_GPIO_QSPI_SCLK_CTRL, 0);
	target_mem_write32(target, RP_GPIO_QSPI_CS_CTRL, 0);
	target_mem_write32(target, RP_GPIO_QSPI_SD0_CTRL, 0);
	target_mem_write32(target, RP_GPIO_QSPI_SD1_CTRL, 0);
	target_mem_write32(target, RP_GPIO_QSPI_SD2_CTRL, 0);
	target_mem_write32(target, RP_GPIO_QSPI_SD3_CTRL, 0);
}

// Set up the SSI controller for standard SPI mode,i.e. for every byte sent we get one back
// This is only called by flash_exit_xip(), not by any of the other functions.
// This makes it possible for the debugger or user code to edit SPI settings
// e.g. baud rate, CPOL/CPHA.
static void rp_flash_init_spi(target_s *const target)
{
	// Disable SSI for further config
	target_mem_write32(target, RP_SSI_ENABLE, 0);
	// Clear sticky errors (clear-on-read)
	target_mem_read32(target, RP_SSI_SR);
	target_mem_read32(target, RP_SSI_ICR);
	// Hopefully-conservative baud rate for boot and programming
	target_mem_write32(target, RP_SSI_BAUD, 6);
	target_mem_write32(target, RP_SSI_CTRL0,
		RP_SSI_CTRL0_FRF_SERIAL |       // Standard 1-bit SPI serial frames
			RP_SSI_CTRL0_DATA_BITS(8) | // 8 clocks per data frame
			RP_SSI_CTRL0_TMOD_BIDI      // TX and RX FIFOs are both used for every byte
	);
	// Slave selected when transfers in progress
	target_mem_write32(target, RP_SSI_SER, 1);
	// Re-enable
	target_mem_write32(target, RP_SSI_ENABLE, 1);
}

// Also allow any unbounded loops to check whether the above abort condition
// was asserted, and terminate early
static bool rp_flash_was_aborted(target_s *const target)
{
	return target_mem_read32(target, RP_GPIO_QSPI_SD1_CTRL) & RP_GPIO_QSPI_SD1_CTRL_INOVER_BITS;
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
	target_s *const target, const uint8_t *const tx, uint8_t *const rx, const size_t count, size_t rx_skip)
{
	// Make sure there is never more data in flight than the depth of the RX
	// FIFO. Otherwise, when we are interrupted for long periods, hardware
	// will overflow the RX FIFO.
	static const size_t max_in_flight = 16U - 2U; // account for data internal to SSI
	size_t tx_count = 0;
	size_t rx_count = 0;
	while (tx_count < count || rx_count < count || rx_skip) {
		// NB order of reads, for pessimism rather than optimism
		const uint32_t tx_level = target_mem_read32(target, RP_SSI_TXFLR);
		const uint32_t rx_level = target_mem_read32(target, RP_SSI_RXFLR);
		bool idle = true; // Expect this to be folded into control flow, not register
		if (tx_count < count && tx_level + rx_level < max_in_flight) {
			target_mem_write32(target, RP_SSI_DR0, (uint32_t)(tx ? tx[tx_count] : 0));
			++tx_count;
			idle = false;
		}
		if (rx_level) {
			const uint8_t data = target_mem_read32(target, RP_SSI_DR0);
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
		if (idle && rp_flash_was_aborted(target))
			break;
	}
	rp_spi_chip_select(target, RP_GPIO_QSPI_CS_DRIVE_HIGH);
}

// Sequence:
// 1. CSn = 1, IO = 4'h0 (via pulldown to avoid contention), x32 clocks
// 2. CSn = 0, IO = 4'hf (via pullup to avoid contention), x32 clocks
// 3. CSn = 1 (brief deassertion)
// 4. CSn = 0, MOSI = 1'b1 driven, x16 clocks
//
// Part 4 is the sequence suggested in W25X10CL datasheet.
// Parts 1 and 2 are to improve compatibility with Micron parts
static void rp_flash_exit_xip(target_s *const target)
{
	uint8_t buf[2];
	memset(buf, 0xffU, sizeof(buf));

	rp_flash_init_spi(target);

	uint32_t padctrl_save = target_mem_read32(target, RP_PADS_QSPI_GPIO_SD0);
	uint32_t padctrl_tmp =
		(padctrl_save &
			~(RP_PADS_QSPI_GPIO_SD0_OD_BITS | RP_PADS_QSPI_GPIO_SD0_PUE_BITS | RP_PADS_QSPI_GPIO_SD0_PDE_BITS)) |
		RP_PADS_QSPI_GPIO_SD0_OD_BITS | RP_PADS_QSPI_GPIO_SD0_PDE_BITS;

	// First two 32-clock sequences
	// CSn is held high for the first 32 clocks, then asserted low for next 32
	rp_spi_chip_select(target, RP_GPIO_QSPI_CS_DRIVE_HIGH);
	for (size_t i = 0; i < 2U; ++i) {
		// This gives 4 16-bit offset store instructions. Anything else seems to
		// produce a large island of constants
		target_mem_write32(target, RP_PADS_QSPI_GPIO_SD0, padctrl_tmp);
		target_mem_write32(target, RP_PADS_QSPI_GPIO_SD1, padctrl_tmp);
		target_mem_write32(target, RP_PADS_QSPI_GPIO_SD2, padctrl_tmp);
		target_mem_write32(target, RP_PADS_QSPI_GPIO_SD3, padctrl_tmp);

		// Brief delay (~6000 cyc) for pulls to take effect
		platform_delay(10);

		rp_flash_put_get(target, NULL, NULL, 4, 0);

		padctrl_tmp = (padctrl_tmp & ~RP_PADS_QSPI_GPIO_SD0_PDE_BITS) | RP_PADS_QSPI_GPIO_SD0_PUE_BITS;

		rp_spi_chip_select(target, RP_GPIO_QSPI_CS_DRIVE_LOW);
	}

	// Restore IO/pad controls, and send 0xff, 0xff. Put pullup on IO2/IO3 as
	// these may be used as WPn/HOLDn at this point, and we are now starting
	// to issue serial commands.

	target_mem_write32(target, RP_PADS_QSPI_GPIO_SD0, padctrl_save);
	target_mem_write32(target, RP_PADS_QSPI_GPIO_SD1, padctrl_save);
	padctrl_save = (padctrl_save & ~RP_PADS_QSPI_GPIO_SD0_PDE_BITS) | RP_PADS_QSPI_GPIO_SD0_PUE_BITS;
	target_mem_write32(target, RP_PADS_QSPI_GPIO_SD2, padctrl_save);
	target_mem_write32(target, RP_PADS_QSPI_GPIO_SD3, padctrl_save);

	rp_spi_chip_select(target, RP_GPIO_QSPI_CS_DRIVE_LOW);
	rp_flash_put_get(target, buf, NULL, 2, 0);

	target_mem_write32(target, RP_GPIO_QSPI_CS_CTRL, 0);
}

// This is a hook for steps to be taken in between programming the flash and
// doing cached XIP reads from the flash. Called by the bootrom before
// entering flash second stage, and called by the debugger after flash
// programming.
static void rp_flash_flush_cache(target_s *const target)
{
	target_mem_write32(target, RP_XIP_FLUSH, 1);
	// Read blocks until flush completion
	target_mem_read32(target, RP_XIP_FLUSH);
	// Enable the cache
	target_mem_write32(target, RP_XIP_CTRL | RP_REG_ACCESS_WRITE_ATOMIC_BITSET, RP_XIP_CTRL_ENABLE);
	rp_spi_chip_select(target, RP_GPIO_QSPI_CS_DRIVE_NORMAL);
}

// Put the SSI into a mode where XIP accesses translate to standard
// serial 03h read commands. The flash remains in its default serial command
// state, so will still respond to other commands.
static void rp_flash_enter_xip(target_s *const target)
{
	target_mem_write32(target, RP_SSI_ENABLE, 0);
	target_mem_write32(target, RP_SSI_CTRL0,
		RP_SSI_CTRL0_FRF_SERIAL |         // Standard 1-bit SPI serial frames
			RP_SSI_CTRL0_DATA_BITS(32U) | // 32 clocks per data frame
			RP_SSI_CTRL0_TMOD_EEPROM      // Send instr + addr, receive data
	);
	target_mem_write32(target, RP_SSI_XIP_SPI_CTRL0,
		RP_SSI_XIP_SPI_CTRL0_XIP_CMD(0x03U) |            // Standard 03h read
			RP_SSI_XIP_SPI_CTRL0_INSTR_LENGTH_8b |       // 8-bit instruction prefix
			RP_SSI_XIP_SPI_CTRL0_ADDRESS_LENGTH(0x03U) | // 24-bit addressing for 03h commands
			RP_SSI_XIP_SPI_CTRL0_TRANS_1C1A              // Command and address both in serial format
	);
	target_mem_write32(target, RP_SSI_ENABLE, RP_SSI_ENABLE_SSI);
}

static uint32_t rp_get_flash_length(target_s *const target)
{
	// Read the JEDEC ID and try to decode it
	spi_flash_id_s flash_id;
	rp_spi_read(target, SPI_FLASH_CMD_READ_JEDEC_ID, 0, &flash_id, sizeof(flash_id));

	DEBUG_INFO("Flash device ID: %02x %02x %02x\n", flash_id.manufacturer, flash_id.type, flash_id.capacity);
	if (flash_id.capacity >= 8U && flash_id.capacity <= 34U)
		return 1U << flash_id.capacity;

	// Guess maximum flash size
	return MAX_FLASH;
}

static bool rp_cmd_erase_sector(target_s *target, int argc, const char **argv)
{
	uint32_t start = target->flash->start;
	uint32_t length;

	if (argc == 3) {
		start = strtoul(argv[1], NULL, 0);
		length = strtoul(argv[2], NULL, 0);
	} else if (argc == 2)
		length = strtoul(argv[1], NULL, 0);
	else
		return false;

	target_flash_s *const flash = target->flash;

	bool result = true; /* catch false returns with &= */
	result &= rp_flash_prepare(target);
	result &= flash->erase(flash, start, length);
	result &= rp_flash_resume(target);
	return result;
}

static bool rp_cmd_reset_usb_boot(target_s *t, int argc, const char **argv)
{
	uint32_t regs[20U] = {0};
	rp_priv_s *ps = (rp_priv_s *)t->target_storage;
	/* Set up the arguments for the function call */
	if (argc > 1)
		regs[0] = strtoul(argv[1], NULL, 0);
	if (argc > 2)
		regs[1] = strtoul(argv[2], NULL, 0);
	/* The USB boot function does not return and takes its arguments in r0 and r1 */
	regs[CORTEX_REG_PC] = ps->rom_reset_usb_boot;
	/* So load the link register with a dummy return address like we just booted the chip */
	regs[CORTEX_REG_LR] = UINT32_MAX;
	/* Configure the stack to the end of SRAM and configure the status register for Thumb execution */
	regs[CORTEX_REG_MSP] = RP_SRAM_BASE + RP_SRAM_SIZE;
	regs[CORTEX_REG_XPSR] = CORTEXM_XPSR_THUMB;
	/* Now reconfigure the core with the new execution environment */
	target_regs_write(t, regs);
	/* And resume the core */
	target_halt_resume(t, false);
	return true;
}

static bool rp_rescue_do_reset(target_s *t)
{
	adiv5_access_port_s *ap = (adiv5_access_port_s *)t->priv;
	uint32_t ctrlstat = ap->dp->low_access(ap->dp, ADIV5_LOW_READ, ADIV5_DP_CTRLSTAT, 0);
	ap->dp->low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_DP_CTRLSTAT, ctrlstat | ADIV5_DP_CTRLSTAT_CDBGPWRUPREQ);
	platform_timeout_s timeout;
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
bool rp_rescue_probe(adiv5_access_port_s *ap)
{
	target_s *t = target_new();
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
