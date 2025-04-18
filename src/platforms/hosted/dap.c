/*
 * Copyright (C) 2013-2015 Alex Taradov <alex@taradov.com>
 * Copyright (C) 2020-2021 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
 * Copyright (C) 2023-2025 1BitSquared <info@1bitsquared.com>
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

#include <string.h>
#include "general.h"
#include "exception.h"
#include "cmsis_dap.h"
#include "dap.h"
#include "dap_command.h"
#include "jtag_scan.h"
#include "buffer_utils.h"

#define SWD_DP_R_IDCODE    0x00U
#define SWD_DP_W_ABORT     0x00U
#define SWD_DP_R_CTRL_STAT 0x04U
#define SWD_DP_W_CTRL_STAT 0x04U // When CTRLSEL == 0
#define SWD_DP_W_WCR       0x04U // When CTRLSEL == 1
#define SWD_DP_R_RESEND    0x08U
#define SWD_DP_W_SELECT    0x08U
#define SWD_DP_W_SELECT1   0x04U
#define SWD_DP_R_RDBUFF    0x0cU

#define SWD_DP_REG(reg, apsel) ((reg) | ((apsel) << 24U))

#define SWD_AP_CSW      (0x00U | DAP_TRANSFER_APnDP)
#define SWD_AP_TAR_LOW  (0x04U | DAP_TRANSFER_APnDP)
#define SWD_AP_TAR_HIGH (0x08U | DAP_TRANSFER_APnDP)
#define SWD_AP_DRW      (0x0cU | DAP_TRANSFER_APnDP)

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

static bool dap_transfer_configure(uint8_t idle_cycles, uint16_t wait_retries, uint16_t match_retries);

static uint32_t dap_current_clock_freq;
static bool dap_nrst_state = false;

bool dap_connect(void)
{
	/*
	 * Setup how DAP_TRANSFER* commands will work
	 * Sets 2 idle cycles between commands,
	 * 128 retries each for wait and match retries
	 */
	if (!dap_transfer_configure(2, 128, 128))
		return false;

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
uint32_t dap_max_frequency(const uint32_t clock)
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

static bool dap_transfer_configure(const uint8_t idle_cycles, const uint16_t wait_retries, const uint16_t match_retries)
{
	/* Setup the request buffer to configure DAP_TRANSFER* handling */
	uint8_t request[6] = {
		DAP_TRANSFER_CONFIGURE,
		idle_cycles,
	};
	write_le2(request, 2, wait_retries);
	write_le2(request, 4, match_retries);
	uint8_t result = DAP_RESPONSE_OK;
	/* Execute it and check if it failed */
	if (!dap_run_cmd(request, 6U, &result, 1U)) {
		DEBUG_PROBE("%s failed\n", __func__);
		return false;
	}
	/* Validate that it actually succeeded */
	return result == DAP_RESPONSE_OK;
}

size_t dap_info(const dap_info_e requested_info, void *const buffer, const size_t buffer_length)
{
	/* Setup the request buffer for the DAP_INFO request */
	const uint8_t request[2] = {
		DAP_INFO,
		requested_info,
	};
	uint8_t response[DAP_INFO_MAX_LENGTH + 1U] = {DAP_INFO_NO_INFO};
	/* Execute it and check if it failed */
	dap_run_cmd(request, 2U, response, DAP_INFO_MAX_LENGTH + 1U);
	if (response[0] == DAP_INFO_NO_INFO) {
		DEBUG_PROBE("%s failed or unsupported\n", __func__);
		return 0U;
	}
	/* Extract the response length, capped to the result buffer length */
	const size_t result_length = MIN(response[0], buffer_length);
	/* Copy out the response and possibly NUL terminate it (if there's room) */
	memcpy(buffer, response + 1, result_length);
	if (result_length < buffer_length)
		((uint8_t *)buffer)[result_length] = 0U;
	/* Return how many bytes were retrieved */
	return result_length;
}

bool dap_nrst_get_val(void)
{
	return dap_nrst_state;
}

bool dap_nrst_set_val(const bool nrst_state)
{
	/* Setup the request for the pin state change request */
	dap_swj_pins_request_s request = {
		.request = DAP_SWJ_PINS,
		/* nRST is active low, so take that into account */
		.pin_values = nrst_state ? 0U : DAP_SWJ_nRST,
		.selected_pins = DAP_SWJ_nRST,
	};
	/* Tell the hardware to wait for 10µs for the pin to settle */
	write_le4(request.wait_time, 0, 10);
	uint8_t response = 0U;
	/* Execute it and check if it failed */
	if (!dap_run_cmd(&request, 7U, &response, 1U)) {
		DEBUG_PROBE("%s failed\n", __func__);
		return false;
	}
	/* Extract the current pin state for the device, de-inverting it */
	dap_nrst_state = !(response & DAP_SWJ_nRST);
	return response == request.pin_values;
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
		if (perform_dap_transfer(target_dp, &request, 1U, NULL, 0U))
			return;
	} while (target_dp->fault == DAP_TRANSFER_WAIT);
}

