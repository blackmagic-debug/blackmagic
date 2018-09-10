/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

#ifndef __ADIV5_H
#define __ADIV5_H

#include "jtag_scan.h"

#define ADIV5_APnDP       0x100
#define ADIV5_DP_REG(x)   (x)
#define ADIV5_AP_REG(x)   (ADIV5_APnDP | (x))

/* ADIv5 DP Register addresses */
#define ADIV5_DP_IDCODE   ADIV5_DP_REG(0x0)
#define ADIV5_DP_ABORT    ADIV5_DP_REG(0x0)
#define ADIV5_DP_CTRLSTAT ADIV5_DP_REG(0x4)
#define ADIV5_DP_SELECT   ADIV5_DP_REG(0x8)
#define ADIV5_DP_RDBUFF   ADIV5_DP_REG(0xC)

#define ADIV5_DP_BANK0    0
#define ADIV5_DP_BANK1    1
#define ADIV5_DP_BANK2    2
#define ADIV5_DP_BANK3    3
#define ADIV5_DP_BANK4    4

#define ADIV5_DP_VERSION_MASK 0xf000
#define ADIV5_DPv1            0x1000
#define ADIV5_DPv2            0x2000

/* AP Abort Register (ABORT) */
/* Bits 31:5 - Reserved */
#define ADIV5_DP_ABORT_ORUNERRCLR	(1 << 4)
#define ADIV5_DP_ABORT_WDERRCLR		(1 << 3)
#define ADIV5_DP_ABORT_STKERRCLR	(1 << 2)
#define ADIV5_DP_ABORT_STKCMPCLR	(1 << 1)
/* Bits 5:1 - SW-DP only, reserved in JTAG-DP */
#define ADIV5_DP_ABORT_DAPABORT		(1 << 0)

/* Control/Status Register (CTRLSTAT) */
#define ADIV5_DP_CTRLSTAT_CSYSPWRUPACK	(1u << 31)
#define ADIV5_DP_CTRLSTAT_CSYSPWRUPREQ	(1u << 30)
#define ADIV5_DP_CTRLSTAT_CDBGPWRUPACK	(1u << 29)
#define ADIV5_DP_CTRLSTAT_CDBGPWRUPREQ	(1u << 28)
#define ADIV5_DP_CTRLSTAT_CDBGRSTACK	(1u << 27)
#define ADIV5_DP_CTRLSTAT_CDBGRSTREQ	(1u << 26)
/* Bits 25:24 - Reserved */
/* Bits 23:12 - TRNCNT */
#define ADIV5_DP_CTRLSTAT_TRNCNT
/* Bits 11:8 - MASKLANE */
#define ADIV5_DP_CTRLSTAT_MASKLANE
/* Bits 7:6 - Reserved in JTAG-DP */
#define ADIV5_DP_CTRLSTAT_WDATAERR	(1u << 7)
#define ADIV5_DP_CTRLSTAT_READOK	(1u << 6)
#define ADIV5_DP_CTRLSTAT_STICKYERR	(1u << 5)
#define ADIV5_DP_CTRLSTAT_STICKYCMP	(1u << 4)
#define ADIV5_DP_CTRLSTAT_TRNMODE_MASK	(3u << 2)
#define ADIV5_DP_CTRLSTAT_STICKYORUN	(1u << 1)
#define ADIV5_DP_CTRLSTAT_ORUNDETECT	(1u << 0)


/* ADIv5 MEM-AP Registers */
#define ADIV5_AP_CSW		ADIV5_AP_REG(0x00)
#define ADIV5_AP_TAR		ADIV5_AP_REG(0x04)
/* 0x08 - Reserved */
#define ADIV5_AP_DRW		ADIV5_AP_REG(0x0C)
#define ADIV5_AP_DB(x)		ADIV5_AP_REG(0x10 + (4*(x)))
/* 0x20:0xF0 - Reserved */
#define ADIV5_AP_CFG		ADIV5_AP_REG(0xF4)
#define ADIV5_AP_BASE		ADIV5_AP_REG(0xF8)
#define ADIV5_AP_IDR		ADIV5_AP_REG(0xFC)

