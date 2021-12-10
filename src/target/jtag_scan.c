/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022  1bitsquared - Rachel Mant <git@dragonmux.network>
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
#include "jtag_devs.h"

struct jtag_dev_s jtag_devs[JTAG_MAX_DEVS+1];
int jtag_dev_count;

/* bucket of ones for don't care TDI */
static const uint8_t ones[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#if PC_HOSTED == 0
void jtag_add_device(const int dev_index, const jtag_dev_t *jtag_dev)
{
	if (dev_index == 0)
		memset(&jtag_devs, 0, sizeof(jtag_devs));
	memcpy(&jtag_devs[dev_index], jtag_dev, sizeof(jtag_dev_t));
	jtag_dev_count = dev_index + 1;
}
#endif

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
	DEBUG_INFO("Resetting TAP\n");
#if PC_HOSTED == 1
	if (platform_jtagtap_init()) {
		DEBUG_WARN("JTAG not available\n");
		return 0;
	}
#else
	jtagtap_init();
#endif
	jtag_proc.jtagtap_reset();

	if (irlens) {
		DEBUG_WARN("Given list of IR lengths, skipping probe\n");
		DEBUG_INFO("Change state to Shift-IR\n");
		jtagtap_shift_ir();
		j = 0;
		while((jtag_dev_count <= JTAG_MAX_DEVS) &&
		      (jtag_devs[jtag_dev_count].ir_len <= JTAG_MAX_IR_LEN)) {
			uint32_t irout;
			if(*irlens == 0)
				break;
			jtag_proc.jtagtap_tdi_tdo_seq((uint8_t*)&irout, 0, ones, *irlens);
			if (!(irout & 1)) {
				DEBUG_WARN("check failed: IR[0] != 1\n");
				return -1;
			}
			jtag_devs[jtag_dev_count].ir_len = *irlens;
			jtag_devs[jtag_dev_count].ir_prescan = j;
			jtag_devs[jtag_dev_count].jd_dev = jtag_dev_count;
			j += *irlens;
			irlens++;
			jtag_dev_count++;
		}
	} else {
		DEBUG_INFO("Change state to Shift-IR\n");
		jtagtap_shift_ir();

		DEBUG_INFO("Scanning out IRs\n");
		if(!jtag_proc.jtagtap_next(0, 1)) {
			DEBUG_WARN("jtag_scan: Sanity check failed: IR[0] shifted out "
					   "as 0\n");
			jtag_dev_count = -1;
			return -1; /* must be 1 */
		}
		jtag_devs[0].ir_len = 1; j = 1;
		while((jtag_dev_count <= JTAG_MAX_DEVS) &&
		      (jtag_devs[jtag_dev_count].ir_len <= JTAG_MAX_IR_LEN)) {
			if(jtag_proc.jtagtap_next(0, 1)) {
				if(jtag_devs[jtag_dev_count].ir_len == 1) break;
				jtag_devs[++jtag_dev_count].ir_len = 1;
				jtag_devs[jtag_dev_count].ir_prescan = j;
				jtag_devs[jtag_dev_count].jd_dev = jtag_dev_count;
			} else jtag_devs[jtag_dev_count].ir_len++;
			j++;
		}
		if(jtag_dev_count > JTAG_MAX_DEVS) {
			DEBUG_WARN("jtag_scan: Maximum device count exceeded\n");
			jtag_dev_count = -1;
			return -1;
		}
		if(jtag_devs[jtag_dev_count].ir_len > JTAG_MAX_IR_LEN) {
			DEBUG_WARN("jtag_scan: Maximum IR length exceeded\n");
			jtag_dev_count = -1;
			return -1;
		}
	}

	DEBUG_INFO("Return to Run-Test/Idle\n");
	jtag_proc.jtagtap_next(1, 1);
	jtagtap_return_idle();

	/* All devices should be in BYPASS now */

	/* Count device on chain */
	DEBUG_INFO("Change state to Shift-DR\n");
	jtagtap_shift_dr();
	for(i = 0; (jtag_proc.jtagtap_next(0, 1) == 0) && (i <= jtag_dev_count); i++)
		jtag_devs[i].dr_postscan = jtag_dev_count - i - 1;

	if(i != jtag_dev_count) {
		DEBUG_WARN("jtag_scan: Sanity check failed: "
			"BYPASS dev count doesn't match IR scan\n");
		jtag_dev_count = -1;
		return -1;
	}

	DEBUG_INFO("Return to Run-Test/Idle\n");
	jtag_proc.jtagtap_next(1, 1);
	jtagtap_return_idle();
	if(!jtag_dev_count) {
		return 0;
	}

	/* Fill in the ir_postscan fields */
	for(i = jtag_dev_count - 1; i; i--)
		jtag_devs[i-1].ir_postscan = jtag_devs[i].ir_postscan +
					jtag_devs[i].ir_len;

	/* Reset jtagtap: should take all devs to IDCODE */
	jtag_proc.jtagtap_reset();
	jtagtap_shift_dr();
	for(i = 0; i < jtag_dev_count; i++) {
		if(!jtag_proc.jtagtap_next(0, 1)) continue;
		jtag_devs[i].jd_idcode = 1;
		for(j = 2; j; j <<= 1)
			if(jtag_proc.jtagtap_next(0, 1)) jtag_devs[i].jd_idcode |= j;

	}
	DEBUG_INFO("Return to Run-Test/Idle\n");
	jtag_proc.jtagtap_next(1, 1);
	jtagtap_return_idle();
#if PC_HOSTED == 1
	/*Transfer needed device information to firmware jtag_devs*/
	for(i = 0; i < jtag_dev_count; i++)
		platform_add_jtag_dev(i, &jtag_devs[i]);
	for(i = 0; i < jtag_dev_count; i++) {
		DEBUG_INFO("Idcode 0x%08" PRIx32, jtag_devs[i].jd_idcode);
		for(j = 0; dev_descr[j].idcode; j++) {
			if((jtag_devs[i].jd_idcode & dev_descr[j].idmask) ==
			   dev_descr[j].idcode) {
				DEBUG_INFO(": %s",
						  (dev_descr[j].descr) ? dev_descr[j].descr : "unknown");
				break;
			}
		}
		DEBUG_INFO("\n");
	}
#endif

	/* Check for known devices and handle accordingly */
	for(i = 0; i < jtag_dev_count; i++) {
		for(j = 0; dev_descr[j].idcode; j++) {
			if((jtag_devs[i].jd_idcode & dev_descr[j].idmask) ==
			   dev_descr[j].idcode) {
				jtag_devs[i].current_ir = -1;
				/* Save description in table */
				jtag_devs[i].jd_descr = dev_descr[j].descr;
				/* Call handler to initialise/probe device further */
				if(dev_descr[j].handler)
					dev_descr[j].handler(i);
				break;
			}
		}
	}

	return jtag_dev_count;
}

