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

#ifndef SFDP_INTERNAL_H
#define SFDP_INTERNAL_H

#include "sfdp.h"

#define SFDP_HEADER_ADDRESS       0U
#define SFDP_TABLE_HEADER_ADDRESS sizeof(sfdp_header_s)

#define SFDP_MAGIC                     "SFDP"
#define SFDP_BASIC_SPI_PARAMETER_TABLE 0xFF00U

#define SFDP_ACCESS_PROTOCOL_LEGACY_JESD216B 0xFFU

#define SFDP_JEDEC_PARAMETER_ID(header) (((header).jedec_parameter_id_high << 8U) | (header).jedec_parameter_id_low)
#define SFDP_TABLE_ADDRESS(header) \
	(((header).table_address[2] << 16U) | ((header).table_address[1] << 8U) | (header).table_address[0])

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

#endif /*SFDP_INTERNAL_H*/
