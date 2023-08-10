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
#define ESP32_C3_SPI1_MISC         (ESP32_C3_SPI1_BASE + 0x034U)
#define ESP32_C3_SPI1_DATA         (ESP32_C3_SPI1_BASE + 0x058U)

/* These define the various stages of a SPI transaction that we can choose to enable */
#define ESP32_C3_SPI_CMD_EXEC_XFER  0x00040000U
#define ESP32_C3_SPI_USER0_CMD      0x80000000U
#define ESP32_C3_SPI_USER0_ADDR     0x40000000U
#define ESP32_C3_SPI_USER0_DUMMY    0x20000000U
#define ESP32_C3_SPI_USER0_DATA_IN  0x10000000U
#define ESP32_C3_SPI_USER0_DATA_OUT 0x08000000U
#define ESP32_C3_SPI_MISC_CS_HOLD   0x00000400U

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

#define ESP32_C3_RTC_BASE           0x60008000U
#define ESP32_C3_RTC_WDT_CONFIG0    (ESP32_C3_RTC_BASE + 0x090U)
#define ESP32_C3_RTC_WDT_FEED       (ESP32_C3_RTC_BASE + 0x0a4U)
#define ESP32_C3_RTC_WDT_WRITE_PROT (ESP32_C3_RTC_BASE + 0x0a8U)
#define ESP32_C3_RTC_SWD_CONFIG     (ESP32_C3_RTC_BASE + 0x0acU)
#define ESP32_C3_RTC_SWD_WRITE_PROT (ESP32_C3_RTC_BASE + 0x0b0U)

#define ESP32_C3_WDT_WRITE_PROT_KEY     0x50d83aa1U
#define ESP32_C3_RTC_SWD_WRITE_PROT_KEY 0x8f1d312aU
#define ESP32_C3_RTC_SWD_CONFIG_DISABLE 0x40000002U
#define ESP32_C3_RTC_SWD_CONFIG_FEED    0x60000002U

#define ESP32_C3_TIMG0_BASE           0x6001f000U
#define ESP32_C3_TIMG0_WDT_CONFIG0    (ESP32_C3_TIMG0_BASE + 0x048U)
#define ESP32_C3_TIMG0_WDT_FEED       (ESP32_C3_TIMG0_BASE + 0x060U)
#define ESP32_C3_TIMG0_WDT_WRITE_PROT (ESP32_C3_TIMG0_BASE + 0x064U)

#define ESP32_C3_TIMG1_BASE           0x60020000U
#define ESP32_C3_TIMG1_WDT_CONFIG0    (ESP32_C3_TIMG1_BASE + 0x048U)
#define ESP32_C3_TIMG1_WDT_FEED       (ESP32_C3_TIMG1_BASE + 0x060U)
#define ESP32_C3_TIMG1_WDT_WRITE_PROT (ESP32_C3_TIMG1_BASE + 0x064U)

#define ESP32_C3_EXTMEM_BASE                0x600c4000U
#define ESP32_C3_EXTMEM_ICACHE_SYNC_CTRL    (ESP32_C3_EXTMEM_BASE + 0x028U)
#define ESP32_C3_EXTMEM_ICACHE_SYNC_ADDR    (ESP32_C3_EXTMEM_BASE + 0x02cU)
#define ESP32_C3_EXTMEM_ICACHE_SYNC_SIZE    (ESP32_C3_EXTMEM_BASE + 0x030U)
#define ESP32_C3_EXTMEM_ICACHE_PRELOAD_CTRL (ESP32_C3_EXTMEM_BASE + 0x034U)
#define ESP32_C3_EXTMEM_ICACHE_PRELOAD_ADDR (ESP32_C3_EXTMEM_BASE + 0x038U)
#define ESP32_C3_EXTMEM_ICACHE_PRELOAD_SIZE (ESP32_C3_EXTMEM_BASE + 0x03cU)