void jtag_dev_write_ir(jtag_proc_t *jp, uint8_t jd_index, uint32_t ir)
{
	jtag_dev_t *d = &jtag_devs[jd_index];
	if(ir == d->current_ir) return;
	for(int i = 0; i < jtag_dev_count; i++)
		jtag_devs[i].current_ir = -1;
	d->current_ir = ir;

	jtagtap_shift_ir();
	jp->jtagtap_tdi_seq(0, ones, d->ir_prescan);
	jp->jtagtap_tdi_seq(d->ir_postscan?0:1, (void*)&ir, d->ir_len);
	jp->jtagtap_tdi_seq(1, ones, d->ir_postscan);
	jtagtap_return_idle();
}

void jtag_dev_shift_dr(jtag_proc_t *jp, uint8_t jd_index, uint8_t *dout, const uint8_t *din, int ticks)
{
	jtag_dev_t *d = &jtag_devs[jd_index];
	jtagtap_shift_dr();
	jp->jtagtap_tdi_seq(0, ones, d->dr_prescan);
	if(dout)
		jp->jtagtap_tdi_tdo_seq((void*)dout, d->dr_postscan?0:1, (void*)din, ticks);
	else
		jp->jtagtap_tdi_seq(d->dr_postscan?0:1, (void*)din, ticks);
	jp->jtagtap_tdi_seq(1, ones, d->dr_postscan);
	jtagtap_return_idle();
}

