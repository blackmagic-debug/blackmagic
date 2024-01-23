/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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

/*
 * This file implements support for the TI ICEPick controller that sits in front
 * of TAPs in the scan chain on some TI devices.
 *
 * References:
 * SPRUH35 - Using the ICEPick TAP (type-C)
 *   https://www.ti.com/lit/ug/spruh35/spruh35.pdf
 */

#include "general.h"
#include "buffer_utils.h"
#include "jtag_scan.h"
#include "jtagtap.h"
#include "icepick.h"

#define IR_ROUTER      0x02U
#define IR_IDCODE      0x04U
#define IR_ICEPICKCODE 0x05U
#define IR_CONNECT     0x07U

/*
 * The type-C value is taken from SPRUH35, the type-D value is
 * from a BeagleBone Black Industrial (AM3358BZCZA100)
 */
#define ICEPICK_TYPE_MASK 0xfff0U
#define ICEPICK_TYPE_C    0x1cc0U
#define ICEPICK_TYPE_D    0xb3d0U

#define ICEPICK_MAJOR_SHIFT 28U
#define ICEPICK_MAJOR_MASK  0xfU
#define ICEPICK_MINOR_SHIFT 24U
#define ICEPICK_MINOR_MASK  0xfU

#define ICEPICK_ROUTING_REG_MASK  0x7fU
#define ICEPICK_ROUTING_REG_SHIFT 24U
#define ICEPICK_ROUTING_DATA_MASK 0x00ffffffU
#define ICEPICK_ROUTING_RNW_MASK  0x80000000U
#define ICEPICK_ROUTING_RNW_WRITE 0x80000000U
#define ICEPICK_ROUTING_FAIL      0x80000000U

#define ICEPICK_ROUTING_SYSCTRL         0x01U
#define ICEPICK_ROUTING_DEBUG_TAP_BASE  0x20U
#define ICEPICK_ROUTING_DEBUG_TAP_COUNT 16U

#define ICEPICK_ROUTING_SYSCTRL_FREE_RUNNING_TCK 0x00001000U
#define ICEPICK_ROUTING_SYSCTRL_KEEP_POWERED     0x00000080U
#define ICEPICK_ROUTING_SYSCTRL_TDO_ALWAYS_OUT   0x00000020U
#define ICEPICK_ROUTING_SYSCTRL_DEVICE_TYPE_MASK 0x0000000eU
#define ICEPICK_ROUTING_SYSCTRL_SYSTEM_RESET     0x00000001U

#define ICEPICK_ROUTING_DEBUG_TAP_POWER_LOST    0x00200000U
#define ICEPICK_ROUTING_DEBUG_TAP_INHIBIT_SLEEP 0x00100000U
#define ICEPICK_ROUTING_DEBUG_TAP_RELEASE_WIR   0x00020000U
#define ICEPICK_ROUTING_DEBUG_TAP_DEBUG_ENABLE  0x00002000U
#define ICEPICK_ROUTING_DEBUG_TAP_SELECT        0x00000100U
#define ICEPICK_ROUTING_DEBUG_TAP_FORCE_ACTIVE  0x00000008U

/*
 * The connect register is 8 bits long and has the following format:
 * [0:3] - Connect key (9 to connect, anything else to disconnect)
 * [4:6] - Reserved, RAZ/WI
 *   [7] - Write Enable, 1 to enable writing this register
 */
#define ICEPICK_CONNECT    0x89U
#define ICEPICK_DISCONNECT 0x80U

void icepick_write_ir(jtag_dev_s *device, uint8_t ir);
uint32_t icepick_shift_dr(const jtag_dev_s *device, uint32_t data_in, size_t clock_cycles);
static void icepick_configure(const jtag_dev_s *device);

void icepick_router_handler(const uint8_t dev_index)
{
	jtag_dev_s *const device = &jtag_devs[dev_index];

	/* Switch the ICEPick TAP into its controller identification mode */
	icepick_write_ir(device, IR_ICEPICKCODE);
	/* Then read out the 32-bit controller ID code */
	const uint32_t icepick_idcode = icepick_shift_dr(device, 0U, 32U);

	/* Check it's a suitable ICEPick controller, and abort if not */
	if ((icepick_idcode & ICEPICK_TYPE_MASK) != ICEPICK_TYPE_D) {
		DEBUG_ERROR("ICEPick is not a type-D controller (%08" PRIx32 ")\n", icepick_idcode);
		return;
	}
	DEBUG_INFO("ICEPick type-D controller v%u.%u (%08" PRIx32 ")\n",
		(icepick_idcode >> ICEPICK_MAJOR_SHIFT) & ICEPICK_MAJOR_MASK,
		(icepick_idcode >> ICEPICK_MINOR_SHIFT) & ICEPICK_MINOR_MASK, icepick_idcode);

	/* Connect to the controller so we can modify the scan chain */
	icepick_write_ir(device, IR_CONNECT);
	icepick_shift_dr(device, ICEPICK_CONNECT, 8U);

	/* Now we're connected, go into the routing inspection/modification mode */
	icepick_write_ir(device, IR_ROUTER);
	/* Configure the router to put the Cortex TAP(s) on chain */
	icepick_configure(device);
	/* Go to an idle state instruction and then run 10 idle cycles to complete reconfiguration */
	icepick_write_ir(device, IR_IDCODE);
	jtag_proc.jtagtap_cycle(false, false, 10U);
}