#define ESP32_C3_EXTMEM_ICACHE_INVALIDATE   0x00000001U
#define ESP32_C3_EXTMEM_ICACHE_SYNC_DONE    0x00000002U
#define ESP32_C3_EXTMEM_ICACHE_PRELOAD      0x00000001U
#define ESP32_C3_EXTMEM_ICACHE_PRELOAD_DONE 0x00000002U

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
#define SPI_FLASH_CMD_WRITE_ENABLE \
	(ESP32_C3_SPI_FLASH_OPCODE_ONLY | ESP32_C3_SPI_FLASH_DUMMY_LEN(0) | ESP32_C3_SPI_FLASH_OPCODE(0x06U))
#define SPI_FLASH_CMD_PAGE_PROGRAM                                                                       \
	(ESP32_C3_SPI_FLASH_OPCODE_3B_ADDR | ESP32_C3_SPI_FLASH_DATA_OUT | ESP32_C3_SPI_FLASH_DUMMY_LEN(0) | \
		ESP32_C3_SPI_FLASH_OPCODE(0x02))
#define SPI_FLASH_CMD_SECTOR_ERASE (ESP32_C3_SPI_FLASH_OPCODE_3B_ADDR | ESP32_C3_SPI_FLASH_DUMMY_LEN(0))
#define SPI_FLASH_CMD_CHIP_ERASE \
	(ESP32_C3_SPI_FLASH_OPCODE_ONLY | ESP32_C3_SPI_FLASH_DUMMY_LEN(0) | ESP32_C3_SPI_FLASH_OPCODE(0x60U))
#define SPI_FLASH_CMD_READ_STATUS                                                                    \
	(ESP32_C3_SPI_FLASH_OPCODE_ONLY | ESP32_C3_SPI_FLASH_DATA_IN | ESP32_C3_SPI_FLASH_DUMMY_LEN(0) | \
		ESP32_C3_SPI_FLASH_OPCODE(0x05U))
#define SPI_FLASH_CMD_READ_JEDEC_ID                                                                  \
	(ESP32_C3_SPI_FLASH_OPCODE_ONLY | ESP32_C3_SPI_FLASH_DATA_IN | ESP32_C3_SPI_FLASH_DUMMY_LEN(0) | \
		ESP32_C3_SPI_FLASH_OPCODE(0x9fU))
#define SPI_FLASH_CMD_READ_SFDP                                                                         \
	(ESP32_C3_SPI_FLASH_OPCODE_3B_ADDR | ESP32_C3_SPI_FLASH_DATA_IN | ESP32_C3_SPI_FLASH_DUMMY_LEN(8) | \
		ESP32_C3_SPI_FLASH_OPCODE(0x5aU))

#define SPI_FLASH_STATUS_BUSY          0x01U
#define SPI_FLASH_STATUS_WRITE_ENABLED 0x02U

typedef struct esp32c3_priv {
	uint32_t wdt_config[4];
	target_addr_t last_invalidated_sector;
} esp32c3_priv_s;

typedef struct esp32c3_spi_flash {
	target_flash_s flash;
	uint32_t page_size;
	uint8_t sector_erase_opcode;
} esp32c3_spi_flash_s;

static void esp32c3_disable_wdts(target_s *target);
static void esp32c3_restore_wdts(target_s *target);
static void esp32c3_halt_request(target_s *target);
static void esp32c3_halt_resume(target_s *target, bool step);
static target_halt_reason_e esp32c3_halt_poll(target_s *target, target_addr_t *watch);

static void esp32c3_spi_read(target_s *target, uint32_t command, target_addr_t address, void *buffer, size_t length);
static void esp32c3_spi_write(
	target_s *target, uint32_t command, target_addr_t address, const void *buffer, size_t length);

static bool esp32c3_mass_erase(target_s *target);
static bool esp32c3_enter_flash_mode(target_s *target);
static bool esp32c3_exit_flash_mode(target_s *target);
static bool esp32c3_spi_flash_erase(target_flash_s *flash, target_addr_t addr, size_t length);
static bool esp32c3_spi_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t length);

static void esp32c3_spi_read_sfdp(
	target_s *const target, const uint16_t command, const uint32_t address, void *const buffer, const size_t length)
{
	esp32c3_spi_read(target, command, address, buffer, length);
}

