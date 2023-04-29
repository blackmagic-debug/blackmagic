/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019  Black Sphere Technologies Ltd.
 * Written by Dave Marples <dave@marples.net>
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef REMOTE_H
#define REMOTE_H

#include <inttypes.h>
#include "general.h"

#define REMOTE_HL_VERSION 3

/*
 * Commands to remote end, and responses
 * =====================================
 *
 * All commands as sent as ASCII and begin with !, ending with #.
 *  Parameters are hex digits and format is per command.
 *
 * !<CMD><PARAM>#
 *   <CMD>   - 2 digit ASCII value
 *   <PARAM> - x digits (according to command) ASCII value
 *
 * So, for example;
 *
 *  SI - swdptap_seq_in_parity
 *         tt - Ticks
 *       e.g. SI21 : Request input with parity, 33 ticks
 *       resp: K<PARAM> - hex value returned.
 *       resp: F<PARAM> - hex value returned, bad parity.
 *             X<err>   - error occurred
 *
 * The whole protocol is defined in this header file. Parameters have
 * to be marshalled in remote.c, swdptap.c and jtagtap.c, so be
 * careful to ensure the parameter handling matches the protocol
 * definition when anything is changed.
 */

/* Protocol error messages */
#define REMOTE_ERROR_UNRECOGNISED 1
#define REMOTE_ERROR_WRONGLEN     2
#define REMOTE_ERROR_FAULT        3
#define REMOTE_ERROR_EXCEPTION    4

/* Start and end of message identifiers */
#define REMOTE_SOM  '!'
#define REMOTE_EOM  '#'
#define REMOTE_RESP '&'

/* Protocol response options */
#define REMOTE_RESP_OK     'K'
#define REMOTE_RESP_PARERR 'P'
#define REMOTE_RESP_ERR    'E'
#define REMOTE_RESP_NOTSUP 'N'

/* Protocol data elements */
#define REMOTE_UINT8  '%', '0', '2', 'x'
#define REMOTE_UINT16 '%', '0', '4', 'x'
#define REMOTE_UINT24 '%', '0', '6', 'x'
#define REMOTE_UINT32 '%', '0', '8', 'x'

/* Generic protocol elements */
#define REMOTE_GEN_PACKET 'G'

#define REMOTE_START         'A'
#define REMOTE_TDITDO_TMS    'D'
#define REMOTE_TDITDO_NOTMS  'd'
#define REMOTE_CYCLE         'c'
#define REMOTE_IN_PAR        'I'
#define REMOTE_TARGET_CLK_OE 'E'
#define REMOTE_FREQ_SET      'F'
#define REMOTE_FREQ_GET      'f'
#define REMOTE_IN            'i'
#define REMOTE_NEXT          'N'
#define REMOTE_OUT_PAR       'O'
#define REMOTE_OUT           'o'
#define REMOTE_PWR_SET       'P'
#define REMOTE_PWR_GET       'p'
#define REMOTE_RESET         'R'
#define REMOTE_INIT          'S'
#define REMOTE_TMS           'T'
#define REMOTE_VOLTAGE       'V'
#define REMOTE_NRST_SET      'Z'
#define REMOTE_NRST_GET      'z'
#define REMOTE_ADD_JTAG_DEV  'J'

#define REMOTE_START_STR                                                            \
	(char[])                                                                        \
	{                                                                               \
		'+', REMOTE_EOM, REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_START, REMOTE_EOM, 0 \
	}
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
#define REMOTE_FREQ_SET_STR                                                               \
	(char[])                                                                              \
	{                                                                                     \
		REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_FREQ_SET, '%', '0', '8', 'x', REMOTE_EOM, 0 \
	}
