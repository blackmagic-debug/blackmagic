/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
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

/* This file implements RVSWD protocol support. TODO */

#include "general.h"
// #include "jtagtap.h"
#include "rvswd_scan.h"

// #include "target.h"
// #include "adiv5.h"
// #include "jtag_devs.h"
// #include "gdb_packet.h"

/*
 * This scans the RVSWD interface for any possible device chain attached.
 * It acomplishes this by performing these basic steps:
 *
 * 1. Perform a SWD -> JTAG transition just in case any ARM devices were in SWD mode
 * 2. Reset the TAPs of any attached device (this ensures they're all in ID code mode)
 * 3. Read out the ID code register chain, shifting in all 1's, until we read an
 *    all-1's ID back (indicating the end of the chain)
 * 4. Read out the active instruction register chain, shifting in all 1's,
 *    and applying quirks as required to calculate how long each IR is
 * 5. Switch back to the DR chain and read out all the devices again now they are in
 *    BYPASS mode as a way to validate we have the chain length right
 *
 * Once this process is complete, all devices should be accounted for, the
 * device structures all set up with suitable pre- and post-scan values for both the
 * IR and DR chains, and all devices should be in BYPASS ready for additional probing
 * and inspection. Finally, we loop through seeing if we understand any of the
 * ID codes seen and dispatching to suitable handlers if we do.
 */
bool rvswd_scan(void)
{
	/* Free the device list if any, and clean state ready */
	target_list_free();

#if CONFIG_BMDA == 0
	rvswd_init();
#endif

	// platform_target_clk_output_enable(false);

	return false;
}

// static bool jtag_read_idcodes(void)
// {
// 	/* Reset the chain ready and transition to Shift-DR */
// 	jtag_proc.jtagtap_reset();
// 	DEBUG_INFO("Change state to Shift-DR\n");
// 	jtagtap_shift_dr();

// 	DEBUG_INFO("Scanning out ID codes\n");
// 	size_t device = 0;
// 	/* Try scanning to one ID past the end of the chain */
// 	for (; device <= JTAG_MAX_DEVS; ++device) {
// 		/* Try to read out 32 bits, while shifting in 1's */
// 		uint32_t idcode = 0;
// 		jtag_proc.jtagtap_tdi_tdo_seq((uint8_t *)&idcode, false, ones, 32);
// 		/* If the IDCode read is all 1's, we've reached the end */
// 		if (idcode == 0xffffffffU)
// 			break;
// 		/* Check if the max supported chain length is exceeded */
// 		if (device == JTAG_MAX_DEVS) {
// 			DEBUG_ERROR("jtag_scan: Maximum chain length exceeded\n");
// 			jtag_dev_count = 0;
// 			return false;
// 		}
// 		/* We got a valid device, add it to the set */
// 		jtag_devs[device].jd_idcode = idcode;
// 	}

// 	/* Well, it worked, so clean up and do housekeeping */
// 	DEBUG_INFO("Return to Run-Test/Idle\n");
// 	jtag_proc.jtagtap_next(true, true);
// 	jtagtap_return_idle(1);
// 	jtag_dev_count = device;
// 	return true;
// }

// static void jtag_display_idcodes(void)
// {
// #if ENABLE_DEBUG == 1
// 	for (size_t device = 0; device < jtag_dev_count; ++device) {
// 		const char *description = "Unknown";
// 		for (size_t idx = 0; dev_descr[idx].idcode; ++idx) {
// 			if ((jtag_devs[device].jd_idcode & dev_descr[idx].idmask) == dev_descr[idx].idcode) {
// 				if (dev_descr[idx].descr)
// 					description = dev_descr[idx].descr;
// 				break;
// 			}
// 		}
// 		DEBUG_INFO("ID code 0x%08" PRIx32 ": %s\n", jtag_devs[device].jd_idcode, description);
// 	}
// #endif
// }

// static jtag_ir_quirks_s jtag_device_get_quirks(const uint32_t idcode)
// {
// 	for (size_t idx = 0; dev_descr[idx].idcode; ++idx) {
// 		if ((idcode & dev_descr[idx].idmask) == dev_descr[idx].idcode)
// 			return dev_descr[idx].ir_quirks;
// 	}
// 	return (jtag_ir_quirks_s){0};
// }

// static bool jtag_read_irs(void)
// {
// 	/* Transition to Shift-IR */
// 	DEBUG_INFO("Change state to Shift-IR\n");
// 	jtagtap_shift_ir();

// 	DEBUG_INFO("Scanning out IRs\n");
// 	/* Start with no prescan and the first device */
// 	size_t prescan = 0U;
// 	size_t device = 0U;
// 	uint8_t ir_len = 0U;
// 	/* Grab the first device's quirks, if any */
// 	jtag_ir_quirks_s ir_quirks = jtag_device_get_quirks(jtag_devs[0].jd_idcode);

// 	/* Try scanning out the IR for the device */
// 	while (ir_len <= JTAG_MAX_IR_LEN) {
// 		/* Read the next IR bit */
// 		const bool next_bit = jtag_proc.jtagtap_next(false, true);
// 		/* If we have quirks, validate the bit against the expected IR */
// 		if (ir_quirks.ir_length && ((ir_quirks.ir_value >> ir_len) & 1U) != next_bit) {
// 			DEBUG_ERROR("jtag_scan: IR does not match the expected value, bailing out\n");
// 			jtag_dev_count = 0;
// 			return false;
// 		}
// 		/* IEEE 1149.1 requires the first bit to be a 1, but not all devices conform (see #1130 on GH) */
// 		if (ir_len == 0 && !next_bit)
// 			DEBUG_WARN("jtag_scan: Sanity check failed: IR[0] shifted out as 0\n");

