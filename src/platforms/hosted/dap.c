/*
 * Copyright (c) 2013-2015, Alex Taradov <alex@taradov.com>
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

/*- Definitions -------------------------------------------------------------*/
#define ID_DAP_INFO               0x00U
#define ID_DAP_LED                0x01U
#define ID_DAP_CONNECT            0x02U
#define ID_DAP_DISCONNECT         0x03U
#define ID_DAP_TRANSFER_CONFIGURE 0x04U
#define ID_DAP_TRANSFER           0x05U
#define ID_DAP_TRANSFER_BLOCK     0x06U
#define ID_DAP_TRANSFER_ABORT     0x07U
#define ID_DAP_WRITE_ABORT        0x08U
#define ID_DAP_DELAY              0x09U
#define ID_DAP_RESET_TARGET       0x0aU
#define ID_DAP_SWJ_PINS           0x10U
#define ID_DAP_SWJ_CLOCK          0x11U
#define ID_DAP_SWJ_SEQUENCE       0x12U
#define ID_DAP_SWD_CONFIGURE      0x13U
#define ID_DAP_JTAG_SEQUENCE      0x14U
#define ID_DAP_JTAG_CONFIGURE     0x15U
#define ID_DAP_JTAG_IDCODE        0x16U
#define ID_DAP_SWD_SEQUENCE       0x1dU

#define DAP_TRANSFER_APnDP       (1U << 0U)
#define DAP_TRANSFER_RnW         (1U << 1U)
#define DAP_TRANSFER_A2          (1U << 2U)
#define DAP_TRANSFER_A3          (1U << 3U)
#define DAP_TRANSFER_MATCH_VALUE (1U << 4U)
#define DAP_TRANSFER_MATCH_MASK  (1U << 5U)

#define DAP_TRANSFER_INVALID   0U
#define DAP_TRANSFER_OK        (1U << 0U)
#define DAP_TRANSFER_WAIT      (1U << 1U)
#define DAP_TRANSFER_FAULT     (1U << 2U)
#define DAP_TRANSFER_ERROR     (1U << 3U)
#define DAP_TRANSFER_MISMATCH  (1U << 4U)
#define DAP_TRANSFER_NO_TARGET 7U

#define DAP_SWJ_SWCLK_TCK (1U << 0U)
#define DAP_SWJ_SWDIO_TMS (1U << 1U)
#define DAP_SWJ_TDI       (1U << 2U)
#define DAP_SWJ_TDO       (1U << 3U)
#define DAP_SWJ_nTRST     (1U << 5U)
#define DAP_SWJ_nRESET    (1U << 7U)

#define DAP_OK    0x00U
#define DAP_ERROR 0xffU

#define DAP_JTAG_TMS         (1U << 6U)
#define DAP_JTAG_TDO_CAPTURE (1U << 7U)

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

#define DP_ABORT_DAPABORT   (1U << 0U)
#define DP_ABORT_STKCMPCLR  (1U << 1U)
#define DP_ABORT_STKERRCLR  (1U << 2U)
#define DP_ABORT_WDERRCLR   (1U << 3U)
#define DP_ABORT_ORUNERRCLR (1U << 4U)

#define DP_CST_ORUNDETECT      (1U << 0U)
#define DP_CST_STICKYORUN      (1U << 1U)
#define DP_CST_TRNMODE_NORMAL  (0U << 2U)
#define DP_CST_TRNMODE_VERIFY  (1U << 2U)
#define DP_CST_TRNMODE_COMPARE (2U << 2U)
#define DP_CST_STICKYCMP       (1U << 4U)
#define DP_CST_STICKYERR       (1U << 5U)
#define DP_CST_READOK          (1U << 6U)
#define DP_CST_WDATAERR        (1U << 7U)
#define DP_CST_MASKLANE(x)     ((x) << 8U)
#define DP_CST_TRNCNT(x)       ((x) << 12U)
#define DP_CST_CDBGRSTREQ      (1U << 26U)
#define DP_CST_CDBGRSTACK      (1U << 27U)
#define DP_CST_CDBGPWRUPREQ    (1U << 28U)
#define DP_CST_CDBGPWRUPACK    (1U << 29U)
#define DP_CST_CSYSPWRUPREQ    (1U << 30U)
#define DP_CST_CSYSPWRUPACK    (1U << 31U)

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

