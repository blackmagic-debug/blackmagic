/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019  Black Sphere Technologies Ltd.
 * Written by Dave Marples <dave@marples.net>
 * Modified 2020 - 2021 by Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
#include "jtagtap.h"
#include "gdb_if.h"
#include "version.h"
#include "exception.h"
#include <stdarg.h>
#include "target/adiv5.h"
#include "target.h"
#include "hex_utils.h"

#define NTOH(x)    (((x) <= 9) ? (x) + '0' : 'a' + (x)-10)
#define HTON(x)    (((x) <= '9') ? (x) - '0' : ((TOUPPER(x)) - 'A' + 10))
#define TOUPPER(x) ((((x) >= 'a') && ((x) <= 'z')) ? ((x) - ('a' - 'A')) : (x))
#define ISHEX(x)   ((((x) >= '0') && ((x) <= '9')) || (((x) >= 'A') && ((x) <= 'F')) || (((x) >= 'a') && ((x) <= 'f')))

/* Return numeric version of string, until illegal hex digit, or max */
uint64_t remotehston(const uint32_t max, const char *const str)
{
	uint64_t ret = 0;
	for (size_t i = 0; i < max; ++i) {
		const char value = str[i];
		if (!ISHEX(value))
			return ret;
		ret = (ret << 4U) | HTON(value);
	}
	return ret;
}

#if PC_HOSTED == 0
static void remote_send_buf(uint8_t *buffer, size_t len)
{
	uint8_t *p = buffer;
	char hex[2];
	do {
		hexify(hex, (const void *)p++, 1);

		gdb_if_putchar(hex[0], 0);
		gdb_if_putchar(hex[1], 0);

	} while (p < (buffer + len));
}

static void remote_respond_buf(char respCode, uint8_t *buffer, size_t len)
{
	gdb_if_putchar(REMOTE_RESP, 0);
	gdb_if_putchar(respCode, 0);

	remote_send_buf(buffer, len);

	gdb_if_putchar(REMOTE_EOM, 1);
}

/* Send response to far end */
static void remote_respond(char respCode, uint64_t param)
{
	char buf[35]; /*Response, code, EOM and 2*16 hex nibbles*/
	char *p = buf;

	gdb_if_putchar(REMOTE_RESP, 0);
	gdb_if_putchar(respCode, 0);

	do {
		*p++ = NTOH((param & 0x0f));
		param >>= 4;
	} while (param);

	/* At this point the number to print is the buf, but backwards, so spool it out */
	do {
		gdb_if_putchar(*--p, 0);
	} while (p > buf);
	gdb_if_putchar(REMOTE_EOM, 1);
}

static void remote_respond_string(char respCode, const char *s)
/* Send response to far end */
{
	gdb_if_putchar(REMOTE_RESP, 0);
	gdb_if_putchar(respCode, 0);
	while (*s) {
		/* Just clobber illegal characters so they don't disturb the protocol */
		if ((*s == '$') || (*s == REMOTE_SOM) || (*s == REMOTE_EOM))
			gdb_if_putchar(' ', 0);
		else
			gdb_if_putchar(*s, 0);
		s++;
	}
	gdb_if_putchar(REMOTE_EOM, 1);
}

static ADIv5_DP_t remote_dp = {
	.ap_read = firmware_ap_read,
	.ap_write = firmware_ap_write,
	.mem_read = firmware_mem_read,
	.mem_write_sized = firmware_mem_write_sized,
};

