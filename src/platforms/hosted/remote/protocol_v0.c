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
#include "swd.h"
#include "jtagtap.h"

#include "protocol_v0.h"
#include "protocol_v0_defs.h"
#include "protocol_v0_swd.h"

void remote_v0_init(void)
{
	remote_funcs = (bmp_remote_protocol_s){
		.swd_init = remote_v0_swd_init,
	};
}

bool remote_v0_swd_init(void)
{
	DEBUG_PROBE("remote_swd_init\n");
	platform_buffer_write(REMOTE_SWD_INIT_STR, sizeof(REMOTE_SWD_INIT_STR));

	char buffer[REMOTE_MAX_MSG_SIZE];
	const int length = platform_buffer_read(buffer, REMOTE_MAX_MSG_SIZE);
	if (!length || buffer[0] == REMOTE_RESP_ERR) {
		DEBUG_ERROR("remote_swd_init failed, error %s\n", length ? buffer + 1 : "unknown");
		return false;
	}

	swd_proc.seq_in = remote_v0_swd_seq_in;
	swd_proc.seq_in_parity = remote_v0_swd_seq_in_parity;
	swd_proc.seq_out = remote_v0_swd_seq_out;
	swd_proc.seq_out_parity = remote_v0_swd_seq_out_parity;
	return true;
}