static void esp32c3_add_flash(target_s *const target)
{
	esp32c3_spi_flash_s *const spi_flash = calloc(1, sizeof(*spi_flash));
	if (!spi_flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
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

	target_flash_s *const flash = &spi_flash->flash;
	flash->start = ESP32_C3_IBUS_FLASH_BASE;
	flash->length = MIN(spi_parameters.capacity, ESP32_C3_IBUS_FLASH_SIZE);
	flash->blocksize = spi_parameters.sector_size;
	flash->write = esp32c3_spi_flash_write;
	flash->erase = esp32c3_spi_flash_erase;
	flash->erased = 0xffU;
	target_add_flash(target, flash);

	spi_flash->page_size = spi_parameters.page_size;
	spi_flash->sector_erase_opcode = spi_parameters.sector_erase_opcode;
}

bool esp32c3_probe(target_s *const target)
{
	const riscv_hart_s *const hart = riscv_hart_struct(target);
	/* Seems that the best we can do is check the marchid and mimplid register values */
	if (hart->archid != ESP32_C3_ARCH_ID || hart->implid != ESP32_C3_IMPL_ID)
		return false;

	esp32c3_priv_s *const priv = calloc(1, sizeof(esp32c3_priv_s));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	target->target_storage = priv;
	target->driver = "ESP32-C3";
	esp32c3_disable_wdts(target);

	/* We have to provide our own halt/resume functions to take care of the WDTs as they cause Problems */
	target->halt_request = esp32c3_halt_request;
	target->halt_resume = esp32c3_halt_resume;
	target->halt_poll = esp32c3_halt_poll;
	/* Provide an implementation of the mass erase command */
	target->mass_erase = esp32c3_mass_erase;
	/* Special care must be taken during Flash programming */
	target->enter_flash_mode = esp32c3_enter_flash_mode;
	target->exit_flash_mode = esp32c3_exit_flash_mode;

	/* Establish the target RAM mappings */
	target_add_ram(target, ESP32_C3_IBUS_SRAM0_BASE, ESP32_C3_IBUS_SRAM0_SIZE);
	target_add_ram(target, ESP32_C3_IBUS_SRAM1_BASE, ESP32_C3_IBUS_SRAM1_SIZE);
	target_add_ram(target, ESP32_C3_DBUS_SRAM1_BASE, ESP32_C3_DBUS_SRAM1_SIZE);
	target_add_ram(target, ESP32_C3_RTC_SRAM_BASE, ESP32_C3_RTC_SRAM_SIZE);

	/* Establish the target Flash mappings */
	esp32c3_add_flash(target);
	return true;
}

static void esp32c3_disable_wdts(target_s *const target)
{
	esp32c3_priv_s *const priv = (esp32c3_priv_s *)target->target_storage;
	/* Disable Timer Group 0's WDT */
	target_mem_write32(target, ESP32_C3_TIMG0_WDT_WRITE_PROT, ESP32_C3_WDT_WRITE_PROT_KEY);
	priv->wdt_config[0] = target_mem_read32(target, ESP32_C3_TIMG0_WDT_CONFIG0);
	target_mem_write32(target, ESP32_C3_TIMG0_WDT_CONFIG0, 0U);
	/* Disable Timer Group 1's WDT */
	target_mem_write32(target, ESP32_C3_TIMG1_WDT_WRITE_PROT, ESP32_C3_WDT_WRITE_PROT_KEY);
	priv->wdt_config[1] = target_mem_read32(target, ESP32_C3_TIMG1_WDT_CONFIG0);
	target_mem_write32(target, ESP32_C3_TIMG1_WDT_CONFIG0, 0U);
	/* Disable the RTC WDT */
	target_mem_write32(target, ESP32_C3_RTC_WDT_WRITE_PROT, ESP32_C3_WDT_WRITE_PROT_KEY);
	priv->wdt_config[2] = target_mem_read32(target, ESP32_C3_RTC_WDT_CONFIG0);
	target_mem_write32(target, ESP32_C3_RTC_WDT_CONFIG0, 0U);
	/* Disable the "super" WDT */
	target_mem_write32(target, ESP32_C3_RTC_SWD_WRITE_PROT, ESP32_C3_RTC_SWD_WRITE_PROT_KEY);
	priv->wdt_config[3] = target_mem_read32(target, ESP32_C3_RTC_SWD_CONFIG);
	target_mem_write32(target, ESP32_C3_RTC_SWD_CONFIG, ESP32_C3_RTC_SWD_CONFIG_DISABLE);
}

static void esp32c3_restore_wdts(target_s *const target)
{
	esp32c3_priv_s *const priv = (esp32c3_priv_s *)target->target_storage;
	/* Restore Timger Group 0's WDT */
	target_mem_write32(target, ESP32_C3_TIMG0_WDT_CONFIG0, priv->wdt_config[0]);
	target_mem_write32(target, ESP32_C3_TIMG0_WDT_WRITE_PROT, 0U);
	/* Restore Timger Group 1's WDT */
	target_mem_write32(target, ESP32_C3_TIMG1_WDT_CONFIG0, priv->wdt_config[1]);
	target_mem_write32(target, ESP32_C3_TIMG1_WDT_WRITE_PROT, 0U);
	/* Restore the RTC WDT */
	target_mem_write32(target, ESP32_C3_RTC_WDT_CONFIG0, priv->wdt_config[2]);
	target_mem_write32(target, ESP32_C3_RTC_WDT_WRITE_PROT, 0U);
	/* Restore the "super" WDT */
	target_mem_write32(target, ESP32_C3_RTC_SWD_CONFIG, priv->wdt_config[2]);
	target_mem_write32(target, ESP32_C3_RTC_SWD_WRITE_PROT, 0U);
}

static void esp32c3_halt_request(target_s *const target)
{
	riscv_halt_request(target);
	esp32c3_disable_wdts(target);
}

static void esp32c3_halt_resume(target_s *const target, const bool step)
{
	if (!step)
		esp32c3_restore_wdts(target);
	riscv_halt_resume(target, step);
}

static target_halt_reason_e esp32c3_halt_poll(target_s *const target, target_addr_t *const watch)
{
	const target_halt_reason_e reason = riscv_halt_poll(target, watch);
	if (reason == TARGET_HALT_BREAKPOINT)
		esp32c3_disable_wdts(target);
	return reason;
}

static uint32_t esp32c3_spi_config(
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
		if (command & ESP32_C3_SPI_FLASH_DATA_OUT)
			enabled_stages |= ESP32_C3_SPI_USER0_DATA_OUT;
		else
			enabled_stages |= ESP32_C3_SPI_USER0_DATA_IN;
	}

	/* Now we've defined all the information needed for user0 and user1, send it */
	target_mem_write32(target, ESP32_C3_SPI1_USER1, user1_value);
	return enabled_stages;
}

