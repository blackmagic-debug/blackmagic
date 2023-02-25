/*
 * Copyright (c) 2013-2015, Alex Taradov <alex@taradov.com>
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Modified for Blackmagic Probe
 * Copyright (c) 2020-21 Uwe Bonnes bon@elektron.ikp.physik.tu-darmstadt.de
 */

#include <string.h>
#include "general.h"
#include "exception.h"
#include "dap.h"
#include "dap_command.h"
#include "jtag_scan.h"
#include "buffer_utils.h"

#define ID_DAP_INFO               0x00U
#define ID_DAP_TRANSFER_CONFIGURE 0x04U
#define ID_DAP_RESET_TARGET       0x0aU
#define ID_DAP_SWJ_PINS           0x10U
#define ID_DAP_SWD_CONFIGURE      0x13U
#define ID_DAP_JTAG_CONFIGURE     0x15U

#define DAP_TRANSFER_APnDP (1U << 0U)
#define DAP_TRANSFER_RnW   (1U << 1U)

#define DAP_TRANSFER_WAIT (1U << 1U)

#define DAP_SWJ_nTRST  (1U << 5U)
#define DAP_SWJ_nRESET (1U << 7U)

#define SWD_DP_R_IDCODE    0x00U
#define SWD_DP_W_ABORT     0x00U
#define SWD_DP_R_CTRL_STAT 0x04U
#define SWD_DP_W_CTRL_STAT 0x04U // When CTRLSEL == 0
#define SWD_DP_W_WCR       0x04U // When CTRLSEL == 1
#define SWD_DP_R_RESEND    0x08U
#define SWD_DP_W_SELECT    0x08U
#define SWD_DP_R_RDBUFF    0x0cU

#define SWD_DP_REG(reg, apsel) ((reg) | ((apsel) << 24U))

#define SWD_AP_CSW (0x00U | DAP_TRANSFER_APnDP)
#define SWD_AP_TAR (0x04U | DAP_TRANSFER_APnDP)
#define SWD_AP_DRW (0x0cU | DAP_TRANSFER_APnDP)

#define SWD_AP_DB0 (0x00U | DAP_TRANSFER_APnDP) // 0x10
#define SWD_AP_DB1 (0x04U | DAP_TRANSFER_APnDP) // 0x14
#define SWD_AP_DB2 (0x08U | DAP_TRANSFER_APnDP) // 0x18
#define SWD_AP_DB3 (0x0cU | DAP_TRANSFER_APnDP) // 0x1c

#define SWD_AP_CFG  (0x04U | DAP_TRANSFER_APnDP) // 0xf4
#define SWD_AP_BASE (0x08U | DAP_TRANSFER_APnDP) // 0xf8
#define SWD_AP_IDR  (0x0cU | DAP_TRANSFER_APnDP) // 0xfc

#define DP_SELECT_CTRLSEL      (1U << 0U)
#define DP_SELECT_APBANKSEL(x) ((x) << 4U)
#define DP_SELECT_APSEL(x)     ((x) << 24U)

#define AP_CSW_SIZE_BYTE      (0U << 0U)
#define AP_CSW_SIZE_HALF      (1U << 0U)
#define AP_CSW_SIZE_WORD      (2U << 0U)
#define AP_CSW_ADDRINC_OFF    (0U << 4U)
#define AP_CSW_ADDRINC_SINGLE (1U << 4U)
#define AP_CSW_ADDRINC_PACKED (2U << 4U)
#define AP_CSW_DEVICEEN       (1U << 6U)
#define AP_CSW_TRINPROG       (1U << 7U)
#define AP_CSW_SPIDEN         (1U << 23U)
#define AP_CSW_PROT(x)        ((x) << 24U)
#define AP_CSW_DBGSWENABLE    (1U << 31U)

static uint32_t dap_current_clock_freq;

bool dap_connect(void)
{
	/* Setup the connection request */
	const uint8_t request[2] = {
		DAP_CONNECT,
		dap_mode == DAP_CAP_JTAG ? DAP_PORT_JTAG : DAP_PORT_SWD,
	};
	uint8_t result = DAP_PORT_DEFAULT;
	/* Execute it and check if it failed */
	if (!dap_run_cmd(request, 2U, &result, 1U)) {
		DEBUG_PROBE("%s failed\n", __func__);
		return false;
	}
	/* Check that the port requested matches the port initialised and setup the LEDs accordingly */
	return dap_led(DAP_LED_CONNECT, result == request[1]) && result == request[1];
}

