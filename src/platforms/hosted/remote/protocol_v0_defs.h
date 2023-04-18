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

#ifndef PLATFORMS_HOSTED_REMOTE_PROTOCOL_V0_DEFS_H
#define PLATFORMS_HOSTED_REMOTE_PROTOCOL_V0_DEFS_H

/* Protocol error messages */
#define REMOTE_ERROR_UNRECOGNISED 1
#define REMOTE_ERROR_WRONGLEN     2

/* Start and end of message identifiers */
#define REMOTE_SOM  '!'
#define REMOTE_EOM  '#'
#define REMOTE_RESP '&'

/* Protocol response codes */
#define REMOTE_RESP_OK     'K'
#define REMOTE_RESP_PARERR 'P'
#define REMOTE_RESP_ERR    'E'
#define REMOTE_RESP_NOTSUP 'N'

/* Protocol data elements */
#define REMOTE_UINT8  '%', '0', '2', 'x'
#define REMOTE_UINT16 '%', '0', '4', 'x'
#define REMOTE_UINT32 '%', '0', '8', 'x'

/* Generic protocol elements */
#define REMOTE_GEN_PACKET 'G'

#define REMOTE_TDITDO_TMS   'D'
#define REMOTE_TDITDO_NOTMS 'd'
#define REMOTE_IN_PAR       'I'
#define REMOTE_IN           'i'
#define REMOTE_NEXT         'N'
#define REMOTE_OUT_PAR      'O'
#define REMOTE_OUT          'o'
#define REMOTE_PWR_SET      'P'
#define REMOTE_PWR_GET      'p'
#define REMOTE_RESET        'R'
#define REMOTE_INIT         'S'
#define REMOTE_TMS          'T'
#define REMOTE_VOLTAGE      'V'
#define REMOTE_NRST_SET     'Z'
#define REMOTE_NRST_GET     'z'

/* Generic protocol messages */
#define REMOTE_VOLTAGE_STR                                           \
	(char[])                                                         \
	{                                                                \
		REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_VOLTAGE, REMOTE_EOM, 0 \
	}
#define REMOTE_NRST_SET_STR                                                     \
	(char[])                                                                    \
	{                                                                           \
		REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_NRST_SET, '%', 'c', REMOTE_EOM, 0 \
	}
#define REMOTE_NRST_GET_STR                                           \
	(char[])                                                          \
	{                                                                 \
		REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_NRST_GET, REMOTE_EOM, 0 \
	}
#define REMOTE_PWR_SET_STR                                                     \
	(char[])                                                                   \
	{                                                                          \
		REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_PWR_SET, '%', 'c', REMOTE_EOM, 0 \
	}
#define REMOTE_PWR_GET_STR                                           \
	(char[])                                                         \
	{                                                                \
		REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_PWR_GET, REMOTE_EOM, 0 \
	}

/* SWD protocol elements and messages */
#define REMOTE_SWD_PACKET 'S'
#define REMOTE_SWD_INIT_STR                                       \
	(char[])                                                      \
	{                                                             \
		REMOTE_SOM, REMOTE_SWD_PACKET, REMOTE_INIT, REMOTE_EOM, 0 \
	}
#define REMOTE_SWD_IN_PAR_STR                                                     \
	(char[])                                                                      \
	{                                                                             \
		REMOTE_SOM, REMOTE_SWD_PACKET, REMOTE_IN_PAR, REMOTE_UINT8, REMOTE_EOM, 0 \
	}
#define REMOTE_SWD_IN_STR                                                     \
	(char[])                                                                  \
	{                                                                         \
		REMOTE_SOM, REMOTE_SWD_PACKET, REMOTE_IN, REMOTE_UINT8, REMOTE_EOM, 0 \
	}
#define REMOTE_SWD_OUT_STR                                                               \
	(char[])                                                                             \
	{                                                                                    \
		REMOTE_SOM, REMOTE_SWD_PACKET, REMOTE_OUT, REMOTE_UINT8, '%', 'x', REMOTE_EOM, 0 \
	}
#define REMOTE_SWD_OUT_PAR_STR                                                               \
	(char[])                                                                                 \
	{                                                                                        \
		REMOTE_SOM, REMOTE_SWD_PACKET, REMOTE_OUT_PAR, REMOTE_UINT8, '%', 'x', REMOTE_EOM, 0 \
	}

/* JTAG protocol elements and messages */
#define REMOTE_JTAG_PACKET 'J'
#define REMOTE_JTAG_INIT_STR                                                        \
	(char[])                                                                        \
	{                                                                               \
		'+', REMOTE_EOM, REMOTE_SOM, REMOTE_JTAG_PACKET, REMOTE_INIT, REMOTE_EOM, 0 \
	}
