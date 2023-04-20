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
#include "protocol_v1.h"
#include "protocol_v2.h"
#include "protocol_v3.h"
#include "protocol_v3_adiv5.h"

void remote_v3_init(void)
{
	remote_funcs = (bmp_remote_protocol_s){
		.swd_init = remote_v0_swd_init,
		.jtag_init = remote_v2_jtag_init,
		.adiv5_init = remote_v3_adiv5_init,
		.add_jtag_dev = remote_v1_add_jtag_dev,
		.get_comms_frequency = remote_v2_get_comms_frequency,
		.set_comms_frequency = remote_v2_set_comms_frequency,
		.target_clk_output_enable = remote_v2_target_clk_output_enable,
	};
}

bool remote_v3_adiv5_init(adiv5_debug_port_s *const dp)
{
	dp->low_access = remote_v3_adiv5_raw_access;
	dp->dp_read = remote_v3_adiv5_dp_read;
	dp->ap_read = remote_v3_adiv5_ap_read;
	dp->ap_write = remote_v3_adiv5_ap_write;
	dp->mem_read = remote_v3_adiv5_mem_read_bytes;
	dp->mem_write = remote_v3_adiv5_mem_write_bytes;
	return true;
}
