/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2021-2025 1BitSquared <info@1bitsquared.com>
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
#include "spi.h"
#include "sfdp.h"

static void onboard_spi_setup_xfer(const uint16_t command, const target_addr32_t address)
{
	platform_spi_chip_select(SPI_DEVICE_INT_FLASH | 0x80U);

	/* Set up the instruction */
	const uint8_t opcode = command & SPI_FLASH_OPCODE_MASK;
	platform_spi_xfer(SPI_BUS_INTERNAL, opcode);

	if ((command & SPI_FLASH_OPCODE_MODE_MASK) == SPI_FLASH_OPCODE_3B_ADDR) {
		/* For each byte sent here, we have to manually clean up from the controller with a read */
		platform_spi_xfer(SPI_BUS_INTERNAL, (address >> 16U) & 0xffU);
		platform_spi_xfer(SPI_BUS_INTERNAL, (address >> 8U) & 0xffU);
		platform_spi_xfer(SPI_BUS_INTERNAL, address & 0xffU);
	}

	const size_t dummy_length = (command & SPI_FLASH_DUMMY_MASK) >> SPI_FLASH_DUMMY_SHIFT;
	for (size_t i = 0; i < dummy_length; ++i)
		/* For each byte sent here, we have to manually clean up from the controller with a read */
		platform_spi_xfer(SPI_BUS_INTERNAL, 0);
}

void onboard_spi_read(target_s *const target, const uint16_t command, const target_addr32_t address, void *const buffer,
	const size_t length)
{
	(void)target;
	/* Setup the transaction */
	onboard_spi_setup_xfer(command, address);
	/* Now read back the data that elicited */
	uint8_t *const data = (uint8_t *const)buffer;
	for (size_t i = 0; i < length; ++i)
		/* Do a write to read */
		data[i] = platform_spi_xfer(SPI_BUS_INTERNAL, 0);
	/* Deselect the Flash */
	platform_spi_chip_select(SPI_DEVICE_INT_FLASH);
}

void onboard_spi_write(target_s *const target, const uint16_t command, const target_addr32_t address,
	const void *const buffer, const size_t length)
{
	(void)target;
	/* Setup the transaction */
	onboard_spi_setup_xfer(command, address);
	/* Now write out back the data requested */
	const uint8_t *const data = (const uint8_t *)buffer;
	for (size_t i = 0; i < length; ++i)
		/* Do a write to read */
		platform_spi_xfer(SPI_BUS_INTERNAL, data[i]);
	/* Deselect the Flash */
	platform_spi_chip_select(SPI_DEVICE_INT_FLASH);
}

void onboard_spi_run_command(target_s *const target, const uint16_t command, const target_addr32_t address)
{
	(void)target;
	/* Setup the transaction */
	onboard_spi_setup_xfer(command, address);
	/* Deselect the Flash */
	platform_spi_chip_select(SPI_DEVICE_INT_FLASH);
}

static bool onboard_flash_add(target_s *const target)
{
	/* Read out the chip's ID code */
	spi_flash_id_s flash_id;
	onboard_spi_read(target, SPI_FLASH_CMD_READ_JEDEC_ID, 0, &flash_id, sizeof(flash_id));

	/* If it doesn't match up to being the expected device (a Winbond Flash), bail */
	if (flash_id.manufacturer != 0xefU) {
		DEBUG_ERROR(
			"%s: Expecting Winbond SPI Flash device, manufacturer ID is %02x\n", __func__, flash_id.manufacturer);
		return false;
	}

	/* Otherwise add it to the providied target */
	bmp_spi_add_flash(
		target, 0U, 1U << flash_id.capacity, onboard_spi_read, onboard_spi_write, onboard_spi_run_command);
	return true;
}

bool onboard_flash_scan(void)
{
	/* Start by trying to create a new target to use for the internal Flash */
	target_list_free();
	target_s *target = target_new();
	if (!target)
		return false;

	/* That succeeded, so initialise the SPI bus and check the chip that's supposed to be there.. is */
	platform_spi_init(SPI_BUS_INTERNAL);
	if (!onboard_flash_add(target)) {
		/* Chip wasn't what was expected, we've told the user, so launder the target list and bail */
		target_list_free();
		return false;
	}

	/* Mark the target as being for the onboard Flash */
	target->driver = "Onboard SPI Flash";
	return true;
}