#define REMOTE_JTAG_RESET_STR                                                        \
	(char[])                                                                         \
	{                                                                                \
		'+', REMOTE_EOM, REMOTE_SOM, REMOTE_JTAG_PACKET, REMOTE_RESET, REMOTE_EOM, 0 \
	}
#define REMOTE_JTAG_TMS_STR                                                               \
	(char[])                                                                              \
	{                                                                                     \
		REMOTE_SOM, REMOTE_JTAG_PACKET, REMOTE_TMS, REMOTE_UINT8, '%', 'x', REMOTE_EOM, 0 \
	}
#define REMOTE_JTAG_TDIDO_STR                                                                \
	(char[])                                                                                 \
	{                                                                                        \
		REMOTE_SOM, REMOTE_JTAG_PACKET, '%', 'c', REMOTE_UINT8, '%', 'l', 'x', REMOTE_EOM, 0 \
	}
#define REMOTE_JTAG_NEXT                                                               \
	(char[])                                                                           \
	{                                                                                  \
		REMOTE_SOM, REMOTE_JTAG_PACKET, REMOTE_NEXT, '%', 'u', '%', 'u', REMOTE_EOM, 0 \
	}

/* ADIv5 protocol elements and messages */
#define REMOTE_ADIv5_PACKET     'H'
#define REMOTE_ADIv5_RAW_ACCESS 'L'
#define REMOTE_DP_READ          'd'
#define REMOTE_AP_READ          'a'
#define REMOTE_AP_WRITE         'A'
#define REMOTE_MEM_READ         'M'
#define REMOTE_MEM_WRITE        'm'

#define REMOTE_ADIv5_AP_SEL    REMOTE_UINT8
#define REMOTE_ADIv5_ADDR16    REMOTE_UINT16
#define REMOTE_ADIv5_ADDR32    REMOTE_UINT32
#define REMOTE_ADIv5_DATA      REMOTE_UINT32
#define REMOTE_ADIv5_CSW       REMOTE_UINT32
#define REMOTE_ADIv5_ALIGNMENT REMOTE_UINT8
#define REMOTE_ADIv5_COUNT     REMOTE_UINT32

#define REMOTE_ADIv5_RAW_ACCESS_STR                                                                         \
	(char[])                                                                                                \
	{                                                                                                       \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_ADIv5_RAW_ACCESS, REMOTE_ADIv5_AP_SEL, REMOTE_ADIv5_ADDR16, \
			REMOTE_ADIv5_DATA, REMOTE_EOM, 0                                                                \
	}
#define REMOTE_DP_READ_STR                                                                            \
	(char[])                                                                                          \
	{                                                                                                 \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_DP_READ, 'f', 'f', REMOTE_ADIv5_ADDR16, REMOTE_EOM, 0 \
	}
#define REMOTE_AP_READ_STR                                                                                       \
	(char[])                                                                                                     \
	{                                                                                                            \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_AP_READ, REMOTE_ADIv5_AP_SEL, REMOTE_ADIv5_ADDR16, REMOTE_EOM, 0 \
	}
#define REMOTE_AP_WRITE_STR                                                                                            \
	(char[])                                                                                                           \
	{                                                                                                                  \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_AP_WRITE, REMOTE_ADIv5_AP_SEL, REMOTE_ADIv5_ADDR16, REMOTE_ADIv5_DATA, \
			REMOTE_EOM, 0                                                                                              \
	}
#define REMOTE_ADIv5_MEM_READ_STR                                                                                     \
	(char[])                                                                                                          \
	{                                                                                                                 \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_MEM_READ, REMOTE_ADIv5_AP_SEL, REMOTE_ADIv5_CSW, REMOTE_ADIv5_ADDR32, \
			REMOTE_ADIv5_COUNT, REMOTE_EOM, 0                                                                         \
	}
/*
 * 3 leader bytes + 2 bytes for AP select + 8 for CSW + 8 for the address
 * and 8 for the count and one trailer gives 32U
 */
#define REMOTE_ADIv5_MEM_READ_LENGTH 30U
#define REMOTE_ADIv5_MEM_WRITE_STR                                                                \
	(char[])                                                                                      \
	{                                                                                             \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_MEM_WRITE, REMOTE_ADIv5_AP_SEL, REMOTE_ADIv5_CSW, \
			REMOTE_ADIv5_ALIGNMENT, REMOTE_ADIv5_ADDR32, REMOTE_ADIv5_COUNT, 0                    \
	}
/*
 * 3 leader bytes + 2 bytes for AP select + 8 for CSW + 2 for the alignment +
 * 8 for the address and 8 for the count and one trailer gives 34U
 */
#define REMOTE_ADIv5_MEM_WRITE_LENGTH 32U

#endif /*PLATFORMS_HOSTED_REMOTE_PROTOCOL_V0_DEFS_H*/