// 		/* The bit validated ok, so increment the counter */
// 		++ir_len;

// 		/*
// 		 * If we do not have quirks in play, this was a 1 bit and we're not reading the first bit of the
// 		 * current IR, or if we've now read sufficient bits for the quirk, we've begun the next device
// 		 */
// 		if ((!ir_quirks.ir_length && next_bit && ir_len > 1U) || ir_len == ir_quirks.ir_length) {
// 			/* If we're not in quirks mode and the IR length is now 2 (2 1-bits in a row read), we're actually done */
// 			if (!ir_quirks.ir_length && ir_len == 2U)
// 				break;

// 			/*
// 			 * If we're reading using quirks, we'll read exactly the right number of bits,
// 			 * if not then we overrun by 1 for the device. Calculate the adjustment.
// 			 */
// 			const uint8_t overrun = ir_quirks.ir_length ? 0U : 1U;
// 			const uint8_t device_ir = ir_len - overrun;

// 			/* Set up the IR fields for the device and set up for the next */
// 			jtag_devs[device].ir_len = device_ir;
// 			jtag_devs[device].ir_prescan = prescan;
// 			jtag_devs[device].current_ir = UINT32_MAX;
// 			prescan += device_ir;
// 			++device;
// 			ir_len = overrun;
// 			/* Grab the device quirks for this new device, if any */
// 			ir_quirks = jtag_device_get_quirks(jtag_devs[device].jd_idcode);
// 		}
// 	}

// 	/* Sanity check that we didn't get an over-long IR */
// 	if (ir_len > JTAG_MAX_IR_LEN) {
// 		DEBUG_ERROR("jtag_scan: Maximum IR length exceeded\n");
// 		jtag_dev_count = 0;
// 		return 0;
// 	}
// 	return true;
// }

// static bool jtag_sanity_check(void)
// {
// 	/* Transition to Shift-DR */
// 	DEBUG_INFO("Change state to Shift-DR\n");
// 	jtagtap_shift_dr();
// 	/* Count devices on chain */
// 	size_t device = 0;
// 	for (; device <= jtag_dev_count; ++device) {
// 		if (jtag_proc.jtagtap_next(false, true))
// 			break;
// 		/* Configure the DR pre/post scan values */
// 		jtag_devs[device].dr_prescan = device;
// 		jtag_devs[device].dr_postscan = jtag_dev_count - device - 1U;
// 	}

// 	/* If the device count gleaned above does not match the device count, error out */
// 	if (device != jtag_dev_count) {
// 		DEBUG_ERROR("jtag_scan: Sanity check failed: BYPASS dev count doesn't match IR scan\n");
// 		jtag_dev_count = 0;
// 		return false;
// 	}

// 	/* Everything's accounted for, so clean up */
// 	DEBUG_INFO("Return to Run-Test/Idle\n");
// 	jtag_proc.jtagtap_next(true, true);
// 	jtagtap_return_idle(1);
// 	/* Return if there are any devices on the scan chain */
// 	return jtag_dev_count;
// }

// void jtag_dev_write_ir(const uint8_t dev_index, const uint32_t ir)
// {
// 	jtag_dev_s *device = &jtag_devs[dev_index];
// 	/* If the request would duplicate work already done, do nothing */
// 	if (ir == device->current_ir)
// 		return;

// 	/* Set all the other devices IR's to being in bypass */
// 	for (size_t device_index = 0; device_index < jtag_dev_count; device_index++)
// 		jtag_devs[device_index].current_ir = UINT32_MAX;
// 	device->current_ir = ir;

// 	/* Do the work to make the scanchain match the jtag_devs state */
// 	jtagtap_shift_ir();
// 	jtag_proc.jtagtap_tdi_seq(false, ones, device->ir_prescan);
// 	jtag_proc.jtagtap_tdi_seq(!device->ir_postscan, (const uint8_t *)&ir, device->ir_len);
// 	jtag_proc.jtagtap_tdi_seq(true, ones, device->ir_postscan);
// 	jtagtap_return_idle(1);
// }

// void jtag_dev_shift_dr(const uint8_t dev_index, uint8_t *data_out, const uint8_t *data_in, const size_t clock_cycles)
// {
// 	jtag_dev_s *device = &jtag_devs[dev_index];
// 	jtagtap_shift_dr();
// 	jtag_proc.jtagtap_tdi_seq(false, ones, device->dr_prescan);
// 	if (data_out)
// 		jtag_proc.jtagtap_tdi_tdo_seq(
// 			(uint8_t *)data_out, !device->dr_postscan, (const uint8_t *)data_in, clock_cycles);
// 	else
// 		jtag_proc.jtagtap_tdi_seq(!device->dr_postscan, (const uint8_t *)data_in, clock_cycles);
// 	jtag_proc.jtagtap_tdi_seq(true, ones, device->dr_postscan);
// 	jtagtap_return_idle(1);
// }
