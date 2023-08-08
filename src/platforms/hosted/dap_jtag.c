/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
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

#include "dap.h"
#include "dap_command.h"
#include "jtag_scan.h"
#include "buffer_utils.h"

static void dap_jtag_reset(void);
static void dap_jtag_tms_seq(uint32_t tms_states, size_t clock_cycles);
static void dap_jtag_tdi_tdo_seq(uint8_t *data_out, bool final_tms, const uint8_t *data_in, size_t clock_cycles);
static void dap_jtag_tdi_seq(bool final_tms, const uint8_t *data_in, size_t clock_cycles);
static bool dap_jtag_next(bool tms, bool tdi);

bool dap_jtag_init(void)
{
	/* If we are not able to talk SWD with this adaptor, make this insta-fail */
	if (!(dap_caps & DAP_CAP_JTAG))
		return false;

	DEBUG_PROBE("-> dap_jtag_init()\n");
	dap_disconnect();
	dap_mode = DAP_CAP_JTAG;
	dap_connect();
	dap_reset_link(NULL);
	jtag_proc.jtagtap_reset = dap_jtag_reset;
	jtag_proc.jtagtap_next = dap_jtag_next;
	jtag_proc.jtagtap_tms_seq = dap_jtag_tms_seq;
	jtag_proc.jtagtap_tdi_tdo_seq = dap_jtag_tdi_tdo_seq;
	jtag_proc.jtagtap_tdi_seq = dap_jtag_tdi_seq;

	if (dap_quirks & DAP_QUIRK_NO_JTAG_MUTLI_TAP)
		DEBUG_WARN("Multi-TAP JTAG is broken on this adaptor firmware revision, please upgrade it\n");
	return true;
}

void dap_jtag_dp_init(adiv5_debug_port_s *target_dp)
{
	if ((dap_quirks & DAP_QUIRK_NO_JTAG_MUTLI_TAP) && jtag_dev_count > 1) {
		DEBUG_WARN("Bailing out on multi-TAP chain\n");
		exit(2);
	}

	/* Try to configure the JTAG engine on the adaptor */
	if (!dap_jtag_configure())
		return;
	target_dp->dp_read = dap_dp_read_reg;
	target_dp->low_access = dap_dp_low_access;
	target_dp->abort = dap_dp_abort;
}

static void dap_jtag_reset(void)
{
	jtagtap_soft_reset();
	/* Is there a way to know if TRST is available?*/
}

static void dap_jtag_tms_seq(const uint32_t tms_states, const size_t clock_cycles)
{
	uint8_t sequence[4] = {0};
	write_le4(sequence, 0, tms_states);
	perform_dap_swj_sequence(clock_cycles, sequence);
	DEBUG_PROBE("jtagtap_tms_seq data_in %08x %zu\n", tms_states, clock_cycles);
}

static void dap_jtag_tdi_tdo_seq(
	uint8_t *const data_out, const bool final_tms, const uint8_t *const data_in, const size_t clock_cycles)
{
	perform_dap_jtag_sequence(data_in, data_out, final_tms, clock_cycles);
	DEBUG_PROBE("jtagtap_tdi_tdo_seq %zu, %02x -> %02x\n", clock_cycles, data_in[0], data_out ? data_out[0] : 0U);
}

static void dap_jtag_tdi_seq(const bool final_tms, const uint8_t *const data_in, const size_t clock_cycles)
{
	perform_dap_jtag_sequence(data_in, NULL, final_tms, clock_cycles);
	DEBUG_PROBE("jtagtap_tdi_seq %zu, %02x\n", clock_cycles, data_in[0]);
}

static bool dap_jtag_next(const bool tms, const bool tdi)
{
#ifdef ENABLE_DEBUG
	const uint8_t tms_byte = tms ? 1 : 0;
#endif
	const uint8_t tdi_byte = tdi ? 1 : 0;
	uint8_t tdo = 0;
	perform_dap_jtag_sequence(&tdi_byte, &tdo, tms, 1U);
	DEBUG_PROBE("jtagtap_next tms=%u tdi=%u tdo=%u\n", tms_byte, tdi_byte, tdo);
	return tdo;
}

bool dap_jtag_configure(void)
{
	/* Check if there are no or too many devices */
	if (!jtag_dev_count || jtag_dev_count >= JTAG_MAX_DEVS)
		return false;
	/* Begin building the configuration packet */
	uint8_t request[2U + JTAG_MAX_DEVS] = {
		DAP_JTAG_CONFIGURE,
		jtag_dev_count,
	};
	/* For each device in the chain copy its IR length to the configuration */
	for (uint32_t device = 0; device < jtag_dev_count; device++) {
		const jtag_dev_s *const dev = &jtag_devs[device];
		request[2U + device] = dev->ir_len;
		DEBUG_PROBE("%" PRIu32 ": irlen = %u\n", device, dev->ir_len);
	}
	uint8_t response = DAP_RESPONSE_OK;
	/* Send the configuration and ensure it succeeded */
	if (!dap_run_cmd(request, 2U + jtag_dev_count, &response, 1U) || response != DAP_RESPONSE_OK)
		DEBUG_ERROR("dap_jtag_configure failed with %02x\n", response);
	return response == DAP_RESPONSE_OK;
}
