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

#ifndef TARGET_ADIV5_H
#define TARGET_ADIV5_H

#include "general.h"
#include "jtag_scan.h"
#include "swd.h"

#if PC_HOSTED == 1
#include "platform.h"
#endif

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

/* DP DPIDR */
#define ADIV5_DP_DPIDR_REVISION_OFFSET 28U
#define ADIV5_DP_DPIDR_REVISION_MASK   (0xfU << ADIV5_DP_DPIDR_VERSION_OFFSET)
#define ADIV5_DP_DPIDR_PARTNO_OFFSET   20U
#define ADIV5_DP_DPIDR_PARTNO_MASK     (0xffU << ADIV5_DP_DPIDR_PARTNO_OFFSET)
#define ADIV5_DP_DPIDR_MINDP_OFFSET    16U
#define ADIV5_DP_DPIDR_MINDP           (1U << ADIV5_DP_DPIDR_MINDP_OFFSET)
#define ADIV5_DP_DPIDR_VERSION_OFFSET  12U
#define ADIV5_DP_DPIDR_VERSION_MASK    (0xfU << ADIV5_DP_DPIDR_VERSION_OFFSET)
#define ADIV5_DP_DPIDR_VERSION_DPv1    (1U << ADIV5_DP_DPIDR_VERSION_OFFSET)
#define ADIV5_DP_DPIDR_VERSION_DPv2    (2U << ADIV5_DP_DPIDR_VERSION_OFFSET)
#define ADIV5_DP_DPIDR_DESIGNER_OFFSET 1U
#define ADIV5_DP_DPIDR_DESIGNER_MASK   (0x7ffU << ADIV5_DP_DPIDR_DESIGNER_OFFSET)

/* DP SELECT register DP bank numbers */
#define ADIV5_DP_BANK0 0U
#define ADIV5_DP_BANK1 1U
#define ADIV5_DP_BANK2 2U
#define ADIV5_DP_BANK3 3U
#define ADIV5_DP_BANK4 4U

/* DP TARGETID */
#define ADIV5_DP_TARGETID_TREVISION_OFFSET 28U
#define ADIV5_DP_TARGETID_TREVISION_MASK   (0xfU << ADIV5_DP_TARGETID_TREVISION_OFFSET)
#define ADIV5_DP_TARGETID_TPARTNO_OFFSET   12U
#define ADIV5_DP_TARGETID_TPARTNO_MASK     (0xffffU << ADIV5_DP_TARGETID_TPARTNO_OFFSET)
#define ADIV5_DP_TARGETID_TDESIGNER_OFFSET 1U
#define ADIV5_DP_TARGETID_TDESIGNER_MASK   (0x7ffU << ADIV5_DP_TARGETID_TDESIGNER_OFFSET)

/* DP TARGETSEL */
#define ADIV5_DP_TARGETSEL_TINSTANCE_OFFSET 28U
#define ADIV5_DP_TARGETSEL_TINSTANCE_MASK   (0xfU << ADIV5_DP_TARGETSEL_TINSTANCE_OFFSET)
#define ADIV5_DP_TARGETSEL_TPARTNO_OFFSET   12U
#define ADIV5_DP_TARGETSEL_TPARTNO_MASK     (0xffffU << ADIV5_DP_TARGETSEL_TPARTNO_OFFSET)
#define ADIV5_DP_TARGETSEL_TDESIGNER_OFFSET 1U
#define ADIV5_DP_TARGETSEL_TDESIGNER_MASK   (0x7ffU << ADIV5_DP_TARGETSEL_TDESIGNER_OFFSET)

/* DP DPIDR/TARGETID/IDCODE DESIGNER */
/* Bits 10:7 - JEP-106 Continuation code */
/* Bits 6:0 - JEP-106 Identity code */
#define ADIV5_DP_DESIGNER_JEP106_CONT_OFFSET 7U
#define ADIV5_DP_DESIGNER_JEP106_CONT_MASK   (0xfU << ADIV5_DP_DESIGNER_JEP106_CONT_OFFSET)
#define ADIV5_DP_DESIGNER_JEP106_CODE_MASK   (0x7fU)