bool dap_disconnect(void)
{
	/* Disconnect is just the command byte and a DAP response code */
	const uint8_t request = DAP_DISCONNECT;
	uint8_t result = DAP_RESPONSE_OK;
	/* Execute it and check if it failed */
	if (!dap_run_cmd(&request, 1U, &result, 1U)) {
		DEBUG_PROBE("%s failed\n", __func__);
		return false;
	}
	/* Setup the LEDs according to the result */
	return dap_led(DAP_LED_CONNECT, result != DAP_RESPONSE_OK) && result == DAP_RESPONSE_OK;
}

bool dap_led(const dap_led_type_e type, const bool state)
{
	/* Setup the host status (LED) information command */
	const uint8_t request[3] = {
		DAP_HOST_STATUS,
		type,
		/* Map the bool state to a DAP status value */
		state ? 1U : 0U,
	};
	uint8_t result = DAP_RESPONSE_OK;
	/* Execute it and check if it failed */
	if (!dap_run_cmd(request, 3U, &result, 1U)) {
		DEBUG_PROBE("%s failed\n", __func__);
		return false;
	}
	return result == DAP_RESPONSE_OK;
}

/*
 * Accessor for the current JTAG/SWD clock frequency.
 * When called with clock == 0, it only returns the current value.
 */
uint32_t dap_swj_clock(const uint32_t clock)
{
	/* Fast-return if clock is 0 */
	if (!clock)
		return dap_current_clock_freq;
	/* Setup the request buffer to change the clock frequency */
	uint8_t request[5] = {DAP_SWJ_CLOCK};
	write_le4(request, 1, clock);
	uint8_t result = DAP_RESPONSE_OK;
	/* Execute it and check if it failed */
	if (!dap_run_cmd(request, 5U, &result, 1U)) {
		DEBUG_PROBE("%s failed\n", __func__);
		return false;
	}
	/* Check that it succeeded before changing the cached frequency */
	if (result == DAP_RESPONSE_OK)
		dap_current_clock_freq = clock;
	return dap_current_clock_freq;
}

void dap_transfer_configure(uint8_t idle, uint16_t count, uint16_t retry)
{
	uint8_t buf[6];

	buf[0] = ID_DAP_TRANSFER_CONFIGURE;
	buf[1] = idle;
	buf[2] = count & 0xffU;
	buf[3] = (count >> 8U) & 0xffU;
	buf[4] = retry & 0xffU;
	buf[5] = (retry >> 8U) & 0xffU;
	dbg_dap_cmd(buf, sizeof(buf), 6);
}

//-----------------------------------------------------------------------------
void dap_swd_configure(uint8_t cfg)
{
	uint8_t buf[2];

	buf[0] = ID_DAP_SWD_CONFIGURE;
	buf[1] = cfg;
	dbg_dap_cmd(buf, sizeof(buf), 2);
}

//-----------------------------------------------------------------------------
size_t dap_info(const dap_info_e info, uint8_t *const data, const size_t size)
{
	uint8_t buf[256];
	size_t rsize;

	buf[0] = ID_DAP_INFO;
	buf[1] = info;
	dbg_dap_cmd(buf, sizeof(buf), 2);

	rsize = size < buf[0] ? size : buf[0];
	memcpy(data, &buf[1], rsize);

	if (rsize < size)
		data[rsize] = 0;

	return rsize;
}

void dap_reset_pin(int state)
{
	uint8_t buf[7];

	buf[0] = ID_DAP_SWJ_PINS;
	buf[1] = state ? DAP_SWJ_nRESET : 0; // Value
	buf[2] = DAP_SWJ_nRESET;             // Select
	buf[3] = 0;                          // Wait
	buf[4] = 0;                          // Wait
	buf[5] = 0;                          // Wait
	buf[6] = 0;                          // Wait
	dbg_dap_cmd(buf, sizeof(buf), 7);
}

void dap_trst_reset(void)
{
	uint8_t buf[7];

	buf[0] = ID_DAP_SWJ_PINS;
	buf[1] = DAP_SWJ_nTRST;
	buf[2] = 0;
	buf[3] = 0;
	buf[4] = 4; /* ~ 1 ms*/
	buf[5] = 0;
	buf[6] = 0;
	dbg_dap_cmd(buf, sizeof(buf), 7);

	buf[0] = ID_DAP_SWJ_PINS;
	buf[1] = DAP_SWJ_nTRST;
	buf[2] = DAP_SWJ_nTRST;
	dbg_dap_cmd(buf, sizeof(buf), 7);
}

