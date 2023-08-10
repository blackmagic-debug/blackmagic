/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
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

#include <string.h>
#include "general.h"
#include "spi.h"
#include "sfdp_internal.h"

#ifdef MIN
#undef MIN
#endif
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static inline void sfdp_debug_print(const uint32_t address, const void *const buffer, const uint32_t length)
{
#if ENABLE_DEBUG
	DEBUG_INFO("%" PRIu32 " byte SFDP read at 0x%" PRIx32 ":\n", length, address);
	const uint8_t *const data = buffer;
	for (size_t i = 0; i < length; i += 8U) {
		DEBUG_INFO("\t%02x %02x %02x %02x %02x %02x %02x %02x\n", data[i], data[i + 1U], data[i + 2U], data[i + 3U],
			data[i + 4U], data[i + 5U], data[i + 6U], data[i + 7U]);
	}
#else
	(void)address;
	(void)buffer;
	(void)length;
#endif
}

static inline size_t sfdp_memory_density_to_capacity_bits(const uint8_t *const density)
{
	if (SFDP_DENSITY_IS_EXPONENTIAL(density))
		return 1U << SFDP_DENSITY_VALUE(density);
	else /* NOLINT(readability-else-after-return) */
		return SFDP_DENSITY_VALUE(density) + 1U;
}

static spi_parameters_s sfdp_read_basic_parameter_table(target_s *const target,
	const sfdp_parameter_table_header_s *const header, const uint32_t address, const size_t length,
	const spi_read_func spi_read)
{
	sfdp_basic_parameter_table_s parameter_table;
	const size_t table_length = MIN(sizeof(sfdp_basic_parameter_table_s), length);
	spi_read(target, SPI_FLASH_CMD_READ_SFDP, address, &parameter_table, table_length);
	sfdp_debug_print(address, &parameter_table, table_length);

	spi_parameters_s result = {0};
	result.capacity = sfdp_memory_density_to_capacity_bits(parameter_table.memory_density) >> 3U;
	for (size_t i = 0; i < SFDP_ERASE_TYPES; ++i) {
		erase_parameters_s *erase_type = &parameter_table.erase_types[i];
		if (erase_type->opcode == parameter_table.sector_erase_opcode) {
			result.sector_erase_opcode = erase_type->opcode;
			result.sector_size = SFDP_ERASE_SIZE(erase_type);
			break;
		}
	}
	// The timing and page size DWORD was added in JESD216A. It is marked as
	// version 1.5.
	if (header->version_major > 1 || (header->version_major == 1 && header->version_minor >= 5))
		result.page_size = SFDP_PAGE_SIZE(parameter_table);
	else
		result.page_size = 256;

	return result;
}

bool sfdp_read_parameters(target_s *const target, spi_parameters_s *params, const spi_read_func spi_read)
{
	sfdp_header_s header;
	spi_read(target, SPI_FLASH_CMD_READ_SFDP, SFDP_HEADER_ADDRESS, &header, sizeof(header));
	sfdp_debug_print(SFDP_HEADER_ADDRESS, &header, sizeof(header));
	if (memcmp(header.magic, SFDP_MAGIC, 4) != 0)
		return false;

	for (size_t i = 0; i <= header.parameter_headers_count; ++i) {
		sfdp_parameter_table_header_s table_header;
		spi_read(target, SPI_FLASH_CMD_READ_SFDP, SFDP_TABLE_HEADER_ADDRESS + (sizeof(table_header) * i), &table_header,
			sizeof(table_header));
		sfdp_debug_print(SFDP_TABLE_HEADER_ADDRESS + (sizeof(table_header) * i), &table_header, sizeof(table_header));
		const uint16_t jedec_parameter_id = SFDP_JEDEC_PARAMETER_ID(table_header);
		if (jedec_parameter_id == SFDP_BASIC_SPI_PARAMETER_TABLE) {
			const uint32_t table_address = SFDP_TABLE_ADDRESS(table_header);
			const uint16_t table_length = table_header.table_length_in_u32s * 4U;
			*params = sfdp_read_basic_parameter_table(target, &table_header, table_address, table_length, spi_read);
			return true;
		}
	}
	return false;
}