/* AP Control and Status Word (CSW) */
#define ADIV5_AP_CSW_DBGSWENABLE	(1u << 31)
/* Bits 30:24 - Prot, Implementation defined, for Cortex-M3: */
#define ADIV5_AP_CSW_MASTERTYPE_DEBUG	(1u << 29)
#define ADIV5_AP_CSW_HPROT1		(1u << 25)
#define ADIV5_AP_CSW_SPIDEN		(1u << 23)
/* Bits 22:12 - Reserved */
/* Bits 11:8 - Mode, must be zero */
#define ADIV5_AP_CSW_TRINPROG		(1u << 7)
#define ADIV5_AP_CSW_DEVICEEN		(1u << 6)
#define ADIV5_AP_CSW_ADDRINC_NONE	(0u << 4)
#define ADIV5_AP_CSW_ADDRINC_SINGLE	(1u << 4)
#define ADIV5_AP_CSW_ADDRINC_PACKED	(2u << 4)
#define ADIV5_AP_CSW_ADDRINC_MASK	(3u << 4)
/* Bit 3 - Reserved */
#define ADIV5_AP_CSW_SIZE_BYTE		(0u << 0)
#define ADIV5_AP_CSW_SIZE_HALFWORD	(1u << 0)
#define ADIV5_AP_CSW_SIZE_WORD		(2u << 0)
#define ADIV5_AP_CSW_SIZE_MASK		(7u << 0)

/* Constants to make RnW parameters more clear in code */
#define ADIV5_LOW_WRITE		0
#define ADIV5_LOW_READ		1

enum align {
	ALIGN_BYTE     = 0,
	ALIGN_HALFWORD = 1,
	ALIGN_WORD     = 2,
	ALIGN_DWORD    = 3
};

/* Try to keep this somewhat absract for later adding SW-DP */
typedef struct ADIv5_DP_s {
	int refcnt;

	uint32_t idcode;
	uint32_t dp_idcode; /* Contains DPvX revision*/
	uint32_t targetid;  /* Contains IDCODE for DPv2 devices.*/

	uint32_t (*dp_read)(struct ADIv5_DP_s *dp, uint16_t addr);
	uint32_t (*error)(struct ADIv5_DP_s *dp);
	uint32_t (*low_access)(struct ADIv5_DP_s *dp, uint8_t RnW,
                               uint16_t addr, uint32_t value);
	void (*abort)(struct ADIv5_DP_s *dp, uint32_t abort);

	union {
		jtag_dev_t *dev;
		uint8_t fault;
	};
} ADIv5_DP_t;

static inline uint32_t adiv5_dp_read(ADIv5_DP_t *dp, uint16_t addr)
{
	return dp->dp_read(dp, addr);
}

static inline uint32_t adiv5_dp_error(ADIv5_DP_t *dp)
{
	return dp->error(dp);
}

static inline uint32_t adiv5_dp_low_access(struct ADIv5_DP_s *dp, uint8_t RnW,
                                           uint16_t addr, uint32_t value)
{
	return dp->low_access(dp, RnW, addr, value);
}

static inline void adiv5_dp_abort(struct ADIv5_DP_s *dp, uint32_t abort)
{
	return dp->abort(dp, abort);
}

typedef struct ADIv5_AP_s {
	int refcnt;

	ADIv5_DP_t *dp;
	uint8_t apsel;

	uint32_t idr;
	uint32_t cfg;
	uint32_t base;
	uint32_t csw;
} ADIv5_AP_t;

void adiv5_dp_init(ADIv5_DP_t *dp);
void adiv5_dp_write(ADIv5_DP_t *dp, uint16_t addr, uint32_t value);

ADIv5_AP_t *adiv5_new_ap(ADIv5_DP_t *dp, uint8_t apsel);
void adiv5_dp_ref(ADIv5_DP_t *dp);
void adiv5_ap_ref(ADIv5_AP_t *ap);
void adiv5_dp_unref(ADIv5_DP_t *dp);
void adiv5_ap_unref(ADIv5_AP_t *ap);

void adiv5_ap_write(ADIv5_AP_t *ap, uint16_t addr, uint32_t value);
uint32_t adiv5_ap_read(ADIv5_AP_t *ap, uint16_t addr);

void adiv5_jtag_dp_handler(jtag_dev_t *dev);

void adiv5_mem_read(ADIv5_AP_t *ap, void *dest, uint32_t src, size_t len);
void adiv5_mem_write(ADIv5_AP_t *ap, uint32_t dest, const void *src, size_t len);
void adiv5_mem_write_sized(ADIv5_AP_t *ap, uint32_t dest, const void *src,
						   size_t len, enum align align);

#endif