uint32_t dap_read_reg(adiv5_debug_port_s *target_dp, const uint8_t reg)
{
	const dap_transfer_request_s request = {.request = reg | DAP_TRANSFER_RnW};
	uint32_t value = 0;
	do {
		if (perform_dap_transfer(target_dp, &request, 1U, &value, 1U)) {
			DEBUG_PROBE("dap_read_reg: %02x -> %08x\n", reg, value);
			return value;
		}
	} while (target_dp->fault == DAP_TRANSFER_WAIT);
	return 0U;
}

void dap_write_reg(adiv5_debug_port_s *target_dp, const uint8_t reg, const uint32_t value)
{
	DEBUG_PROBE("dap_write_reg: %02x <- %08x\n", reg, value);
	const dap_transfer_request_s request = {
		.request = reg & ~DAP_TRANSFER_RnW,
		.data = value,
	};

	do {
		if (perform_dap_transfer(target_dp, &request, 1U, NULL, 0))
			return;
	} while (target_dp->fault == DAP_TRANSFER_WAIT);
}

bool dap_read_block(
	adiv5_access_port_s *const target_ap, void *dest, uint32_t src, const size_t len, const align_e align)
{
	const size_t blocks = len >> MIN(align, 2U);
	uint32_t data[256];
	if (!perform_dap_transfer_block_read(target_ap->dp, SWD_AP_DRW, blocks, data)) {
		DEBUG_WARN("dap_read_block failed\n");
		return false;
	}

	if (align > ALIGN_HALFWORD)
		memcpy(dest, data, len);
	else {
		for (size_t i = 0; i < blocks; ++i) {
			dest = adiv5_unpack_data(dest, src, data[i], align);
			src += 1U << align;
		}
	}
	return true;
}

bool dap_write_block(
	adiv5_access_port_s *const target_ap, uint32_t dest, const void *src, const size_t len, const align_e align)
{
	const size_t blocks = len >> MAX(align, 2U);
	uint32_t data[256];

	if (align > ALIGN_HALFWORD)
		memcpy(data, src, len);
	else {
		for (size_t i = 0; i < blocks; ++i) {
			src = adiv5_pack_data(dest, src, data + i, align);
			dest += 1U << align;
		}
	}

	const bool result = perform_dap_transfer_block_write(target_ap->dp, SWD_AP_DRW, blocks, data);
	if (!result)
		DEBUG_WARN("dap_write_block failed\n");
	return result;
}

void dap_reset_link(adiv5_debug_port_s *const target_dp, const bool jtag)
{
	uint8_t sequence[18U];
	size_t bytes = 8U;
	memset(sequence, 0xffU, bytes);

	if (jtag) {
		sequence[bytes + 0] = 0x3cU;
		sequence[bytes + 1] = 0xe7U;
		sequence[bytes + 2] = 0x1fU;
		bytes += 3U;
	} else {
		sequence[bytes + 0] = 0x9e;
		sequence[bytes + 1] = 0xe7;
		bytes += 2;
		memset(sequence + bytes, 0xffU, 7U);
		bytes += 7U;
		sequence[bytes++] = 0x00U;
	}
	const size_t cycles = (bytes * 8U) - (jtag ? 2U : 0U);
	perform_dap_swj_sequence(cycles, sequence);

	if (!jtag) {
		const dap_transfer_request_s request = {.request = SWD_DP_R_IDCODE | DAP_TRANSFER_RnW};
		uint32_t response = 0;
		perform_dap_transfer(target_dp, &request, 1, &response, 1);
	}
}

static void mem_access_setup(const adiv5_access_port_s *const target_ap,
	dap_transfer_request_s *const transfer_requests, const uint32_t addr, const align_e align)
{
	uint32_t csw = target_ap->csw | ADIV5_AP_CSW_ADDRINC_SINGLE;
	switch (align) {
	case ALIGN_BYTE:
		csw |= ADIV5_AP_CSW_SIZE_BYTE;
		break;
	case ALIGN_HALFWORD:
		csw |= ADIV5_AP_CSW_SIZE_HALFWORD;
		break;
	case ALIGN_DWORD:
	case ALIGN_WORD:
		csw |= ADIV5_AP_CSW_SIZE_WORD;
		break;
	}
	/* Select the bank for the CSW register */
	transfer_requests[0].request = SWD_DP_W_SELECT;
	transfer_requests[0].data = SWD_DP_REG(ADIV5_AP_CSW & 0xf0U, target_ap->apsel);
	/* Then write the CSW register to the new value */
	transfer_requests[1].request = SWD_AP_CSW;
	transfer_requests[1].data = csw;
	/* Finally write the TAR register to its new value */
	transfer_requests[2].request = SWD_AP_TAR;
	transfer_requests[2].data = addr;
}

