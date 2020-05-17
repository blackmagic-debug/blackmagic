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
 * Copyright (c) 2020 Uwe Bonnes bon@elektron.ikp.physik.tu-darmstadt.de
 */

/*- Includes ----------------------------------------------------------------*/
#include <general.h>
#include <stdlib.h>
#include "dap.h"
#include "jtag_scan.h"

/*- Definitions -------------------------------------------------------------*/
enum
{
	ID_DAP_INFO               = 0x00,
	ID_DAP_LED                = 0x01,
	ID_DAP_CONNECT            = 0x02,
	ID_DAP_DISCONNECT         = 0x03,
	ID_DAP_TRANSFER_CONFIGURE = 0x04,
	ID_DAP_TRANSFER           = 0x05,
	ID_DAP_TRANSFER_BLOCK     = 0x06,
	ID_DAP_TRANSFER_ABORT     = 0x07,
	ID_DAP_WRITE_ABORT        = 0x08,
	ID_DAP_DELAY              = 0x09,
	ID_DAP_RESET_TARGET       = 0x0a,
	ID_DAP_SWJ_PINS           = 0x10,
	ID_DAP_SWJ_CLOCK          = 0x11,
	ID_DAP_SWJ_SEQUENCE       = 0x12,
	ID_DAP_SWD_CONFIGURE      = 0x13,
	ID_DAP_JTAG_SEQUENCE      = 0x14,
	ID_DAP_JTAG_CONFIGURE     = 0x15,
	ID_DAP_JTAG_IDCODE        = 0x16,
};

enum
{
	DAP_TRANSFER_APnDP        = 1 << 0,
	DAP_TRANSFER_RnW          = 1 << 1,
	DAP_TRANSFER_A2           = 1 << 2,
	DAP_TRANSFER_A3           = 1 << 3,
	DAP_TRANSFER_MATCH_VALUE  = 1 << 4,
	DAP_TRANSFER_MATCH_MASK   = 1 << 5,
};

enum
{
	DAP_TRANSFER_INVALID      = 0,
	DAP_TRANSFER_OK           = 1 << 0,
	DAP_TRANSFER_WAIT         = 1 << 1,
	DAP_TRANSFER_FAULT        = 1 << 2,
	DAP_TRANSFER_ERROR        = 1 << 3,
	DAP_TRANSFER_MISMATCH     = 1 << 4,
	DAP_TRANSFER_NO_TARGET    = 7,
};

enum
{
	DAP_SWJ_SWCLK_TCK = 1 << 0,
	DAP_SWJ_SWDIO_TMS = 1 << 1,
	DAP_SWJ_TDI       = 1 << 2,
	DAP_SWJ_TDO       = 1 << 3,
	DAP_SWJ_nTRST     = 1 << 5,
	DAP_SWJ_nRESET    = 1 << 7,
};

enum
{
	DAP_OK    = 0x00,
	DAP_ERROR = 0xff,
};

enum
{
	DAP_JTAG_TMS         = 1 << 6,
	DAP_JTAG_TDO_CAPTURE = 1 << 7,
};

enum
{
	SWD_DP_R_IDCODE    = 0x00,
	SWD_DP_W_ABORT     = 0x00,
	SWD_DP_R_CTRL_STAT = 0x04,
	SWD_DP_W_CTRL_STAT = 0x04, // When CTRLSEL == 0
	SWD_DP_W_WCR       = 0x04, // When CTRLSEL == 1
	SWD_DP_R_RESEND    = 0x08,
	SWD_DP_W_SELECT    = 0x08,
	SWD_DP_R_RDBUFF    = 0x0c,
};

enum
{
	SWD_AP_CSW  = 0x00 | DAP_TRANSFER_APnDP,
	SWD_AP_TAR  = 0x04 | DAP_TRANSFER_APnDP,
	SWD_AP_DRW  = 0x0c | DAP_TRANSFER_APnDP,