/* Read an ICEPick routing register */
static bool icepick_read_reg(const jtag_dev_s *const device, const uint8_t reg, uint32_t *const result)
{
	/* Start by building a read request and sending it to the controller */
	icepick_shift_dr(device, (uint32_t)(reg & ICEPICK_ROUTING_REG_MASK) << ICEPICK_ROUTING_REG_SHIFT, 32U);
	/* Having completed this, now do a dummy request to reg 0 to find out what the response is */
	const uint32_t value = icepick_shift_dr(device, 0U, 32U);
	/* Now check if the request failed */
	if (value & ICEPICK_ROUTING_FAIL)
		return false;
	/* It did not, so now extract the data portion of the result */
	*result = value & ICEPICK_ROUTING_DATA_MASK;
	return true;
}

/* Write an ICEPick routing register */
static bool icepick_write_reg(const jtag_dev_s *const device, const uint8_t reg, const uint32_t value)
{
	/* Build a write a request and send it to the controller */
	icepick_shift_dr(device,
		ICEPICK_ROUTING_RNW_WRITE | ((uint32_t)(reg & ICEPICK_ROUTING_REG_MASK) << ICEPICK_ROUTING_REG_SHIFT) |
			(value & ICEPICK_ROUTING_DATA_MASK),
		32U);
	/* Having completed this, now do a dummy request to reg 0 to find out what the response is */
	const uint32_t result = icepick_shift_dr(device, 0U, 32U);
	/* Now check if the request failed */
	return !(result & ICEPICK_ROUTING_FAIL);
}

static void icepick_configure(const jtag_dev_s *const device)
{
	/* Try to read out the system control register */
	uint32_t sysctrl = 0U;
	if (!icepick_read_reg(device, ICEPICK_ROUTING_SYSCTRL, &sysctrl)) {
		DEBUG_ERROR("Failed to read ICEPick System Control register\n");
		return;
	}

	/* Decode the register to determine what we've got */
	DEBUG_INFO("ICEPick sysctrl = %08" PRIx32 "\n", sysctrl);
	/*
	 * Make sure the controller is set up for non-free-running TCK, that will be reset
	 * when doing a test logic reset, and that TDO is always an output
	 */
	sysctrl &= ~(ICEPICK_ROUTING_SYSCTRL_FREE_RUNNING_TCK | ICEPICK_ROUTING_SYSCTRL_KEEP_POWERED |
		ICEPICK_ROUTING_SYSCTRL_TDO_ALWAYS_OUT);
	if (!icepick_write_reg(device, ICEPICK_ROUTING_SYSCTRL, sysctrl)) {
		DEBUG_ERROR("Failed to configure ICEPick\n");
		return;
	}

	for (uint8_t tap = 0U; tap < ICEPICK_ROUTING_DEBUG_TAP_COUNT; ++tap) {
		uint32_t tap_config = 0U;
		if (icepick_read_reg(device, ICEPICK_ROUTING_DEBUG_TAP_BASE + tap, &tap_config)) {
			DEBUG_INFO("ICEPick TAP %u: %06" PRIx32 "\n", tap, tap_config);
			if (!icepick_write_reg(device, ICEPICK_ROUTING_DEBUG_TAP_BASE + tap,
					ICEPICK_ROUTING_DEBUG_TAP_POWER_LOST | ICEPICK_ROUTING_DEBUG_TAP_INHIBIT_SLEEP |
						ICEPICK_ROUTING_DEBUG_TAP_RELEASE_WIR | ICEPICK_ROUTING_DEBUG_TAP_DEBUG_ENABLE |
						ICEPICK_ROUTING_DEBUG_TAP_SELECT | ICEPICK_ROUTING_DEBUG_TAP_FORCE_ACTIVE))
				DEBUG_ERROR("ICEPick TAP %u write failed\n", tap);
		} else
			DEBUG_PROBE("ICEPick TAP %u read failed\n", tap);
	}
}

void icepick_write_ir(jtag_dev_s *const device, const uint8_t ir)
{
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
	jtag_proc.jtagtap_tdi_seq(!device->ir_postscan, &ir, device->ir_len);
	/* Make sure we're in Exit1-IR having clocked out 1's for any more devices on the chain */
	jtag_proc.jtagtap_tdi_seq(true, ones, device->ir_postscan);
	/* Now go to Update-IR but do not go back to Idle */
	jtagtap_return_idle(0);
}

uint32_t icepick_shift_dr(const jtag_dev_s *const device, const uint32_t data_in, const size_t clock_cycles)
{
	/* Prepare the data to send */
	uint8_t data[4];
	write_le4(data, 0, data_in);
	/* Switch into Shift-DR */
	jtagtap_shift_dr();
	/* Now we're in Shift-DR, clock out 1's till we hit the right device in the chain */
	jtag_proc.jtagtap_tdi_seq(false, ones, device->dr_prescan);
	/* Now clock out the new DR value and get the response */
	jtag_proc.jtagtap_tdi_tdo_seq(data, !device->dr_postscan, data, clock_cycles);
	/* Make sure we're in Exit1-DR having clocked out 1's for any more devices on the chain */
	jtag_proc.jtagtap_tdi_seq(true, ones, device->dr_postscan);
	/* Now go to Update-DR but do not go back to Idle */
	jtagtap_return_idle(0);
	/* Extract the resulting data */
	return read_le4(data, 0);
}