static void esp32c3_spi_wait_complete(target_s *const target)
{
	/* Now trigger the configured transaction */
	target_mem_write32(target, ESP32_C3_SPI1_CMD, ESP32_C3_SPI_CMD_EXEC_XFER);
	/* And wait for the transaction to complete */
	while (target_mem_read32(target, ESP32_C3_SPI1_CMD) & ESP32_C3_SPI_CMD_EXEC_XFER)
		continue;
}

static void esp32c3_spi_read(
	target_s *const target, const uint32_t command, const target_addr_t address, void *const buffer, size_t length)
{
	/* Start by setting up the common components of the transaction */
	const uint32_t enabled_stages = esp32c3_spi_config(target, command, address, length);
	uint8_t *const data = (uint8_t *)buffer;
	const uint32_t misc_reg = target_mem_read32(target, ESP32_C3_SPI1_MISC) & ~ESP32_C3_SPI_MISC_CS_HOLD;
	/*
	 * The transfer has to proceed in no more than 64 bytes at a time because that's
	 * how many data registers are available in the SPI peripheral
	 */
	for (size_t offset = 0U; offset < length; offset += 64U) {
		const uint32_t amount = MIN(length - offset, 64U);
		/* Tell the controller how many bytes we want received in this transaction */
		target_mem_write32(target, ESP32_C3_SPI1_DATA_IN_LEN, ESP32_C3_SPI_DATA_BIT_LEN(amount));
		/* Configure which transaction stages to use */
		if (offset)
			target_mem_write32(target, ESP32_C3_SPI1_USER0, ESP32_C3_SPI_USER0_DATA_IN);
		else
			target_mem_write32(target, ESP32_C3_SPI1_USER0, enabled_stages);

		/* On the final transfer, clear the chip select hold bit, otherwise set it */
		if (length - offset == amount)
			target_mem_write32(target, ESP32_C3_SPI1_MISC, misc_reg);
		else
			target_mem_write32(target, ESP32_C3_SPI1_MISC, misc_reg | ESP32_C3_SPI_MISC_CS_HOLD);

		/* Run the transaction */
		esp32c3_spi_wait_complete(target);

		/* Extract and unpack the received data */
		target_mem_read(target, data + offset, ESP32_C3_SPI1_DATA, amount);
	}
}

