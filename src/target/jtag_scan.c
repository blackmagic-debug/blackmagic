/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022-2024 1BitSquared <info@1bitsquared.com>
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

/*
 * This file implements JTAG protocol support.  Provides functionality
 * to detect devices on the scan chain and read their IDCODEs.
 * It depends on the low-level function provided by the platform's jtagtap.c.
 */

#include "general.h"
#include "jtagtap.h"
#include "jtag_scan.h"
#include "target.h"
#include "adiv5.h"
#include "jtag_devs.h"
#include "gdb_packet.h"

jtag_dev_s jtag_devs[JTAG_MAX_DEVS];
uint32_t jtag_dev_count = 0;

/* bucket of ones for don't care TDI */
const uint8_t ones[8] = {0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU};

static bool jtag_read_idcodes(void);
static void jtag_display_idcodes(void);
static bool jtag_read_irs(void);
static bool jtag_sanity_check(void);

#if CONFIG_BMDA == 0
void jtag_add_device(const uint32_t dev_index, const jtag_dev_s *jtag_dev)
{
	if (dev_index == 0)
		memset(&jtag_devs, 0, sizeof(jtag_devs));
	memcpy(&jtag_devs[dev_index], jtag_dev, sizeof(jtag_dev_s));
	jtag_dev_count = dev_index + 1U;
}
#endif

/*
 * This scans the JTAG interface for any possible device chain attached.
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
bool jtag_scan(void)
{
	/*
	 * Initialise the JTAG backend if it's not already
	 * This will automatically do the SWD-to-JTAG sequence just in case we've got
	 * any SWD/JTAG DPs in the chain
	 */
	DEBUG_INFO("Resetting TAP\n");
#if CONFIG_BMDA == 1
	if (!bmda_jtag_init()) {
		DEBUG_ERROR("JTAG not available\n");
		return false;
	}
#else
	jtagtap_init();
#endif

	/* Reset the chain ready */
	jtag_proc.jtagtap_reset();
	return jtag_discover();
}

bool jtag_discover(void)
{
	/* Free the device list if any, and clean state ready */
	target_list_free();

	jtag_dev_count = 0;
	memset(jtag_devs, 0, sizeof(jtag_devs));

	/* Start by reading out the ID Codes for all the devices on the chain */
	if (!jtag_read_idcodes() ||
		/* Otherwise, try and learn the chain IR lengths */
		!jtag_read_irs())
		return false;

	/* IRs are all successfully accounted for, so clean up and do housekeeping */
	DEBUG_INFO("Return to Run-Test/Idle\n");
	jtag_proc.jtagtap_next(true, true);
	jtagtap_return_idle(1);

	/* All devices should be in BYPASS now so do the sanity check */
	if (!jtag_sanity_check())
		return false;

	/* Fill in the ir_postscan fields */
	uint8_t postscan = 0;
	for (size_t device = 0; device < jtag_dev_count; ++device) {
		/* Traverse the device list from the back */
		const size_t idx = (jtag_dev_count - 1U) - device;
		/* Copy the current postscan value in and add this device's IR to it for the next lowest in the list  */
		jtag_devs[idx].ir_postscan = postscan;
		postscan += jtag_devs[idx].ir_len;
	}

#if CONFIG_BMDA == 1
	/*Transfer needed device information to firmware jtag_devs */
	for (size_t device = 0; device < jtag_dev_count; ++device)
		bmda_add_jtag_dev(device, jtag_devs + device);
#endif

	jtag_display_idcodes();

#if CONFIG_BMDA == 1
	DEBUG_PROBE("Enumerated %" PRIu32 " devices\n", jtag_dev_count);
	for (size_t device = 0; device < jtag_dev_count; ++device) {
		const jtag_dev_s *dev = &jtag_devs[device];
		DEBUG_PROBE("%" PRIu32 ": IR length = %u, ID %08" PRIx32 "\n", (uint32_t)device, dev->ir_len, dev->jd_idcode);
		DEBUG_PROBE("-> IR prescan: %u, postscan: %u\n", dev->ir_prescan, dev->ir_postscan);
		DEBUG_PROBE("-> DR prescan: %u, postscan: %u\n", dev->dr_prescan, dev->dr_postscan);
	}
#endif

	/* Check for known devices and handle accordingly */
	for (size_t device = 0; device < jtag_dev_count; device++) {
		for (size_t descr = 0; dev_descr[descr].idcode; descr++) {
			if ((jtag_devs[device].jd_idcode & dev_descr[descr].idmask) == dev_descr[descr].idcode) {
				/* Call handler to initialise/probe device further */
				if (dev_descr[descr].handler)
					dev_descr[descr].handler(device);
				break;
			}
		}
	}

	return jtag_dev_count > 0;
}

