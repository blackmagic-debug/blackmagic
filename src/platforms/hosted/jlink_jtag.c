/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Uwe Bonnes bon@elektron.ikp.physik.tu-darmstadt.de
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
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
 * Low level JTAG implementation using J-Link.
 */

#include "general.h"
#include <unistd.h>
#include <assert.h>
#include <memory.h>
#include <stdlib.h>

#include "exception.h"
#include "jtagtap.h"
#include "jlink.h"
#include "jlink_protocol.h"
#include "cli.h"

static void jlink_jtag_reset(void);
static void jlink_jtag_tms_seq(uint32_t tms_states, size_t clock_cycles);
static void jlink_jtag_tdi_tdo_seq(uint8_t *data_out, bool final_tms, const uint8_t *data_in, size_t clock_cycles);
static void jlink_jtag_tdi_seq(bool final_tms, const uint8_t *data_in, size_t clock_cycles);
static bool jlink_jtag_next(bool tms, bool tdi);

static const uint8_t jlink_switch_to_jtag_seq[9U] = {0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0x3cU, 0xe7U};

bool jlink_jtag_init(void)
{
	DEBUG_PROBE("-> jlink_jtag_init\n");

	/* Try to switch the adaptor into JTAG mode */
	if (!jlink_select_interface(JLINK_INTERFACE_JTAG)) {
		DEBUG_ERROR("Failed to select JTAG interface\n");
		return false;
	}

	/* Ensure we're in JTAG mode */
	DEBUG_PROBE("%s: Switch to JTAG\n", __func__);
	if (!jlink_transfer(sizeof(jlink_switch_to_jtag_seq) * 8U, jlink_switch_to_jtag_seq, NULL, NULL)) {
		DEBUG_ERROR("Switch to JTAG failed\n");
		return false;
	}

	/* Set up the underlying JTAG functions using the implementation below */
	jtag_proc.jtagtap_reset = jlink_jtag_reset;
	jtag_proc.jtagtap_next = jlink_jtag_next;
	jtag_proc.jtagtap_tms_seq = jlink_jtag_tms_seq;
	jtag_proc.jtagtap_tdi_tdo_seq = jlink_jtag_tdi_tdo_seq;
	jtag_proc.jtagtap_tdi_seq = jlink_jtag_tdi_seq;
	return true;
}

static void jlink_jtag_reset(void)
{
	jtagtap_soft_reset();
}

static void jlink_jtag_tms_seq(const uint32_t tms_states, const size_t clock_cycles)
{
	/* Ensure the transaction's not too long */
	if (clock_cycles > 32U)
		return;
	DEBUG_PROBE("jtagtap_tms_seq 0x%08" PRIx32 ", clock cycles: %zu\n", tms_states, clock_cycles);
	/* Set up a buffer for tms_states to make sure the values are in the proper order */
	uint8_t tms[4] = {0};
	for (size_t cycle = 0; cycle < clock_cycles; cycle += 8U)
		tms[cycle >> 3U] = (tms_states >> cycle) & 0xffU;
	/* Attempt the transaction, check for errors and raise an exception if there is one */
	if (!jlink_transfer(clock_cycles, tms, tms, NULL))
		raise_exception(EXCEPTION_ERROR, "tagtap_tms_seq failed");
}

static void jlink_jtag_tdi_tdo_seq(
	uint8_t *const data_out, const bool final_tms, const uint8_t *const data_in, const size_t clock_cycles)
{
	const bool result = jlink_transfer_fixed_tms(clock_cycles, final_tms, data_in, data_out);
	DEBUG_PROBE("jtagtap_tdi_tdo_seq %zu, %02x -> %02x\n", clock_cycles, data_in[0], data_out ? data_out[0] : 0);
	if (!result)
		raise_exception(EXCEPTION_ERROR, "jtagtap_tdi_tdo_seq failed");
}

static void jlink_jtag_tdi_seq(const bool final_tms, const uint8_t *const data_in, const size_t clock_cycles)
{
	const bool result = jlink_transfer_fixed_tms(clock_cycles, final_tms, data_in, NULL);
	DEBUG_PROBE("jtagtap_tdi_seq %zu, %02x\n", clock_cycles, data_in[0]);
	if (!result)
		raise_exception(EXCEPTION_ERROR, "jtagtap_tdi_seq failed");
}

static bool jlink_jtag_next(bool tms, bool tdi)
{
	const uint8_t tms_byte = tms ? 1 : 0;
	const uint8_t tdi_byte = tdi ? 1 : 0;
	uint8_t tdo = 0;
	const bool result = jlink_transfer(1U, &tms_byte, &tdi_byte, &tdo);
	DEBUG_PROBE("jtagtap_next tms=%u tdi=%u tdo=%u\n", tms_byte, tdi_byte, tdo);
	if (!result)
		raise_exception(EXCEPTION_ERROR, "jtagtap_next failed");
	return tdo;
}