static void esp32c3_spi_write(target_s *const target, const uint32_t command, const target_addr_t address,
	const void *const buffer, const size_t length)
{
	/* Start by setting up the common components of the transaction */
	const uint32_t enabled_stages = esp32c3_spi_config(target, command, address, length);
	const uint8_t *const data = (const uint8_t *)buffer;
	const uint32_t misc_reg = target_mem_read32(target, ESP32_C3_SPI1_MISC) & ~ESP32_C3_SPI_MISC_CS_HOLD;

	/*
	 * The transfer has to proceed in no more than 64 bytes at a time because that's
	 * how many data registers are available in the SPI peripheral
	 */
	for (size_t offset = 0U; offset < length; offset += 64U) {
		const uint32_t amount = MIN(length - offset, 64U);
		/* Tell the controller how many bytes we want sent in this transaction */
		target_mem_write32(target, ESP32_C3_SPI1_DATA_OUT_LEN, ESP32_C3_SPI_DATA_BIT_LEN(amount));
		/* Configure which transaction stages to use */
		if (offset)
			target_mem_write32(target, ESP32_C3_SPI1_USER0, ESP32_C3_SPI_USER0_DATA_OUT);
		else
			target_mem_write32(target, ESP32_C3_SPI1_USER0, enabled_stages);

		/* On the final transfer, clear the chip select hold bit, otherwise set it */
		if (length - offset == amount)
			target_mem_write32(target, ESP32_C3_SPI1_MISC, misc_reg);
		else
			target_mem_write32(target, ESP32_C3_SPI1_MISC, misc_reg | ESP32_C3_SPI_MISC_CS_HOLD);

		/* Pack and stage the data to transmit */
		target_mem_write(target, ESP32_C3_SPI1_DATA, data + offset, amount);

		/* Run the transaction */
		esp32c3_spi_wait_complete(target);
	}
}

static inline uint8_t esp32c3_spi_read_status(target_s *const target)
{
	uint8_t status = 0;
	esp32c3_spi_read(target, SPI_FLASH_CMD_READ_STATUS, 0, &status, sizeof(status));
	return status;
}

static inline void esp32c3_spi_run_command(target_s *const target, const uint32_t command, const target_addr_t address)
{
	/* Start by setting up the common components of the transaction */
	const uint32_t enabled_stages = esp32c3_spi_config(target, command, address, 0U);
	/* Write the stages to execute and run the transaction */
	target_mem_write32(target, ESP32_C3_SPI1_USER0, enabled_stages);
	esp32c3_spi_wait_complete(target);
}

static bool esp32c3_mass_erase(target_s *const target)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500U);
	esp32c3_spi_run_command(target, SPI_FLASH_CMD_WRITE_ENABLE, 0U);
	if (!(esp32c3_spi_read_status(target) & SPI_FLASH_STATUS_WRITE_ENABLED))
		return false;

	esp32c3_spi_run_command(target, SPI_FLASH_CMD_CHIP_ERASE, 0U);
	while (esp32c3_spi_read_status(target) & SPI_FLASH_STATUS_BUSY)
		target_print_progress(&timeout);

	return true;
}

static bool esp32c3_enter_flash_mode(target_s *const target)
{
	esp32c3_disable_wdts(target);
	return true;
}