/* AP Abort Register (ABORT) */
/* Bits 31:5 - Reserved */
/* Bits 5:1 - DPv1+, reserved in DPv0 */
#define ADIV5_DP_ABORT_ORUNERRCLR (1U << 4U)
#define ADIV5_DP_ABORT_WDERRCLR   (1U << 3U)
#define ADIV5_DP_ABORT_STKERRCLR  (1U << 2U)
#define ADIV5_DP_ABORT_STKCMPCLR  (1U << 1U)
/* Bit 1 is always defined as DAP Abort. */
#define ADIV5_DP_ABORT_DAPABORT (1U << 0U)

/* Control/Status Register (CTRLSTAT) */
#define ADIV5_DP_CTRLSTAT_CSYSPWRUPACK (1U << 31U)
#define ADIV5_DP_CTRLSTAT_CSYSPWRUPREQ (1U << 30U)
#define ADIV5_DP_CTRLSTAT_CDBGPWRUPACK (1U << 29U)
#define ADIV5_DP_CTRLSTAT_CDBGPWRUPREQ (1U << 28U)
#define ADIV5_DP_CTRLSTAT_CDBGRSTACK   (1U << 27U)
#define ADIV5_DP_CTRLSTAT_CDBGRSTREQ   (1U << 26U)
/* Bits 25:24 - Reserved */
/* Bits 23:12 - TRNCNT */
#define ADIV5_DP_CTRLSTAT_TRNCNT(x) ((x & 0xfffU) << 12U)
/* Bits 11:8 - MASKLANE */
#define ADIV5_DP_CTRLSTAT_MASKLANE
/* Bits 7:6 - Reserved in JTAG-DP */
#define ADIV5_DP_CTRLSTAT_WDATAERR     (1U << 7U)
#define ADIV5_DP_CTRLSTAT_READOK       (1U << 6U)
#define ADIV5_DP_CTRLSTAT_STICKYERR    (1U << 5U)
#define ADIV5_DP_CTRLSTAT_STICKYCMP    (1U << 4U)
#define ADIV5_DP_CTRLSTAT_TRNMODE_MASK (3U << 2U)
#define ADIV5_DP_CTRLSTAT_STICKYORUN   (1U << 1U)
#define ADIV5_DP_CTRLSTAT_ORUNDETECT   (1U << 0U)
/* Mask for bits: sticky overrun, sticky cmp, sticky error, and the system + debug powerup bits */
#define ADIV5_DP_CTRLSTAT_ERRMASK 0xf0000032U

/* ADIv5 MEM-AP Registers */
#define ADIV5_AP_CSW ADIV5_AP_REG(0x00U)
#define ADIV5_AP_TAR ADIV5_AP_REG(0x04U)
/* 0x08 - Reserved */
#define ADIV5_AP_DRW   ADIV5_AP_REG(0x0cU)
#define ADIV5_AP_DB(x) ADIV5_AP_REG(0x10U + (4U * (x)))
/* 0x20:0xf0 - Reserved */
#define ADIV5_AP_CFG  ADIV5_AP_REG(0xf4U)
#define ADIV5_AP_BASE ADIV5_AP_REG(0xf8U)
#define ADIV5_AP_IDR  ADIV5_AP_REG(0xfcU)

/* AP Control and Status Word (CSW) */
#define ADIV5_AP_CSW_DBGSWENABLE (1U << 31U)
/* Bits 30:24 - Prot, Implementation defined, for Cortex-M: */
#define ADIV5_AP_CSW_HNOSEC           (1U << 30U)
#define ADIV5_AP_CSW_MASTERTYPE_DEBUG (1U << 29U)
#define ADIV5_AP_CSW_HPROT1           (1U << 25U)
#define ADIV5_AP_CSW_SPIDEN           (1U << 23U)
/* Bits 22:16 - Reserved */
/* Bit 15 - MTE (Memory Tagging Enable) for AXI buses */
#define ADIV5_AP_CSW_MTE (1U << 15U)
/* Bits 14:12 - Reserved */
/* Bits 11:8 - Mode, must be zero */
#define ADIV5_AP_CSW_TRINPROG       (1U << 7U)
#define ADIV5_AP_CSW_DEVICEEN       (1U << 6U)
#define ADIV5_AP_CSW_ADDRINC_NONE   (0U << 4U)
#define ADIV5_AP_CSW_ADDRINC_SINGLE (1U << 4U)
#define ADIV5_AP_CSW_ADDRINC_PACKED (2U << 4U)
#define ADIV5_AP_CSW_ADDRINC_MASK   (3U << 4U)
/* Bit 3 - Reserved */
#define ADIV5_AP_CSW_SIZE_BYTE     (0U << 0U)
#define ADIV5_AP_CSW_SIZE_HALFWORD (1U << 0U)
#define ADIV5_AP_CSW_SIZE_WORD     (2U << 0U)
#define ADIV5_AP_CSW_SIZE_MASK     (7U << 0U)

