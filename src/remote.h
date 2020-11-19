/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019  Black Sphere Technologies Ltd.
 * Written by Dave Marples <dave@marples.net>
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

#ifndef _REMOTE_
#define _REMOTE_

#include <inttypes.h>
#include "general.h"

#define REMOTE_HL_VERSION 1

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
 *             X<err>   - error occured
 *
 * The whole protocol is defined in this header file. Parameters have
 * to be marshalled in remote.c, swdptap.c and jtagtap.c, so be
 * careful to ensure the parameter handling matches the protocol
 * definition when anything is changed.
 */

/* Protocol error messages */
#define REMOTE_ERROR_UNRECOGNISED 1
#define REMOTE_ERROR_WRONGLEN     2

/* Start and end of message identifiers */
#define REMOTE_SOM         '!'
#define REMOTE_EOM         '#'
#define REMOTE_RESP        '&'

/* Generic protocol elements */
#define REMOTE_START        'A'
#define REMOTE_TDITDO_TMS   'D'
#define REMOTE_TDITDO_NOTMS 'd'
#define REMOTE_IN_PAR       'I'
#define REMOTE_FREQ_SET     'F'
#define REMOTE_FREQ_GET     'f'
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
#define REMOTE_SRST_SET     'Z'
#define REMOTE_SRST_GET     'z'
#define REMOTE_ADD_JTAG_DEV 'J'

/* Protocol response options */
#define REMOTE_RESP_OK     'K'
#define REMOTE_RESP_PARERR 'P'
#define REMOTE_RESP_ERR    'E'
#define REMOTE_RESP_NOTSUP 'N'

/* High level protocol elements */
#define REMOTE_HL_CHECK     'C'
#define REMOTE_HL_PACKET 'H'
#define REMOTE_DP_READ      'd'
#define REMOTE_LOW_ACCESS   'L'
#define REMOTE_AP_READ      'a'
#define REMOTE_AP_WRITE     'A'
#define REMOTE_AP_MEM_READ  'M'
#define REMOTE_MEM_READ           'h'
#define REMOTE_MEM_WRITE_SIZED    'H'
#define REMOTE_AP_MEM_WRITE_SIZED 'm'


/* Generic protocol elements */
#define REMOTE_GEN_PACKET  'G'

#define REMOTE_START_STR (char []){ '+', REMOTE_EOM, REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_START, REMOTE_EOM, 0 }
#define REMOTE_VOLTAGE_STR (char []){ REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_VOLTAGE, REMOTE_EOM, 0 }
#define REMOTE_SRST_SET_STR (char []){ REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_SRST_SET, '%', 'c', REMOTE_EOM, 0 }
#define REMOTE_SRST_GET_STR (char []){ REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_SRST_GET, REMOTE_EOM, 0 }
#define REMOTE_FREQ_SET_STR (char []){ REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_FREQ_SET, '%', '0', '8', 'x', REMOTE_EOM, 0 }
#define REMOTE_FREQ_GET_STR (char []){ REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_FREQ_GET, REMOTE_EOM, 0 }
#define REMOTE_PWR_SET_STR (char []){ REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_PWR_SET, '%', 'c', REMOTE_EOM, 0 }
#define REMOTE_PWR_GET_STR (char []){ REMOTE_SOM, REMOTE_GEN_PACKET, REMOTE_PWR_GET, REMOTE_EOM, 0 }

/* SWDP protocol elements */
#define REMOTE_SWDP_PACKET 'S'
#define REMOTE_SWDP_INIT_STR (char []){ REMOTE_SOM, REMOTE_SWDP_PACKET, REMOTE_INIT, REMOTE_EOM, 0 }

#define REMOTE_SWDP_IN_PAR_STR (char []){ REMOTE_SOM, REMOTE_SWDP_PACKET, REMOTE_IN_PAR, \
                                          '%','0','2','x',REMOTE_EOM, 0 }

#define REMOTE_SWDP_IN_STR (char []){ REMOTE_SOM, REMOTE_SWDP_PACKET, REMOTE_IN, \
                                      '%','0','2','x',REMOTE_EOM, 0 }

#define REMOTE_SWDP_OUT_STR (char []){ REMOTE_SOM, REMOTE_SWDP_PACKET, REMOTE_OUT, \
                                       '%','0','2','x','%','x',REMOTE_EOM, 0 }