/*- Implementations ---------------------------------------------------------*/

//-----------------------------------------------------------------------------
void dap_led(int index, int state)
{
	uint8_t buf[3];

	buf[0] = ID_DAP_LED;
	buf[1] = index;
	buf[2] = state;
	dbg_dap_cmd(buf, sizeof(buf), 3);
}

//-----------------------------------------------------------------------------
void dap_connect(bool jtag)
{
	uint8_t buf[2];

	buf[0] = ID_DAP_CONNECT;
	buf[1] = jtag ? DAP_CAP_JTAG : DAP_CAP_SWD;
	dbg_dap_cmd(buf, sizeof(buf), 2);
}

//-----------------------------------------------------------------------------
void dap_disconnect(void)
{
	uint8_t buf[65];

	buf[0] = ID_DAP_DISCONNECT;
	dbg_dap_cmd(buf, sizeof(buf), 1);
}

static uint32_t swj_clock;

/* Set/Get JTAG/SWD clock frequency
 *
 * With clock == 0, return last set value.
 */
uint32_t dap_swj_clock(uint32_t clock)
{
	if (clock == 0)
		return swj_clock;
	uint8_t buf[5];
	buf[0] = ID_DAP_SWJ_CLOCK;
	buf[1] = clock & 0xffU;
	buf[2] = (clock >> 8U) & 0xffU;
	buf[3] = (clock >> 16U) & 0xffU;
	buf[4] = (clock >> 24U) & 0xffU;
	dbg_dap_cmd(buf, sizeof(buf), 5);
	if (buf[0])
		DEBUG_WARN("dap_swj_clock failed\n");
	else
		swj_clock = clock;
	return swj_clock;
}

//-----------------------------------------------------------------------------
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

void dap_line_reset(void)
{
	const uint8_t data[8] = {
		0xffU,
		0xffU,
		0xffU,
		0xffU,
		0xffU,
		0xffU,
		0xffU,
		0x0fU,
	};
	if (!perform_dap_swj_sequence(64, data))
		DEBUG_WARN("line reset failed\n");
}

static uint32_t wait_word(uint8_t *buf, size_t response_length, size_t request_length, uint8_t *dp_fault)
{
	uint8_t cmd_copy[request_length];
	memcpy(cmd_copy, buf, request_length);
	do {
		memcpy(buf, cmd_copy, request_length);
		dbg_dap_cmd(buf, response_length, request_length);
		if (buf[1] < DAP_TRANSFER_WAIT)
			break;
	} while (buf[1] == DAP_TRANSFER_WAIT);

	if (buf[1] == SWDP_ACK_FAULT) {
		*dp_fault = 1;
		return 0;
	}

	if (buf[1] != SWDP_ACK_OK)
		raise_exception(EXCEPTION_ERROR, "SWDP invalid ACK");
	uint32_t res = ((uint32_t)buf[5] << 24U) | ((uint32_t)buf[4] << 16U) | ((uint32_t)buf[3] << 8U) | (uint32_t)buf[2];
	return res;
}

//-----------------------------------------------------------------------------
uint32_t dap_read_reg(adiv5_debug_port_s *dp, uint8_t reg)
{
	uint8_t buf[8];
	uint8_t dap_index = 0;
	dap_index = dp->dp_jd_index;
	buf[0] = ID_DAP_TRANSFER;
	buf[1] = dap_index;
	buf[2] = 0x01; // Request size
	buf[3] = reg | DAP_TRANSFER_RnW;
	uint32_t res = wait_word(buf, 8, 4, &dp->fault);
	DEBUG_WIRE("\tdap_read_reg %02x %08x\n", reg, res);
	return res;
}

