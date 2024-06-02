/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

/* Support for the Renesas RZ family of microprocessors */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexar.h"
#include "spi.h"
#include "sfdp.h"

/*
 * Part numbering scheme
 *
 *  R7   S   xx   x   x   xx   x   x   xx
 * \__/ \_/ \__/ \_/ \_/ \__/ \_/ \_/ \__/
 *  |    |   |    |   |   |    |   |   |
 *  |    |   |    |   |   |    |   |   \_ Package type
 *  |    |   |    |   |   |    |   \_____ Quality Grade
 *  |    |   |    |   |   |    \_________ Operating temperature
 *  |    |   |    |   |   \______________ Group/Tier number?
 *  |    |   |    |   \__________________ Feature set
 *  |    |   |    \______________________ Group number
 *  |    |   \___________________________ Series name
 *  |    \_______________________________ Family (S: RZ)
 *  \____________________________________ Renesas microprocessor (always 'R7')
 *
 *  R9   A   xx   x   x   xx   x   x   xx
 * \__/ \_/ \__/ \_/ \_/ \__/ \_/ \_/ \__/
 *  |    |   |    |   |   |    |   |   |
 *  |    |   |    |   |   |    |   |   \_ Package type
 *  |    |   |    |   |   |    |   \_____ Quality Grade
 *  |    |   |    |   |   |    \_________ Operating temperature
 *  |    |   |    |   |   \______________ Group/Tier number?
 *  |    |   |    |   \__________________ Feature set
 *  |    |   |    \______________________ Group number
 *  |    |   \___________________________ Series name
 *  |    \_______________________________ Family (A: RZ)
 *  \____________________________________ Renesas microprocessor (always 'R9')
 *
 * Renesas Flash MCUs have an internal 16 byte read only register that stores
 * the part number, the code is stored ascii encoded, starting from the lowest memory address
 * except for pnrs stored in 'FIXED_PNR1', where the code is stored in reverse order (but the last 3 bytes are still 0x20 aka ' ')
 */

/* Base address and size for the 4 OCRAM regions + their mirrors (includes RETRAM) */
#define RENESAS_OCRAM_BASE        0x20000000U
#define RENESAS_OCRAM_MIRROR_BASE 0x60000000U
#define RENESAS_OCRAM_SIZE        0x00200000U
/* Base address and max size for the SPI Flash XIP region */
#define RENESAS_SPI_FLASH_BASE 0x18000000U
#define RENESAS_SPI_FLASH_SIZE 0x04000000U

/* Base address for the boundary scan controller and boot mode register */
/*
 * NB: These addresses are only documented by rev 1 of the manual,
 * all further versions deleted these addresses and their documentation
 * wholesale. This has also been deduced in part from the ROM.
 */
#define RENESAS_BSCAN_BASE      0xfcfe1800U
#define RENESAS_BSCAN_BOOT_MODE (RENESAS_BSCAN_BASE + 0x000U)
#define RENESAS_BSCAN_BSID      (RENESAS_BSCAN_BASE + 0x004U)

#define RENESAS_BSCAN_BOOT_MODE_SPI  0x00000004U
#define RENESAS_BSCAN_BOOT_MODE_MASK 0x00000006U
#define RENESAS_BSCAN_BSID_RZ_A1L    0x081a6447U
#define RENESAS_BSCAN_BSID_RZ_A1LU   0x08178447U
#define RENESAS_BSCAN_BSID_RZ_A1LC   0x082f4447U

/* SPI Multi I/O Bus Controller registers, from R01UH0437EJ0600 §17.4, pg739 (17-4) */
#define RENESAS_MULTI_IO_SPI_BASE             0x3fefa000U
#define RENESAS_MULTI_IO_SPI_COMMON_CTRL      (RENESAS_MULTI_IO_SPI_BASE + 0x000U)
#define RENESAS_MULTI_IO_SPI_READ_CTRL        (RENESAS_MULTI_IO_SPI_BASE + 0x00cU)
#define RENESAS_MULTI_IO_SPI_MODE_CTRL        (RENESAS_MULTI_IO_SPI_BASE + 0x020U)
#define RENESAS_MULTI_IO_SPI_MODE_CMD         (RENESAS_MULTI_IO_SPI_BASE + 0x024U)
#define RENESAS_MULTI_IO_SPI_MODE_ADDR        (RENESAS_MULTI_IO_SPI_BASE + 0x028U)
#define RENESAS_MULTI_IO_SPI_MODE_DUMMY_DATA  (RENESAS_MULTI_IO_SPI_BASE + 0x02cU)
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG (RENESAS_MULTI_IO_SPI_BASE + 0x030U)
#define RENESAS_MULTI_IO_SPI_MODE_READ_DATA   (RENESAS_MULTI_IO_SPI_BASE + 0x038U)
#define RENESAS_MULTI_IO_SPI_MODE_WRITE_DATA  (RENESAS_MULTI_IO_SPI_BASE + 0x040U)
#define RENESAS_MULTI_IO_SPI_MODE_STATUS      (RENESAS_MULTI_IO_SPI_BASE + 0x048U)