static void remote_packet_process_swd(unsigned i, char *packet)
{
	uint8_t ticks;
	uint32_t param;
	bool badParity;

	switch (packet[1]) {
	case REMOTE_INIT: /* SS = initialise =============================== */
		if (i == 2) {
			remote_dp.dp_read = firmware_swdp_read;
			remote_dp.low_access = firmware_swdp_low_access;
			remote_dp.abort = firmware_swdp_abort;
			swdptap_init(&remote_dp);
			remote_respond(REMOTE_RESP_OK, 0);
		} else {
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_WRONGLEN);
		}
		break;

	case REMOTE_IN_PAR: /* SI = In parity ============================= */
		ticks = remotehston(2, &packet[2]);
		badParity = remote_dp.seq_in_parity(&param, ticks);
		remote_respond(badParity ? REMOTE_RESP_PARERR : REMOTE_RESP_OK, param);
		break;

	case REMOTE_IN: /* Si = In ======================================= */
		ticks = remotehston(2, &packet[2]);
		param = remote_dp.seq_in(ticks);
		remote_respond(REMOTE_RESP_OK, param);
		break;

	case REMOTE_OUT: /* So= Out ====================================== */
		ticks = remotehston(2, &packet[2]);
		param = remotehston(-1, &packet[4]);
		remote_dp.seq_out(param, ticks);
		remote_respond(REMOTE_RESP_OK, 0);
		break;

	case REMOTE_OUT_PAR: /* SO = Out parity ========================== */
		ticks = remotehston(2, &packet[2]);
		param = remotehston(-1, &packet[4]);
		remote_dp.seq_out_parity(param, ticks);
		remote_respond(REMOTE_RESP_OK, 0);
		break;

	default:
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
}

static void remote_packet_process_jtag(unsigned i, char *packet)
{
	uint32_t MS;
	uint64_t DO = 0;
	size_t ticks;
	uint64_t DI = 0;
	jtag_dev_t jtag_dev;
	switch (packet[1]) {
	case REMOTE_INIT: /* JS = initialise ============================= */
		remote_dp.dp_read = fw_adiv5_jtagdp_read;
		remote_dp.low_access = fw_adiv5_jtagdp_low_access;
		remote_dp.abort = adiv5_jtagdp_abort;
		jtagtap_init();
		remote_respond(REMOTE_RESP_OK, 0);
		break;

	case REMOTE_RESET: /* JR = reset ================================= */
		jtag_proc.jtagtap_reset();
		remote_respond(REMOTE_RESP_OK, 0);
		break;

	case REMOTE_TMS: /* JT = TMS Sequence ============================ */
		ticks = remotehston(2, &packet[2]);
		MS = remotehston(2, &packet[4]);

		if (i < 4) {
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_WRONGLEN);
		} else {
			jtag_proc.jtagtap_tms_seq(MS, ticks);
			remote_respond(REMOTE_RESP_OK, 0);
		}
		break;

	case REMOTE_CYCLE: { /* JC = clock cycle ============================ */
		ticks = remotehston(8, &packet[4]);
		const bool tms = packet[2] != '0';
		const bool tdi = packet[3] != '0';
		jtag_proc.jtagtap_cycle(tms, tdi, ticks);
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	}

	case REMOTE_TDITDO_TMS: /* JD = TDI/TDO  ========================================= */
	case REMOTE_TDITDO_NOTMS:

		if (i < 5) {
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_WRONGLEN);
		} else {
			ticks = remotehston(2, &packet[2]);
			DI = remotehston(-1, &packet[4]);
			const uint8_t *const data_in = (uint8_t *)&DI;
			uint8_t *data_out = (uint8_t *)&DO;
			jtag_proc.jtagtap_tdi_tdo_seq(data_out, packet[1] == REMOTE_TDITDO_TMS, data_in, ticks);

			remote_respond(REMOTE_RESP_OK, DO);
		}
		break;

	case REMOTE_NEXT: /* JN = NEXT ======================================== */
		if (i != 4)
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_WRONGLEN);
		else {
			const bool tdo = jtag_proc.jtagtap_next(packet[2] == '1', packet[3] == '1');
			remote_respond(REMOTE_RESP_OK, tdo ? 1U : 0U);
		}
		break;

	case REMOTE_ADD_JTAG_DEV: /* JJ = fill firmware jtag_devs */
		if (i < 22) {
			remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_WRONGLEN);
		} else {
			memset(&jtag_dev, 0, sizeof(jtag_dev));
			const uint32_t index = remotehston(2, &packet[2]);
			jtag_dev.dr_prescan = remotehston(2, &packet[4]);
			jtag_dev.dr_postscan = remotehston(2, &packet[6]);
			jtag_dev.ir_len = remotehston(2, &packet[8]);
			jtag_dev.ir_prescan = remotehston(2, &packet[10]);
			jtag_dev.ir_postscan = remotehston(2, &packet[12]);
			jtag_dev.current_ir = remotehston(8, &packet[14]);
			jtag_add_device(index, &jtag_dev);
			remote_respond(REMOTE_RESP_OK, 0);
		}
		break;

	default:
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
}

