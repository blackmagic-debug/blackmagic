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

#include "spi.h"
#include "sfdp.h"

static bool bmp_spi_flash_erase(target_flash_s *flash, target_addr_t addr, size_t length);
static bool bmp_spi_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t length);

#if PC_HOSTED == 0
static void bmp_spi_setup_xfer(
	const spi_bus_e bus, const uint8_t device, const uint16_t command, const target_addr_t address)
{
	platform_spi_chip_select(device | 0x80U);

	/* Set up the instruction */
	const uint8_t opcode = command & SPI_FLASH_OPCODE_MASK;
	platform_spi_xfer(bus, opcode);

	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR) {
		/* For each byte sent here, we have to manually clean up from the controller with a read */
		platform_spi_xfer(bus, (address >> 16U) & 0xffU);
		platform_spi_xfer(bus, (address >> 8U) & 0xffU);
		platform_spi_xfer(bus, address & 0xffU);
	}

	const size_t dummy_length = (command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT;
	for (size_t i = 0; i < dummy_length; ++i)
		/* For each byte sent here, we have to manually clean up from the controller with a read */
		platform_spi_xfer(bus, 0);
}

void bmp_spi_read(const spi_bus_e bus, const uint8_t device, const uint16_t command, const target_addr_t address,
	void *const buffer, const size_t length)
{
	/* Setup the transaction */
	bmp_spi_setup_xfer(bus, device, command, address);
	/* Now read back the data that elicited */
	uint8_t *const data = (uint8_t *const)buffer;
	for (size_t i = 0; i < length; ++i)
		/* Do a write to read */
		data[i] = platform_spi_xfer(bus, 0);
	/* Deselect the Flash */
	platform_spi_chip_select(device);
}

void bmp_spi_write(const spi_bus_e bus, const uint8_t device, const uint16_t command, const target_addr_t address,
	const void *const buffer, const size_t length)
{
	/* Setup the transaction */
	bmp_spi_setup_xfer(bus, device, command, address);
	/* Now write out back the data requested */
	uint8_t *const data = (uint8_t *const)buffer;
	for (size_t i = 0; i < length; ++i)
		/* Do a write to read */
		platform_spi_xfer(bus, data[i]);
	/* Deselect the Flash */
	platform_spi_chip_select(device);
}

void bmp_spi_run_command(const spi_bus_e bus, const uint8_t device, const uint16_t command, const target_addr_t address)
{
	/* Setup the transaction */
	bmp_spi_setup_xfer(bus, device, command, address);
	/* Deselect the Flash */
	platform_spi_chip_select(device);
}
#endif

static inline uint8_t bmp_spi_read_status(target_s *const target, const spi_flash_s *const flash)
{
	uint8_t status = 0;
	/* Read the main status register of the Flash */
	flash->read(target, SPI_FLASH_CMD_READ_STATUS, 0U, &status, sizeof(status));
	return status;
}

spi_flash_s *bmp_spi_add_flash(target_s *const target, const target_addr_t begin, const size_t length,
	const spi_read_func spi_read, const spi_write_func spi_write, const spi_run_command_func spi_run_command)
{
	spi_flash_s *spi_flash = calloc(1, sizeof(*spi_flash));
	if (!spi_flash) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return NULL;
	}

	spi_parameters_s spi_parameters;
	if (!sfdp_read_parameters(target, &spi_parameters, spi_read)) {
		/* SFDP readout failed, so make some assumptions and hope for the best. */
		spi_parameters.page_size = 256U;
		spi_parameters.sector_size = 4096U;
		spi_parameters.capacity = length;
		spi_parameters.sector_erase_opcode = SPI_FLASH_OPCODE_SECTOR_ERASE;
		DEBUG_WARN("SFDP read failed. Using best guess.\n");
	}
	DEBUG_INFO("Flash size: %" PRIu32 "MiB\n", (uint32_t)spi_parameters.capacity / (1024U * 1024U));

	target_flash_s *const flash = &spi_flash->flash;
	flash->start = begin;
	flash->length = spi_parameters.capacity;
	flash->blocksize = spi_parameters.sector_size;
	flash->write = bmp_spi_flash_write;
	flash->erase = bmp_spi_flash_erase;
	flash->erased = 0xffU;
	target_add_flash(target, flash);

	spi_flash->page_size = spi_parameters.page_size;
	spi_flash->sector_erase_opcode = spi_parameters.sector_erase_opcode;
	spi_flash->read = spi_read;
	spi_flash->write = spi_write;
	spi_flash->run_command = spi_run_command;
	return spi_flash;
}

/* Note: These routines assume that the first Flash registered on the target is a SPI Flash device */
bool bmp_spi_mass_erase(target_s *const target)
{
	/* Extract the Flash structure and set up timeouts */
	const spi_flash_s *const flash = (spi_flash_s *)target->flash;
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	DEBUG_TARGET("Running %s\n", __func__);
	/* Go into Flash mode and tell the Flash to enable writing */
	target->enter_flash_mode(target);
	flash->run_command(target, SPI_FLASH_CMD_WRITE_ENABLE, 0U);
	if (!(bmp_spi_read_status(target, flash) & SPI_FLASH_STATUS_WRITE_ENABLED)) {
		target->exit_flash_mode(target);
		return false;
	}

	/* Execute a full chip erase and wait for the operatoin to complete */
	flash->run_command(target, SPI_FLASH_CMD_CHIP_ERASE, 0U);
	while (bmp_spi_read_status(target, flash) & SPI_FLASH_STATUS_BUSY)
		target_print_progress(&timeout);

	/* Finally, leave Flash mode to conclude business */
	return target->exit_flash_mode(target);
}

static bool bmp_spi_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t length)
{
	(void)length;
	target_s *const target = flash->t;
	const spi_flash_s *const spi_flash = (spi_flash_s *)flash;
	spi_flash->run_command(target, SPI_FLASH_CMD_WRITE_ENABLE, 0U);
	if (!(bmp_spi_read_status(target, spi_flash) & SPI_FLASH_STATUS_WRITE_ENABLED))
		return false;

	spi_flash->run_command(
		target, SPI_FLASH_CMD_SECTOR_ERASE | SPI_FLASH_OPCODE(spi_flash->sector_erase_opcode), addr - flash->start);
	while (bmp_spi_read_status(target, spi_flash) & SPI_FLASH_STATUS_BUSY)
		continue;
	return true;
}

static bool bmp_spi_flash_write(
	target_flash_s *const flash, const target_addr_t dest, const void *const src, const size_t length)
{
	target_s *const target = flash->t;
	const spi_flash_s *const spi_flash = (spi_flash_s *)flash;
	const target_addr_t begin = dest - flash->start;
	const char *const buffer = (const char *)src;
	for (size_t offset = 0; offset < length; offset += spi_flash->page_size) {
		spi_flash->run_command(target, SPI_FLASH_CMD_WRITE_ENABLE, 0U);
		if (!(bmp_spi_read_status(target, spi_flash) & SPI_FLASH_STATUS_WRITE_ENABLED))
			return false;

		const size_t amount = MIN(length - offset, spi_flash->page_size);
		spi_flash->write(target, SPI_FLASH_CMD_PAGE_PROGRAM, begin + offset, buffer + offset, amount);
		while (bmp_spi_read_status(target, spi_flash) & SPI_FLASH_STATUS_BUSY)
			continue;
	}
	return true;
}
