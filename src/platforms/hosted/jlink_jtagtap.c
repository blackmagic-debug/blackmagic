/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Uwe Bonnes bon@elektron.ikp.physik.tu-darmstadt.de
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

/* Low level JTAG implementation using jlink.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <assert.h>

#include "general.h"
#include "exception.h"

#include "jlink.h"
#include "cl_utils.h"

static void jtagtap_reset(void)
{
	jtagtap_soft_reset();
}

static void jtagtap_tms_seq(uint32_t MS, int ticks)
{
	if (cl_debuglevel)
		printf("jtagtap_tms_seq 0x%08" PRIx32 ", ticks %d\n", MS, ticks);
	int len = (ticks + 7) / 8;
	uint8_t cmd[12];
	cmd[0] = CMD_HW_JTAG3;
	cmd[1] = 0;
	cmd[2] = ticks;
	cmd[3] = 0;
	uint8_t *tms = cmd + 4;
	for (int i = 0; i < len; i++) {
		*tms = MS & 0xff;
		*(tms + len) = *tms;
		tms++;
		MS >>= 8;
	}
	uint8_t res[4];
	send_recv(info.usb_link, cmd, 4 + 2 * len, res, len);
	send_recv(info.usb_link, NULL, 0, res, 1);
	if (res[0] != 0)
		raise_exception(EXCEPTION_ERROR, "tagtap_tms_seq failed");
}

static void jtagtap_tdi_tdo_seq(uint8_t *DO, const uint8_t final_tms,
						 const uint8_t *DI, int ticks)
{
	if (!ticks)
		return;
	int len = (ticks + 7) / 8;
	if (cl_debuglevel) {
		printf("jtagtap_tdi_tdo %s, ticks %d, DI: ",
			   (final_tms) ? "Final TMS" : "", ticks);
		for (int i = 0; i < len; i++) {
			printf("%02x", DI[i]);
		}
		printf("\n");
	}
	uint8_t *cmd = alloca(4 + 2 * len);
	cmd[0] = CMD_HW_JTAG3;
	cmd[1] = 0;
	cmd[2] = ticks;
	cmd[3] = 0;
	uint8_t *tms = cmd + 4;
	for (int i = 0; i < len; i++)
		*tms++ = 0;
	if (final_tms)
		cmd[4 + (ticks - 1) / 8] |= (1 << ((ticks - 1) % 8));
	uint8_t *tdi = tms;
	if (DI)
		for (int i = 0; i < len; i++)
			*tdi++ = DI[i];
	if (DO)
		send_recv(info.usb_link, cmd, 4 + 2 * len, DO, len);
	else
		send_recv(info.usb_link, cmd, 4 + 2 * len, cmd, len);
	uint8_t res[1];
	send_recv(info.usb_link, NULL, 0, res, 1);
	if (res[0] != 0)
		raise_exception(EXCEPTION_ERROR, "jtagtap_tdi_tdi failed");
}

static void jtagtap_tdi_seq(const uint8_t final_tms, const uint8_t *DI,
							int ticks)
{
	return jtagtap_tdi_tdo_seq(NULL,  final_tms, DI, ticks);
}

static uint8_t jtagtap_next(uint8_t dTMS, uint8_t dTDI)
{
	if (cl_debuglevel)
		printf("jtagtap_next TMS 0x%02x, TDI %02x\n", dTMS, dTDI);
	uint8_t cmd[6];
	cmd[0] = CMD_HW_JTAG3;
	cmd[1] = 0;
	cmd[2] = 1;
	cmd[3] = 0;
	cmd[4] = (dTMS) ? 0xff : 0;
	cmd[5] = (dTDI) ? 0xff : 0;
	uint8_t ret[1];
	send_recv(info.usb_link, cmd, 6, ret, 1);
	uint8_t res[1];
	send_recv(info.usb_link, NULL, 0, res, 1);
	if (res[0] != 0)
		raise_exception(EXCEPTION_ERROR, "jtagtap_next failed");
	return (ret[0] & 1);
}

int jlink_jtagtap_init(bmp_info_t *info, jtag_proc_t *jtag_proc)
{
	if (cl_debuglevel)
		printf("jtap_init\n");
	uint8_t cmd_switch[2] = {CMD_GET_SELECT_IF, JLINK_IF_GET_AVAILABLE};
	uint8_t res[4];
	send_recv(info->usb_link, cmd_switch, 2, res, sizeof(res));
	if (!(res[0] & JLINK_IF_JTAG)) {
		fprintf(stderr, "JTAG not available\n");
		return -1;
	}
	cmd_switch[1] = SELECT_IF_JTAG;
	send_recv(info->usb_link, cmd_switch, 2, res, sizeof(res));
	platform_delay(10);
	/* Set speed 256 kHz*/
	unsigned int speed = 2000;
	uint8_t jtag_speed[3] = {5, speed & 0xff, speed >> 8};
	send_recv(info->usb_link, jtag_speed, 3, NULL, 0);
	uint8_t cmd[44];
	cmd[0]  = CMD_HW_JTAG3;
	cmd[1]  = 0;
	/* write 8 Bytes.*/
	cmd[2]  = 9 * 8;
	cmd[3]  = 0;
	uint8_t *tms = cmd + 4;
	tms[0]  = 0xff;
	tms[1]  = 0xff;
	tms[2]  = 0xff;
	tms[3]  = 0xff;
	tms[4]  = 0xff;
	tms[5]  = 0xff;
	tms[6]  = 0xff;
	tms[7]  = 0x3c;
	tms[8]  = 0xe7;
	send_recv(info->usb_link, cmd, 4 + 2 * 9, cmd, 9);
	send_recv(info->usb_link, NULL, 0, res,  1);

	if (res[0] != 0) {
		fprintf(stderr, "Switch to JTAGt failed\n");
		return 0;
	}
	jtag_proc->jtagtap_reset = jtagtap_reset;
	jtag_proc->jtagtap_next =jtagtap_next;
	jtag_proc->jtagtap_tms_seq = jtagtap_tms_seq;
	jtag_proc->jtagtap_tdi_tdo_seq = jtagtap_tdi_tdo_seq;
	jtag_proc->jtagtap_tdi_seq = jtagtap_tdi_seq;
	return 0;
}