static void remotePacketProcessGEN(unsigned i, char *packet)
{
	(void)i;
	uint32_t freq;
	switch (packet[1]) {
	case REMOTE_VOLTAGE:
		remote_respond_string(REMOTE_RESP_OK, platform_target_voltage());
		break;

	case REMOTE_NRST_SET:
		platform_nrst_set_val(packet[2] == '1');
		remote_respond(REMOTE_RESP_OK, 0);
		break;

	case REMOTE_NRST_GET:
		remote_respond(REMOTE_RESP_OK, platform_nrst_get_val());
		break;
	case REMOTE_FREQ_SET:
		platform_max_frequency_set(remotehston(8, packet + 2));
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	case REMOTE_FREQ_GET:
		freq = platform_max_frequency_get();
		remote_respond_buf(REMOTE_RESP_OK, (uint8_t *)&freq, 4);
		break;

	case REMOTE_PWR_SET:
#ifdef PLATFORM_HAS_POWER_SWITCH
		if (packet[2] == '1' && !platform_target_get_power() &&
			platform_target_voltage_sense() > POWER_CONFLICT_THRESHOLD) {
			/* want to enable target power, but voltage > 0.5V sensed
			 * on the pin -> cancel
			 */
			remote_respond(REMOTE_RESP_ERR, 0);
		} else {
			platform_target_set_power(packet[2] == '1');
			remote_respond(REMOTE_RESP_OK, 0);
		}
#else
		remote_respond(REMOTE_RESP_NOTSUP, 0);
#endif
		break;

	case REMOTE_PWR_GET:
#ifdef PLATFORM_HAS_POWER_SWITCH
		remote_respond(REMOTE_RESP_OK, platform_target_get_power());
#else
		remote_respond(REMOTE_RESP_NOTSUP, 0);
#endif
		break;

#if !defined(BOARD_IDENT) && defined(BOARD_IDENT)
#define PLATFORM_IDENT() BOARD_IDENT
#endif
	case REMOTE_START:
		remote_respond_string(REMOTE_RESP_OK, PLATFORM_IDENT "" FIRMWARE_VERSION);
		break;

	case REMOTE_TARGET_CLK_OE:
		platform_target_clk_output_enable(packet[2] != '0');
		remote_respond(REMOTE_RESP_OK, 0);
		break;

	default:
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
}

static void remotePacketProcessHL(unsigned i, char *packet)

{
	(void)i;
	SET_IDLE_STATE(0);

	ADIv5_AP_t remote_ap;
	/* Re-use packet buffer. Align to DWORD! */
	void *src = (void *)(((uint32_t)packet + 7) & ~7);
	char index = packet[1];
	if (index == REMOTE_HL_CHECK) {
		remote_respond(REMOTE_RESP_OK, REMOTE_HL_VERSION);
		return;
	}
	packet += 2;
	remote_dp.dp_jd_index = remotehston(2, packet);
	packet += 2;
	remote_ap.apsel = remotehston(2, packet);
	remote_ap.dp = &remote_dp;
	switch (index) {
	case REMOTE_DP_READ: /* Hd = Read from DP register */
		packet += 2;
		uint16_t addr16 = remotehston(4, packet);
		uint32_t data = adiv5_dp_read(&remote_dp, addr16);
		remote_respond_buf(REMOTE_RESP_OK, (uint8_t *)&data, 4);
		break;
	case REMOTE_LOW_ACCESS: /* HL = Low level access */
		packet += 2;
		addr16 = remotehston(4, packet);
		packet += 4;
		uint32_t value = remotehston(8, packet);
		data = remote_dp.low_access(&remote_dp, remote_ap.apsel, addr16, value);
		remote_respond_buf(REMOTE_RESP_OK, (uint8_t *)&data, 4);
		break;
	case REMOTE_AP_READ: /* Ha = Read from AP register*/
		packet += 2;
		addr16 = remotehston(4, packet);
		data = adiv5_ap_read(&remote_ap, addr16);
		remote_respond_buf(REMOTE_RESP_OK, (uint8_t *)&data, 4);
		break;
	case REMOTE_AP_WRITE: /* Ha = Write to AP register*/
		packet += 2;
		addr16 = remotehston(4, packet);
		packet += 4;
		value = remotehston(8, packet);
		adiv5_ap_write(&remote_ap, addr16, value);
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	case REMOTE_AP_MEM_READ: /* HM = Read from Mem and set csw */
		packet += 2;
		remote_ap.csw = remotehston(8, packet);
		packet += 6;
		/*fall through*/
	case REMOTE_MEM_READ: /* Hh = Read from Mem */
		packet += 2;
		uint32_t address = remotehston(8, packet);
		packet += 8;
		uint32_t count = remotehston(8, packet);
		packet += 8;
		adiv5_mem_read(&remote_ap, src, address, count);
		if (remote_ap.dp->fault == 0) {
			remote_respond_buf(REMOTE_RESP_OK, src, count);
			break;
		}
		remote_respond(REMOTE_RESP_ERR, 0);
		remote_ap.dp->fault = 0;
		break;
	case REMOTE_AP_MEM_WRITE_SIZED: /* Hm = Write to memory and set csw */
		packet += 2;
		remote_ap.csw = remotehston(8, packet);
		packet += 6;
		/*fall through*/
	case REMOTE_MEM_WRITE_SIZED: /* HH = Write to memory*/
		packet += 2;
		enum align align = remotehston(2, packet);
		packet += 2;
		uint32_t dest = remotehston(8, packet);
		packet += 8;
		size_t len = remotehston(8, packet);
		packet += 8;
		if (len & ((1 << align) - 1)) {
			/* len  and align do not fit*/
			remote_respond(REMOTE_RESP_ERR, 0);
			break;
		}
		/* Read as stream of hexified bytes*/
		unhexify(src, packet, len);
		adiv5_mem_write_sized(&remote_ap, dest, src, len, align);
		if (remote_ap.dp->fault) {
			/* Errors handles on hosted side.*/
			remote_respond(REMOTE_RESP_ERR, 0);
			remote_ap.dp->fault = 0;
			break;
		}
		remote_respond(REMOTE_RESP_OK, 0);
		break;
	default:
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
	SET_IDLE_STATE(1);
}

void remotePacketProcess(unsigned i, char *packet)
{
	switch (packet[0]) {
	case REMOTE_SWDP_PACKET:
		remote_packet_process_swd(i, packet);
		break;

	case REMOTE_JTAG_PACKET:
		remote_packet_process_jtag(i, packet);
		break;

	case REMOTE_GEN_PACKET:
		remotePacketProcessGEN(i, packet);
		break;

	case REMOTE_HL_PACKET:
		remotePacketProcessHL(i, packet);
		break;

	default: /* Oh dear, unrecognised, return an error */
		remote_respond(REMOTE_RESP_ERR, REMOTE_ERROR_UNRECOGNISED);
		break;
	}
}
#endif