bool dap_mem_read_block(
	adiv5_access_port_s *const target_ap, void *dest, target_addr64_t src, const size_t len, const align_e align)
{
	/* Try to read the 32-bit blocks requested */
	const size_t blocks = len >> MIN(align, 2U);
	uint32_t data[127U] = {0U};
	const bool result = perform_dap_transfer_block_read(target_ap->dp, SWD_AP_DRW, blocks, data);

	/* Unpack the data from those blocks */
	if (align > ALIGN_16BIT)
		memcpy(dest, data, len);
	else {
		for (size_t i = 0; i < blocks; ++i) {
			dest = adiv5_unpack_data(dest, src, data[i], align);
			src += 1U << align;
		}
	}

	/* Report if it actually failed and then propagate the failure up accordingly */
	if (!result)
		DEBUG_ERROR("dap_read_block failed\n");
	return result;
}

bool dap_mem_write_block(
	adiv5_access_port_s *const target_ap, target_addr64_t dest, const void *src, const size_t len, const align_e align)
{
	const size_t blocks = len >> MIN(align, 2U);
	uint32_t data[126U];

	/* Pack the data to send into 32-bit blocks */
	if (align > ALIGN_16BIT)
		memcpy(data, src, len);
	else {
		for (size_t i = 0; i < blocks; ++i) {
			src = adiv5_pack_data(dest, src, data + i, align);
			dest += 1U << align;
		}
	}

	/* Try to write the blocks to the target's memory */
	const bool result = perform_dap_transfer_block_write(target_ap->dp, SWD_AP_DRW, blocks, data);
	/* Report if it actually failed and then propagate the failure up accordingly */
	if (!result)
		DEBUG_ERROR("dap_write_block failed\n");
	return result;
}

