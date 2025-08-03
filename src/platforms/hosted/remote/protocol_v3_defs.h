/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023-2025 1BitSquared <info@1bitsquared.com>
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

#ifndef PLATFORMS_HOSTED_REMOTE_PROTOCOL_V3_DEFS_H
#define PLATFORMS_HOSTED_REMOTE_PROTOCOL_V3_DEFS_H

/* Bring in the v2 protocol definitions and undefine the ADIv5 acceleration protocol */
#include "protocol_v2_defs.h"

#undef REMOTE_ADIV5_PACKET
#undef REMOTE_DP_READ
#undef REMOTE_AP_READ
#undef REMOTE_AP_WRITE
#undef REMOTE_ADIV5_RAW_ACCESS
#undef REMOTE_MEM_READ
#undef REMOTE_MEM_WRITE

/* This version of the protocol introdces proper error reporting */
#define REMOTE_ERROR_FAULT     3
#define REMOTE_ERROR_EXCEPTION 4

/* This version of the protocol completely reimplements the ADIv5 acceleration protocol message IDs */
#define REMOTE_ADIV5_PACKET     'A'
#define REMOTE_DP_READ          'd'
#define REMOTE_AP_READ          'a'
#define REMOTE_AP_WRITE         'A'
#define REMOTE_ADIV5_RAW_ACCESS 'R'
#define REMOTE_MEM_READ         'm'
#define REMOTE_MEM_WRITE        'M'

/* This version of the protocol introduces a SPI access interface */
#define REMOTE_SPI_PACKET      's'
#define REMOTE_SPI_BEGIN       'B'
#define REMOTE_SPI_END         'E'
#define REMOTE_SPI_CHIP_SELECT 'C'
#define REMOTE_SPI_TRANSFER    'X'
#define REMOTE_SPI_READ        'r'
#define REMOTE_SPI_WRTIE       'w'
#define REMOTE_SPI_CHIP_ID     'I'
#define REMOTE_SPI_RUN_COMMAND 'c'

#define REMOTE_SPI_BEGIN_STR                                                                          \
	(char[])                                                                                          \
	{                                                                                                 \
		'+', REMOTE_EOM, REMOTE_SOM, REMOTE_SPI_PACKET, REMOTE_SPI_BEGIN, REMOTE_UINT8, REMOTE_EOM, 0 \
	}
#define REMOTE_SPI_END_STR                                                         \
	(char[])                                                                       \
	{                                                                              \
		REMOTE_SOM, REMOTE_SPI_PACKET, REMOTE_SPI_END, REMOTE_UINT8, REMOTE_EOM, 0 \
	}
#define REMOTE_SPI_CHIP_SELECT_STR                                                         \
	(char[])                                                                               \
	{                                                                                      \
		REMOTE_SOM, REMOTE_SPI_PACKET, REMOTE_SPI_CHIP_SELECT, REMOTE_UINT8, REMOTE_EOM, 0 \
	}
#define REMOTE_SPI_TRANSFER_STR                                                                        \
	(char[])                                                                                           \
	{                                                                                                  \
		REMOTE_SOM, REMOTE_SPI_PACKET, REMOTE_SPI_TRANSFER, REMOTE_UINT8, REMOTE_UINT8, REMOTE_EOM, 0, \
	}
#define REMOTE_SPI_READ_STR                                                                                       \
	(char[])                                                                                                      \
	{                                                                                                             \
		REMOTE_SOM, REMOTE_SPI_PACKET, REMOTE_SPI_READ, REMOTE_UINT8, REMOTE_UINT8, REMOTE_UINT16, REMOTE_UINT24, \
			REMOTE_UINT16, REMOTE_EOM, 0,                                                                         \
	}
#define REMOTE_SPI_WRITE_STR                                                                                       \
	(char[])                                                                                                       \
	{                                                                                                              \
		REMOTE_SOM, REMOTE_SPI_PACKET, REMOTE_SPI_WRITE, REMOTE_UINT8, REMOTE_UINT8, REMOTE_UINT16, REMOTE_UINT24, \
			REMOTE_UINT16, 0,                                                                                      \
	}
#define REMOTE_SPI_CHIP_ID_STR                                                                       \
	(char[])                                                                                         \
	{                                                                                                \
		REMOTE_SOM, REMOTE_SPI_PACKET, REMOTE_SPI_CHIP_ID, REMOTE_UINT8, REMOTE_UINT8, REMOTE_EOM, 0 \
	}
#define REMOTE_SPI_RUN_COMMAND_STR                                                                        \
	(char[])                                                                                              \
	{                                                                                                     \
		REMOTE_SOM, REMOTE_SPI_PACKET, REMOTE_SPI_RUN_COMMAND, REMOTE_UINT8, REMOTE_UINT8, REMOTE_UINT16, \
			REMOTE_UINT24, REMOTE_EOM, 0                                                                  \
	}

#endif /*PLATFORMS_HOSTED_REMOTE_PROTOCOL_V3_DEFS_H*/
