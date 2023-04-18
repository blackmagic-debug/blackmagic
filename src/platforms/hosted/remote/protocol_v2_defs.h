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

#ifndef PLATFORMS_HOSTED_REMOTE_PROTOCOL_V2_DEFS_H
#define PLATFORMS_HOSTED_REMOTE_PROTOCOL_V2_DEFS_H

/* Bring in the v1 protocol definitions */
#include "protocol_v1_defs.h"

/* This version of the protocol introduces commands for accessing and manipulating the JTAG/SWD frequency */
#define REMOTE_FREQ_SET 'F'
#define REMOTE_FREQ_GET 'f'
/* It also introduces a new command for running JTAG clock cycles without changing any of the other JTAG pin states */
#define REMOTE_CYCLE 'c'
/* Finally it introduces commands for making the TCK/SWCLK pin high-z and driven */
#define REMOTE_TARGET_CLK_OE 'E'

/* Generic protocol messages for the JTAG/SWD frequency */
#define REMOTE_FREQ_SET_STR                                                          \
	(char[])                                                                         \
	{                                                                                \
		REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_FREQ_SET, REMOTE_UINT32, REMOTE_EOM, 0 \
	}
#define REMOTE_FREQ_GET_STR                                           \
	(char[])                                                          \
	{                                                                 \
		REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_FREQ_GET, REMOTE_EOM, 0 \
	}

/* JTAG protocol message for running JTAG clock cycles */
#define REMOTE_JTAG_CYCLE_STR                                                                          \
	(char[])                                                                                           \
	{                                                                                                  \
		REMOTE_SOM, REMOTE_JTAG_PACKET, REMOTE_CYCLE, '%', 'u', '%', 'u', REMOTE_UINT32, REMOTE_EOM, 0 \
	}

/* Generic protocol message for changing the TCK/SWCLK high impedance state */
#define REMOTE_TARGET_CLK_OE_STR                                                     \
	(char[])                                                                         \
	{                                                                                \
		REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_TARGET_CLK_OE, '%', 'c', REMOTE_EOM, 0 \
	}

#endif /*PLATFORMS_HOSTED_REMOTE_PROTOCOL_V2_DEFS_H*/