#define REMOTE_SWDP_OUT_PAR_STR (char []){ REMOTE_SOM, REMOTE_SWDP_PACKET, REMOTE_OUT_PAR, \
                                           '%','0','2','x','%','x',REMOTE_EOM, 0 }

/* JTAG protocol elements */
#define REMOTE_JTAG_PACKET 'J'

#define REMOTE_JTAG_INIT_STR (char []){ '+',REMOTE_EOM, REMOTE_SOM, REMOTE_JTAG_PACKET, REMOTE_INIT, REMOTE_EOM, 0 }

#define REMOTE_JTAG_RESET_STR (char []){ '+',REMOTE_EOM, REMOTE_SOM, REMOTE_JTAG_PACKET, REMOTE_RESET, REMOTE_EOM, 0 }

#define REMOTE_JTAG_TMS_STR (char []){ REMOTE_SOM, REMOTE_JTAG_PACKET, REMOTE_TMS, \
                                           '%','0','2','x','%','x',REMOTE_EOM, 0 }

#define REMOTE_JTAG_TDIDO_STR (char []){ REMOTE_SOM, REMOTE_JTAG_PACKET, '%', 'c', \
      '%','0','2','x','%','l', 'x', REMOTE_EOM, 0 }

#define REMOTE_JTAG_NEXT (char []){ REMOTE_SOM, REMOTE_JTAG_PACKET, REMOTE_NEXT, \
                                       '%','c','%','c',REMOTE_EOM, 0 }
/* HL protocol elements */
#define HEX '%', '0', '2', 'x'
#define HEX_U32(x) '%', '0', '8', 'x'
#define CHR(x) '%', 'c'

#define REMOTE_JTAG_ADD_DEV_STR (char []){ REMOTE_SOM, REMOTE_JTAG_PACKET,\
			REMOTE_ADD_JTAG_DEV,											\
			'%','0','2','x', /* index */								\
			'%','0','2','x', /* dr_prescan */							\
			'%','0','2','x', /*	dr_postscan	*/							\
			'%','0','2','x', /* ir_len */								\
			'%','0','2','x', /* ir_prescan */							\
			'%','0','2','x', /* ir_postscan */							\
			HEX_U32(current_ir), /* current_ir */						\
			REMOTE_EOM, 0}

#define REMOTE_HL_CHECK_STR (char []){ REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_HL_CHECK, REMOTE_EOM, 0 }
#define REMOTE_DP_READ_STR (char []){ REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_DP_READ, \
			'%','0', '2', 'x', 'f', 'f', '%', '0', '4', 'x', REMOTE_EOM, 0 }
#define REMOTE_LOW_ACCESS_STR (char []){ REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_LOW_ACCESS, \
			'%','0', '2', 'x', '%','0', '2', 'x', '%', '0', '4', 'x', HEX_U32(csw), REMOTE_EOM, 0 }
#define REMOTE_AP_READ_STR (char []){ REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_AP_READ, \
			'%','0', '2', 'x', '%','0','2','x', '%', '0', '4', 'x', REMOTE_EOM, 0 }
#define REMOTE_AP_WRITE_STR (char []){ REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_AP_WRITE, \
			'%','0', '2', 'x', '%','0','2','x', '%', '0', '4', 'x', HEX_U32(csw), REMOTE_EOM, 0 }
#define REMOTE_AP_MEM_READ_STR (char []){ REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_AP_MEM_READ, \
			'%','0', '2', 'x', '%','0','2','x',HEX_U32(csw), HEX_U32(address), HEX_U32(count), \
			REMOTE_EOM, 0 }
#define REMOTE_AP_MEM_WRITE_SIZED_STR (char []){ REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_AP_MEM_WRITE_SIZED, \
			'%','0', '2', 'x', '%', '0', '2', 'x', HEX_U32(csw), '%', '0', '2', 'x', HEX_U32(address), HEX_U32(count), 0}
#define REMOTE_MEM_WRITE_SIZED_STR (char []){ REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_AP_MEM_WRITE_SIZED, \
			'%','0', '2', 'x', '%','0','2','x', HEX_U32(address), HEX_U32(count), 0}

uint64_t remotehston(uint32_t limit, char *s);
void remotePacketProcess(uint8_t i, char *packet);

#endif