void dap_ap_mem_access_setup(adiv5_access_port_s *const target_ap, const uint32_t addr, const align_e align)
{
	/* Start by setting up the transfer and attempting it */
	dap_transfer_request_s requests[3];
	mem_access_setup(target_ap, requests, addr, align);
	adiv5_debug_port_s *const target_dp = target_ap->dp;
	const bool result = perform_dap_transfer_recoverable(target_dp, requests, 3U, NULL, 0U);
	/* If it didn't go well, say something and abort */
	if (!result) {
		if (target_dp->fault != DAP_TRANSFER_NO_RESPONSE)
			DEBUG_WARN("Transport error (%u), aborting\n", target_dp->fault);
		else
			DEBUG_WARN("Transaction unrecoverably failed\n");
		exit(-1);
	}
}

uint32_t dap_ap_read(adiv5_access_port_s *const target_ap, const uint16_t addr)
{
	dap_transfer_request_s requests[2];
	DEBUG_PROBE("dap_ap_read addr %x\n", addr);
	/* Select the bank for the register */
	requests[0].request = SWD_DP_W_SELECT;
	requests[0].data = SWD_DP_REG(addr & 0xf0U, target_ap->apsel);
	/* Read the register */
	requests[1].request = (addr & 0x0cU) | DAP_TRANSFER_RnW | (addr & 0x100U ? DAP_TRANSFER_APnDP : 0);
	uint32_t result = 0;
	adiv5_debug_port_s *const target_dp = target_ap->dp;
	if (!perform_dap_transfer(target_dp, requests, 2U, &result, 1U)) {
		DEBUG_WARN("dap_ap_read failed (fault = %u)\n", target_dp->fault);
		return 0U;
	}
	return result;
}

void dap_ap_write(adiv5_access_port_s *const target_ap, const uint16_t addr, const uint32_t value)
{
	dap_transfer_request_s requests[2];
	DEBUG_PROBE("dap_ap_write addr %04x value %08" PRIx32 "\n", addr, value);
	/* Select the bank for the register */
	requests[0].request = SWD_DP_W_SELECT;
	requests[0].data = SWD_DP_REG(addr & 0xf0U, target_ap->apsel);
	/* Write the register */
	requests[1].request = (addr & 0x0cU) | (addr & 0x100U ? DAP_TRANSFER_APnDP : 0);
	requests[1].data = value;
	adiv5_debug_port_s *const target_dp = target_ap->dp;
	if (!perform_dap_transfer(target_dp, requests, 2U, NULL, 0U))
		DEBUG_WARN("dap_ap_write failed (fault = %u)\n", target_dp->fault);
}

void dap_read_single(adiv5_access_port_s *const target_ap, void *const dest, const uint32_t src, const align_e align)
{
	dap_transfer_request_s requests[4];
	mem_access_setup(target_ap, requests, src, align);
	requests[3].request = SWD_AP_DRW | DAP_TRANSFER_RnW;
	uint32_t result;
	adiv5_debug_port_s *target_dp = target_ap->dp;
	if (!perform_dap_transfer_recoverable(target_dp, requests, 4U, &result, 1U)) {
		DEBUG_WARN("dap_read_single failed (fault = %u)\n", target_dp->fault);
		memset(dest, 0, 1U << align);
		return;
	}
	/* Pull out the data. AP_DRW access implies an RDBUFF in CMSIS-DAP, so this is safe */
	adiv5_unpack_data(dest, src, result, align);
}

void dap_write_single(
	adiv5_access_port_s *const target_ap, const uint32_t dest, const void *const src, const align_e align)
{
	dap_transfer_request_s requests[4];
	mem_access_setup(target_ap, requests, dest, align);
	requests[3].request = SWD_AP_DRW;
	/* Pack data into correct data lane */
	adiv5_pack_data(dest, src, &requests[3].data, align);
	adiv5_debug_port_s *target_dp = target_ap->dp;
	if (!perform_dap_transfer_recoverable(target_dp, requests, 4U, NULL, 0U))
		DEBUG_WARN("dap_write_single failed (fault = %u)\n", target_dp->fault);
}

bool dap_jtag_configure(void)
{
	/* Check if there are no or too many devices */
	if (!jtag_dev_count || jtag_dev_count >= JTAG_MAX_DEVS)
		return false;
	/* Begin building the configuration packet */
	uint8_t request[2U + JTAG_MAX_DEVS] = {
		ID_DAP_JTAG_CONFIGURE,
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
		DEBUG_WARN("dap_jtag_configure failed with %02x\n", response);
	return response == DAP_RESPONSE_OK;
}
