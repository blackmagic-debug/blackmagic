/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2021 Uwe Bonnes(bon@elektron.ikp.physik.tu-darmstadt.de)
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
#include "target.h"
#include "adiv5.h"

jtag_dev_t jtag_devs[JTAG_MAX_DEVS+1];
int jtag_dev_count;

/* bucket of ones for don't care TDI */
static const uint8_t ones[] = {0xff, 0xFF, 0xFF, 0xFF};

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
 *
 * https://www.fpga4fun.com/JTAG3.html
 * Count the number of devices in the JTAG chain
 *
 * shift enough ones in IR
 * shift enough zeros in DR
 * Now shift out ones and stop if first '1' is seen. This gets the number
 * of devices
 *
 * Assume max 32 devices with max IR len 16 = 512 bits = 16 loops * 32 bit
 *
 * Reset the TAP state machine again. This should load all IRs with IDCODE.
 * Read 32 bit IDCODE for all devices.
  */

int jtag_scan(const uint8_t *irlens)
{
	int i;
	void (*jd_handlers[JTAG_MAX_DEVS])(jtag_dev_t *jd);
	target_list_free();

	memset(jd_handlers, 0, sizeof(jd_handlers));

	/* Run throught the SWD to JTAG sequence for the case where an
	 * attached SWJ-DP is in SW-DP mode.
	 */
#if PC_HOSTED == 1
	if (platform_jtagtap_init()) {
		DEBUG_WARN("JTAG not available\n");
		return 0;
	}
#else
	jtagtap_init();
#endif
	jtag_proc.jtagtap_reset();
#define LOOPS 16
	jtagtap_shift_ir();
	i = LOOPS;
	uint8_t ir_chain[64], *din = ir_chain;
	while (i--) {
		jtag_proc.jtagtap_tdi_tdo_seq(din, (i == 0) ? 1 : 0, ones,
									  sizeof(ones) * 8);
		din += sizeof(ones);
	}
	if (!(ir_chain[0] & 1)) {
		DEBUG_WARN("Unexpected IR chain!\n");
		return 0;
	}
	jtagtap_return_idle();
	jtagtap_shift_dr();
	i = LOOPS;
	uint8_t zeros[] = {0, 0, 0, 0};
	while(i--) {
		jtag_proc.jtagtap_tdi_seq(0, zeros, sizeof(zeros) * 8);
	}
	int num_devices = 0;
	while (!jtag_proc.jtagtap_next(0,1) && (i++ < 6))
			num_devices++;
	jtag_proc.jtagtap_reset();
	jtagtap_shift_dr();
	jtag_dev_count = num_devices;
	if (!num_devices)
		return 0;
	DEBUG_TARGET("Found %d devices\n", num_devices);
	int irbit = 1;
	int j = 0;
	for (i = 0; i < num_devices; i++) {
		uint8_t id[4];
		jtag_proc.jtagtap_tdi_tdo_seq(id, 0, ones, 32);
		if (!(id[0] & 1)) {
			DEBUG_WARN("Invalid IDCode!\n");
			return 0;
		}
		uint32_t idcode = id[3] << 24 | id[2] << 16 |  id[1] << 8 | id[0];
		unsigned int designer = ((id[1] & 0xf) << 8) | (id[0] >> 1);
		unsigned int product = id[2] | ((id[3] & 0xf) << 8);
		unsigned int expected_irlen = 0;
		switch (designer) {
		case AP_DESIGNER_ARM:
			switch (product) {
			case 0xba0:
				jtag_devs[i].jd_descr = "ADIv5 JTAG-DP port";
				jd_handlers[i] = adiv5_jtag_dp_handler;
				expected_irlen = 4;
				break;
			default:
				jtag_devs[i].jd_descr = "ARM";
			}
			break;
		case AP_DESIGNER_STM:
			expected_irlen = 5;
			jtag_devs[i].jd_descr = "STM32 BSD";
			break;
		case AP_DESIGNER_ATMEL:
			if ((product >= 0x940) & (product < 0x990)) {
				jtag_devs[i].jd_descr = "ATMEL AVR8";
				expected_irlen = 4;
				break;
			}
			jtag_devs[i].jd_descr = "ATMEL";
			break;
		case DESIGNER_XILINX:
			if (!irlens) {
				/* Guessed irlen for XILINX devices is wrong.
				 * IR data contains status bits!
				 */
				DEBUG_WARN("Please provide irlens as chain contains XILINX devices!\n");
				return 0;
			}
			jtag_devs[i].jd_descr = "XILINX";
			break;
		case DESIGNER_XAMBALA:
			expected_irlen = 5;
			jtag_devs[i].jd_descr = "RVDBG013";
			break;
		case AP_DESIGNER_GIGADEVICE:
			expected_irlen = 5;
			jtag_devs[i].jd_descr = "GIGADEVICE BSD";
			break;
		}
		if (!jtag_devs[i].jd_descr) {
			DEBUG_WARN("Unhandled designer %x\n", designer);
			jtag_devs[i].jd_descr = "Unknow";
		}
		bool bit;
		int guessed_irlen = 0;
		int advance = irbit;
		do {
			/* Guess IR length from the IR scan after JTAG Reset
			 * First bit should be '1', following bits are '0', if not used
			 * for instruction capture, as for Xilinx parts.
			 */
			bit = (ir_chain[advance / 8] & (1 << (advance & 7)));
			guessed_irlen++;
			advance++;
		} while (!bit && (advance < (JTAG_MAX_DEVS * 16)));
		if (irlens) { /* Allow to overwrite from the command line!*/
			if (*irlens != guessed_irlen) {
				DEBUG_TARGET("Provides irlen %d vs guessed %d for device %d\n",
							 *irlens, guessed_irlen, i + 1);
			}
			expected_irlen = *irlens++;
		}
		if (!expected_irlen) {
			expected_irlen = guessed_irlen++;
		}
		jtag_devs[i].ir_len = expected_irlen;
		jtag_devs[i].ir_prescan = j;
		jtag_devs[i].jd_dev = i;
		jtag_devs[i].jd_idcode = idcode;
		jtag_devs[i].dr_postscan = jtag_dev_count - i - 1;
		jtag_devs[i].current_ir = -1;
		j += expected_irlen;
		irbit += expected_irlen;
		DEBUG_INFO("%2d: IDCODE: 0x%08" PRIx32 ", IR len %d %s%s\n", i + 1,
				   idcode,jtag_devs[i].ir_len, jtag_devs[i].jd_descr,
				   (jd_handlers[i]) ? "" : " (Unhandled) ");
	}
	jtag_proc.jtagtap_reset();
	/* Fill in the ir_postscan fields */
	for(i = jtag_dev_count - 1; i; i--) {
		jtag_devs[i-1].ir_postscan = jtag_devs[i].ir_postscan +
					jtag_devs[i].ir_len;
	}
#if PC_HOSTED == 1
	/*Transfer needed device information to firmware jtag_devs*/
	for(i = 0; i < jtag_dev_count; i++) {
		platform_add_jtag_dev(i, &jtag_devs[i]);
	}
#endif
	/* Check for known devices and handle accordingly */
	for(i = 0; i < jtag_dev_count; i++)
		/* Call handler to initialise/probe device further */
		if (jd_handlers[i])
			jd_handlers[i](&jtag_devs[i]);
	return jtag_dev_count;
}

