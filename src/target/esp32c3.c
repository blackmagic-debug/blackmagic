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

#include "general.h"
#include "target_internal.h"
#include "target_probe.h"
#include "riscv_debug.h"
#include "sfdp.h"

#define ESP32_C3_ARCH_ID 0x80000001U
#define ESP32_C3_IMPL_ID 0x00000001U

#define ESP32_C3_DBUS_SRAM1_BASE 0x3fc80000U
#define ESP32_C3_DBUS_SRAM1_SIZE 0x00060000U
#define ESP32_C3_IBUS_SRAM0_BASE 0x4037c000U
#define ESP32_C3_IBUS_SRAM0_SIZE 0x00004000U
#define ESP32_C3_IBUS_SRAM1_BASE 0x40380000U
#define ESP32_C3_IBUS_SRAM1_SIZE 0x00060000U
#define ESP32_C3_RTC_SRAM_BASE   0x50000000U
#define ESP32_C3_RTC_SRAM_SIZE   0x00002000U

#define ESP32_C3_IBUS_FLASH_BASE 0x42000000U
#define ESP32_C3_IBUS_FLASH_SIZE 0x00800000U

#define ESP32_C3_SPI1_BASE         0x60002000U
#define ESP32_C3_SPI1_CMD          (ESP32_C3_SPI1_BASE + 0x000U)
#define ESP32_C3_SPI1_ADDR         (ESP32_C3_SPI1_BASE + 0x004U)
#define ESP32_C3_SPI1_USER0        (ESP32_C3_SPI1_BASE + 0x018U)
#define ESP32_C3_SPI1_USER1        (ESP32_C3_SPI1_BASE + 0x01cU)
#define ESP32_C3_SPI1_USER2        (ESP32_C3_SPI1_BASE + 0x020U)
#define ESP32_C3_SPI1_DATA_OUT_LEN (ESP32_C3_SPI1_BASE + 0x024U)
#define ESP32_C3_SPI1_DATA_IN_LEN  (ESP32_C3_SPI1_BASE + 0x028U)
#define ESP32_C3_SPI1_DATA         (ESP32_C3_SPI1_BASE + 0x058U)

/* These define the various stages of a SPI transaction that we can choose to enable */
#define ESP32_C3_SPI_CMD_EXEC_XFER  0x00040000U
#define ESP32_C3_SPI_USER0_CMD      0x80000000U
#define ESP32_C3_SPI_USER0_ADDR     0x40000000U
#define ESP32_C3_SPI_USER0_DUMMY    0x20000000U
#define ESP32_C3_SPI_USER0_DATA_IN  0x10000000U
#define ESP32_C3_SPI_USER0_DATA_OUT 0x08000000U

/* These define the various bit ranges used to store the cycle counts for the enabled stages */
#define ESP32_C3_SPI_USER2_CMD_LEN_MASK   0xf0000000U
#define ESP32_C3_SPI_USER2_CMD_LEN_SHIFT  28U
#define ESP32_C3_SPI_USER2_CMD_LEN(x)     ((((x)-1U) << ESP32_C3_SPI_USER2_CMD_LEN_SHIFT) & ESP32_C3_SPI_USER2_CMD_LEN_MASK)
#define ESP32_C3_SPI_USER1_ADDR_LEN_MASK  0xfc000000U
#define ESP32_C3_SPI_USER1_ADDR_LEN_SHIFT 26U
#define ESP32_C3_SPI_USER1_ADDR_LEN(x) \
	((((x)-1U) << ESP32_C3_SPI_USER1_ADDR_LEN_SHIFT) & ESP32_C3_SPI_USER1_ADDR_LEN_MASK)
#define ESP32_C3_SPI_USER1_DUMMY_LEN_MASK 0x0000003fU
#define ESP32_C3_SPI_USER1_DUMMY_LEN(x)   (((x)-1U) & ESP32_C3_SPI_USER1_DUMMY_LEN_MASK)
#define ESP32_C3_SPI_DATA_BIT_LEN_MASK    0x000003ffU
#define ESP32_C3_SPI_DATA_BIT_LEN(x)      ((((x)*8U) - 1U) & ESP32_C3_SPI_DATA_BIT_LEN_MASK)

