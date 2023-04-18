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

#include "bmp_remote.h"
#include "hex_utils.h"

#include "protocol_v0.h"
#include "protocol_v0_jtag.h"
#include "protocol_v1.h"
#include "protocol_v2.h"
#include "protocol_v2_defs.h"

static void remote_v2_jtag_cycle(bool tms, bool tdi, size_t clock_cycles);

void remote_v2_init(void)
{
	remote_funcs = (bmp_remote_protocol_s){
		.swd_init = remote_v0_swd_init,
		.jtag_init = remote_v2_jtag_init,
		.adiv5_init = remote_v1_adiv5_init,
		.add_jtag_dev = remote_v1_add_jtag_dev,
		.get_comms_frequency = remote_v2_get_comms_frequency,
		.set_comms_frequency = remote_v2_set_comms_frequency,
		.target_clk_output_enable = remote_v2_target_clk_output_enable,
	};
}

bool remote_v2_jtag_init(void)
{
	DEBUG_PROBE("remote_jtag_init\n");
	platform_buffer_write(REMOTE_JTAG_INIT_STR, sizeof(REMOTE_JTAG_INIT_STR));

	char buffer[REMOTE_MAX_MSG_SIZE];
	const int length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!length || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("remote_jtag_init failed, error %s\n", length ? buffer + 1 : "unknown");
		return false;
	}

	jtag_proc.jtagtap_reset = remote_v0_jtag_reset;
	jtag_proc.jtagtap_next = remote_v0_jtag_next;
	jtag_proc.jtagtap_tms_seq = remote_v0_jtag_tms_seq;
	jtag_proc.jtagtap_tdi_tdo_seq = remote_v0_jtag_tdi_tdo_seq;
	jtag_proc.jtagtap_tdi_seq = remote_v0_jtag_tdi_seq;
	jtag_proc.jtagtap_cycle = remote_v2_jtag_cycle;
	jtag_proc.tap_idle_cycles = 1;
	return true;
}

uint32_t remote_v2_get_comms_frequency(void)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, "%s", REMOTE_FREQ_GET_STR);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR)
		return FREQ_FIXED;
	uint32_t freq;
	unhexify(&freq, buffer + 1, 4);
	return freq;
}

bool remote_v2_set_comms_frequency(const uint32_t freq)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_FREQ_SET_STR, freq);
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("remote_set_comms_frequency: Failed to set SWD/JTAG clock frequency, error %s\n",
			length ? buffer + 1 : "with communication");
		return false;
	}
	return true;
}

void remote_v2_target_clk_output_enable(const bool enable)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_TARGET_CLK_OE_STR, enable ? '1' : '0');
	platform_buffer_write(buffer, length);
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0] == REMOTE_RESP_ERR)
		DEBUG_ERROR("remote_target_clk_output_enable failed, error %s\n", length ? buffer + 1 : "with communication");
}

static inline uint8_t bool_to_int(const bool value)
{
	return value ? 1U : 0U;
}

static void remote_v2_jtag_cycle(const bool tms, const bool tdi, const size_t clock_cycles)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	int length =
		snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_JTAG_CYCLE_STR, bool_to_int(tms), bool_to_int(tdi), clock_cycles);
	platform_buffer_write(buffer, length);

	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!length || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("jtagtap_cycle failed, error %s\n", length ? buffer + 1 : "unknown");
		exit(-1);
	}
}