//-----------------------------------------------------------------------------
void dap_write_reg(adiv5_debug_port_s *dp, uint8_t reg, uint32_t data)
{
	DEBUG_PROBE("\tdap_write_reg %02x %08x\n", reg, data);

	uint8_t buf[8] = {
		ID_DAP_TRANSFER,
		dp->dp_jd_index,
		0x01, // Request size
		reg & ~DAP_TRANSFER_RnW,
		data & 0xffU,
		(data >> 8U) & 0xffU,
		(data >> 16U) & 0xffU,
		(data >> 24U) & 0xffU,
	};

	uint8_t cmd_copy[8];
	memcpy(cmd_copy, buf, 8);
	do {
		memcpy(buf, cmd_copy, 8);
		dbg_dap_cmd(buf, sizeof(buf), 8);
	} while (buf[1] == DAP_TRANSFER_WAIT);

	if (buf[1] > DAP_TRANSFER_WAIT) {
		DEBUG_WARN("dap_write_reg %02x data %08x:fault\n", reg, data);
		dp->fault = 1;
	}
	if (buf[1] == DAP_TRANSFER_ERROR) {
		DEBUG_WARN("dap_write_reg %02x data %08x: protocol error\n", reg, data);
		dp->fault = 7;
	}
}

bool dap_read_block(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len, align_e align)
{
	const size_t blocks = len >> MIN(align, 2U);
	uint32_t data[256];
	if (!perform_dap_transfer_block_read(ap->dp, SWD_AP_DRW, blocks, data)) {
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

bool dap_write_block(adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align)
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

	const bool result = perform_dap_transfer_block_write(ap->dp, SWD_AP_DRW, blocks, data);
	if (!result)
		DEBUG_WARN("dap_write_block failed\n");
	return result;
}

void dap_reset_link(bool jtag)
{
	uint8_t buf[128], *p = buf;

	//-------------
	*p++ = ID_DAP_SWJ_SEQUENCE;
	p++;
	*p++ = 0xff;
	*p++ = 0xff;
	*p++ = 0xff;
	*p++ = 0xff;
	*p++ = 0xff;
	*p++ = 0xff;
	*p++ = 0xff;
	if (jtag) {
		*p++ = 0x3c;
		*p++ = 0xe7;
		*p++ = 0x1f;
		buf[1] = ((p - (buf + 2U)) * 8U) - 2U;
	} else {
		*p++ = 0x9e;
		*p++ = 0xe7;
		*p++ = 0xff;
		*p++ = 0xff;
		*p++ = 0xff;
		*p++ = 0xff;
		*p++ = 0xff;
		*p++ = 0xff;
		*p++ = 0xff;
		*p++ = 0x00;
		buf[1] = (p - (buf + 2U)) * 8U;
	}
	dbg_dap_cmd(buf, sizeof(buf), p - buf);

	if (!jtag) {
		//-------------
		buf[0] = ID_DAP_TRANSFER;
		buf[1] = 0; // DAP index
		buf[2] = 1; // Request size
		buf[3] = SWD_DP_R_IDCODE | DAP_TRANSFER_RnW;
		dbg_dap_cmd(buf, sizeof(buf), 4);
	}
}

//-----------------------------------------------------------------------------
uint32_t dap_read_idcode(adiv5_debug_port_s *dp)
{
	return dap_read_reg(dp, SWD_DP_R_IDCODE);
}

static void mem_access_setup(const adiv5_access_port_s *const ap, dap_transfer_request_s *const transfer_requests,
	const uint32_t addr, const align_e align)
{
	uint32_t csw = ap->csw | ADIV5_AP_CSW_ADDRINC_SINGLE;
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
	transfer_requests[0].data = SWD_DP_REG(ADIV5_AP_CSW & 0xf0U, ap->apsel);
	/* Then write the CSW register to the new value */
	transfer_requests[1].request = SWD_AP_CSW;
	transfer_requests[1].data = csw;
	/* Finally write the TAR register to its new value */
	transfer_requests[2].request = SWD_AP_TAR;
	transfer_requests[2].data = addr;
}

void dap_ap_mem_access_setup(adiv5_access_port_s *ap, uint32_t addr, align_e align)
{
	/* Start by setting up the transfer and attempting it */
	dap_transfer_request_s requests[3];
	mem_access_setup(ap, requests, addr, align);
	adiv5_debug_port_s *dp = ap->dp;
	const bool result = perform_dap_transfer_recoverable(dp, requests, 3U, NULL, 0U);
	/* If it didn't go well, say something and abort */
	if (!result) {
		if (dp->fault != DAP_TRANSFER_NO_RESPONSE)
			DEBUG_WARN("Transport error (%u), aborting\n", dp->fault);
		else
			DEBUG_WARN("Transaction unrecoverably failed\n");
		exit(-1);
	}
}

uint32_t dap_ap_read(adiv5_access_port_s *ap, uint16_t addr)
{
	DEBUG_PROBE("dap_ap_read_start addr %x\n", addr);
	uint8_t buf[63], *p = buf;
	buf[0] = ID_DAP_TRANSFER;
	uint8_t dap_index = 0;
	dap_index = ap->dp->dp_jd_index;
	*p++ = ID_DAP_TRANSFER;
	*p++ = dap_index;
	*p++ = 2; /* Nr transfers */
	*p++ = SWD_DP_W_SELECT;
	*p++ = (addr & 0xf0U);
	*p++ = 0;
	*p++ = 0;
	*p++ = ap->apsel & 0xffU;
	*p++ = (addr & 0x0cU) | DAP_TRANSFER_RnW | (addr & 0x100U ? DAP_TRANSFER_APnDP : 0);
	uint32_t res = wait_word(buf, 63, p - buf, &ap->dp->fault);
	if (buf[0] != 2U || buf[1] != 1U)
		DEBUG_WARN("dap_ap_read error %x\n", buf[1]);
	return res;
}

void dap_ap_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value)
{
	DEBUG_PROBE("dap_ap_write addr %04x value %08x\n", addr, value);
	uint8_t buf[63], *p = buf;
	uint8_t dap_index = 0;
	dap_index = ap->dp->dp_jd_index;
	*p++ = ID_DAP_TRANSFER;
	*p++ = dap_index;
	*p++ = 2; /* Nr transfers */
	*p++ = SWD_DP_W_SELECT;
	*p++ = (addr & 0xf0U);
	*p++ = 0;
	*p++ = 0;
	*p++ = ap->apsel & 0xffU;
	*p++ = (addr & 0x0cU) | (addr & 0x100U ? DAP_TRANSFER_APnDP : 0);
	*p++ = (value >> 0U) & 0xffU;
	*p++ = (value >> 8U) & 0xffU;
	*p++ = (value >> 16U) & 0xffU;
	*p++ = (value >> 24U) & 0xffU;
	dbg_dap_cmd(buf, sizeof(buf), p - buf);
	if (buf[0] != 2U || buf[1] != 1U)
		DEBUG_WARN("dap_ap_write error %x\n", buf[1]);
}

