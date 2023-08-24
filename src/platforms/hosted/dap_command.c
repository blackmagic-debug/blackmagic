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

#include <string.h>
#include "dap.h"
#include "dap_command.h"
#include "exception.h"
#include "buffer_utils.h"

#define DAP_TRANSFER_APnDP       (1U << 0U)
#define DAP_TRANSFER_RnW         (1U << 1U)
#define DAP_TRANSFER_A2          (1U << 2U)
#define DAP_TRANSFER_A3          (1U << 3U)
#define DAP_TRANSFER_MATCH_VALUE (1U << 4U)
#define DAP_TRANSFER_MATCH_MASK  (1U << 5U)

#define DAP_JTAG_TMS_SET     (1U << 6U)
#define DAP_JTAG_TMS_CLEAR   (0U << 6U)
#define DAP_JTAG_TDO_CAPTURE (1U << 7U)

static size_t dap_encode_transfer(
	const dap_transfer_request_s *const transfer, uint8_t *const buffer, const size_t offset)
{
	buffer[offset] = transfer->request;
	const uint8_t request_flags = transfer->request & (DAP_TRANSFER_RnW | DAP_TRANSFER_MATCH_VALUE);
	/* If the transfer is a read and has no match value, the encoded length is 1 (just the command) */
	if ((request_flags & DAP_TRANSFER_RnW) && !(request_flags & DAP_TRANSFER_MATCH_VALUE))
		return 1U;
	/* Otherwise if it's a write or there's a match value, encode that too, making the request length 5 */
	write_le4(buffer, offset + 1U, transfer->data);
	return 5U;
}

static void dap_dispatch_status(adiv5_debug_port_s *const dp, const dap_transfer_status_e status)
{
	switch (status) {
	case DAP_TRANSFER_OK:
		break;
	case DAP_TRANSFER_WAIT:
		dp->fault = status;
		break;
	case DAP_TRANSFER_FAULT:
		DEBUG_ERROR("Access resulted in fault\n");
		dp->fault = status;
		break;
	case DAP_TRANSFER_NO_RESPONSE:
		DEBUG_ERROR("Access resulted in no response\n");
		dp->fault = status;
		break;
	default:
		DEBUG_ERROR("Access has invalid ack %x\n", status);
		raise_exception(EXCEPTION_ERROR, "Invalid ACK");
		break;
	}
}

/* https://www.keil.com/pack/doc/CMSIS/DAP/html/group__DAP__Transfer.html */
bool perform_dap_transfer(adiv5_debug_port_s *const target_dp, const dap_transfer_request_s *const transfer_requests,
	const size_t requests, uint32_t *const response_data, const size_t responses)
{
	/* Validate that the number of requests this transfer is valid. We artificially limit it to 12 (from 256) */
	if (!requests || requests > 12 || (responses && !response_data))
		return false;

	DEBUG_PROBE("-> dap_transfer (%zu requests)\n", requests);
	/* 63 is 3 + (12 * 5) where 5 is the max length of each transfer request */
	uint8_t request[63] = {
		DAP_TRANSFER,
		target_dp->dev_index,
		requests,
	};
	/* Encode the transfers into the buffer and detect if we're doing any reads */
	size_t offset = 3U;
	for (size_t i = 0; i < requests; ++i)
		offset += dap_encode_transfer(&transfer_requests[i], request, offset);

	dap_transfer_response_s response = {.processed = 0, .status = DAP_TRANSFER_OK};
	/* Run the request */
	if (!dap_run_cmd(request, offset, &response, 2U + (responses * 4U))) {
		dap_dispatch_status(target_dp, response.status);
		return false;
	}

	/* Look at the response and decipher what went on */
	if (response.processed == requests && response.status == DAP_TRANSFER_OK) {
		for (size_t i = 0; i < responses; ++i)
			response_data[i] = read_le4(response.data[i], 0);
		return true;
	}

	DEBUG_PROBE("-> transfer failed with %u after processing %u requests\n", response.status, response.processed);
	dap_dispatch_status(target_dp, response.status);
	return false;
}

