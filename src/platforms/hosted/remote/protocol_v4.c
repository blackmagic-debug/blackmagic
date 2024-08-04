/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
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
#include "protocol_v1.h"
#include "protocol_v2.h"
#include "protocol_v3.h"
#include "protocol_v3_adiv5.h"
#include "protocol_v4.h"
#include "protocol_v4_defs.h"
#include "protocol_v4_adiv5.h"
#include "protocol_v4_riscv.h"

bool remote_v4_init(void)
{
	/* Before we initialise the remote functions structure, determine what accelerations are available */
	platform_buffer_write(REMOTE_HL_ACCEL_STR, sizeof(REMOTE_HL_ACCEL_STR));

	char buffer[REMOTE_MAX_MSG_SIZE];
	const ssize_t length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	/* Check for communication failures */
	if (length < 1 || buffer[0] != REMOTE_RESP_OK) {
		DEBUG_ERROR("%s comms error: %zd\n", __func__, length);
		return false;
	}

	const uint64_t accelerations = remote_decode_response(buffer + 1, length - 1);

	/* Fill in the base set that will always be available */
	remote_funcs = (bmp_remote_protocol_s){
		.swd_init = remote_v0_swd_init,
		.jtag_init = remote_v2_jtag_init,
		.add_jtag_dev = remote_v1_add_jtag_dev,
		.get_comms_frequency = remote_v2_get_comms_frequency,
		.set_comms_frequency = remote_v2_set_comms_frequency,
		.target_clk_output_enable = remote_v2_target_clk_output_enable,
	};

	/* Now fill in acceleration-specific functions */
	if (accelerations & REMOTE_ACCEL_ADIV5)
		remote_funcs.adiv5_init = remote_v4_adiv5_init;
	if (accelerations & REMOTE_ACCEL_RISCV) {
		/* For RISC-V we have to ask the acceleration backend what protocols it supports */
		platform_buffer_write(REMOTE_RISCV_PROTOCOLS_STR, sizeof(REMOTE_RISCV_PROTOCOLS_STR));

		const ssize_t protocols_length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
		/* Check for communication failures */
		if (protocols_length < 1 || buffer[0] != REMOTE_RESP_OK) {
			DEBUG_ERROR("%s comms error: %zd\n", __func__, protocols_length);
			return false;
		}

		const uint64_t riscv_protocols = remote_decode_response(buffer + 1, protocols_length - 1);

		if (riscv_protocols & REMOTE_RISCV_PROTOCOL_JTAG)
			remote_funcs.riscv_jtag_init = remote_v4_riscv_jtag_init;
	}

	return true;
}

bool remote_v4_adiv5_init(adiv5_debug_port_s *const dp)
{
	dp->low_access = remote_v3_adiv5_raw_access;
	dp->dp_read = remote_v3_adiv5_dp_read;
	dp->ap_read = remote_v3_adiv5_ap_read;
	dp->ap_write = remote_v3_adiv5_ap_write;
	dp->mem_read = remote_v4_adiv5_mem_read_bytes;
	dp->mem_write = remote_v4_adiv5_mem_write_bytes;
	return true;
}

bool remote_v4_riscv_jtag_init(riscv_dmi_s *const dmi)
{
	/* Format the RISC-V JTAG DTM init request into a new buffer and send it to the probe */
	char buffer[REMOTE_MAX_MSG_SIZE];
	ssize_t length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_RISCV_INIT_STR, REMOTE_RISCV_JTAG);
	platform_buffer_write(buffer, length);

	/* Read back the answer and check for errors */
	length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (length < 1 || buffer[0U] != REMOTE_RESP_OK) {
		DEBUG_ERROR("%s failed, error %s\n", __func__, length ? buffer + 1 : "with communication");
		return false;
	}

	dmi->read = remote_v4_riscv_jtag_dmi_read;
	dmi->write = remote_v4_riscv_jtag_dmi_write;
	return true;
}