/* AP Debug Base Address Register (BASE) */
#define ADIV5_AP_BASE_BASEADDR UINT32_C(0xfffff000)
#define ADIV5_AP_BASE_PRESENT  (1U << 0U)

/* AP Identification Register (IDR) */
#define ADIV5_AP_IDR_REVISION_OFFSET 28U
#define ADIV5_AP_IDR_REVISION_MASK   0xf0000000U
#define ADIV5_AP_IDR_REVISION(idr)   (((idr)&ADIV5_AP_IDR_REVISION_MASK) >> ADIV5_AP_IDR_REVISION_OFFSET)
#define ADIV5_AP_IDR_DESIGNER_OFFSET 17U
#define ADIV5_AP_IDR_DESIGNER_MASK   0x0ffe0000U
#define ADIV5_AP_IDR_DESIGNER(idr)   (((idr)&ADIV5_AP_IDR_DESIGNER_MASK) >> ADIV5_AP_IDR_DESIGNER_OFFSET)
#define ADIV5_AP_IDR_CLASS_OFFSET    13U
#define ADIV5_AP_IDR_CLASS_MASK      0x0001e000U
#define ADIV5_AP_IDR_CLASS(idr)      (((idr)&ADIV5_AP_IDR_CLASS_MASK) >> ADIV5_AP_IDR_CLASS_OFFSET)
#define ADIV5_AP_IDR_VARIANT_OFFSET  4U
#define ADIV5_AP_IDR_VARIANT_MASK    0x000000f0U
#define ADIV5_AP_IDR_VARIANT(idr)    (((idr)&ADIV5_AP_IDR_VARIANT_MASK) >> ADIV5_AP_IDR_VARIANT_OFFSET)
#define ADIV5_AP_IDR_TYPE_OFFSET     0U
#define ADIV5_AP_IDR_TYPE_MASK       0x0000000fU
#define ADIV5_AP_IDR_TYPE(idr)       ((idr)&ADIV5_AP_IDR_TYPE_MASK)

/* ADIv5 Class 0x1 ROM Table Registers */
#define ADIV5_ROM_MEMTYPE          0xfccU
#define ADIV5_ROM_MEMTYPE_SYSMEM   (1U << 0U)
#define ADIV5_ROM_ROMENTRY_PRESENT (1U << 0U)
#define ADIV5_ROM_ROMENTRY_OFFSET  UINT32_C(0xfffff000)

/* JTAG TAP IDCODE */
#define JTAG_IDCODE_VERSION_OFFSET  28U
#define JTAG_IDCODE_VERSION_MASK    (0xfU << JTAG_IDCODE_VERSION_OFFSET)
#define JTAG_IDCODE_PARTNO_OFFSET   12U
#define JTAG_IDCODE_PARTNO_MASK     (0xffffU << JTAG_IDCODE_PARTNO_OFFSET)
#define JTAG_IDCODE_DESIGNER_OFFSET 1U
#define JTAG_IDCODE_DESIGNER_MASK   (0x7ffU << JTAG_IDCODE_DESIGNER_OFFSET)
/* Bits 10:7 - JEP-106 Continuation code */
/* Bits 6:0 - JEP-106 Identity code */
#define JTAG_IDCODE_DESIGNER_JEP106_CONT_OFFSET 7U
#define JTAG_IDCODE_DESIGNER_JEP106_CONT_MASK   (0xfU << ADIV5_DP_DESIGNER_JEP106_CONT_OFFSET)
#define JTAG_IDCODE_DESIGNER_JEP106_CODE_MASK   (0x7fU)