	SWD_AP_DB0  = 0x00 | DAP_TRANSFER_APnDP, // 0x10
	SWD_AP_DB1  = 0x04 | DAP_TRANSFER_APnDP, // 0x14
	SWD_AP_DB2  = 0x08 | DAP_TRANSFER_APnDP, // 0x18
	SWD_AP_DB3  = 0x0c | DAP_TRANSFER_APnDP, // 0x1c

	SWD_AP_CFG  = 0x04 | DAP_TRANSFER_APnDP, // 0xf4
	SWD_AP_BASE = 0x08 | DAP_TRANSFER_APnDP, // 0xf8
	SWD_AP_IDR  = 0x0c | DAP_TRANSFER_APnDP, // 0xfc
};

#define DP_ABORT_DAPABORT      (1 << 0)
#define DP_ABORT_STKCMPCLR     (1 << 1)
#define DP_ABORT_STKERRCLR     (1 << 2)
#define DP_ABORT_WDERRCLR      (1 << 3)
#define DP_ABORT_ORUNERRCLR    (1 << 4)

#define DP_CST_ORUNDETECT      (1 << 0)
#define DP_CST_STICKYORUN      (1 << 1)
#define DP_CST_TRNMODE_NORMAL  (0 << 2)
#define DP_CST_TRNMODE_VERIFY  (1 << 2)
#define DP_CST_TRNMODE_COMPARE (2 << 2)
#define DP_CST_STICKYCMP       (1 << 4)
#define DP_CST_STICKYERR       (1 << 5)
#define DP_CST_READOK          (1 << 6)
#define DP_CST_WDATAERR        (1 << 7)
#define DP_CST_MASKLANE(x)     ((x) << 8)
#define DP_CST_TRNCNT(x)       ((x) << 12)
#define DP_CST_CDBGRSTREQ      (1 << 26)
#define DP_CST_CDBGRSTACK      (1 << 27)
#define DP_CST_CDBGPWRUPREQ    (1 << 28)
#define DP_CST_CDBGPWRUPACK    (1 << 29)
#define DP_CST_CSYSPWRUPREQ    (1 << 30)
#define DP_CST_CSYSPWRUPACK    (1 << 31)

#define DP_SELECT_CTRLSEL      (1 << 0)
#define DP_SELECT_APBANKSEL(x) ((x) << 4)
#define DP_SELECT_APSEL(x)     ((x) << 24)

#define AP_CSW_SIZE_BYTE       (0 << 0)
#define AP_CSW_SIZE_HALF       (1 << 0)
#define AP_CSW_SIZE_WORD       (2 << 0)
#define AP_CSW_ADDRINC_OFF     (0 << 4)
#define AP_CSW_ADDRINC_SINGLE  (1 << 4)
#define AP_CSW_ADDRINC_PACKED  (2 << 4)
#define AP_CSW_DEVICEEN        (1 << 6)
#define AP_CSW_TRINPROG        (1 << 7)
#define AP_CSW_SPIDEN          (1 << 23)
#define AP_CSW_PROT(x)         ((x) << 24)
#define AP_CSW_DBGSWENABLE     (1 << 31)

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
	buf[1] = (jtag) ? DAP_CAP_JTAG : DAP_CAP_SWD;
	dbg_dap_cmd(buf, sizeof(buf), 2);

}

//-----------------------------------------------------------------------------
void dap_disconnect(void)
{
	uint8_t buf[1];

	buf[0] = ID_DAP_DISCONNECT;
	dbg_dap_cmd(buf, sizeof(buf), 1);
}

//-----------------------------------------------------------------------------
void dap_swj_clock(uint32_t clock)
{
	uint8_t buf[5];

	buf[0] = ID_DAP_SWJ_CLOCK;
	buf[1] = clock & 0xff;
	buf[2] = (clock >> 8) & 0xff;
	buf[3] = (clock >> 16) & 0xff;
	buf[4] = (clock >> 24) & 0xff;
	dbg_dap_cmd(buf, sizeof(buf), 5);

}