static bool esp32c3_exit_flash_mode(target_s *const target)
{
	esp32c3_priv_s *const priv = (esp32c3_priv_s *)target->target_storage;
	/* Calculate the length of the region to invalidate and reload */
	const uint32_t region_length = priv->last_invalidated_sector - ESP32_C3_IBUS_FLASH_BASE;
	/* Invalidate the i-cache for the required length */
	target_mem_write32(target, ESP32_C3_EXTMEM_ICACHE_SYNC_ADDR, ESP32_C3_IBUS_FLASH_BASE);
	target_mem_write32(target, ESP32_C3_EXTMEM_ICACHE_SYNC_SIZE, region_length);
	target_mem_write32(target, ESP32_C3_EXTMEM_ICACHE_SYNC_CTRL, ESP32_C3_EXTMEM_ICACHE_INVALIDATE);
	/* Wait for invalidation to complete */
	while (!(target_mem_read32(target, ESP32_C3_EXTMEM_ICACHE_SYNC_CTRL) & ESP32_C3_EXTMEM_ICACHE_SYNC_DONE))
		continue;
	/* Now preload the cache with the new data */
	target_mem_write32(target, ESP32_C3_EXTMEM_ICACHE_PRELOAD_ADDR, ESP32_C3_IBUS_FLASH_BASE);
	target_mem_write32(target, ESP32_C3_EXTMEM_ICACHE_PRELOAD_SIZE, region_length);
	target_mem_write32(target, ESP32_C3_EXTMEM_ICACHE_PRELOAD_CTRL, ESP32_C3_EXTMEM_ICACHE_PRELOAD);
	/* Wait for preload to complete */
	while (!(target_mem_read32(target, ESP32_C3_EXTMEM_ICACHE_PRELOAD_CTRL) & ESP32_C3_EXTMEM_ICACHE_PRELOAD_DONE))
		continue;
	target_reset(target);
	return true;
}

static bool esp32c3_spi_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t length)
{
	target_s *const target = flash->t;
	const esp32c3_spi_flash_s *const spi_flash = (esp32c3_spi_flash_s *)flash;
	const target_addr_t begin = addr - flash->start;
	for (size_t offset = 0; offset < length; offset += flash->blocksize) {
		esp32c3_spi_run_command(target, SPI_FLASH_CMD_WRITE_ENABLE, 0U);
		if (!(esp32c3_spi_read_status(target) & SPI_FLASH_STATUS_WRITE_ENABLED))
			return false;

		esp32c3_spi_run_command(target,
			SPI_FLASH_CMD_SECTOR_ERASE | ESP32_C3_SPI_FLASH_OPCODE(spi_flash->sector_erase_opcode), begin + offset);
		while (esp32c3_spi_read_status(target) & SPI_FLASH_STATUS_BUSY)
			continue;
	}
	/* Update the address of the last invalidated sector so we can correctly invalidate the i-cache and reload it */
	esp32c3_priv_s *const priv = (esp32c3_priv_s *)target->target_storage;
	priv->last_invalidated_sector = addr + length;
	return true;
}

static bool esp32c3_spi_flash_write(
	target_flash_s *const flash, const target_addr_t dest, const void *const src, const size_t length)
{
	target_s *const target = flash->t;
	// const esp32c3_spi_flash_s *const spi_flash = (esp32c3_spi_flash_s *)flash;
	const target_addr_t begin = dest - flash->start;
	const char *const buffer = (const char *)src;
	for (size_t offset = 0; offset < length; offset += 64U) {
		esp32c3_spi_run_command(target, SPI_FLASH_CMD_WRITE_ENABLE, 0U);
		if (!(esp32c3_spi_read_status(target) & SPI_FLASH_STATUS_WRITE_ENABLED))
			return false;

		const size_t amount = MIN(length - offset, 64U);
		esp32c3_spi_write(target, SPI_FLASH_CMD_PAGE_PROGRAM, begin + offset, buffer + offset, amount);
		while (esp32c3_spi_read_status(target) & SPI_FLASH_STATUS_BUSY)
			continue;
	}
	return true;
}