static bool jtag_read_idcodes(void)
{
	/* Transition to Shift-DR */
	DEBUG_INFO("Change state to Shift-DR\n");
	jtagtap_shift_dr();

	DEBUG_INFO("Scanning out ID codes\n");
	size_t device = 0;
	/* Try scanning to one ID past the end of the chain */
	for (; device <= JTAG_MAX_DEVS; ++device) {
		/* Try to read out 32 bits, while shifting in 1's */
		uint32_t idcode = 0;
		jtag_proc.jtagtap_tdi_tdo_seq((uint8_t *)&idcode, false, ones, 32);
		/* If the IDCode read is all 1's, we've reached the end */
		if (idcode == 0xffffffffU)
			break;
		/* Check if the max supported chain length is exceeded */
		if (device == JTAG_MAX_DEVS) {
			DEBUG_ERROR("jtag_scan: Maximum chain length exceeded\n");
			jtag_dev_count = 0;
			return false;
		}
		/* We got a valid device, add it to the set */
		jtag_devs[device].jd_idcode = idcode;
	}

	/* Well, it worked, so clean up and do housekeeping */
	DEBUG_INFO("Return to Run-Test/Idle\n");
	jtag_proc.jtagtap_next(true, true);
	jtagtap_return_idle(1);
	jtag_dev_count = device;
	return true;
}

static void jtag_display_idcodes(void)
{
#if ENABLE_DEBUG == 1
	for (size_t device = 0; device < jtag_dev_count; ++device) {
		const char *description = "Unknown";
		for (size_t idx = 0; dev_descr[idx].idcode; ++idx) {
			if ((jtag_devs[device].jd_idcode & dev_descr[idx].idmask) == dev_descr[idx].idcode) {
				if (dev_descr[idx].descr)
					description = dev_descr[idx].descr;
				break;
			}
		}
		DEBUG_INFO("ID code 0x%08" PRIx32 ": %s\n", jtag_devs[device].jd_idcode, description);
	}
#endif
}

static jtag_ir_quirks_s jtag_device_get_quirks(const uint32_t idcode)
{
	for (size_t idx = 0; dev_descr[idx].idcode; ++idx) {
		if ((idcode & dev_descr[idx].idmask) == dev_descr[idx].idcode)
			return dev_descr[idx].ir_quirks;
	}
	return (jtag_ir_quirks_s){0};
}

static bool jtag_read_irs(void)
{
	/* Transition to Shift-IR */
	DEBUG_INFO("Change state to Shift-IR\n");
	jtagtap_shift_ir();

	DEBUG_INFO("Scanning out IRs\n");
	/* Start with no prescan and the first device */
	size_t prescan = 0U;
	size_t device = 0U;
	uint8_t ir_len = 0U;
	/* Grab the first device's quirks, if any */
	jtag_ir_quirks_s ir_quirks = jtag_device_get_quirks(jtag_devs[0].jd_idcode);

	/* Try scanning out the IR for the device */
	while (ir_len <= JTAG_MAX_IR_LEN) {
		/* Read the next IR bit */
		const bool next_bit = jtag_proc.jtagtap_next(false, true);
		/* If we have quirks, validate the bit against the expected IR */
		if (ir_quirks.ir_length && ((ir_quirks.ir_value >> ir_len) & 1U) != next_bit) {
			DEBUG_ERROR("jtag_scan: IR does not match the expected value, bailing out\n");
			jtag_dev_count = 0;
			return false;
		}
		/* IEEE 1149.1 requires the first bit to be a 1, but not all devices conform (see #1130 on GH) */
		if (ir_len == 0 && !next_bit)
			DEBUG_WARN("jtag_scan: Sanity check failed: IR[0] shifted out as 0\n");

		/* The bit validated ok, so increment the counter */
		++ir_len;

		/*
		 * If we do not have quirks in play, this was a 1 bit and we're not reading the first bit of the
		 * current IR, or if we've now read sufficient bits for the quirk, we've begun the next device
		 */
		if ((!ir_quirks.ir_length && next_bit && ir_len > 1U) || ir_len == ir_quirks.ir_length) {
			/* If we're not in quirks mode and the IR length is now 2 (2 1-bits in a row read), we're actually done */
			if (!ir_quirks.ir_length && ir_len == 2U)
				break;

			/*
			 * If we're reading using quirks, we'll read exactly the right number of bits,
			 * if not then we overrun by 1 for the device. Calculate the adjustment.
			 */
			const uint8_t overrun = ir_quirks.ir_length ? 0U : 1U;
			const uint8_t device_ir = ir_len - overrun;

			/* Set up the IR fields for the device and set up for the next */
			jtag_devs[device].ir_len = device_ir;
			jtag_devs[device].ir_prescan = prescan;
			jtag_devs[device].current_ir = UINT32_MAX;
			prescan += device_ir;
			++device;
			ir_len = overrun;
			/* Grab the device quirks for this new device, if any */
			ir_quirks = jtag_device_get_quirks(jtag_devs[device].jd_idcode);
		}
	}

	/* Sanity check that we didn't get an over-long IR */
	if (ir_len > JTAG_MAX_IR_LEN) {
		DEBUG_ERROR("jtag_scan: Maximum IR length exceeded\n");
		jtag_dev_count = 0;
		return 0;
	}
	return true;
}

