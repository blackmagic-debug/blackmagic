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

#include "general.h"
#include "remote.h"
#include "gdb_packet.h"
#include "swdptap.h"
#include "jtagtap.h"
#include "gdb_if.h"
#include "version.h"
#include <stdarg.h>


#define NTOH(x) ((x<=9)?x+'0':'a'+x-10)
#define HTON(x) ((x<='9')?x-'0':((TOUPPER(x))-'A'+10))
#define TOUPPER(x) ((((x)>='a') && ((x)<='z'))?((x)-('a'-'A')):(x))
#define ISHEX(x) (						\
		(((x)>='0') && ((x)<='9')) ||					\
		(((x)>='A') && ((x)<='F')) ||					\
		(((x)>='a') && ((x)<='f'))						\
		)


uint64_t remotehston(uint32_t limit, char *s)

/* Return numeric version of string, until illegal hex digit, or limit */

{
	uint64_t ret=0L;
	char c;

	while (limit--) {
		c=*s++;
		if (!ISHEX(c))
			return ret;
		ret=(ret<<4)|HTON(c);
    }

	return ret;
}

#if PC_HOSTED == 0
static void _respond(char respCode, uint64_t param)

/* Send response to far end */

{
	char buf[35]; /*Response, code, EOM and 2*16 hex nibbles*/
	char *p=buf;

	gdb_if_putchar(REMOTE_RESP,0);
	gdb_if_putchar(respCode,0);

	do {
		*p++=NTOH((param&0x0f));
		param>>=4;
    }
	while (param);

	/* At this point the number to print is the buf, but backwards, so spool it out */
	do {
		gdb_if_putchar(*--p,0);
    } while (p>buf);
	gdb_if_putchar(REMOTE_EOM,1);
}

static void _respondS(char respCode, const char *s)
/* Send response to far end */
{
	gdb_if_putchar(REMOTE_RESP,0);
	gdb_if_putchar(respCode,0);
	while (*s) {
		/* Just clobber illegal characters so they don't disturb the protocol */
		if ((*s=='$') || (*s==REMOTE_SOM) || (*s==REMOTE_EOM))
			gdb_if_putchar(' ', 0);
		else
			gdb_if_putchar(*s, 0);
		s++;
    }
	gdb_if_putchar(REMOTE_EOM,1);
}

void remotePacketProcessSWD(uint8_t i, char *packet)
{
	uint8_t ticks;
	uint32_t param;
	bool badParity;

	switch (packet[1]) {
    case REMOTE_INIT: /* SS = initialise ================================= */
		if (i==2) {
			swdptap_init();
			_respond(REMOTE_RESP_OK, 0);
		} else {
			_respond(REMOTE_RESP_ERR,REMOTE_ERROR_WRONGLEN);
		}
		break;

    case REMOTE_IN_PAR: /* = In parity ================================== */
		ticks=remotehston(2,&packet[2]);
		badParity = swd_proc.swdptap_seq_in_parity(&param, ticks);
		_respond(badParity?REMOTE_RESP_PARERR:REMOTE_RESP_OK,param);
		break;

    case REMOTE_IN: /* = In ========================================= */
		ticks=remotehston(2,&packet[2]);
		param = swd_proc.swdptap_seq_in(ticks);
		_respond(REMOTE_RESP_OK,param);
		break;

    case REMOTE_OUT: /* = Out ======================================== */
		ticks=remotehston(2,&packet[2]);
		param=remotehston(-1, &packet[4]);
		swd_proc.swdptap_seq_out(param, ticks);
		_respond(REMOTE_RESP_OK, 0);
		break;

    case REMOTE_OUT_PAR: /* = Out parity ================================= */
		ticks=remotehston(2,&packet[2]);
		param=remotehston(-1, &packet[4]);
		swd_proc.swdptap_seq_out_parity(param, ticks);
		_respond(REMOTE_RESP_OK, 0);
		break;

    default:
		_respond(REMOTE_RESP_ERR,REMOTE_ERROR_UNRECOGNISED);
		break;
    }
}