#define REMOTE_FREQ_GET_STR                                           \
	(char[])                                                          \
	{                                                                 \
		REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_FREQ_GET, REMOTE_EOM, 0 \
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
#define REMOTE_TARGET_CLK_OE_STR                                                     \
	(char[])                                                                         \
	{                                                                                \
		REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_TARGET_CLK_OE, '%', 'c', REMOTE_EOM, 0 \
	}

/* SWDP protocol elements */
#define REMOTE_SWDP_PACKET 'S'
#define REMOTE_SWDP_INIT_STR                                       \
	(char[])                                                       \
	{                                                              \
		REMOTE_SOM, REMOTE_SWDP_PACKET, REMOTE_INIT, REMOTE_EOM, 0 \
	}

#define REMOTE_SWDP_IN_PAR_STR                                                           \
	(char[])                                                                             \
	{                                                                                    \
		REMOTE_SOM, REMOTE_SWDP_PACKET, REMOTE_IN_PAR, '%', '0', '2', 'x', REMOTE_EOM, 0 \
	}

#define REMOTE_SWDP_IN_STR                                                           \
	(char[])                                                                         \
	{                                                                                \
		REMOTE_SOM, REMOTE_SWDP_PACKET, REMOTE_IN, '%', '0', '2', 'x', REMOTE_EOM, 0 \
	}

#define REMOTE_SWDP_OUT_STR                                                                     \
	(char[])                                                                                    \
	{                                                                                           \
		REMOTE_SOM, REMOTE_SWDP_PACKET, REMOTE_OUT, '%', '0', '2', 'x', '%', 'x', REMOTE_EOM, 0 \
	}

#define REMOTE_SWDP_OUT_PAR_STR                                                                     \
	(char[])                                                                                        \
	{                                                                                               \
		REMOTE_SOM, REMOTE_SWDP_PACKET, REMOTE_OUT_PAR, '%', '0', '2', 'x', '%', 'x', REMOTE_EOM, 0 \
	}

/* JTAG protocol elements */
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

#define REMOTE_JTAG_TMS_STR                                                                     \
	(char[])                                                                                    \
	{                                                                                           \
		REMOTE_SOM, REMOTE_JTAG_PACKET, REMOTE_TMS, '%', '0', '2', 'x', '%', 'x', REMOTE_EOM, 0 \
	}

#define REMOTE_JTAG_TDIDO_STR                                                                      \
	(char[])                                                                                       \
	{                                                                                              \
		REMOTE_SOM, REMOTE_JTAG_PACKET, '%', 'c', '%', '0', '2', 'x', '%', 'l', 'x', REMOTE_EOM, 0 \
	}

#define REMOTE_JTAG_CYCLE_STR                                                                               \
	(char[])                                                                                                \
	{                                                                                                       \
		REMOTE_SOM, REMOTE_JTAG_PACKET, REMOTE_CYCLE, '%', 'u', '%', 'u', '%', '0', '8', 'x', REMOTE_EOM, 0 \
	}

#define REMOTE_JTAG_NEXT                                                               \
	(char[])                                                                           \
	{                                                                                  \
		REMOTE_SOM, REMOTE_JTAG_PACKET, REMOTE_NEXT, '%', 'u', '%', 'u', REMOTE_EOM, 0 \
	}

/* High-level protocol elements */
#define REMOTE_HL_PACKET 'H'
#define REMOTE_HL_CHECK  'C'

#define REMOTE_HL_CHECK_STR                                          \
	(char[])                                                         \
	{                                                                \
		REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_HL_CHECK, REMOTE_EOM, 0 \
	}
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

/* ADIv5 protocol elements */
#define REMOTE_ADIv5_PACKET     'A'
#define REMOTE_DP_READ          'd'
#define REMOTE_AP_READ          'a'
#define REMOTE_AP_WRITE         'A'
#define REMOTE_ADIv5_RAW_ACCESS 'R'
#define REMOTE_MEM_READ         'm'
#define REMOTE_MEM_WRITE        'M'

#define REMOTE_ADIv5_DEV_INDEX REMOTE_UINT8
#define REMOTE_ADIv5_AP_SEL    REMOTE_UINT8
#define REMOTE_ADIv5_ADDR16    REMOTE_UINT16
#define REMOTE_ADIv5_ADDR32    REMOTE_UINT32
#define REMOTE_ADIv5_DATA      REMOTE_UINT32
#define REMOTE_ADIv5_CSW       REMOTE_UINT32
#define REMOTE_ADIv5_ALIGNMENT REMOTE_UINT8
#define REMOTE_ADIv5_COUNT     REMOTE_UINT32

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
#define REMOTE_ADIv5_RAW_ACCESS_STR                                                                            \
	(char[])                                                                                                   \
	{                                                                                                          \
		REMOTE_SOM, REMOTE_ADIv5_PACKET, REMOTE_ADIv5_RAW_ACCESS, REMOTE_ADIv5_DEV_INDEX, REMOTE_ADIv5_AP_SEL, \
			REMOTE_ADIv5_ADDR16, REMOTE_ADIv5_DATA, REMOTE_EOM, 0                                              \
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

/* SPI protocol elements */
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

uint64_t remote_hex_string_to_num(uint32_t limit, const char *str);
void remote_packet_process(unsigned int i, char *packet);

#endif /* REMOTE_H */