void dap_read_single(adiv5_access_port_s *ap, void *dest, uint32_t src, align_e align)
{
	dap_transfer_request_s requests[4];
	mem_access_setup(ap, requests, src, align);
	requests[3].request = SWD_AP_DRW | DAP_TRANSFER_RnW;
	uint32_t result;
	adiv5_debug_port_s *dp = ap->dp;
	if (!perform_dap_transfer_recoverable(dp, requests, 4U, &result, 1U)) {
		DEBUG_WARN("dap_read_single failed (fault = %u)\n", dp->fault);
		memset(dest, 0, 1U << align);
		return;
	}
	/* Pull out the data. AP_DRW access implies an RDBUFF in CMSIS-DAP, so this is safe */
	adiv5_unpack_data(dest, src, result, align);
}

void dap_write_single(adiv5_access_port_s *ap, uint32_t dest, const void *src, align_e align)
{
	dap_transfer_request_s requests[4];
	mem_access_setup(ap, requests, dest, align);
	requests[3].request = SWD_AP_DRW;
	/* Pack data into correct data lane */
	switch (align) {
	case ALIGN_BYTE:
		requests[3].data = ((uint32_t) * (uint8_t *)src) << ((dest & 3U) << 3U);
		break;
	case ALIGN_HALFWORD:
		requests[3].data = ((uint32_t) * (uint16_t *)src) << ((dest & 2U) << 3U);
		break;
	case ALIGN_DWORD:
	case ALIGN_WORD:
		requests[3].data = *(uint32_t *)src;
		break;
	default:
		requests[3].data = 0;
	}
	adiv5_debug_port_s *dp = ap->dp;
	if (!perform_dap_transfer_recoverable(dp, requests, 4U, NULL, 0U))
		DEBUG_WARN("dap_write_single failed (fault = %u)\n", dp->fault);
}