#define RENESAS_MULTI_IO_SPI_COMMON_CTRL_MODE_SPI             (1U << 31U)
#define RENESAS_MULTI_IO_SPI_READ_CTRL_CS_UNSELECT            (1U << 24U)
#define RENESAS_MULTI_IO_SPI_READ_CTRL_CACHE_FLUSH            (1U << 9U)
#define RENESAS_MULTI_IO_SPI_MODE_CTRL_CS_HOLD                (1U << 8U)
#define RENESAS_MULTI_IO_SPI_MODE_CTRL_READ_ENABLE            (1U << 2U)
#define RENESAS_MULTI_IO_SPI_MODE_CTRL_WRITE_ENABLE           (1U << 1U)
#define RENESAS_MULTI_IO_SPI_MODE_CTRL_RUN_XFER               (1U << 0U)
#define RENESAS_MULTI_IO_SPI_MODE_CMD_SHIFT                   16U
#define RENESAS_MULTI_IO_SPI_MODE_CMD_MASK                    0x00ff0000U
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_CMD             (1U << 14U)
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_ADDR_NONE       (0x0U << 8U)
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_ADDR_3B         (0x7U << 8U)
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_ADDR_4B         (0xfU << 8U)
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_DUMMY_SHIFT     4U
#define RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_DATA_XFER_SHIFT 0U
#define RENESAS_MULTI_IO_SPI_MODE_STATUS_XFER_COMPLETE        (1U << 0U)

/* ARM PL310 L2 cache controller registers, from DDI0246C §3.2, pg83 (3-5) */
#define ARM_PL310_BASE                        0x3ffff000U
#define ARM_PL310_CACHE_SYNC                  (ARM_PL310_BASE + 0x730U)
#define ARM_PL310_INVALIDATE_BY_WAY           (ARM_PL310_BASE + 0x77cU)
#define ARM_PL310_CLEAN_AND_INVALIDATE_BY_WAY (ARM_PL310_BASE + 0x7fcU)

#define RENESAS_ARM_PL310_CACHE_ASSOCIATIVITY 8U

/* This is the part number from the ROM table of a R7S721030 and is a guess */
#define ID_RZ_A1 0x012U

static const char *renesas_rz_part_name(uint32_t part_id);
static bool renesas_rz_flash_prepare(target_s *target);
static bool renesas_rz_flash_resume(target_s *target);
static void renesas_rz_spi_read(target_s *target, uint16_t command, target_addr_t address, void *buffer, size_t length);
static void renesas_rz_spi_write(
	target_s *target, uint16_t command, target_addr_t address, const void *buffer, size_t length);
static void renesas_rz_spi_run_command(target_s *target, uint16_t command, target_addr_t address);

static void renesas_rz_add_flash(target_s *const target)
{
	target->enter_flash_mode = renesas_rz_flash_prepare;
	target->exit_flash_mode = renesas_rz_flash_resume;

	/* Put the controller into manual SPI operations mode */
	renesas_rz_flash_prepare(target);
	/* Register the Flash via the SPI Flash implementation */
	bmp_spi_add_flash(target, RENESAS_SPI_FLASH_BASE, RENESAS_SPI_FLASH_SIZE, renesas_rz_spi_read, renesas_rz_spi_write,
		renesas_rz_spi_run_command);
	/* Put the controller back into bus usage mode */
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_COMMON_CTRL,
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_COMMON_CTRL) & ~RENESAS_MULTI_IO_SPI_COMMON_CTRL_MODE_SPI);

	/* Register the SPI Flash mass erase implementation for mass erase */
	target->mass_erase = bmp_spi_mass_erase;
}

