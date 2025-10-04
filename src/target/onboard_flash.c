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
#include "gdb_reg.h"
#include "spi.h"
#include "sfdp.h"

typedef enum flash_error {
	flash_ok,
	flash_bad_address,
	flash_bad_length
} flash_error_e;

typedef struct onboard_flash {
	flash_error_e error_state;
} onboard_flash_s;

static target_halt_reason_e onboard_flash_halt_poll(target_s *target, target_addr64_t *watch);
static bool onboard_flash_check_error(target_s *target);
static void onboard_flash_read(target_s *target, void *dest, target_addr64_t src, size_t len);
static const char *onboard_flash_target_description(target_s *target);

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
	onboard_flash_s *const priv = target->priv;
	/* Setup the transaction */
	onboard_spi_setup_xfer(command, address);
	/* Now read back the data that elicited */
	uint8_t *const data = (uint8_t *const)buffer;
	for (size_t i = 0; i < length; ++i)
		/* Do a write to read */
		data[i] = platform_spi_xfer(SPI_BUS_INTERNAL, 0);
	/* Deselect the Flash */
	platform_spi_chip_select(SPI_DEVICE_INT_FLASH);
	priv->error_state = flash_ok;
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
	DEBUG_INFO("Attempting on-board Flash ident\n");
	spi_flash_id_s flash_id;
	onboard_spi_read(target, SPI_FLASH_CMD_READ_JEDEC_ID, 0, &flash_id, sizeof(flash_id));

	/* If it doesn't match up to being the expected device (a Winbond Flash), bail */
	if (flash_id.manufacturer != 0xefU) {
		DEBUG_ERROR(
			"%s: Expecting Winbond SPI Flash device, manufacturer ID is %02x\n", __func__, flash_id.manufacturer);
		return false;
	}

	DEBUG_INFO(
		"Found Flash chip w/ ID: 0x%02x 0x%02x 0x%02x\n", flash_id.manufacturer, flash_id.type, flash_id.capacity);
	target->core = "Windbond";
	/* Otherwise add it to the providied target */
	bmp_spi_add_flash(
		target, 0U, 1U << flash_id.capacity, onboard_spi_read, onboard_spi_write, onboard_spi_run_command);
	return true;
}

bool onboard_flash_scan(void)
{
	/* Clear out any stray/previous targets */
	target_list_free();

	/* Try to allocate storage for our private state */
	onboard_flash_s *const priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return false;
	}

	/* Try to create a new target to use for the internal Flash */
	target_s *target = target_new();
	if (!target)
		return false;

	/* Start setting up the target structure with core information */
	target->priv = priv;
	target->priv_free = free;
	priv->error_state = flash_ok;

	/* That succeeded, so initialise the SPI bus and check the chip that's supposed to be there.. is */
	platform_spi_init(SPI_BUS_INTERNAL);
	if (!onboard_flash_add(target)) {
		/* Chip wasn't what was expected, we've told the user, so launder the target list and bail */
		target_list_free();
		return false;
	}

	/* Mark the target as being for the onboard Flash */
	target->driver = "Onboard SPI Flash";
	/* Set up memory access state for the Flash */
	target->check_error = onboard_flash_check_error;
	target->mem_read = onboard_flash_read;
	/* Set up GDB support members */
	target->halt_poll = onboard_flash_halt_poll;
	target->regs_size = 0U;
	target->regs_description = onboard_flash_target_description;
	return true;
}

static target_halt_reason_e onboard_flash_halt_poll(target_s *const target, target_addr64_t *const watch)
{
	(void)target;
	(void)watch;
	return TARGET_HALT_REQUEST;
}

static bool onboard_flash_check_error(target_s *const target)
{
	onboard_flash_s *const priv = target->priv;
	const flash_error_e error_state = priv->error_state;
	priv->error_state = flash_ok;
	return error_state != flash_ok;
}

static void onboard_flash_read(target_s *const target, void *const dest, const target_addr64_t src, const size_t len)
{
	onboard_flash_s *const priv = target->priv;
	target_flash_s *flash = target->flash;

	if (src < flash->start || src >= flash->start + flash->length)
		priv->error_state = flash_bad_address;
	else if (len >= flash->length - (src - flash->start))
		priv->error_state = flash_bad_length;
	else {
		onboard_spi_read(target, SPI_FLASH_CMD_PAGE_READ, src - flash->start, dest, len);

#if ENABLE_DEBUG
		DEBUG_PROTO("%s: @ %08" PRIx32 " len %zu:", __func__, (uint32_t)src, len);
#ifndef DEBUG_PROTO_IS_NOOP
		const uint8_t *const data = (const uint8_t *)dest;
#endif
		for (size_t offset = 0; offset < len; ++offset) {
			if (offset == 16U)
				break;
			DEBUG_PROTO(" %02x", data[offset]);
		}
		if (len > 16U)
			DEBUG_PROTO(" ...");
		DEBUG_PROTO("\n");
#endif
	}
}

/*
 * This function creates the dummy target description XML string for the on-board Flash.
 * This is done this way to decrease string duplication and thus code size, making it
 * unfortunately much less readable than the string literal it is equivalent to.
 *
 * This string it creates is the XML-equivalent to the following:
 *  <?xml version=\"1.0\"?>
 *  <!DOCTYPE target SYSTEM \"gdb-target.dtd\">
 *  <target>
 *    <architecture></architecture>
 *  </target>
 */
static size_t onboard_flash_build_target_description(char *const buffer, const size_t max_length)
{
	size_t print_size = max_length;
	/* Start with the "preamble" chunks, which are mostly common across targets save for 2 words. */
	int offset = snprintf(
		buffer, print_size, "%s target %s%s", gdb_xml_preamble_first, gdb_xml_preamble_second, gdb_xml_preamble_third);
	if (max_length != 0)
		print_size = max_length - (size_t)offset;

	offset += (size_t)snprintf(buffer + offset, print_size, "</target>");
	/* offset is now the total length of the string created, discard the sign and return it. */
	return offset;
}

static const char *onboard_flash_target_description(target_s *const target)
{
	(void)target;
	const size_t description_length = onboard_flash_build_target_description(NULL, 0) + 1U;
	char *const description = malloc(description_length);
	if (description)
		(void)onboard_flash_build_target_description(description, description_length);
	return description;
}
