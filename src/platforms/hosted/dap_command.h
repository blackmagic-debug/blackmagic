/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
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

#ifndef PLATFORMS_HOSTED_DAP_COMMAND_H
#define PLATFORMS_HOSTED_DAP_COMMAND_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "adiv5.h"

typedef enum dap_command {
	DAP_INFO = 0x00U,
	DAP_HOST_STATUS = 0x01U,
	DAP_CONNECT = 0x02U,
	DAP_DISCONNECT = 0x03U,
	DAP_TRANSFER_CONFIGURE = 0x04U,
	DAP_TRANSFER = 0x05U,
	DAP_TRANSFER_BLOCK = 0x06U,
	DAP_SWJ_PINS = 0x10U,
	DAP_SWJ_CLOCK = 0x11U,
	DAP_SWJ_SEQUENCE = 0x12U,
	DAP_SWD_CONFIGURE = 0x13U,
	DAP_JTAG_SEQUENCE = 0x14U,
	DAP_JTAG_CONFIGURE = 0x15U,
	DAP_SWD_SEQUENCE = 0x1dU,
} dap_command_e;

typedef enum dap_response_status {
	DAP_RESPONSE_OK = 0x00U,
	DAP_RESPONSE_ERROR = 0xffU,
} dap_response_status_e;

typedef enum dap_port {
	DAP_PORT_DEFAULT = 0U,
	DAP_PORT_SWD = 1U,
	DAP_PORT_JTAG = 2U,
} dap_port_e;

typedef enum dap_transfer_status {
	DAP_TRANSFER_OK = 0x01U,
	DAP_TRANSFER_WAIT = 0x02U,
	DAP_TRANSFER_FAULT = 0x04U,
	DAP_TRANSFER_NO_RESPONSE = 0x07U,
} dap_transfer_status_e;

typedef enum dap_info_status {
	DAP_INFO_NO_INFO = 0U,
	DAP_INFO_NO_STRING = 1U,
} dap_info_status_e;

#define DAP_SWD_OUT_SEQUENCE 0U
#define DAP_SWD_IN_SEQUENCE  1U

#define DAP_INFO_MAX_LENGTH 256U

#define DAP_SWJ_SWCLK_TCK (1U << 0U)
#define DAP_SWJ_SWDIO_TMS (1U << 1U)
#define DAP_SWJ_TDI       (1U << 2U)
#define DAP_SWJ_TDO       (1U << 3U)
#define DAP_SWJ_nTRST     (1U << 5U)
#define DAP_SWJ_nRST      (1U << 7U)

typedef struct dap_transfer_request {
	uint8_t request;
	uint32_t data;
} dap_transfer_request_s;

typedef struct dap_transfer_response {
	uint8_t processed;
	uint8_t status;
	uint8_t data[12][4];
} dap_transfer_response_s;

typedef struct dap_transfer_block_request_read {
	uint8_t command;
	uint8_t index;
	uint8_t block_count[2];
	uint8_t request;
} dap_transfer_block_request_read_s;

typedef struct dap_transfer_block_request_write {
	uint8_t command;
	uint8_t index;
	uint8_t block_count[2];
	uint8_t request;
	uint8_t data[256][4];
} dap_transfer_block_request_write_s;

#define DAP_CMD_BLOCK_WRITE_HDR_LEN offsetof(dap_transfer_block_request_write_s, data)

typedef struct dap_transfer_block_response_read {
	uint8_t count[2];
	uint8_t status;
	uint8_t data[256][4];
} dap_transfer_block_response_read_s;

#define DAP_CMD_BLOCK_READ_HDR_LEN offsetof(dap_transfer_block_response_read_s, data)

typedef struct dap_transfer_block_response_write {
	uint8_t count[2];
	uint8_t status;
} dap_transfer_block_response_write_s;

typedef struct dap_swd_sequence {
	uint8_t cycles : 7;
	uint8_t direction : 1;
	uint8_t data[8];
} dap_swd_sequence_s;

typedef struct dap_swj_pins_request {
	uint8_t request;
	uint8_t pin_values;
	uint8_t selected_pins;
	uint8_t wait_time[4];
} dap_swj_pins_request_s;

bool perform_dap_transfer(adiv5_debug_port_s *target_dp, const dap_transfer_request_s *transfer_requests,
	size_t requests, uint32_t *response_data, size_t responses);
bool perform_dap_transfer_recoverable(adiv5_debug_port_s *target_dp, const dap_transfer_request_s *transfer_requests,
	size_t requests, uint32_t *response_data, size_t responses);
bool perform_dap_transfer_block_read(
	adiv5_debug_port_s *target_dp, uint8_t reg, uint16_t block_count, uint32_t *blocks);
bool perform_dap_transfer_block_write(
	adiv5_debug_port_s *target_dp, uint8_t reg, uint16_t block_count, const uint32_t *blocks);

bool perform_dap_swj_sequence(size_t clock_cycles, const uint8_t *data);

bool perform_dap_jtag_sequence(const uint8_t *data_in, uint8_t *data_out, bool final_tms, size_t clock_cycles);
bool perform_dap_jtag_tms_sequence(uint64_t tms_states, size_t clock_cycles);

bool perform_dap_swd_sequences(dap_swd_sequence_s *sequences, uint8_t sequence_count);

#endif /*PLATFORMS_HOSTED_DAP_COMMAND_H*/