//-----------------------------------------------------------------------------
void dap_transfer_configure(uint8_t idle, uint16_t count, uint16_t retry)
{
	uint8_t buf[6];

	buf[0] = ID_DAP_TRANSFER_CONFIGURE;
	buf[1] = idle;
	buf[2] = count & 0xff;
	buf[3] = (count >> 8) & 0xff;
	buf[4] = retry & 0xff;
	buf[5] = (retry >> 8) & 0xff;
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
int dap_info(int info, uint8_t *data, int size)
{
	uint8_t buf[256];
	int rsize;

	buf[0] = ID_DAP_INFO;
	buf[1] = info;
	dbg_dap_cmd(buf, sizeof(buf), 2);

	rsize = (size < buf[0]) ? size : buf[0];
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
	buf[2] = DAP_SWJ_nRESET; // Select
	buf[3] = 0; // Wait
	buf[4] = 0;
	buf[5] = 0;
	buf[6] = 0;
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

static void dap_line_reset(void)
{
	uint8_t buf[] = {
		ID_DAP_SWJ_SEQUENCE,
		64,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0xff,
		0
	};
	dbg_dap_cmd(buf, sizeof(buf), 10);
	if (buf[0])
		DEBUG_WARN("line reset failed\n");
}

static uint32_t wait_word(uint8_t *buf, int size, int len, uint8_t *dp_fault)
{
	do {
		dbg_dap_cmd(buf, size, len);
		if (buf[1] < DAP_TRANSFER_WAIT)
			break;
	} while (buf[1] == DAP_TRANSFER_WAIT);

	if (buf[1] > DAP_TRANSFER_WAIT) {
//	  DEBUG_WARN("dap_read_reg fault\n");
		*dp_fault = 1;
	}
	if (buf[1] == DAP_TRANSFER_ERROR) {
		DEBUG_WARN("dap_read_reg, protocoll error\n");
		dap_line_reset();
	}
	uint32_t res =
		((uint32_t)buf[5] << 24) | ((uint32_t)buf[4] << 16) |
		((uint32_t)buf[3] << 8) | (uint32_t)buf[2];
	return res;
}

//-----------------------------------------------------------------------------
uint32_t dap_read_reg(ADIv5_DP_t *dp, uint8_t reg)
{
	uint8_t buf[8];
	uint8_t dap_index = 0;
	if (dp->dev)
		dap_index = dp->dev->dev;
	buf[0] = ID_DAP_TRANSFER;
	buf[1] = dap_index;
	buf[2] = 0x01; // Request size
	buf[3] = reg | DAP_TRANSFER_RnW;
	uint32_t res = wait_word(buf, 8, 4, &dp->fault);
	DEBUG_WIRE("\tdap_read_reg %02x %08x\n", reg, res);
	return res;
}

//-----------------------------------------------------------------------------
void dap_write_reg(ADIv5_DP_t *dp, uint8_t reg, uint32_t data)
{
	uint8_t buf[8];
	DEBUG_PROBE("\tdap_write_reg %02x %08x\n", reg, data);

	buf[0] = ID_DAP_TRANSFER;
	uint8_t dap_index = 0;
	if (dp->dev)
		dap_index = dp->dev->dev;
	buf[1] = dap_index;
	buf[2] = 0x01; // Request size
	buf[3] = reg & ~DAP_TRANSFER_RnW;;
	buf[4] = data & 0xff;
	buf[5] = (data >> 8) & 0xff;
	buf[6] = (data >> 16) & 0xff;
	buf[7] = (data >> 24) & 0xff;
	do {
		dbg_dap_cmd(buf, sizeof(buf), 8);
		if (buf[1] < DAP_TRANSFER_WAIT)
			break;
	} while (buf[1] == DAP_TRANSFER_WAIT);

	if (buf[1] > DAP_TRANSFER_WAIT) {
		DEBUG_PROBE("dap_write_reg %02x data %08x:fault\n", reg, data);
		dp->fault = 1;
	}
	if (buf[1] == DAP_TRANSFER_ERROR) {
		DEBUG_PROBE("dap_write_reg %02x data %08x: protocoll error\n",
					reg, data);
		dap_line_reset();
	}
}

unsigned int dap_read_block(ADIv5_AP_t *ap, void *dest, uint32_t src,
							size_t len, enum align align)
{
	uint8_t buf[1024];
	unsigned int sz = len >> align;
	uint8_t dap_index = 0;
	if (ap->dp->dev)
		dap_index = ap->dp->dev->dev;
    buf[0] = ID_DAP_TRANSFER_BLOCK;
    buf[1] = dap_index;
    buf[2] =  sz & 0xff;
    buf[3] = (sz >> 8) & 0xff;
    buf[4] = SWD_AP_DRW | DAP_TRANSFER_RnW;
    dbg_dap_cmd(buf, 1023, 5 + 1);
	unsigned int transferred = buf[0] + (buf[1] << 8);
	if (buf[2] > DAP_TRANSFER_FAULT) {
		DEBUG_PROBE("line_reset\n");
		dap_line_reset();
	}
	if (sz != transferred) {
		return 1;
	} else if (align > ALIGN_HALFWORD) {
		memcpy(dest, &buf[3], len);
	} else {
		uint32_t *p = (uint32_t *)&buf[3];
		while(sz) {
			dest = extract(dest, src, *p, align);
			p++;
			src  += (1 << align);
			dest += (1 << align);
			sz--;
		}
	}
	return (buf[2] > DAP_TRANSFER_WAIT) ? 1 : 0;
}

unsigned int dap_write_block(ADIv5_AP_t *ap, uint32_t dest, const void *src,
							 size_t len, enum align align)
{
	uint8_t buf[1024];
	unsigned int sz = len >> align;
	uint8_t dap_index = 0;
	if (ap->dp->dev)
		dap_index = ap->dp->dev->dev;
    buf[0] = ID_DAP_TRANSFER_BLOCK;
    buf[1] = dap_index;
    buf[2] =  sz & 0xff;
    buf[3] = (sz >> 8) & 0xff;
    buf[4] = SWD_AP_DRW;
	if (align > ALIGN_HALFWORD) {
		memcpy(&buf[5], src, len);
	} else {
		unsigned int size = len;
		uint32_t *p = (uint32_t *)&buf[5];
		while (size) {
			uint32_t tmp = 0;
			/* Pack data into correct data lane */
			if (align == ALIGN_BYTE) {
				tmp = ((uint32_t)*(uint8_t  *)src) << ((dest & 3) << 3);
			} else {
				tmp = ((uint32_t)*(uint16_t *)src) << ((dest & 2) << 3);
			}
			src = src + (1 << align);
			dest += (1 << align);
			size--;
			*p++ = tmp;
		}
	}
	dbg_dap_cmd(buf, 1023, 5 + (sz << 2));
	if (buf[2] > DAP_TRANSFER_FAULT) {
		dap_line_reset();
	}
	return (buf[2] > DAP_TRANSFER_WAIT) ? 1 : 0;
}

//-----------------------------------------------------------------------------
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
		buf[1] = ((p - &buf[2]) * 8) - 2;
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
		buf[1] = (p - &buf[2]) * 8;
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
uint32_t dap_read_idcode(ADIv5_DP_t *dp)
{
	return dap_read_reg(dp, SWD_DP_R_IDCODE);
}

static uint8_t *mem_access_setup(ADIv5_AP_t *ap, uint8_t *p,
								 uint32_t addr, enum align align)
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
	uint8_t dap_index = 0;
	if (ap->dp->dev)
		dap_index = ap->dp->dev->dev;
	*p++ = ID_DAP_TRANSFER;
	*p++ = dap_index;
	*p++ = 3; /* Nr transfers */
	*p++ = SWD_DP_W_SELECT;
	*p++ = ADIV5_AP_CSW & 0xF0;
	*p++ = 0;
	*p++ = 0;
	*p++ = ap->apsel & 0xff;
	*p++ = SWD_AP_CSW;
	*p++ = (csw >>  0) & 0xff;
	*p++ = (csw >>  8) & 0xff;
	*p++ = (csw >> 16) & 0xff;
	*p++ = (csw >> 24) & 0xff;
	*p++ = SWD_AP_TAR ;
	*p++ = (addr >>  0) & 0xff;
	*p++ = (addr >>  8) & 0xff;
	*p++ = (addr >> 16) & 0xff;
	*p++ = (addr >> 24) & 0xff;
	return p;
}

void dap_ap_mem_access_setup(ADIv5_AP_t *ap, uint32_t addr, enum align align)
{
	uint8_t buf[63];
	uint8_t *p = mem_access_setup(ap, buf, addr, align);
	dbg_dap_cmd(buf, sizeof(buf), p - buf);
}

uint32_t dap_ap_read(ADIv5_AP_t *ap, uint16_t addr)
{
	DEBUG_PROBE("dap_ap_read_start\n");
	uint8_t buf[63], *p = buf;
	buf[0] = ID_DAP_TRANSFER;
	uint8_t dap_index = 0;
	if (ap->dp->dev)
		dap_index = ap->dp->dev->dev;
	*p++ = ID_DAP_TRANSFER;
	*p++ = dap_index;
	*p++ = 2; /* Nr transfers */
	*p++ = SWD_DP_W_SELECT;
	*p++ = (addr & 0xF0);
	*p++ = 0;
	*p++ = 0;
	*p++ = ap->apsel & 0xff;
	*p++ = (addr & 0x0c) | DAP_TRANSFER_RnW  |
		((addr & 0x100) ?  DAP_TRANSFER_APnDP : 0);
	uint32_t res = wait_word(buf, 63, p - buf, &ap->dp->fault);
	return res;
}

void dap_ap_write(ADIv5_AP_t *ap, uint16_t addr, uint32_t value)
{
	DEBUG_PROBE("dap_ap_write addr %04x value %08x\n", addr, value);
	uint8_t buf[63], *p = buf;
	uint8_t dap_index = 0;
	if (ap->dp->dev)
		dap_index = ap->dp->dev->dev;
	*p++ = ID_DAP_TRANSFER;
	*p++ = dap_index;
	*p++ = 2; /* Nr transfers */
	*p++ = SWD_DP_W_SELECT;
	*p++ = (addr & 0xF0);
	*p++ = 0;
	*p++ = 0;
	*p++ = ap->apsel & 0xff;
	*p++ = (addr & 0x0c) | ((addr & 0x100) ?  DAP_TRANSFER_APnDP : 0);
	*p++ = (value >>  0) & 0xff;
	*p++ = (value >>  8) & 0xff;
	*p++ = (value >> 16) & 0xff;
	*p++ = (value >> 24) & 0xff;
	dbg_dap_cmd(buf, sizeof(buf), p - buf);
}

void dap_read_single(ADIv5_AP_t *ap, void *dest, uint32_t src, enum align align)
{
	uint8_t buf[63];
	uint8_t *p = mem_access_setup(ap, buf, src, align);
	*p++ = SWD_AP_DRW | DAP_TRANSFER_RnW;
	buf[2] = 4;
	uint32_t tmp = wait_word(buf, 63, p - buf, &ap->dp->fault);
	dest = extract(dest, src, tmp, align);
}

void dap_write_single(ADIv5_AP_t *ap, uint32_t dest, const void *src,
					  enum align align)
{
	uint8_t buf[63];
	uint8_t *p = mem_access_setup(ap, buf, dest, align);
	*p++ = SWD_AP_DRW;
	uint32_t tmp = 0;
	/* Pack data into correct data lane */
	switch (align) {
	case ALIGN_BYTE:
		tmp = ((uint32_t)*(uint8_t *)src) << ((dest & 3) << 3);
		break;
	case ALIGN_HALFWORD:
		tmp = ((uint32_t)*(uint16_t *)src) << ((dest & 2) << 3);
		break;
	case ALIGN_DWORD:
	case ALIGN_WORD:
		tmp = *(uint32_t *)src;
		break;
	}
	*p++ = (tmp >>  0) & 0xff;
	*p++ = (tmp >>  8) & 0xff;
	*p++ = (tmp >> 16) & 0xff;
	*p++ = (tmp >> 24) & 0xff;
	buf[2] = 4;
	dbg_dap_cmd(buf, sizeof(buf), p - buf);
}

void dap_jtagtap_tdi_tdo_seq(uint8_t *DO, bool final_tms, const uint8_t *TMS,
							 const uint8_t *DI, int ticks)
{
	uint8_t buf[64];
	if (!TMS) {
		int last_byte = 0;
		int last_bit = 0;
		if (final_tms) {
			last_byte = ticks >> 3;
			last_bit = ticks & 7;
			ticks --;
		}
		while (ticks) {
			int transfers = ticks;
			if (transfers > 64)
				transfers = 64;
			uint8_t *p = buf;
			*p++ = ID_DAP_JTAG_SEQUENCE;
			*p++ = 1;
			*p++ = transfers | ((DO) ? DAP_JTAG_TDO_CAPTURE : 0);
			int n_di_bytes = (transfers + 7) >> 3;
			if (DI) {
				p = memcpy(p, DI, n_di_bytes);
				DI += n_di_bytes;
			} else {
				p = memset(p, 0xff, n_di_bytes);
			}
			p += n_di_bytes;
			dbg_dap_cmd(buf, sizeof(buf), p - buf);
			if (buf[0] != DAP_OK)
				DEBUG_WARN("dap_jtagtap_tdi_tdo_seq failed %02x\n", buf[0]);
			if (DO) {
				memcpy(DO, &buf[1], (transfers + 7) >> 3);
				DO += (transfers + 7) >> 3;
			}
			ticks -= transfers;
		}
		if (final_tms) {
			uint8_t *p = buf;
			*p++ = ID_DAP_JTAG_SEQUENCE;
			*p++ = 1;
			*p++ = 1 | ((DO) ? DAP_JTAG_TDO_CAPTURE : 0) | DAP_JTAG_TMS;
			if (DI) {
				*p++ = ((DI[last_byte] & (1 << last_bit)) ? 1 : 0);
			} else {
				*p++ = 0;
			}
			dbg_dap_cmd(buf, sizeof(buf), p - buf);
			if (buf[0] == DAP_ERROR)
				DEBUG_WARN("dap_jtagtap_tdi_tdo_seq failed %02x\n", buf[0]);
			if (DO) {
				if (buf[1] & 1)
					DO[last_byte] |= (1 << last_bit);
				else
					DO[last_byte] &= ~(1 << last_bit);
			}
		}
	} else {
		while(ticks) {
			uint8_t *p = buf;
			int transfers = ticks;
			if (transfers > 64)
				transfers = 64;
			p = buf;
			*p++ = ID_DAP_JTAG_SEQUENCE;
			*p++ = transfers;
			for (int i = 0; i < transfers; i++) {
				*p++ = 1 | ((DO) ? DAP_JTAG_TDO_CAPTURE : 0) |
					((TMS[i >> 8] & (1 << (i & 7))) ? DAP_JTAG_TMS : 0);
				if (DI)
					*p++ = (DI[i >> 8] & (1 << (i & 7))) ? 1 : 0;
				else
					*p++ = 0x55;
			}
			dbg_dap_cmd(buf, sizeof(buf), p - buf);
			if (buf[0] == DAP_ERROR)
				DEBUG_WARN("dap_jtagtap_tdi_tdo_seq failed %02x\n", buf[0]);
			if (DO) {
				for (int i = 0; i < transfers; i++) {
					if (buf[i + 1])
						DO[i >> 8] |= (1 << (i & 7));
					else
						DO[i >> 8] &= ~(1 << (i & 7));
				}
			}
			ticks -= transfers;
		}
	}
}

int dap_jtag_configure(void)
{
	uint8_t buf[64], *p = &buf[2];
	int i = 0;
	for (; i < jtag_dev_count; i++) {
		struct jtag_dev_s *jtag_dev = &jtag_devs[i];
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