void remotePacketProcessJTAG(uint8_t i, char *packet)
{
	uint32_t MS;
	uint64_t DO;
	uint8_t ticks;
	uint64_t DI;

	switch (packet[1]) {
    case REMOTE_INIT: /* = initialise ================================= */
		jtagtap_init();
		_respond(REMOTE_RESP_OK, 0);
		break;

    case REMOTE_RESET: /* = reset ================================= */
		jtag_proc.jtagtap_reset();
		_respond(REMOTE_RESP_OK, 0);
		break;

    case REMOTE_TMS: /* = TMS Sequence ================================== */
		ticks=remotehston(2,&packet[2]);
		MS=remotehston(2,&packet[4]);

		if (i<4) {
			_respond(REMOTE_RESP_ERR,REMOTE_ERROR_WRONGLEN);
		} else {
			jtag_proc.jtagtap_tms_seq( MS, ticks);
			_respond(REMOTE_RESP_OK, 0);
		}
		break;

    case REMOTE_TDITDO_TMS: /* = TDI/TDO  ========================================= */
    case REMOTE_TDITDO_NOTMS:

		if (i<5) {
			_respond(REMOTE_RESP_ERR,REMOTE_ERROR_WRONGLEN);
		} else {
			ticks=remotehston(2,&packet[2]);
			DI=remotehston(-1,&packet[4]);
			jtag_proc.jtagtap_tdi_tdo_seq((void *)&DO, (packet[1]==REMOTE_TDITDO_TMS), (void *)&DI, ticks);

			/* Mask extra bits on return value... */
			DO &= (1LL << (ticks + 1)) - 1;

			_respond(REMOTE_RESP_OK, DO);
		}
		break;

    case REMOTE_NEXT: /* = NEXT ======================================== */
		if (i!=4) {
			_respond(REMOTE_RESP_ERR,REMOTE_ERROR_WRONGLEN);
		} else {
			uint32_t dat=jtag_proc.jtagtap_next( (packet[2]=='1'), (packet[3]=='1'));
			_respond(REMOTE_RESP_OK,dat);
		}
		break;

    default:
		_respond(REMOTE_RESP_ERR,REMOTE_ERROR_UNRECOGNISED);
		break;
    }
}

void remotePacketProcessGEN(uint8_t i, char *packet)

{
	(void)i;
	switch (packet[1]) {
    case REMOTE_VOLTAGE:
		_respondS(REMOTE_RESP_OK,platform_target_voltage());
		break;

    case REMOTE_SRST_SET:
		platform_srst_set_val(packet[2]=='1');
		_respond(REMOTE_RESP_OK,0);
		break;

    case REMOTE_SRST_GET:
		_respond(REMOTE_RESP_OK,platform_srst_get_val());
		break;

    case REMOTE_PWR_SET:
#ifdef PLATFORM_HAS_POWER_SWITCH
		platform_target_set_power(packet[2]=='1');
		_respond(REMOTE_RESP_OK,0);
#else
		_respond(REMOTE_RESP_NOTSUP,0);
#endif
		break;

    case REMOTE_PWR_GET:
#ifdef PLATFORM_HAS_POWER_SWITCH
		_respond(REMOTE_RESP_OK,platform_target_get_power());
#else
		_respond(REMOTE_RESP_NOTSUP,0);
#endif
		break;

#if !defined(BOARD_IDENT) && defined(PLATFORM_IDENT)
# define BOARD_IDENT PLATFORM_IDENT
#endif
	case REMOTE_START:
		_respondS(REMOTE_RESP_OK, BOARD_IDENT " " FIRMWARE_VERSION);
		break;

    default:
		_respond(REMOTE_RESP_ERR,REMOTE_ERROR_UNRECOGNISED);
		break;
    }
}

void remotePacketProcess(uint8_t i, char *packet)
{
	switch (packet[0]) {
    case REMOTE_SWDP_PACKET:
		remotePacketProcessSWD(i,packet);
		break;

    case REMOTE_JTAG_PACKET:
		remotePacketProcessJTAG(i,packet);
		break;

    case REMOTE_GEN_PACKET:
		remotePacketProcessGEN(i,packet);
		break;

    default: /* Oh dear, unrecognised, return an error */
		_respond(REMOTE_RESP_ERR,REMOTE_ERROR_UNRECOGNISED);
		break;
    }
}
#endif