#define ESP32_C3_SPI_FLASH_OPCODE_MASK      0x000000ffU
#define ESP32_C3_SPI_FLASH_OPCODE(x)        ((x)&ESP32_C3_SPI_FLASH_OPCODE_MASK)
#define ESP32_C3_SPI_FLASH_DUMMY_MASK       0x0000ff00U
#define ESP32_C3_SPI_FLASH_DUMMY_SHIFT      8U
#define ESP32_C3_SPI_FLASH_DUMMY_LEN(x)     (((x) << ESP32_C3_SPI_FLASH_DUMMY_SHIFT) & ESP32_C3_SPI_FLASH_DUMMY_MASK)
#define ESP32_C3_SPI_FLASH_OPCODE_MODE_MASK 0x00010000U
#define ESP32_C3_SPI_FLASH_OPCODE_ONLY      (0U << 16U)
#define ESP32_C3_SPI_FLASH_OPCODE_3B_ADDR   (1U << 16U)
#define ESP32_C3_SPI_FLASH_DATA_IN          (0U << 17U)
#define ESP32_C3_SPI_FLASH_DATA_OUT         (1U << 17U)
#define ESP32_C3_SPI_FLASH_COMMAND_MASK     0x0003ffffU

#define SPI_FLASH_OPCODE_SECTOR_ERASE 0x20U
#define SPI_FLASH_CMD_READ_JEDEC_ID                                                                  \
	(ESP32_C3_SPI_FLASH_OPCODE_ONLY | ESP32_C3_SPI_FLASH_DATA_IN | ESP32_C3_SPI_FLASH_DUMMY_LEN(0) | \
		ESP32_C3_SPI_FLASH_OPCODE(0x9fU))
#define SPI_FLASH_CMD_READ_SFDP                                                                         \
	(ESP32_C3_SPI_FLASH_OPCODE_3B_ADDR | ESP32_C3_SPI_FLASH_DATA_IN | ESP32_C3_SPI_FLASH_DUMMY_LEN(8) | \
		ESP32_C3_SPI_FLASH_OPCODE(0x5aU))

static void esp32c3_spi_read(target_s *target, uint32_t command, target_addr_t address, void *buffer, size_t length);

static void esp32c3_spi_read_sfdp(
	target_s *const target, const uint32_t address, void *const buffer, const size_t length)
{
	esp32c3_spi_read(target, SPI_FLASH_CMD_READ_SFDP, address, buffer, length);
}

