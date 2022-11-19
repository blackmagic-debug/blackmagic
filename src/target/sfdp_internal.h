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

#ifndef TARGET_SFDP_INTERNAL_H
#define TARGET_SFDP_INTERNAL_H

#include "sfdp.h"

#define SFDP_HEADER_ADDRESS       0U
#define SFDP_TABLE_HEADER_ADDRESS sizeof(sfdp_header_s)

#define SFDP_MAGIC                     "SFDP"
#define SFDP_BASIC_SPI_PARAMETER_TABLE 0xff00U

#define SFDP_ACCESS_PROTOCOL_LEGACY_JESD216B 0xffU

#define SFDP_JEDEC_PARAMETER_ID(header) (((header).jedec_parameter_id_high << 8U) | (header).jedec_parameter_id_low)
#define SFDP_TABLE_ADDRESS(header) \
	(((header).table_address[2] << 16U) | ((header).table_address[1] << 8U) | (header).table_address[0])

#define SFDP_DENSITY_IS_EXPONENTIAL(density) ((density)[3] & 0x80U)
#define SFDP_DENSITY_VALUE(density) \
	((((density)[3] & 0x7fU) << 24U) | ((density)[2] << 16U) | ((density)[1] << 8U) | (density)[0])

#define SFDP_ERASE_TYPES            4U
#define SFDP_ERASE_SIZE(erase_type) (1U << ((erase_type)->erase_size_exponent))
#define SFDP_PAGE_SIZE(parameter_table) \
	(1U << ((parameter_table).programming_and_chip_erase_timing.programming_timing_ratio_and_page_size >> 4U))

typedef struct sfdp_header {
	char magic[4];
	uint8_t version_minor;
	uint8_t version_major;
	uint8_t parameter_headers_count;
	uint8_t access_protocol;
} sfdp_header_s;

typedef struct sfdp_parameter_table_header {
	uint8_t jedec_parameter_id_low;
	uint8_t version_minor;
	uint8_t version_major;
	uint8_t table_length_in_u32s;
	uint8_t table_address[3];
	uint8_t jedec_parameter_id_high;
} sfdp_parameter_table_header_s;

typedef struct timings_and_opcode {
	uint8_t timings;
	uint8_t opcode;
} timings_and_opcode_s;

typedef struct erase_parameters {
	uint8_t erase_size_exponent;
	uint8_t opcode;
} erase_parameters_s;

typedef struct programming_and_chip_erase_timing {
	uint8_t programming_timing_ratio_and_page_size;
	uint8_t erase_timings[3];
} programming_and_chip_erase_timing_s;

typedef struct sfdp_basic_parameter_table {
	uint8_t value1;
	uint8_t sector_erase_opcode;
	uint8_t value2;
	uint8_t reserved1;
	uint8_t memory_density[4];
	timings_and_opcode_s fast_quad_io;
	timings_and_opcode_s fast_quad_output;
	timings_and_opcode_s fast_dual_output;
	timings_and_opcode_s fast_dual_io;
	uint8_t fast_support_flags;
	uint8_t reserved2[5];
	timings_and_opcode_s fast_dual_dpi;
	uint8_t reserved3[2];
	timings_and_opcode_s fast_quad_qpi;
	erase_parameters_s erase_types[SFDP_ERASE_TYPES];
	uint32_t erase_timing;
	programming_and_chip_erase_timing_s programming_and_chip_erase_timing;
	uint8_t operational_prohibitions;
	uint8_t suspend_latency_specs[3];
	uint8_t program_resume_opcode;
	uint8_t program_suspend_opcode;
	uint8_t resume_opcode;
	uint8_t suspend_opcode;
	uint8_t status_register_polling_flags;
	uint8_t deep_powerdown[3];
	uint8_t dual_and_quad_mode[3];
	uint8_t reserved4;
	uint32_t status_and_addressing_mode;
} sfdp_basic_parameter_table_s;

#endif /* TARGET_SFDP_INTERNAL_H */
