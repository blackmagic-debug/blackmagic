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

#include "dap.h"
#include "dap_command.h"
#include "buffer_utils.h"
#include "maths_utils.h"

typedef enum dap_swd_turnaround_cycles {
	DAP_SWD_TURNAROUND_1_CYCLE = 0U,
	DAP_SWD_TURNAROUND_2_CYCLES = 1U,
	DAP_SWD_TURNAROUND_3_CYCLES = 2U,
	DAP_SWD_TURNAROUND_4_CYCLES = 3U,
} dap_swd_turnaround_cycles_e;

typedef enum dap_swd_fault_cfg {
	DAP_SWD_FAULT_NO_DATA_PHASE = 0U,
	DAP_SWD_FAULT_ALWAYS_DATA_PHASE = 4U,
} dap_swd_fault_cfg_e;

static uint32_t dap_swd_seq_in(size_t clock_cycles);
static bool dap_swd_seq_in_parity(uint32_t *result, size_t clock_cycles);
static void dap_swd_seq_out(uint32_t tms_states, size_t clock_cycles);
static void dap_swd_seq_out_parity(uint32_t tms_states, size_t clock_cycles);

static bool dap_swd_configure(dap_swd_turnaround_cycles_e turnaround, dap_swd_fault_cfg_e fault_cfg);
static bool dap_write_reg_no_check(uint16_t addr, uint32_t data);
static uint32_t dap_read_reg_no_check(uint16_t addr);

bool dap_swd_init(adiv5_debug_port_s *target_dp)
{
	/* If we are not able to talk SWD with this adaptor, make this insta-fail */
	if (!(dap_caps & DAP_CAP_SWD))
		return false;

	DEBUG_PROBE("-> dap_swd_init(%u)\n", target_dp->dev_index);
	/* Mark that we're going into SWD mode and configure the CMSIS-DAP adaptor accordingly */
	dap_disconnect();
	dap_mode = DAP_CAP_SWD;
	dap_swd_configure(DAP_SWD_TURNAROUND_1_CYCLE, DAP_SWD_FAULT_NO_DATA_PHASE);
	dap_connect();

	/* Set up the underlying SWD functions using the implementation below */
	swd_proc.seq_in = dap_swd_seq_in;
	swd_proc.seq_in_parity = dap_swd_seq_in_parity;
	swd_proc.seq_out = dap_swd_seq_out;
	swd_proc.seq_out_parity = dap_swd_seq_out_parity;

	/* If we have SWD sequences available, make use of them */
	if (dap_has_swd_sequence)
		target_dp->write_no_check = dap_write_reg_no_check;
	else
		target_dp->write_no_check = NULL;
	/* Set up the accelerated SWD functions for basic target operations */
	target_dp->read_no_check = dap_read_reg_no_check;
	target_dp->dp_read = dap_dp_read_reg;
	target_dp->low_access = dap_dp_raw_access;
	target_dp->abort = dap_dp_abort;
	return true;
}

static bool dap_swd_configure(const dap_swd_turnaround_cycles_e turnaround, const dap_swd_fault_cfg_e fault_cfg)
{
	/* Setup the request buffer to configure how DAP_SWD_SEQUENCE works */
	const uint8_t request[2] = {
		DAP_SWD_CONFIGURE,
		turnaround | fault_cfg,
	};
	uint8_t result = DAP_RESPONSE_OK;
	/* Execute it and check if it failed */
	if (!dap_run_cmd(request, 2U, &result, 1U)) {
		DEBUG_PROBE("%s failed\n", __func__);
		return false;
	}
	/* Validate that it actually succeeded */
	return result == DAP_RESPONSE_OK;
}

static void dap_swd_seq_out(const uint32_t tms_states, const size_t clock_cycles)
{
	/* Setup the sequence */
	dap_swd_sequence_s sequence = {
		clock_cycles,
		DAP_SWD_OUT_SEQUENCE,
	};
	write_le4(sequence.data, 0, tms_states);
	/* And perform it */
	if (!perform_dap_swd_sequences(&sequence, 1U))
		DEBUG_ERROR("dap_swd_seq_out failed\n");
}