bool renesas_rz_probe(target_s *const target)
{
	/* Determine that it's *probably* a RZ part */
	if (target->part_id != ID_RZ_A1)
		return false;

	/* Read out the BSID register to confirm that */
	const uint32_t part_id = target_mem32_read32(target, RENESAS_BSCAN_BSID);
	/* If the read failed, it's not a RZ/A1L* part */
	if (!part_id)
		return false;

	target->driver = renesas_rz_part_name(part_id);

	/* Now determine the boot mode */
	const uint8_t boot_mode = target_mem32_read32(target, RENESAS_BSCAN_BOOT_MODE) & RENESAS_BSCAN_BOOT_MODE_MASK;
	if (boot_mode == RENESAS_BSCAN_BOOT_MODE_SPI)
		renesas_rz_add_flash(target);

	target_add_ram32(target, RENESAS_OCRAM_BASE, RENESAS_OCRAM_SIZE);
	target_add_ram32(target, RENESAS_OCRAM_MIRROR_BASE, RENESAS_OCRAM_SIZE);
	return true;
}

static const char *renesas_rz_part_name(const uint32_t part_id)
{
	switch (part_id) {
	case RENESAS_BSCAN_BSID_RZ_A1L:
		return "RZ/A1L";
	case RENESAS_BSCAN_BSID_RZ_A1LC:
		return "RZ/A1LC";
	case RENESAS_BSCAN_BSID_RZ_A1LU:
		/* This is common to A1LU and A1H at least */
		return "RZ/A1";
	}
	return "Unknown";
}

static bool renesas_rz_flash_prepare(target_s *const target)
{
	/* Halt any ongoing bust reads */
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_READ_CTRL,
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_READ_CTRL) | RENESAS_MULTI_IO_SPI_READ_CTRL_CS_UNSELECT);
	/* Wait for any existing operations to complete */
	while (!(
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_MODE_STATUS) & RENESAS_MULTI_IO_SPI_MODE_STATUS_XFER_COMPLETE))
		continue;
	/* Bring the controller out of bus usage mode */
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_COMMON_CTRL,
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_COMMON_CTRL) | RENESAS_MULTI_IO_SPI_COMMON_CTRL_MODE_SPI);
	return true;
}

static bool renesas_rz_flash_resume(target_s *const target)
{
	/* Flush the controller's read cache */
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_READ_CTRL,
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_READ_CTRL) | RENESAS_MULTI_IO_SPI_READ_CTRL_CACHE_FLUSH);
	target_mem32_read32(target, RENESAS_MULTI_IO_SPI_READ_CTRL);
	/* Put the controller back into bus usage mode */
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_COMMON_CTRL,
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_COMMON_CTRL) & ~RENESAS_MULTI_IO_SPI_COMMON_CTRL_MODE_SPI);
	/* Invalidate the L1 D-caches and I-caches */
	cortexar_invalidate_all_caches(target);
	/* Invalidate the L2 cache ways so we get a clean state */
	const uint32_t l2_cache_ways_mask = (1U << RENESAS_ARM_PL310_CACHE_ASSOCIATIVITY) - 1U;
	target_mem32_write32(target, ARM_PL310_CLEAN_AND_INVALIDATE_BY_WAY, l2_cache_ways_mask);
	while (target_mem32_read32(target, ARM_PL310_CLEAN_AND_INVALIDATE_BY_WAY) & l2_cache_ways_mask)
		continue;
	target_mem32_write32(target, ARM_PL310_CACHE_SYNC, 0U);
	return true;
}

static uint32_t renesas_rz_spi_setup_xfer(
	target_s *const target, const uint16_t command, const target_addr_t address, const size_t length)
{
	/* Set up the command byte, dummy bytes and address for the transfer */
	const uint8_t opcode = command & SPI_FLASH_OPCODE_MASK;
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_CMD, opcode << RENESAS_MULTI_IO_SPI_MODE_CMD_SHIFT);
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_ADDR, address);
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_DUMMY_DATA, 0U);

	/* Set up the phases that need to be enabled for the transfer */
	uint32_t config = RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_CMD;
	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR)
		config |= RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_ADDR_3B;
	/* If we need to insert any dummy byte cycles into the transaction */
	const uint8_t dummy_bytes = (command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT;
	if (dummy_bytes) {
		/*
		 * Create a bit mask for how much of the dummy data register to send to generate enough cycles.
		 * This will start out right aligned - eg, for 3 dummy cycles, 0b0111, so we need to then
		 * left align this within the nibble by shifting by 4 - dummy_bytes, producing a value like 0b1110.
		 * Finally we then shift this up into place within the config value.
		 * NB: This will not work for any more than 4 dummy byte cycles.
		 */
		config |= (((1U << dummy_bytes) - 1U) << (4U - dummy_bytes))
			<< RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_DUMMY_SHIFT;
	}
	/* Create a bit mask for the first part of the required data transfer, same process as with the dummy bytes */
	const uint8_t initial_bytes = MIN(4U, length);
	config |= (((1U << initial_bytes) - 1U) << (4U - initial_bytes))
		<< RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_DATA_XFER_SHIFT;
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG, config);

	/* If the transfer has no data associated with it, do not enable either transfer direction */
	if (!length)
		return 0;
	/*
	 * Convert the transaction direction into a control register direction setting and
	 * configure if we need ~CS held at the end of the transfer
	 */
	return ((command & SPI_FLASH_DATA_MASK) == SPI_FLASH_DATA_IN ? RENESAS_MULTI_IO_SPI_MODE_CTRL_READ_ENABLE :
																   RENESAS_MULTI_IO_SPI_MODE_CTRL_WRITE_ENABLE) |
		(length > 4U ? RENESAS_MULTI_IO_SPI_MODE_CTRL_CS_HOLD : 0U);
}

