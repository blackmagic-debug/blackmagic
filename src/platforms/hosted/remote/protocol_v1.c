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
#include "protocol_v0.h"
#include "protocol_v1.h"
#include "protocol_v1_defs.h"
#include "protocol_v1_adiv5.h"

void remote_v1_init(void)
{
	DEBUG_WARN("Probe firmware does not support the newer JTAG commands, please update it.\n");
	remote_funcs = (bmp_remote_protocol_s){
		.swd_init = remote_v0_swd_init,
		.jtag_init = remote_v0_jtag_init,
		.adiv5_init = remote_v1_adiv5_init,
		.add_jtag_dev = remote_v1_add_jtag_dev,
	};
}

bool remote_v1_adiv5_init(adiv5_debug_port_s *const dp)
{
	DEBUG_WARN("Please update your probe's firmware for improved error handling\n");
	dp->low_access = remote_v1_adiv5_raw_access;
	dp->dp_read = remote_v1_adiv5_dp_read;
	dp->ap_read = remote_v1_adiv5_ap_read;
	dp->ap_write = remote_v1_adiv5_ap_write;
	dp->mem_read = remote_v1_adiv5_mem_read_bytes;
	dp->mem_write = remote_v1_adiv5_mem_write_bytes;
	return true;
}

void remote_v1_add_jtag_dev(const uint32_t dev_index, const jtag_dev_s *const jtag_dev)
{
	char buffer[REMOTE_MAX_MSG_SIZE];
	const int length = snprintf(buffer, REMOTE_MAX_MSG_SIZE, REMOTE_JTAG_ADD_DEV_STR, dev_index, jtag_dev->dr_prescan,
		jtag_dev->dr_postscan, jtag_dev->ir_len, jtag_dev->ir_prescan, jtag_dev->ir_postscan, jtag_dev->current_ir);
	platform_buffer_write(buffer, length);
	(void)platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	/* Don't need to check for error here - it's already done in remote_adiv5_dp_init */
}
