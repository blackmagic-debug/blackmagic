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

static void jtagtap_reset(void);
static void jtagtap_tms_seq(uint32_t tms_states, size_t clock_cycles);
static void jtagtap_tdi_tdo_seq(uint8_t *data_out, bool final_tms, const uint8_t *data_in, size_t clock_cycles);
static void jtagtap_tdi_seq(bool final_tms, const uint8_t *data_in, size_t clock_cycles);
static bool jtagtap_next(bool tms, bool tdi);

/*
 * In this file, command buffers with the command code JLINK_CMD_IO_TRANSACT are built.
 * These have the following format:
 * ┌─────────┬─────────┬───────────────┬─────────┐
 * │    0    │    1    │       2       │    3    │
 * ├╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌┤
 * │ Command │ Unknown │  Cycle count  │ Unknown │
 * ├─────────┼─────────┼───────────────┼─────────┤
 * │    4    │    …    │ 4 + tms_bytes │    …    │
 * ├╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┼╌╌╌╌╌╌╌╌╌┤
 * │  TMS data bytes…  │     TDI data bytes…     │
 * └─────────┴─────────┴───────────────┴─────────┘
 * where the byte counts for each of TDI and TMS are defined by:
 * count = ⌈cycle_count / 8⌉
 *
 * ⌈⌉ is defined as the ceiling function.
 */

bool jlink_jtagtap_init(bmp_info_s *const info)
{
	DEBUG_PROBE("jtap_init\n");
	uint8_t res[4];
	if (jlink_simple_request(JLINK_CMD_TARGET_IF, JLINK_IF_GET_AVAILABLE, res, sizeof(res)) < 0 ||
		!(res[0] & JLINK_IF_JTAG) || jlink_simple_request(JLINK_CMD_TARGET_IF, SELECT_IF_JTAG, res, sizeof(res)) < 0) {
		DEBUG_ERROR("JTAG not available\n");
		return false;
	}
	platform_delay(10);
	/* Set adaptor JTAG frequency to 256 kHz */
	jlink_set_frequency(2000);

	uint8_t cmd[22];
	memset(cmd, 0, 22);
	cmd[0] = JLINK_CMD_IO_TRANSACT;
	/* write 9 bytes */
	cmd[2] = 9U * 8U;
	memset(cmd + 4U, 0xffU, 7);
	cmd[11] = 0x3c;
	cmd[12] = 0xe7;
	bmda_usb_transfer(info->usb_link, cmd, 22, cmd, 9);
	bmda_usb_transfer(info->usb_link, NULL, 0, res, 1);

	if (res[0] != 0) {
		DEBUG_ERROR("Switch to JTAG failed\n");
		return false;
	}
	jtag_proc.jtagtap_reset = jtagtap_reset;
	jtag_proc.jtagtap_next = jtagtap_next;
	jtag_proc.jtagtap_tms_seq = jtagtap_tms_seq;
	jtag_proc.jtagtap_tdi_tdo_seq = jtagtap_tdi_tdo_seq;
	jtag_proc.jtagtap_tdi_seq = jtagtap_tdi_seq;
	return true;
}

static void jtagtap_reset(void)
{
	jtagtap_soft_reset();
}

static void jtagtap_tms_seq(const uint32_t tms_states, const size_t clock_cycles)
{
	if (clock_cycles > 32U)
		return;
	DEBUG_PROBE("jtagtap_tms_seq 0x%08" PRIx32 ", clock cycles: %zu\n", tms_states, clock_cycles);
	const size_t len = (clock_cycles + 7U) / 8U;
	uint8_t cmd[12];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = JLINK_CMD_IO_TRANSACT;
	cmd[2] = clock_cycles;
	const size_t total_chunks = (clock_cycles >> 3U) + ((clock_cycles & 7U) ? 1U : 0U);
	for (size_t cycle = 0; cycle < clock_cycles; cycle += 8U) {
		const size_t index = 4 + (cycle >> 3U);
		cmd[index] = (tms_states >> cycle) & 0xffU;
		cmd[index + total_chunks] = cmd[index];
	}
	uint8_t result[4];
	bmda_usb_transfer(info.usb_link, cmd, 4U + (total_chunks * 2U), result, len);
	bmda_usb_transfer(info.usb_link, NULL, 0, result, 1);
	if (result[0] != 0)
		raise_exception(EXCEPTION_ERROR, "tagtap_tms_seq failed");
}

static void jtagtap_tdi_tdo_seq(
	uint8_t *const data_out, const bool final_tms, const uint8_t *const data_in, const size_t clock_cycles)
{
	if (!clock_cycles)
		return;
	const size_t total_chunks = (clock_cycles >> 3U) + ((clock_cycles & 7U) ? 1U : 0U);
	DEBUG_PROBE("jtagtap_tdi_tdo final tms: %u, clock cycles: %zu, data_in: ", final_tms ? 1 : 0, clock_cycles);
	for (size_t i = 0; i < total_chunks; ++i)
		DEBUG_PROBE("%02x", data_in[i]);
	DEBUG_PROBE("\n");
	const size_t cmd_len = 4 + (total_chunks * 2U);
	uint8_t *cmd = calloc(1, cmd_len);
	cmd[0] = JLINK_CMD_IO_TRANSACT;
	cmd[2] = clock_cycles;
	if (final_tms) {
		const size_t bit_offset = (clock_cycles - 1U) & 7U;
		cmd[4 + total_chunks - 1U] |= 1U << bit_offset;
	}
	if (data_in) {
		for (size_t cycle = 0; cycle < clock_cycles; cycle += 8U) {
			const size_t chunk = cycle >> 3U;
			const size_t index = 4 + total_chunks + chunk;
			cmd[index] = data_in[chunk];
		}
	}
	uint8_t result[4];
	bmda_usb_transfer(info.usb_link, cmd, cmd_len, data_out ? data_out : result, total_chunks);
	bmda_usb_transfer(info.usb_link, NULL, 0, result, 1);
	free(cmd);
	if (result[0] != 0)
		raise_exception(EXCEPTION_ERROR, "jtagtap_tdi_tdi failed");
}

static void jtagtap_tdi_seq(const bool final_tms, const uint8_t *const data_in, const size_t clock_cycles)
{
	DEBUG_PROBE("jtagtap_tdi_seq final tms: %u, data_in: ", final_tms ? 1 : 0);
	for (size_t cycle = 0; cycle < clock_cycles; cycle += 8U) {
		const size_t chunk = cycle >> 3U;
		if (chunk > 16U) {
			DEBUG_PROBE(" ...");
			break;
		}
		DEBUG_PROBE(" %02x", data_in[chunk]);
	}
	DEBUG_PROBE("\n");
	return jtagtap_tdi_tdo_seq(NULL, final_tms, data_in, clock_cycles);
}

static bool jtagtap_next(bool tms, bool tdi)
{
	DEBUG_PROBE("jtagtap_next tms: %u, tdi %u\n", tms ? 1 : 0, tdi ? 1 : 0);
	uint8_t cmd[6];
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = JLINK_CMD_IO_TRANSACT;
	cmd[2] = 1;
	if (tms)
		cmd[4] = 1;
	if (tdi)
		cmd[5] = 1;
	uint8_t tdo;
	bmda_usb_transfer(info.usb_link, cmd, 6, &tdo, 1);
	uint8_t result;
	bmda_usb_transfer(info.usb_link, NULL, 0, &result, 1);
	if (result != 0)
		raise_exception(EXCEPTION_ERROR, "jtagtap_next failed");
	return tdo & 1U;
}