static void dap_swd_seq_out_parity(const uint32_t tms_states, const size_t clock_cycles)
{
	/* Setup the sequence */
	dap_swd_sequence_s sequence = {
		clock_cycles + 1,
		DAP_SWD_OUT_SEQUENCE,
	};
	write_le4(sequence.data, 0, tms_states);
	sequence.data[4] = calculate_odd_parity(tms_states);
	/* And perform it */
	if (!perform_dap_swd_sequences(&sequence, 1U))
		DEBUG_ERROR("dap_swd_seq_out_parity failed\n");
}

static uint32_t dap_swd_seq_in(const size_t clock_cycles)
{
	/* Setup the sequence */
	dap_swd_sequence_s sequence = {
		clock_cycles,
		DAP_SWD_IN_SEQUENCE,
	};
	/* And perform it */
	if (!perform_dap_swd_sequences(&sequence, 1U)) {
		DEBUG_ERROR("dap_swd_seq_in failed\n");
		return 0U;
	}

	uint32_t result = 0;
	for (size_t offset = 0; offset < clock_cycles; offset += 8U)
		result |= sequence.data[offset >> 3U] << offset;
	return result;
}

static bool dap_swd_seq_in_parity(uint32_t *const result, const size_t clock_cycles)
{
	/* Setup the sequence */
	dap_swd_sequence_s sequence = {
		clock_cycles + 1U,
		DAP_SWD_IN_SEQUENCE,
	};
	/* And perform it */
	if (!perform_dap_swd_sequences(&sequence, 1U)) {
		DEBUG_ERROR("dap_swd_seq_in_parity failed\n");
		return false;
	}

	uint32_t data = 0;
	for (size_t offset = 0; offset < clock_cycles; offset += 8U)
		data |= (uint32_t)sequence.data[offset >> 3U] << offset;
	*result = data;
	uint8_t parity = calculate_odd_parity(data);
	return parity == (sequence.data[4] & 1U);
}

static bool dap_write_reg_no_check(const uint16_t addr, const uint32_t data)
{
	DEBUG_PROBE("dap_write_reg_no_check %04x <- %08" PRIx32 "\n", addr, data);
	/* Setup the sequences */
	dap_swd_sequence_s sequences[4] = {
		/* Write the 8 byte request */
		{
			8U,
			DAP_SWD_OUT_SEQUENCE,
			{make_packet_request(ADIV5_LOW_WRITE, addr)},
		},
		/* Perform one turn-around cycle then read the 3 bit ACK */
		{4U, DAP_SWD_IN_SEQUENCE},
		/* Perform another turnaround cycle */
		{1U, DAP_SWD_OUT_SEQUENCE, {0}},
		/* Now write out the 32b of data to send and the 1b of parity */
		{
			33U,
			DAP_SWD_OUT_SEQUENCE,
			/* The 4 data bytes are filled in below with write_le4() */
			{0U, 0U, 0U, 0U, calculate_odd_parity(data)},
		},
	};
	write_le4(sequences[3].data, 0, data);
	/* Now perform the sequences */
	if (!perform_dap_swd_sequences(sequences, 4U)) {
		DEBUG_ERROR("dap_write_reg_no_check failed\n");
		return false;
	}
	/* Check the ack state */
	const uint8_t ack = (sequences[1].data[0] >> 1U) & 7U;
	return ack != SWDP_ACK_OK;
}

/* This is a wrapper around dap_read_reg() for use by target_dp as the read_no_check function */
uint32_t dap_read_reg_no_check(const uint16_t addr)
{
	/* Create a dummy DP, the only use for it is to pass the DAP Index to perform_dap_transfer which is ignored for SWD transfers, and return the fault code, which we don't care about */
	adiv5_debug_port_s dummy_dp = {0};
	return dap_read_reg(&dummy_dp, addr);
}