#define JTAG_IDCODE_PARTNO_DPv0 0xba00U

/* Constants to make RnW parameters more clear in code */
#define ADIV5_LOW_WRITE 0
#define ADIV5_LOW_READ  1

#define SWDP_ACK_OK          0x01U
#define SWDP_ACK_WAIT        0x02U
#define SWDP_ACK_FAULT       0x04U
#define SWDP_ACK_NO_RESPONSE 0x07U

/* Constants for the DP's quirks field */
#define ADIV5_DP_QUIRK_MINDP    (1U << 0U) /* DP is a minimal DP implementation */
#define ADIV5_DP_QUIRK_DUPED_AP (1U << 1U) /* DP has only 1 AP but the address decoding is bugged */
/* This one is not a quirk, but the field's a convinient place to store this */
#define ADIV5_AP_ACCESS_BANKED (1U << 7U) /* Last AP access was done using the banked interface */

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
	void (*read_block)(uint32_t addr, uint8_t *data, int size);
	void (*dap_write_block_sized)(uint32_t addr, uint8_t *data, int size, align_e align);
#endif
	uint32_t (*ap_read)(adiv5_access_port_s *ap, uint16_t addr);
	void (*ap_write)(adiv5_access_port_s *ap, uint16_t addr, uint32_t value);

	void (*mem_read)(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len);
	void (*mem_write)(adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align);
	uint8_t dev_index;
	uint8_t fault;

	uint8_t quirks;

	/* targetsel DPv2 */
	uint8_t instance;
	uint32_t targetsel;

	uint8_t version;

	/* DP designer (not implementer!) and partno */
	uint16_t designer_code;
	uint16_t partno;

	/* TARGETID designer and partno, present on DPv2 */
	uint16_t target_designer_code;
	uint16_t target_partno;
	uint8_t target_revision;
};

struct adiv5_access_port {
	int refcnt;

	adiv5_debug_port_s *dp;
	uint8_t apsel;

	uint32_t idr;
	uint32_t base;
	uint32_t csw;
	uint32_t ap_cortexm_demcr; /* Copy of demcr when starting */
	uint32_t ap_storage;       /* E.g to hold STM32F7 initial DBGMCU_CR value.*/

	/* AP designer and partno */
	uint16_t designer_code;
	uint16_t partno;
};

uint8_t make_packet_request(uint8_t RnW, uint16_t addr);

#if PC_HOSTED == 0
static inline bool adiv5_write_no_check(adiv5_debug_port_s *const dp, uint16_t addr, const uint32_t value)
{
	return dp->write_no_check(addr, value);
}

static inline uint32_t adiv5_read_no_check(adiv5_debug_port_s *const dp, uint16_t addr)
{
	return dp->read_no_check(addr);
}

static inline uint32_t adiv5_dp_read(adiv5_debug_port_s *dp, uint16_t addr)
{
	return dp->dp_read(dp, addr);
}

static inline uint32_t adiv5_dp_error(adiv5_debug_port_s *dp)
{
	return dp->error(dp, false);
}

static inline uint32_t adiv5_dp_low_access(adiv5_debug_port_s *dp, uint8_t RnW, uint16_t addr, uint32_t value)
{
	return dp->low_access(dp, RnW, addr, value);
}

static inline void adiv5_dp_abort(adiv5_debug_port_s *dp, uint32_t abort)
{
	dp->abort(dp, abort);
}

static inline uint32_t adiv5_ap_read(adiv5_access_port_s *ap, uint16_t addr)
{
	return ap->dp->ap_read(ap, addr);
}

static inline void adiv5_ap_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value)
{
	ap->dp->ap_write(ap, addr, value);
}

static inline void adiv5_mem_read(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len)
{
	ap->dp->mem_read(ap, dest, src, len);
}

static inline void adiv5_mem_write_sized(
	adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align)
{
	ap->dp->mem_write(ap, dest, src, len, align);
}

static inline void adiv5_dp_write(adiv5_debug_port_s *dp, uint16_t addr, uint32_t value)
{
	dp->low_access(dp, ADIV5_LOW_WRITE, addr, value);
}

