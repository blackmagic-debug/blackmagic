/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Black Sphere Technologies Ltd.
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

#ifndef TARGET_ADIV5_INTERNAL_H
#define TARGET_ADIV5_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include "adiv5.h"

#define ADIV5_APnDP     0x100U
#define ADIV5_DP_REG(x) (x)
#define ADIV5_AP_REG(x) (ADIV5_APnDP | (x))

/* ADIv5 DP Register addresses */
#define ADIV5_DP_DPIDR     ADIV5_DP_REG(0x0U)
#define ADIV5_DP_ABORT     ADIV5_DP_REG(0x0U)
#define ADIV5_DP_CTRLSTAT  ADIV5_DP_REG(0x4U)
#define ADIV5_DP_TARGETID  ADIV5_DP_REG(0x4U) /* ADIV5_DP_BANK2 */
#define ADIV5_DP_SELECT    ADIV5_DP_REG(0x8U)
#define ADIV5_DP_RDBUFF    ADIV5_DP_REG(0xcU)
#define ADIV5_DP_TARGETSEL ADIV5_DP_REG(0xcU)

/* ADIv5 SWD/JTAG Select Sequence */
#define ADIV5_SWD_TO_JTAG_SELECT_SEQUENCE 0xe73cU /* 16 bits, LSB (MSB: 0x3ce7) */
#define ADIV5_JTAG_TO_SWD_SELECT_SEQUENCE 0xe79eU /* 16 bits, LSB (MSB: 0x79e7) */

/*
 * ADIv5 Selection Alert sequence
 * This sequence is sent MSB first and can be represented as either:
 * - 0x49cf9046 a9b4a161 97f5bbc7 45703d98 transmitted MSB first
 * - 0x19bc0ea2 e3ddafe9 86852d95 6209f392 transmitted LSB first
 */
#define ADIV5_SELECTION_ALERT_SEQUENCE_0 0x6209f392U /* 32 bits, LSB */
#define ADIV5_SELECTION_ALERT_SEQUENCE_1 0x86852d95U /* 32 bits, LSB */
#define ADIV5_SELECTION_ALERT_SEQUENCE_2 0xe3ddafe9U /* 32 bits, LSB */
#define ADIV5_SELECTION_ALERT_SEQUENCE_3 0x19bc0ea2U /* 32 bits, LSB */

/* ADIv5 Dormant state activation codes */
#define ADIV5_ACTIVATION_CODE_ARM_SWD_DP  0x1aU /* 8bits, LSB (MSB: 0x58) */
#define ADIV5_ACTIVATION_CODE_ARM_JTAG_DP 0x0aU /* 8bits, LSB (MSB: 0x50) */

/* DP SELECT register DP bank numbers */
#define ADIV5_DP_BANK0 0U
#define ADIV5_DP_BANK1 1U
#define ADIV5_DP_BANK2 2U
#define ADIV5_DP_BANK3 3U
#define ADIV5_DP_BANK4 4U

/* ADIv5 MEM-AP Registers */
#define ADIV5_AP_CSW      ADIV5_AP_REG(0x00U)
#define ADIV5_AP_TAR_LOW  ADIV5_AP_REG(0x04U)
#define ADIV5_AP_TAR_HIGH ADIV5_AP_REG(0x08U)
/* 0x08 - Reserved */
#define ADIV5_AP_DRW   ADIV5_AP_REG(0x0cU)
#define ADIV5_AP_DB(x) ADIV5_AP_REG(0x10U + (4U * (x)))
/* 0x20:0xec - Reserved */
#define ADIV5_AP_BASE_HIGH ADIV5_AP_REG(0xf0U)
#define ADIV5_AP_CFG       ADIV5_AP_REG(0xf4U)
#define ADIV5_AP_BASE_LOW  ADIV5_AP_REG(0xf8U)
#define ADIV5_AP_IDR       ADIV5_AP_REG(0xfcU)

#define SWDP_ACK_OK          0x01U
#define SWDP_ACK_WAIT        0x02U
#define SWDP_ACK_FAULT       0x04U
#define SWDP_ACK_NO_RESPONSE 0x07U

/* Constants to make RnW parameters more clear in code */
#define ADIV5_LOW_WRITE 0
#define ADIV5_LOW_READ  1

typedef struct adiv5_access_port adiv5_access_port_s;
typedef struct adiv5_debug_port adiv5_debug_port_s;

struct adiv5_debug_port {
	int refcnt;

	/* write_no_check returns true if no OK response, but ignores errors */
	bool (*write_no_check)(uint16_t addr, const uint32_t data);
	uint32_t (*read_no_check)(uint16_t addr);
	uint32_t (*dp_read)(adiv5_debug_port_s *dp, uint16_t addr);
	uint32_t (*error)(adiv5_debug_port_s *dp, bool protocol_recovery);
	uint32_t (*low_access)(adiv5_debug_port_s *dp, uint8_t RnW, uint16_t addr, uint32_t value);
	void (*abort)(adiv5_debug_port_s *dp, uint32_t abort);

#if PC_HOSTED == 1
	void (*ap_regs_read)(adiv5_access_port_s *ap, void *data);
	uint32_t (*ap_reg_read)(adiv5_access_port_s *ap, uint8_t reg_num);
	void (*ap_reg_write)(adiv5_access_port_s *ap, uint8_t num, uint32_t value);
#endif
	uint32_t (*ap_read)(adiv5_access_port_s *ap, uint16_t addr);
	void (*ap_write)(adiv5_access_port_s *ap, uint16_t addr, uint32_t value);

	void (*mem_read)(adiv5_access_port_s *ap, void *dest, target_addr64_t src, size_t len);
	void (*mem_write)(adiv5_access_port_s *ap, target_addr64_t dest, const void *src, size_t len, align_e align);
	/* The index of the device on the JTAG scan chain or DP index on SWD */
	uint8_t dev_index;
	/* Whether a fault has occured, and which one */
	uint8_t fault;
	/* Bitfield of the DP's quirks such as if it is a minimal DP or has the duped APs bug */
	uint8_t quirks;
	/* DP version */
	uint8_t version;

	/* DPv2 specific target selection value */
	uint32_t targetsel;

	/* DP designer (not implementer!) and partno */
	uint16_t designer_code;
	uint16_t partno;

	/* TARGETID designer and partno, present on DPv2 */
	uint16_t target_designer_code;
	uint16_t target_partno;
};

struct adiv5_access_port {
	int refcnt;

	adiv5_debug_port_s *dp;
	uint8_t apsel;
	uint8_t flags;

	uint32_t idr;
	target_addr64_t base;
	uint32_t csw;
	uint32_t ap_cortexm_demcr; /* Copy of demcr when starting */

	/* AP designer and partno */
	uint16_t designer_code;
	uint16_t partno;
};

/* Helper for building an ADIv5 request */
uint8_t make_packet_request(uint8_t rnw, uint16_t addr);

#endif /* TARGET_ADIV5_INTERNAL_H */
