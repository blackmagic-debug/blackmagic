/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

/* This file implements JTAG protocol support.  Provides functionality
 * to detect devices on the scan chain and read their IDCODEs.
 * It depends on the low-level function provided by the platform's jtagtap.c.
 */

#include "general.h"
#include "jtagtap.h"
#include "jtag_scan.h"
#include "target.h"
#include "adiv5.h"

struct jtag_dev_s jtag_devs[JTAG_MAX_DEVS+1];
int jtag_dev_count;

static const struct jtag_dev_descr_s {
	const uint32_t idcode;
	const uint32_t idmask;
	const char * const descr;
	void (*const handler)(jtag_dev_t *dev);
} dev_descr[] = {
	{.idcode = 0x0BA00477, .idmask = 0x0FFF0FFF,
		.descr = "ARM Limited: ADIv5 JTAG-DP port.",
		.handler = adiv5_jtag_dp_handler},
	{.idcode = 0x06410041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32, Medium density."},
	{.idcode = 0x06412041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32, Low density."},
	{.idcode = 0x06414041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32, High density."},
	{.idcode = 0x06416041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32L."},
	{.idcode = 0x06418041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32, Connectivity Line."},
	{.idcode = 0x06420041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32, Value Line."},
	{.idcode = 0x06428041, .idmask = 0x0FFFFFFF,
		.descr = "ST Microelectronics: STM32, Value Line, High density."},
	{.idcode = 0x06411041, .idmask = 0xFFFFFFFF,
		.descr = "ST Microelectronics: STM32F2xx."},
	{.idcode = 0x06413041 , .idmask = 0xFFFFFFFF,
		.descr = "ST Microelectronics: STM32F4xx."},
	{.idcode = 0x0BB11477 , .idmask = 0xFFFFFFFF,
		.descr = "NPX: LPC11C24."},
	{.idcode = 0x4BA00477 , .idmask = 0xFFFFFFFF,
		.descr = "NXP: LPC17xx family."},
/* Just for fun, unsupported */
	{.idcode = 0x8940303F, .idmask = 0xFFFFFFFF, .descr = "ATMEL: ATMega16."},
	{.idcode = 0x0792603F, .idmask = 0xFFFFFFFF, .descr = "ATMEL: AT91SAM9261."},
	{.idcode = 0x20270013, .idmask = 0xFFFFFFFF, .descr = "Intel: i80386ex."},
	{.idcode = 0x07B7617F, .idmask = 0xFFFFFFFF, .descr = "Broadcom: BCM2835."},
	{.idcode = 0x4BA00477, .idmask = 0xFFFFFFFF, .descr = "Broadcom: BCM2836."},
	{.idcode = 0, .idmask = 0, .descr = "Unknown"},
};

/* bucket of ones for don't care TDI */
static const uint8_t ones[] = "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF";

/* Scan JTAG chain for devices, store IR length and IDCODE (if present).
 * Reset TAP state machine.
 * Select Shift-IR state.
 * Each device is assumed to shift out IR at 0x01. (this may not always be true)
 * Shift in ones until we read two consecutive ones, then we have shifted out the
 * 	IRs of all devices.
 *
 * After this process all the IRs are loaded with the BYPASS command.
 * Select Shift-DR state.
 * Shift in ones and count zeros shifted out. Should be one for each device.
 * Check this against device count obtained by IR scan above.
 *
 * Reset the TAP state machine again. This should load all IRs with IDCODE.
 * For each device, shift out one bit. If this is zero IDCODE isn't present,
 *	continue to next device. If this is one shift out the remaining 31 bits
 *	of the IDCODE register.
 */
int jtag_scan(const uint8_t *irlens)
{
	int i;
	uint32_t j;

	target_list_free();

	jtag_dev_count = 0;
	memset(&jtag_devs, 0, sizeof(jtag_devs));

	/* Run throught the SWD to JTAG sequence for the case where an attached SWJ-DP is
	 * in SW-DP mode.
	 */
	DEBUG("Resetting TAP\n");
	jtagtap_init();
	jtagtap_reset();

	if (irlens) {
		DEBUG("Given list of IR lengths, skipping probe\n");
		DEBUG("Change state to Shift-IR\n");
		jtagtap_shift_ir();
		j = 0;
		while((jtag_dev_count <= JTAG_MAX_DEVS) &&
		      (jtag_devs[jtag_dev_count].ir_len <= JTAG_MAX_IR_LEN)) {
			uint32_t irout;
			if(*irlens == 0)
				break;
			jtagtap_tdi_tdo_seq((uint8_t*)&irout, 0, ones, *irlens);
			if (!(irout & 1)) {
				DEBUG("check failed: IR[0] != 1\n");
				return -1;
			}
			jtag_devs[jtag_dev_count].ir_len = *irlens;
			jtag_devs[jtag_dev_count].ir_prescan = j;
			jtag_devs[jtag_dev_count].dev = jtag_dev_count;
			j += *irlens;
			irlens++;
			jtag_dev_count++;
		}
	} else {
		DEBUG("Change state to Shift-IR\n");
		jtagtap_shift_ir();

		DEBUG("Scanning out IRs\n");
		if(!jtagtap_next(0, 1)) {
			DEBUG("jtag_scan: Sanity check failed: IR[0] shifted out as 0\n");
			jtag_dev_count = -1;
			return -1; /* must be 1 */
		}
		jtag_devs[0].ir_len = 1; j = 1;
		while((jtag_dev_count <= JTAG_MAX_DEVS) &&
		      (jtag_devs[jtag_dev_count].ir_len <= JTAG_MAX_IR_LEN)) {
			if(jtagtap_next(0, 1)) {
				if(jtag_devs[jtag_dev_count].ir_len == 1) break;
				jtag_devs[++jtag_dev_count].ir_len = 1;
				jtag_devs[jtag_dev_count].ir_prescan = j;
				jtag_devs[jtag_dev_count].dev = jtag_dev_count;
			} else jtag_devs[jtag_dev_count].ir_len++;
			j++;
		}
		if(jtag_dev_count > JTAG_MAX_DEVS) {
			DEBUG("jtag_scan: Maximum device count exceeded\n");
			jtag_dev_count = -1;
			return -1;
		}
		if(jtag_devs[jtag_dev_count].ir_len > JTAG_MAX_IR_LEN) {
			DEBUG("jtag_scan: Maximum IR length exceeded\n");
			jtag_dev_count = -1;
			return -1;
		}
	}

	DEBUG("Return to Run-Test/Idle\n");
	jtagtap_next(1, 1);
	jtagtap_return_idle();

	/* All devices should be in BYPASS now */

	/* Count device on chain */
	DEBUG("Change state to Shift-DR\n");
	jtagtap_shift_dr();
	for(i = 0; (jtagtap_next(0, 1) == 0) && (i <= jtag_dev_count); i++)
		jtag_devs[i].dr_postscan = jtag_dev_count - i - 1;

	if(i != jtag_dev_count) {
		DEBUG("jtag_scan: Sanity check failed: "
			"BYPASS dev count doesn't match IR scan\n");
		jtag_dev_count = -1;
		return -1;
	}

	DEBUG("Return to Run-Test/Idle\n");
	jtagtap_next(1, 1);
	jtagtap_return_idle();
	if(!jtag_dev_count) {
		return 0;
	}

	/* Fill in the ir_postscan fields */
	for(i = jtag_dev_count - 1; i; i--)
		jtag_devs[i-1].ir_postscan = jtag_devs[i].ir_postscan +
					jtag_devs[i].ir_len;

	/* Reset jtagtap: should take all devs to IDCODE */
	jtagtap_reset();
	jtagtap_shift_dr();
	for(i = 0; i < jtag_dev_count; i++) {
		if(!jtagtap_next(0, 1)) continue;
		jtag_devs[i].idcode = 1;
		for(j = 2; j; j <<= 1)
			if(jtagtap_next(0, 1)) jtag_devs[i].idcode |= j;

	}
	DEBUG("Return to Run-Test/Idle\n");
	jtagtap_next(1, 1);
	jtagtap_return_idle();

	/* Check for known devices and handle accordingly */
	for(i = 0; i < jtag_dev_count; i++)
		for(j = 0; dev_descr[j].idcode; j++)
			if((jtag_devs[i].idcode & dev_descr[j].idmask) ==
			   dev_descr[j].idcode) {
				jtag_devs[i].current_ir = -1;
				/* Save description in table */
				jtag_devs[i].descr = dev_descr[j].descr;
				/* Call handler to initialise/probe device further */
				if(dev_descr[j].handler)
					dev_descr[j].handler(&jtag_devs[i]);
				break;
			}

	return jtag_dev_count;
}

void jtag_dev_write_ir(jtag_dev_t *d, uint32_t ir)
{
	if(ir == d->current_ir) return;
	for(int i = 0; i < jtag_dev_count; i++)
		jtag_devs[i].current_ir = -1;
	d->current_ir = ir;

	jtagtap_shift_ir();
	jtagtap_tdi_seq(0, ones, d->ir_prescan);
	jtagtap_tdi_seq(d->ir_postscan?0:1, (void*)&ir, d->ir_len);
	jtagtap_tdi_seq(1, ones, d->ir_postscan);
	jtagtap_return_idle();
}

void jtag_dev_shift_dr(jtag_dev_t *d, uint8_t *dout, const uint8_t *din, int ticks)
{
	jtagtap_shift_dr();
	jtagtap_tdi_seq(0, ones, d->dr_prescan);
	if(dout)
		jtagtap_tdi_tdo_seq((void*)dout, d->dr_postscan?0:1, (void*)din, ticks);
	else
		jtagtap_tdi_seq(d->dr_postscan?0:1, (void*)din, ticks);
	jtagtap_tdi_seq(1, ones, d->dr_postscan);
	jtagtap_return_idle();
}