bool perform_dap_transfer_recoverable(adiv5_debug_port_s *const target_dp,
	const dap_transfer_request_s *const transfer_requests, const size_t requests, uint32_t *const response_data,
	const size_t responses)
{
	const bool result = perform_dap_transfer(target_dp, transfer_requests, requests, response_data, responses);
	/* If all went well, or we can't recover, we get to early return */
	if (result || target_dp->fault != DAP_TRANSFER_NO_RESPONSE)
		return result;
	DEBUG_WARN("Recovering and re-trying access\n");
	/* Otherwise clear the error and try again as our best and final answer */
	target_dp->error(target_dp, true);
	return perform_dap_transfer(target_dp, transfer_requests, requests, response_data, responses);
}

/* https://www.keil.com/pack/doc/CMSIS/DAP/html/group__DAP__TransferBlock.html */
bool perform_dap_transfer_block_read(
	adiv5_debug_port_s *const target_dp, const uint8_t reg, const uint16_t block_count, uint32_t *const blocks)
{
	if (block_count > 256U)
		return false;

	DEBUG_PROBE("-> dap_transfer_block (%u transfer blocks)\n", block_count);
	dap_transfer_block_request_read_s request = {
		DAP_TRANSFER_BLOCK,
		target_dp->dev_index,
		{0},
		reg | DAP_TRANSFER_RnW,
	};
	write_le2(request.block_count, 0, block_count);

	dap_transfer_block_response_read_s response;
	/* Run the request having set up the request buffer */
	if (!dap_run_cmd(&request, sizeof(request), &response, 3U + (block_count * 4U)))
		return false;

	/* Check the response over */
	const uint16_t blocks_read = read_le2(response.count, 0);
	if (blocks_read == block_count && response.status == DAP_TRANSFER_OK) {
		for (size_t i = 0; i < block_count; ++i)
			blocks[i] = read_le4(response.data[i], 0);
		return true;
	}
	if (response.status != DAP_TRANSFER_OK)
		target_dp->fault = response.status;
	else
		target_dp->fault = 0;

	DEBUG_PROBE("-> transfer failed with %u after processing %u blocks\n", response.status, blocks_read);
	return false;
}

bool perform_dap_transfer_block_write(
	adiv5_debug_port_s *const target_dp, const uint8_t reg, const uint16_t block_count, const uint32_t *const blocks)
{
	if (block_count > 256U)
		return false;

	DEBUG_PROBE("-> dap_transfer_block (%u transfer blocks)\n", block_count);
	dap_transfer_block_request_write_s request = {
		DAP_TRANSFER_BLOCK,
		target_dp->dev_index,
		{0},
		reg & ~DAP_TRANSFER_RnW,
	};
	write_le2(request.block_count, 0, block_count);
	for (size_t i = 0; i < block_count; ++i)
		write_le4(request.data[i], 0, blocks[i]);

	dap_transfer_block_response_write_s response;
	/* Run the request having set up the request buffer */
	if (!dap_run_cmd(&request, DAP_CMD_BLOCK_WRITE_HDR_LEN + (block_count * 4U), &response, sizeof(response)))
		return false;

	/* Check the response over */
	const uint16_t blocks_written = read_le2(response.count, 0);
	if (blocks_written == block_count && response.status == DAP_TRANSFER_OK)
		return true;
	if (response.status != DAP_TRANSFER_OK)
		target_dp->fault = response.status;
	else
		target_dp->fault = 0;

	DEBUG_PROBE("-> transfer failed with %u after processing %u blocks\n", response.status, blocks_written);
	return false;
}