void dap_jtagtap_tdi_tdo_seq(
	uint8_t *data_out, bool const final_tms, const uint8_t *tms, const uint8_t *data_in, size_t ticks)
{
	DEBUG_PROBE("dap_jtagtap_tdi_tdo_seq %s %d ticks\n", final_tms ? "final" : "", ticks);
	uint8_t buf[64];
	const uint8_t *din = data_in;
	uint8_t *dout = data_out;
	if (!tms) {
		if (!ticks)
			return;
		int last_byte = last_byte = (ticks - 1U) >> 3U;
		int last_bit = (ticks - 1U) & 7U;
		if (final_tms)
			ticks--;
		while (ticks) {
			size_t transfers = ticks;
			if (transfers > 64U)
				transfers = 64;
			uint8_t *p = buf;
			*p++ = ID_DAP_JTAG_SEQUENCE;
			*p++ = 1;
			*p++ = (transfers == 64U ? 0 : transfers) | (data_out ? DAP_JTAG_TDO_CAPTURE : 0);
			size_t n_di_bytes = (transfers + 7U) >> 3U;
			if (din) {
				p = memcpy(p, din, n_di_bytes);
				din += n_di_bytes;
			} else {
				p = memset(p, 0xff, n_di_bytes);
			}
			p += n_di_bytes;
			dbg_dap_cmd(buf, sizeof(buf), p - buf);
			if (buf[0] != DAP_OK)
				DEBUG_WARN("dap_jtagtap_tdi_tdo_seq failed %02x\n", buf[0]);
			if (dout) {
				memcpy(dout, &buf[1], (transfers + 7U) >> 3U);
				dout += (transfers + 7U) >> 3U;
			}
			ticks -= transfers;
		}
		if (final_tms) {
			uint8_t *p = buf;
			*p++ = ID_DAP_JTAG_SEQUENCE;
			*p++ = 1;
			*p++ = 1 | (dout ? DAP_JTAG_TDO_CAPTURE : 0) | DAP_JTAG_TMS;
			if (din) {
				*p++ = data_in[last_byte] & (1U << last_bit) ? 1 : 0;
			} else {
				*p++ = 0;
			}
			dbg_dap_cmd(buf, sizeof(buf), p - buf);
			if (buf[0] == DAP_ERROR)
				DEBUG_WARN("dap_jtagtap_tdi_tdo_seq failed %02x\n", buf[0]);
			if (dout) {
				if (buf[1] & 1U)
					data_out[last_byte] |= 1U << last_bit;
				else
					data_out[last_byte] &= ~(1U << last_bit);
			}
		}
	} else {
		while (ticks) {
			uint8_t *p = buf;
			size_t transfers = ticks;
			if (transfers > 64U)
				transfers = 64;
			p = buf;
			*p++ = ID_DAP_JTAG_SEQUENCE;
			*p++ = transfers;
			for (size_t i = 0; i < transfers; i++) {
				*p++ =
					1U | (data_out ? DAP_JTAG_TDO_CAPTURE : 0) | (tms[i >> 3U] & (1U << (i & 7U)) ? DAP_JTAG_TMS : 0);
				if (data_in)
					*p++ = data_in[i >> 3U] & (1U << (i & 7U)) ? 1 : 0;
				else
					*p++ = 1;
			}
			dbg_dap_cmd(buf, sizeof(buf), p - buf);
			if (buf[0] == DAP_ERROR)
				DEBUG_WARN("dap_jtagtap_tdi_tdo_seq failed %02x\n", buf[0]);
			if (data_out) {
				for (size_t i = 0; i < transfers; i++) {
					if (buf[i + 1U])
						data_out[i >> 3U] |= (1U << (i & 7U));
					else
						data_out[i >> 3U] &= ~(1U << (i & 7U));
				}
			}
			ticks -= transfers;
		}
	}
}