static bool jtag_sanity_check(void)
{
	/* Transition to Shift-DR */
	DEBUG_INFO("Change state to Shift-DR\n");
	jtagtap_shift_dr();
	/* Count devices on chain */
	size_t device = 0;
	for (; device <= jtag_dev_count; ++device) {
		if (jtag_proc.jtagtap_next(false, true))
			break;
		/* Configure the DR pre/post scan values */
		jtag_devs[device].dr_prescan = device;
		jtag_devs[device].dr_postscan = jtag_dev_count - device - 1U;
	}

	/* If the device count gleaned above does not match the device count, error out */
	if (device != jtag_dev_count) {
		DEBUG_ERROR("jtag_scan: Sanity check failed: BYPASS dev count doesn't match IR scan\n");
		jtag_dev_count = 0;
		return false;
	}

	/* Everything's accounted for, so clean up */
	DEBUG_INFO("Return to Run-Test/Idle\n");
	jtag_proc.jtagtap_next(true, true);
	jtagtap_return_idle(1);
	/* Return if there are any devices on the scan chain */
	return jtag_dev_count;
}

void jtag_dev_write_ir(const uint8_t dev_index, const uint32_t ir)
{
	jtag_dev_s *const device = &jtag_devs[dev_index];
	/* If the request would duplicate work already done, do nothing */
	if (ir == device->current_ir)
		return;

	/* Set all the other devices IR's to being in bypass */
	for (size_t device_index = 0; device_index < jtag_dev_count; device_index++)
		jtag_devs[device_index].current_ir = UINT32_MAX;
	/* Put the current device IR into the requested state */
	device->current_ir = ir;

	/* Do the work to make the scanchain match the jtag_devs state */
	jtagtap_shift_ir();
	/* Once in Shift-IR, clock out 1's till we hit the right device in the chain */
	jtag_proc.jtagtap_tdi_seq(false, ones, device->ir_prescan);
	/* Then clock out the new IR value and drop into Exit1-IR on the last cycle if we're the last device */
	jtag_proc.jtagtap_tdi_seq(!device->ir_postscan, (const uint8_t *)&ir, device->ir_len);
	/* Make sure we're in Exit1-IR having clocked out 1's for any more devices on the chain */
	jtag_proc.jtagtap_tdi_seq(true, ones, device->ir_postscan);
	/* Now go through Update-IR and back to Idle */
	jtagtap_return_idle(1);
}

void jtag_dev_shift_dr(
	const uint8_t dev_index, uint8_t *const data_out, const uint8_t *const data_in, const size_t clock_cycles)
{
	const jtag_dev_s *const device = &jtag_devs[dev_index];
	/* Switch into Shift-DR */
	jtagtap_shift_dr();
	/* Now we're in Shift-DR, clock out 1's till we hit the right device in the chain */
	jtag_proc.jtagtap_tdi_seq(false, ones, device->dr_prescan);
	/* Now clock out the new DR value and get the response */
	if (data_out)
		jtag_proc.jtagtap_tdi_tdo_seq(data_out, !device->dr_postscan, data_in, clock_cycles);
	else
		jtag_proc.jtagtap_tdi_seq(!device->dr_postscan, data_in, clock_cycles);
	/* Make sure we're in Exit1-DR having clocked out 1's for any more devices on the chain */
	jtag_proc.jtagtap_tdi_seq(true, ones, device->dr_postscan);
	/* Now go through Update-DR and back to Idle */
	jtagtap_return_idle(1);
}