/* https://www.keil.com/pack/doc/CMSIS/DAP/html/group__DAP__SWJ__Sequence.html */
bool perform_dap_swj_sequence(size_t clock_cycles, const uint8_t *data)
{
	/* Validate that clock_cycles is in range for the request (spec limits it to 256) */
	if (clock_cycles > 256)
		return false;

	DEBUG_PROBE("-> dap_swj_sequence (%zu cycles)\n", clock_cycles);
	/* Construct the request buffer */
	uint8_t request[34] = {
		DAP_SWJ_SEQUENCE,
		(uint8_t)clock_cycles,
	};
	/* Calculate the number of bytes needed to represent the requested number of clock cycles */
	const size_t bytes = (clock_cycles + 7U) >> 3U;
	/* And copy the data into the buffer */
	memcpy(request + 2U, data, bytes);

	/* Sequence response is a single byte */
	uint8_t response = DAP_RESPONSE_OK;
	/* Run the request */
	if (!dap_run_cmd(request, 2U + bytes, &response, 1U))
		return false;
	/* And check that it succeeded */
	return response == DAP_RESPONSE_OK;
}

bool perform_dap_jtag_sequence(
	const uint8_t *const data_in, uint8_t *const data_out, const bool final_tms, const size_t clock_cycles)
{
	/* Check for any over-long sequences */
	if (clock_cycles > 64)
		return false;

	DEBUG_PROBE("-> dap_jtag_sequence (%zu cycles)\n", clock_cycles);
	/* Check for 0-length sequences */
	if (!clock_cycles)
		return true;

	const uint8_t capture_tdo = data_out ? DAP_JTAG_TDO_CAPTURE : 0U;
	/*
	 * If final_tms is true, we need to generate 2 sequences because of how TMS data is sent,
	 * except if we need to generate just a single clock cycle.
	 */
	const uint8_t sequences = final_tms && clock_cycles > 1 ? 2U : 1U;
	/* Adjust clock_cycles accordingly */
	const uint8_t cycles = clock_cycles - (sequences - 1U);

	/* 3 + 2 bytes for the request preambles + 9 for the sending data */
	uint8_t request[14] = {
		DAP_JTAG_SEQUENCE,
		sequences,
		/* The number of clock cycles to run is encoded with 64 remapped to 0 */
		(cycles & 63U) | (final_tms && sequences == 1U ? DAP_JTAG_TMS_SET : DAP_JTAG_TMS_CLEAR) | capture_tdo,
	};
	/* Copy in a suitable amount of data from the source buffer */
	const size_t sequence_length = (cycles + 7U) >> 3U;
	memcpy(request + 3U, data_in, sequence_length);
	size_t offset = 3U + sequence_length;
	/* Figure out where the final bit is */
	const uint8_t final_byte = cycles >> 3U;
	const uint8_t final_bit = cycles & 7U;
	/* If we need to build a second sequence, set up for it */
	if (sequences == 2U) {
		request[offset++] = 1U | DAP_JTAG_TMS_SET | DAP_JTAG_TDO_CAPTURE | capture_tdo;
		/* Copy the final bit out to the request LSb */
		request[offset++] = data_in[final_byte] >> final_bit;
	}

	/*
	 * If we should capture TDO, then calculate the response length as the sequence length + 1 if final_tms needs.
	 * Otherwise, if not capturing TDO, it is 0
	 */
	const size_t response_length = capture_tdo ? sequence_length + (sequences == 2U ? 1U : 0U) : 0U;
	uint8_t response[6U] = {DAP_RESPONSE_OK};
	/* Run the request having set up the request buffer */
	if (!dap_run_cmd(request, offset, response, 1U + response_length)) {
		DEBUG_PROBE("-> sequence failed with %u\n", response[0U]);
		return false;
	}

	if (response_length) {
		/* Copy the captured data out */
		memcpy(data_out, response + 1U, sequence_length);
		/* And the final bit from the second response LSb */
		if (sequences == 2U)
			data_out[final_byte] = (response[1 + sequence_length] & 1U) << final_bit;
	}
	/* And check that it succeeded */
	return response[0] == DAP_RESPONSE_OK;
}