#else
bool adiv5_write_no_check(adiv5_debug_port_s *dp, uint16_t addr, uint32_t value);
uint32_t adiv5_read_no_check(adiv5_debug_port_s *dp, uint16_t addr);
uint32_t adiv5_dp_read(adiv5_debug_port_s *dp, uint16_t addr);
uint32_t adiv5_dp_error(adiv5_debug_port_s *dp);
uint32_t adiv5_dp_low_access(adiv5_debug_port_s *dp, uint8_t RnW, uint16_t addr, uint32_t value);
void adiv5_dp_abort(adiv5_debug_port_s *dp, uint32_t abort);
uint32_t adiv5_ap_read(adiv5_access_port_s *ap, uint16_t addr);
void adiv5_ap_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value);
void adiv5_mem_read(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len);
void adiv5_mem_write_sized(adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align);
void adiv5_dp_write(adiv5_debug_port_s *dp, uint16_t addr, uint32_t value);
#endif

static inline uint32_t adiv5_dp_recoverable_access(adiv5_debug_port_s *dp, uint8_t rnw, uint16_t addr, uint32_t value)
{
	const uint32_t result = dp->low_access(dp, rnw, addr, value);
	/* If the access results in the no-response response, retry after clearing the error state */
	if (dp->fault == SWDP_ACK_NO_RESPONSE) {
		uint32_t response;
		/* Wait the response period, then clear the error */
		swd_proc.seq_in_parity(&response, 32);
		DEBUG_WARN("Recovering and re-trying access\n");
		dp->error(dp, true);
		return dp->low_access(dp, rnw, addr, value);
	}
	return result;
}

void adiv5_dp_init(adiv5_debug_port_s *dp);
void bmda_adiv5_dp_init(adiv5_debug_port_s *dp);
adiv5_access_port_s *adiv5_new_ap(adiv5_debug_port_s *dp, uint8_t apsel);
void remote_jtag_dev(const jtag_dev_s *jtag_dev);
void adiv5_ap_ref(adiv5_access_port_s *ap);
void adiv5_ap_unref(adiv5_access_port_s *ap);
void bmda_add_jtag_dev(uint32_t dev_index, const jtag_dev_s *jtag_dev);

void adiv5_jtag_dp_handler(uint8_t jd_index);
#if PC_HOSTED == 1
void bmda_jtag_dp_init(adiv5_debug_port_s *dp);
bool bmda_swd_dp_init(adiv5_debug_port_s *dp);
#endif

void adiv5_mem_write(adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len);
void *adiv5_unpack_data(void *dest, uint32_t src, uint32_t val, align_e align);
const void *adiv5_pack_data(uint32_t dest, const void *src, uint32_t *data, align_e align);

void ap_mem_access_setup(adiv5_access_port_s *ap, uint32_t addr, align_e align);
void adiv5_mem_write_bytes(adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align);
void advi5_mem_read_bytes(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t len);
void firmware_ap_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value);
uint32_t firmware_ap_read(adiv5_access_port_s *ap, uint16_t addr);
uint32_t firmware_swdp_low_access(adiv5_debug_port_s *dp, uint8_t RnW, uint16_t addr, uint32_t value);
uint32_t fw_adiv5_jtagdp_low_access(adiv5_debug_port_s *dp, uint8_t RnW, uint16_t addr, uint32_t value);
uint32_t firmware_swdp_read(adiv5_debug_port_s *dp, uint16_t addr);
uint32_t fw_adiv5_jtagdp_read(adiv5_debug_port_s *dp, uint16_t addr);

bool adiv5_swd_write_no_check(uint16_t addr, uint32_t data);
uint32_t adiv5_swd_read_no_check(uint16_t addr);
uint32_t adiv5_swd_clear_error(adiv5_debug_port_s *dp, bool protocol_recovery);
uint32_t adiv5_jtagdp_error(adiv5_debug_port_s *dp, bool protocol_recovery);

void firmware_swdp_abort(adiv5_debug_port_s *dp, uint32_t abort);
void adiv5_jtagdp_abort(adiv5_debug_port_s *dp, uint32_t abort);

void adiv5_swd_multidrop_scan(adiv5_debug_port_s *dp, uint32_t targetid);

uint32_t adiv5_dp_read_dpidr(adiv5_debug_port_s *dp);

#endif /* TARGET_ADIV5_H */