int dap_jtag_configure(void)
{
	uint8_t buf[64], *p = &buf[2];
	uint32_t i = 0;
	for (; i < jtag_dev_count; i++) {
		jtag_dev_s *jtag_dev = &jtag_devs[i];
		*p++ = jtag_dev->ir_len;
		DEBUG_PROBE("irlen %d\n", jtag_dev->ir_len);
	}
	if ((!i || i >= JTAG_MAX_DEVS))
		return -1;
	buf[0] = 0x15;
	buf[1] = i;
	dbg_dap_cmd(buf, sizeof(buf), p - buf);
	if (buf[0] != DAP_OK)
		DEBUG_WARN("dap_jtag_configure Failed %02x\n", buf[0]);
	return 0;
}

void dap_swdptap_seq_out(const uint32_t tms_states, const size_t clock_cycles)
{
	uint8_t data[4] = {
		(tms_states >> 0U) & 0xffU,
		(tms_states >> 8U) & 0xffU,
		(tms_states >> 16U) & 0xffU,
		(tms_states >> 24U) & 0xffU,
	};
	if (!perform_dap_swj_sequence(clock_cycles, data))
		DEBUG_WARN("dap_swdptap_seq_out error\n");
}

void dap_swdptap_seq_out_parity(const uint32_t tms_states, const size_t clock_cycles)
{
	uint8_t data[5] = {
		(tms_states >> 0U) & 0xffU,
		(tms_states >> 8U) & 0xffU,
		(tms_states >> 16U) & 0xffU,
		(tms_states >> 24U) & 0xffU,
		__builtin_parity(tms_states),
	};
	if (!perform_dap_swj_sequence(clock_cycles + 1U, data))
		DEBUG_WARN("dap_swdptap_seq_out_parity error\n");
}

#define SWD_SEQUENCE_IN 0x80U

uint32_t dap_swdptap_seq_in(const size_t clock_cycles)
{
	uint8_t buf[6] = {
		ID_DAP_SWD_SEQUENCE,
		1,
		clock_cycles + SWD_SEQUENCE_IN,
	};
	const size_t sequence_bytes = (clock_cycles + 7U) >> 3U;
	dbg_dap_cmd(buf, 2U + sequence_bytes, 3U);
	if (buf[0])
		DEBUG_WARN("dap_swdptap_seq_in error\n");

	uint32_t result = 0;
	for (size_t offset = 0; offset < clock_cycles; offset += 8U)
		result |= buf[1 + (offset >> 3U)] << offset;
	return result;
}

bool dap_swdptap_seq_in_parity(uint32_t *const result, const size_t clock_cycles)
{
	uint8_t buf[7] = {
		ID_DAP_SWD_SEQUENCE,
		1,
		clock_cycles + 1U + SWD_SEQUENCE_IN,
	};
	const size_t sequence_bytes = (clock_cycles + 8U) >> 3U;
	dbg_dap_cmd(buf, 2U + sequence_bytes, 4U);
	if (buf[0])
		DEBUG_WARN("dap_swdptap_seq_in_parity error\n");

	uint32_t data = 0;
	for (size_t offset = 0; offset < clock_cycles; offset += 8U)
		data |= buf[1 + (offset >> 3U)] << offset;
	*result = data;
	uint8_t parity = __builtin_parity(data) & 1U;
	parity ^= buf[5] & 1U;
	return !parity;
}