bool perform_dap_jtag_tms_sequence(const uint64_t tms_states, const size_t clock_cycles)
{
	/* Check for any over-long sequences */
	if (clock_cycles > 64)
		return false;

	DEBUG_PROBE("-> dap_jtag_sequence (%zu cycles)\n", clock_cycles);
	/* Check for 0-length sequences */
	if (!clock_cycles)
		return true;

	/* 2 + (2 * 64) bytes for the request */
	uint8_t request[130] = {
		DAP_JTAG_SEQUENCE,
		clock_cycles,
	};
	size_t offset = 2;
	/* Build all the TMS cycles required */
	for (size_t cycle = 0; cycle < clock_cycles; ++cycle) {
		const bool tms = (tms_states >> cycle) & 1U;
		request[offset + 0] = 1U | (tms ? DAP_JTAG_TMS_SET : DAP_JTAG_TMS_CLEAR);
		request[offset + 1] = 1U;
		offset += 2U;
	}

	uint8_t response = DAP_RESPONSE_OK;
	/* Run the request having set up the request buffer */
	if (!dap_run_cmd(request, offset, &response, 1U)) {
		DEBUG_PROBE("-> sequence failed with %u\n", response);
		return false;
	}
	/* And check that it succeeded */
	return response == DAP_RESPONSE_OK;
}

static size_t dap_encode_swd_sequence(
	const dap_swd_sequence_s *const sequence, uint8_t *const buffer, const size_t offset)
{
	/* If the sequence is over-long, ignore it */
	if (sequence->cycles > 64U)
		return 0U;

	const uint8_t clock_cycles = sequence->cycles;
	/* Encode the cycle count and direction information */
	buffer[offset] = (clock_cycles & 0x3fU) | (sequence->direction << 7U);
	/* If we're dealing with an output sequence, encode the data here */
	if (sequence->direction == DAP_SWD_OUT_SEQUENCE) {
		/* Calculate how many data bytes are in use and copy that much data in */
		const size_t bytes = (clock_cycles + 7U) >> 3U;
		memcpy(buffer + offset + 1U, sequence->data, bytes);
		/* Then return the offset adjustment */
		return 1U + bytes;
	}
	/* If we're encoding an input sequence, we only encode the control byte */
	return 1U;
}

bool perform_dap_swd_sequences(dap_swd_sequence_s *const sequences, const uint8_t sequence_count)
{
	if (sequence_count > 4)
		return false;

	DEBUG_PROBE("-> dap_swd_sequence (%u sequences)\n", sequence_count);
	/* 38 is 2 + (4 * 9) where 9 is the max length of each sequence request */
	uint8_t request[38] = {
		DAP_SWD_SEQUENCE,
		sequence_count,
	};
	/* Encode the transfers into the buffer */
	size_t offset = 2U;
	size_t result_length = 0U;
	for (uint8_t i = 0; i < sequence_count; ++i) {
		const dap_swd_sequence_s *const sequence = &sequences[i];
		const size_t adjustment = dap_encode_swd_sequence(sequence, request, offset);
		/* If encoding failed, return */
		if (!adjustment)
			return false;
		offset += adjustment;
		/* Count up how many response bytes we're expecting */
		if (sequence->direction == DAP_SWD_IN_SEQUENCE)
			result_length += (sequence->cycles + 7U) >> 3U;
	}

	uint8_t response[33] = {DAP_RESPONSE_OK};
	/* Run the request having set up the request buffer */
	if (!dap_run_cmd(request, offset, response, 1U + result_length)) {
		DEBUG_PROBE("-> sequence failed with %u\n", response[0]);
		return false;
	}

	/* Now we have data, grab the response bytes and stuff them back into the sequence structures */
	offset = 1U;
	for (uint8_t i = 0; i < sequence_count; ++i) {
		dap_swd_sequence_s *const sequence = &sequences[i];
		/* If this one is not an in sequence, skip it */
		if (sequence->direction == DAP_SWD_OUT_SEQUENCE)
			continue;
		/* Figure out how many bytes to copy back and copy them */
		const size_t bytes = (sequence->cycles + 7U) >> 3U;
		memcpy(sequence->data, response + offset, bytes);
		offset += bytes;
	}
	/* Finally, check that it all succeeded */
	return response[0] == DAP_RESPONSE_OK;
}
