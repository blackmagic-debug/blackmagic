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

#ifndef PLATFORMS_HOSTED_REMOTE_PROTOCOL_V1_DEFS_H
#define PLATFORMS_HOSTED_REMOTE_PROTOCOL_V1_DEFS_H

/* Bring in the v0 protocol definitions and undefine the ADIv5 acceleration protocol */
#include "protocol_v0_defs.h"

#undef REMOTE_ADIv5_RAW_ACCESS_STR
#undef REMOTE_DP_READ_STR
#undef REMOTE_AP_READ_STR
#undef REMOTE_AP_WRITE_STR
#undef REMOTE_ADIv5_MEM_READ_STR
#undef REMOTE_ADIv5_MEM_READ_LENGTH
#undef REMOTE_ADIv5_MEM_WRITE_STR
#undef REMOTE_ADIv5_MEM_WRITE_LENGTH

/* This version of the protocol introduces a command for sending JTAG device information */
#define REMOTE_HL_PACKET    'H'
#define REMOTE_ADD_JTAG_DEV 'J'
/* And introduces a device index concept to the ADIv5 protocol */
#define REMOTE_ADIv5_DEV_INDEX REMOTE_UINT8

/* High-level protocol message for sending a jtag_dev_s */
#define REMOTE_JTAG_ADD_DEV_STR                                                            \
	(char[])                                                                               \
	{                                                                                      \
		REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_ADD_JTAG_DEV, REMOTE_UINT8, /* index */       \
			REMOTE_UINT8,                                                /* dr_prescan */  \
			REMOTE_UINT8,                                                /* dr_postscan */ \
			REMOTE_UINT8,                                                /* ir_len */      \
			REMOTE_UINT8,                                                /* ir_prescan */  \
			REMOTE_UINT8,                                                /* ir_postscan */ \
			REMOTE_UINT32,                                               /* current_ir */  \
			REMOTE_EOM, 0                                                                  \
	}

/* ADIv5 remote protocol messages */
#define REMOTE_ADIv5_RAW_ACCESS_STR                                                                            \
	(char[])                                                                                                   \
	{                                                                                                          \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_ADIv5_RAW_ACCESS, REMOTE_ADIv5_DEV_INDEX, REMOTE_ADIv5_AP_SEL, \
			REMOTE_ADIv5_ADDR16, REMOTE_ADIv5_DATA, REMOTE_EOM, 0                                              \
	}
#define REMOTE_DP_READ_STR                                                                                      \
	(char[])                                                                                                    \
	{                                                                                                           \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_DP_READ, REMOTE_ADIv5_DEV_INDEX, 'f', 'f', REMOTE_ADIv5_ADDR16, \
			REMOTE_EOM, 0                                                                                       \
	}
#define REMOTE_AP_READ_STR                                                                            \
	(char[])                                                                                          \
	{                                                                                                 \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_AP_READ, REMOTE_ADIv5_DEV_INDEX, REMOTE_ADIv5_AP_SEL, \
			REMOTE_ADIv5_ADDR16, REMOTE_EOM, 0                                                        \
	}
#define REMOTE_AP_WRITE_STR                                                                            \
	(char[])                                                                                           \
	{                                                                                                  \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_AP_WRITE, REMOTE_ADIv5_DEV_INDEX, REMOTE_ADIv5_AP_SEL, \
			REMOTE_ADIv5_ADDR16, REMOTE_ADIv5_DATA, REMOTE_EOM, 0                                      \
	}
#define REMOTE_ADIv5_MEM_READ_STR                                                                      \
	(char[])                                                                                           \
	{                                                                                                  \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_MEM_READ, REMOTE_ADIv5_DEV_INDEX, REMOTE_ADIv5_AP_SEL, \
			REMOTE_ADIv5_CSW, REMOTE_ADIv5_ADDR32, REMOTE_ADIv5_COUNT, REMOTE_EOM, 0                   \
	}
/*
 * 3 leader bytes + 2 bytes for dev index + 2 bytes for AP select + 8 for CSW + 8 for the address
 * and 8 for the count and one trailer gives 32U
 */
#define REMOTE_ADIv5_MEM_READ_LENGTH 32U
#define REMOTE_ADIv5_MEM_WRITE_STR                                                                      \
	(char[])                                                                                            \
	{                                                                                                   \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_MEM_WRITE, REMOTE_ADIv5_DEV_INDEX, REMOTE_ADIv5_AP_SEL, \
			REMOTE_ADIv5_CSW, REMOTE_ADIv5_ALIGNMENT, REMOTE_ADIv5_ADDR32, REMOTE_ADIv5_COUNT, 0        \
	}
/*
 * 3 leader bytes + 2 bytes for dev index + 2 bytes for AP select + 8 for CSW + 2 for the alignment +
 * 8 for the address and 8 for the count and one trailer gives 34U
 */
#define REMOTE_ADIv5_MEM_WRITE_LENGTH 34U

#endif /*PLATFORMS_HOSTED_REMOTE_PROTOCOL_V1_DEFS_H*/