static void esp32c3_add_flash(target_s *const target)
{
	target_flash_s *const flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	spi_parameters_s spi_parameters;
	if (!sfdp_read_parameters(target, &spi_parameters, esp32c3_spi_read_sfdp)) {
		/* SFDP readout failed, so read the JTAG ID of the device next */
		spi_flash_id_s flash_id;
		esp32c3_spi_read(target, SPI_FLASH_CMD_READ_JEDEC_ID, 0, &flash_id, sizeof(flash_id));
		const uint32_t capacity = 1U << flash_id.capacity;

		/* Now make some assumptions and hope for the best. */
		spi_parameters.page_size = 256U;
		spi_parameters.sector_size = 4096U;
		spi_parameters.capacity = MIN(capacity, ESP32_C3_IBUS_FLASH_SIZE);
		spi_parameters.sector_erase_opcode = SPI_FLASH_OPCODE_SECTOR_ERASE;
	}

	flash->start = ESP32_C3_IBUS_FLASH_BASE;
	flash->length = MIN(spi_parameters.capacity, ESP32_C3_IBUS_FLASH_SIZE);
	flash->blocksize = spi_parameters.sector_size;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

bool esp32c3_probe(target_s *const target)
{
	const riscv_hart_s *const hart = riscv_hart_struct(target);
	/* Seems that the best we can do is check the marchid and mimplid register values */
	if (hart->archid != ESP32_C3_ARCH_ID || hart->implid != ESP32_C3_IMPL_ID)
		return false;

	target->driver = "ESP32-C3";
	/* Establish the target RAM mappings */
	target_add_ram(target, ESP32_C3_IBUS_SRAM0_BASE, ESP32_C3_IBUS_SRAM0_SIZE);
	target_add_ram(target, ESP32_C3_IBUS_SRAM1_BASE, ESP32_C3_IBUS_SRAM1_SIZE);
	target_add_ram(target, ESP32_C3_DBUS_SRAM1_BASE, ESP32_C3_DBUS_SRAM1_SIZE);
	target_add_ram(target, ESP32_C3_RTC_SRAM_BASE, ESP32_C3_RTC_SRAM_SIZE);

	/* Establish the target Flash mappings */
	esp32c3_add_flash(target);

	return true;
}

static void esp32c3_spi_config(
	target_s *const target, const uint32_t command, const target_addr_t address, size_t length)
{
	uint32_t enabled_stages = ESP32_C3_SPI_USER0_CMD;
	uint32_t user1_value = 0;

	/* Set up the command phase */
	const uint8_t spi_command = command & ESP32_C3_SPI_FLASH_OPCODE_MASK;
	target_mem_write32(target, ESP32_C3_SPI1_USER2, ESP32_C3_SPI_USER2_CMD_LEN(8) | spi_command);

	/* Configure the address to send */
	if ((command & ESP32_C3_SPI_FLASH_OPCODE_MODE_MASK) == ESP32_C3_SPI_FLASH_OPCODE_3B_ADDR) {
		enabled_stages |= ESP32_C3_SPI_USER0_ADDR;
		target_mem_write32(target, ESP32_C3_SPI1_ADDR, address);
		user1_value |= ESP32_C3_SPI_USER1_ADDR_LEN(24U);
	}

	/* Configure the number of dummy cycles required */
	if (command & ESP32_C3_SPI_FLASH_DUMMY_MASK) {
		enabled_stages |= ESP32_C3_SPI_USER0_DUMMY;
		uint8_t dummy_cycles = (command & ESP32_C3_SPI_FLASH_DUMMY_MASK) >> ESP32_C3_SPI_FLASH_DUMMY_SHIFT;
		user1_value |= ESP32_C3_SPI_USER1_DUMMY_LEN(dummy_cycles);
	}

	/* Configure the data phase */
	if (length) {
		if (command & ESP32_C3_SPI_FLASH_DATA_OUT) {
			enabled_stages |= ESP32_C3_SPI_USER0_DATA_OUT;
			target_mem_write32(target, ESP32_C3_SPI1_DATA_OUT_LEN, ESP32_C3_SPI_DATA_BIT_LEN(length));
		} else {
			enabled_stages |= ESP32_C3_SPI_USER0_DATA_IN;
			target_mem_write32(target, ESP32_C3_SPI1_DATA_IN_LEN, ESP32_C3_SPI_DATA_BIT_LEN(length));
		}
	}

	/* Now we've defined all the information needed for user0 and user1, send it */
	target_mem_write32(target, ESP32_C3_SPI1_USER0, enabled_stages);
	target_mem_write32(target, ESP32_C3_SPI1_USER1, user1_value);
}

static void esp32c3_spi_read(
	target_s *const target, const uint32_t command, const target_addr_t address, void *const buffer, size_t length)
{
	/* Start by setting up the common components of the transaction */
	esp32c3_spi_config(target, command, address, length);
	/* Now trigger the configured transaction */
	target_mem_write32(target, ESP32_C3_SPI1_CMD, ESP32_C3_SPI_CMD_EXEC_XFER);
	/* And wait for the transaction to complete */
	while (target_mem_read32(target, ESP32_C3_SPI1_CMD) & ESP32_C3_SPI_CMD_EXEC_XFER)
		continue;

	uint8_t *const data = (uint8_t *)buffer;
	for (size_t offset = 0; offset < length; offset += 4U) {
		const uint32_t value = target_mem_read32(target, ESP32_C3_SPI1_DATA + offset);
		const size_t amount = MIN(4U, length - offset);
		memcpy(data + offset, &value, amount);
	}
}