static size_t dap_adiv5_mem_access_build(const adiv5_access_port_s *const target_ap,
	dap_transfer_request_s *const transfer_requests, const target_addr64_t addr, const align_e align)
{
	uint32_t csw = target_ap->csw | ADIV5_AP_CSW_ADDRINC_SINGLE;
	switch (align) {
	case ALIGN_8BIT:
		csw |= ADIV5_AP_CSW_SIZE_BYTE;
		break;
	case ALIGN_16BIT:
		csw |= ADIV5_AP_CSW_SIZE_HALFWORD;
		break;
	case ALIGN_64BIT:
	case ALIGN_32BIT:
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
	if (target_ap->flags & ADIV5_AP_FLAGS_64BIT) {
		transfer_requests[2].request = SWD_AP_TAR_HIGH;
		transfer_requests[2].data = (uint32_t)(addr >> 32U);
		transfer_requests[3].request = SWD_AP_TAR_LOW;
		transfer_requests[3].data = (uint32_t)addr;
		return 4U;
	} else {
		transfer_requests[2].request = SWD_AP_TAR_LOW;
		transfer_requests[2].data = (uint32_t)addr;
		return 3U;
	}
}

bool dap_adiv5_mem_access_setup(adiv5_access_port_s *const target_ap, const target_addr64_t addr, const align_e align)
{
	/* Start by setting up the transfer and attempting it */
	dap_transfer_request_s requests[4];
	const size_t requests_count = dap_adiv5_mem_access_build(target_ap, requests, addr, align);
	adiv5_debug_port_s *const target_dp = target_ap->dp;
	/* The result of this call is then fed up the stack for proper handling */
	return perform_dap_transfer_recoverable(target_dp, requests, requests_count, NULL, 0U);
}

static size_t dap_adiv6_mem_access_build(const adiv6_access_port_s *const target_ap,
	dap_transfer_request_s *const transfer_requests, const target_addr64_t addr, const align_e align)
{
	uint32_t csw = target_ap->base.csw | ADIV5_AP_CSW_ADDRINC_SINGLE;
	switch (align) {
	case ALIGN_8BIT:
		csw |= ADIV5_AP_CSW_SIZE_BYTE;
		break;
	case ALIGN_16BIT:
		csw |= ADIV5_AP_CSW_SIZE_HALFWORD;
		break;
	case ALIGN_64BIT:
	case ALIGN_32BIT:
		csw |= ADIV5_AP_CSW_SIZE_WORD;
		break;
	}
	/* Select the AP base address via SELECT1 */
	transfer_requests[0].request = SWD_DP_W_SELECT;
	transfer_requests[0].data = ADIV5_DP_BANK5;
	transfer_requests[1].request = SWD_DP_W_SELECT1;
	transfer_requests[1].data = (uint32_t)(target_ap->ap_address >> 32U);
	/* Select the bank for the CSW register */
	transfer_requests[2].request = SWD_DP_W_SELECT;
	transfer_requests[2].data = (uint32_t)target_ap->ap_address | (ADIV5_AP_CSW & ADIV6_AP_BANK_MASK);
	/* Then write the CSW register to the new value */
	transfer_requests[3].request = SWD_AP_CSW;
	transfer_requests[3].data = csw;
	/* Finally write the TAR register to its new value */
	if (target_ap->base.flags & ADIV5_AP_FLAGS_64BIT) {
		transfer_requests[4].request = SWD_AP_TAR_HIGH;
		transfer_requests[4].data = (uint32_t)(addr >> 32U);
		transfer_requests[5].request = SWD_AP_TAR_LOW;
		transfer_requests[5].data = (uint32_t)addr;
		return 6U;
	} else {
		transfer_requests[4].request = SWD_AP_TAR_LOW;
		transfer_requests[4].data = (uint32_t)addr;
		return 5U;
	}
}

bool dap_adiv6_mem_access_setup(adiv6_access_port_s *const target_ap, const target_addr64_t addr, const align_e align)
{
	/* Start by setting up the transfer and attempting it */
	dap_transfer_request_s requests[6];
	const size_t requests_count = dap_adiv6_mem_access_build(target_ap, requests, addr, align);
	adiv5_debug_port_s *const target_dp = target_ap->base.dp;
	/* The result of this call is then fed up the stack for proper handling */
	return perform_dap_transfer_recoverable(target_dp, requests, requests_count, NULL, 0U);
}

uint32_t dap_adiv5_ap_read(adiv5_access_port_s *const target_ap, const uint16_t addr)
{
	dap_transfer_request_s requests[2];
	DEBUG_PROBE("%s addr %x\n", __func__, addr);
	/* Select the bank for the register */
	requests[0].request = SWD_DP_W_SELECT;
	requests[0].data = SWD_DP_REG(addr & 0xf0U, target_ap->apsel);
	/* Read the register */
	requests[1].request = (addr & 0x0cU) | DAP_TRANSFER_RnW | (addr & ADIV5_APnDP ? DAP_TRANSFER_APnDP : 0);
	uint32_t result = 0;
	adiv5_debug_port_s *const target_dp = target_ap->dp;
	if (!perform_dap_transfer(target_dp, requests, 2U, &result, 1U)) {
		DEBUG_ERROR("%s failed (fault = %u)\n", __func__, target_dp->fault);
		return 0U;
	}
	return result;
}

void dap_adiv5_ap_write(adiv5_access_port_s *const target_ap, const uint16_t addr, const uint32_t value)
{
	dap_transfer_request_s requests[2];
	DEBUG_PROBE("%s addr %04x value %08" PRIx32 "\n", __func__, addr, value);
	/* Select the bank for the register */
	requests[0].request = SWD_DP_W_SELECT;
	requests[0].data = SWD_DP_REG(addr & 0xf0U, target_ap->apsel);
	/* Write the register */
	requests[1].request = (addr & 0x0cU) | (addr & ADIV5_APnDP ? DAP_TRANSFER_APnDP : 0);
	requests[1].data = value;
	adiv5_debug_port_s *const target_dp = target_ap->dp;
	if (!perform_dap_transfer(target_dp, requests, 2U, NULL, 0U))
		DEBUG_ERROR("%s failed (fault = %u)\n", __func__, target_dp->fault);
}

uint32_t dap_adiv6_ap_read(adiv5_access_port_s *const base_ap, const uint16_t addr)
{
	adiv6_access_port_s *const target_ap = (adiv6_access_port_s *)base_ap;
	dap_transfer_request_s requests[4];
	DEBUG_PROBE("%s addr %x\n", __func__, addr);
	/* Set SELECT1 in the DP up first */
	requests[0].request = SWD_DP_W_SELECT;
	requests[0].data = ADIV5_DP_BANK5;
	requests[1].request = SWD_DP_W_SELECT1;
	requests[1].data = (uint32_t)(target_ap->ap_address >> 32U);
	/* Now set up SELECT in the DP */
	requests[2].request = SWD_DP_W_SELECT;
	requests[2].data = (uint32_t)target_ap->ap_address | (addr & ADIV6_AP_BANK_MASK);
	/* Read the register */
	requests[3].request = (addr & 0x0cU) | DAP_TRANSFER_RnW | (addr & ADIV5_APnDP ? DAP_TRANSFER_APnDP : 0);
	uint32_t result = 0;
	adiv5_debug_port_s *const target_dp = base_ap->dp;
	if (!perform_dap_transfer(target_dp, requests, 4U, &result, 1U)) {
		DEBUG_ERROR("%s failed (fault = %u)\n", __func__, target_dp->fault);
		return 0U;
	}
	return result;
}

void dap_adiv6_ap_write(adiv5_access_port_s *const base_ap, const uint16_t addr, const uint32_t value)
{
	adiv6_access_port_s *const target_ap = (adiv6_access_port_s *)base_ap;
	dap_transfer_request_s requests[4];
	DEBUG_PROBE("%s addr %04x value %08" PRIx32 "\n", __func__, addr, value);
	/* Set SELECT1 in the DP up first */
	requests[0].request = SWD_DP_W_SELECT;
	requests[0].data = ADIV5_DP_BANK5;
	requests[1].request = SWD_DP_W_SELECT1;
	requests[1].data = (uint32_t)(target_ap->ap_address >> 32U);
	/* Now set up SELECT in the DP */
	requests[2].request = SWD_DP_W_SELECT;
	requests[2].data = (uint32_t)target_ap->ap_address | (addr & ADIV6_AP_BANK_MASK);
	/* Write the register */
	requests[3].request = (addr & 0x0cU) | (addr & ADIV5_APnDP ? DAP_TRANSFER_APnDP : 0);
	requests[3].data = value;
	adiv5_debug_port_s *const target_dp = base_ap->dp;
	if (!perform_dap_transfer(target_dp, requests, 4U, NULL, 0U))
		DEBUG_ERROR("%s failed (fault = %u)\n", __func__, target_dp->fault);
}

void dap_adiv5_mem_read_single(
	adiv5_access_port_s *const target_ap, void *const dest, const target_addr64_t src, const align_e align)
{
	dap_transfer_request_s requests[5];
	const size_t requests_count = dap_adiv5_mem_access_build(target_ap, requests, src, align);
	requests[requests_count].request = SWD_AP_DRW | DAP_TRANSFER_RnW;
	uint32_t result;
	adiv5_debug_port_s *target_dp = target_ap->dp;
	if (!perform_dap_transfer_recoverable(target_dp, requests, requests_count + 1U, &result, 1U)) {
		DEBUG_ERROR("dap_read_single failed (fault = %u)\n", target_dp->fault);
		memset(dest, 0, 1U << align);
		return;
	}
	/* Pull out the data. AP_DRW access implies an RDBUFF in CMSIS-DAP, so this is safe */
	adiv5_unpack_data(dest, src, result, align);
}

void dap_adiv5_mem_write_single(
	adiv5_access_port_s *const target_ap, const target_addr64_t dest, const void *const src, const align_e align)
{
	dap_transfer_request_s requests[5];
	const size_t requests_count = dap_adiv5_mem_access_build(target_ap, requests, dest, align);
	requests[requests_count].request = SWD_AP_DRW;
	/* Pack data into correct data lane */
	adiv5_pack_data(dest, src, &requests[requests_count].data, align);
	adiv5_debug_port_s *target_dp = target_ap->dp;
	if (!perform_dap_transfer_recoverable(target_dp, requests, requests_count + 1U, NULL, 0U))
		DEBUG_ERROR("dap_write_single failed (fault = %u)\n", target_dp->fault);
}

void dap_adiv6_mem_read_single(
	adiv6_access_port_s *const target_ap, void *const dest, const target_addr64_t src, const align_e align)
{
	dap_transfer_request_s requests[7];
	const size_t requests_count = dap_adiv6_mem_access_build(target_ap, requests, src, align);
	requests[requests_count].request = SWD_AP_DRW | DAP_TRANSFER_RnW;
	uint32_t result;
	adiv5_debug_port_s *target_dp = target_ap->base.dp;
	if (!perform_dap_transfer_recoverable(target_dp, requests, requests_count + 1U, &result, 1U)) {
		DEBUG_ERROR("dap_read_single failed (fault = %u)\n", target_dp->fault);
		memset(dest, 0, 1U << align);
		return;
	}
	/* Pull out the data. AP_DRW access implies an RDBUFF in CMSIS-DAP, so this is safe */
	adiv5_unpack_data(dest, src, result, align);
}

void dap_adiv6_mem_write_single(
	adiv6_access_port_s *const target_ap, const target_addr64_t dest, const void *const src, const align_e align)
{
	dap_transfer_request_s requests[7];
	const size_t requests_count = dap_adiv6_mem_access_build(target_ap, requests, dest, align);
	requests[requests_count].request = SWD_AP_DRW;
	/* Pack data into correct data lane */
	adiv5_pack_data(dest, src, &requests[requests_count].data, align);
	adiv5_debug_port_s *target_dp = target_ap->base.dp;
	if (!perform_dap_transfer_recoverable(target_dp, requests, requests_count + 1U, NULL, 0U))
		DEBUG_ERROR("dap_write_single failed (fault = %u)\n", target_dp->fault);
}