void fw_jtag_dev_shift_ir(jtag_proc_t *jp, uint8_t jd_index, uint32_t ir,
	uint32_t *ir_out)
{
	uint8_t dout[4];
	jtag_dev_t *d = &jtag_devs[jd_index];
	if ((!ir_out) && (ir == d->current_ir))
		return;
	for(int i = 0; i < jtag_dev_count; i++)
		jtag_devs[i].current_ir = -1;
	d->current_ir = ir;

	jtagtap_shift_ir();
	jp->jtagtap_tdi_seq(0, ones, d->ir_prescan);
	jp->jtagtap_tdi_tdo_seq(dout, d->ir_postscan?0:1, (void*)&ir, d->ir_len);
	jp->jtagtap_tdi_seq(1, ones, d->ir_postscan);
	jtagtap_return_idle();
	if (ir_out)
		*ir_out = dout[0] | (dout[1] << 8) | (dout[2] << 16) | (dout[3] << 24);
}

void jtag_dev_shift_ir(jtag_proc_t *jp, uint8_t jd_index, uint32_t ir,
	uint32_t *ir_out)
{
	return jp->dev_shift_ir(jp, jd_index, ir, ir_out);
}

void fw_jtag_dev_shift_dr(jtag_proc_t *jp, uint8_t jd_index, uint8_t *dout, const uint8_t *din, int ticks)
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

void jtag_dev_shift_dr(jtag_proc_t *jp, uint8_t jd_index, uint8_t *dout, const uint8_t *din, int ticks)
{
	return jp->dev_shift_dr(jp, jd_index, dout, din, ticks);
}