static void renesas_rz_spi_run_xfer(target_s *const target, const uint32_t ctrl)
{
	/* Set the requested transfer running */
	target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_CTRL, ctrl | RENESAS_MULTI_IO_SPI_MODE_CTRL_RUN_XFER);
	/* Wait for it to complete */
	while (!(
		target_mem32_read32(target, RENESAS_MULTI_IO_SPI_MODE_STATUS) & RENESAS_MULTI_IO_SPI_MODE_STATUS_XFER_COMPLETE))
		continue;
}

static void renesas_rz_spi_read(target_s *const target, const uint16_t command, const target_addr_t address,
	void *const buffer, const size_t length)
{
	/* Set up the transaction */
	uint32_t ctrl = renesas_rz_spi_setup_xfer(target, command, address, length);
	uint8_t *const data = (uint8_t *)buffer;
	/* For each 4 byte chunk to be read */
	for (size_t offset = 0U; offset < length;) {
		/* Run the transfer that's configured */
		renesas_rz_spi_run_xfer(target, ctrl);
		/* Read back the data read and copy it into the output buffer */
		const uint32_t value = target_mem32_read32(target, RENESAS_MULTI_IO_SPI_MODE_READ_DATA);
		memcpy(data + offset, &value, MIN(length - offset, 4U));
		/* Turn off all the optional phases and set up the next transfer chunk */
		offset += 4U;
		const uint8_t amount = MIN(4U, length - offset);
		target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG,
			(((1U << amount) - 1U) << (4U - amount)) << RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_DATA_XFER_SHIFT);
		/* Adjust the control value if we're going into the last read of the block */
		if (length - offset <= 4U)
			ctrl &= ~RENESAS_MULTI_IO_SPI_MODE_CTRL_CS_HOLD;
	}
}

static void renesas_rz_spi_write(target_s *const target, const uint16_t command, const target_addr_t address,
	const void *const buffer, const size_t length)
{
	/* Set up the transaction */
	uint32_t ctrl = renesas_rz_spi_setup_xfer(target, command, address, length);
	const uint8_t *const data = (const uint8_t *)buffer;
	/* For each 4 byte chunk to be written */
	for (size_t offset = 0U; offset < length;) {
		/* Prepare the data to send from the input buffer and write it to the target */
		uint32_t value = 0U;
		memcpy(&value, data + offset, MIN(length - offset, 4U));
		target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_WRITE_DATA, value);
		/* Run the transfer that's configured */
		renesas_rz_spi_run_xfer(target, ctrl);
		/* Turn off all the optional phases and set up the next transfer chunk */
		offset += 4U;
		const uint8_t amount = MIN(4U, length - offset);
		target_mem32_write32(target, RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG,
			(((1U << amount) - 1U) << (4U - amount)) << RENESAS_MULTI_IO_SPI_MODE_XFER_CONFIG_DATA_XFER_SHIFT);
		/* Adjust the control value if we're going into the last write of the block */
		if (length - offset <= 4U)
			ctrl &= ~RENESAS_MULTI_IO_SPI_MODE_CTRL_CS_HOLD;
	}
}

static void renesas_rz_spi_run_command(target_s *const target, const uint16_t command, const target_addr_t address)
{
	/* Set up and run the requested command transaction */
	const uint32_t ctrl = renesas_rz_spi_setup_xfer(target, command, address, 0U);
	renesas_rz_spi_run_xfer(target, ctrl);
}
