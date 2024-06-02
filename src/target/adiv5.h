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

#ifndef TARGET_ADIV5_H
#define TARGET_ADIV5_H

#include "general.h"
#include "jtag_scan.h"
#include "swd.h"
#include "adiv5_internal.h"
#include "adiv5_interface.h"

/* DP DPIDR */
#define ADIV5_DP_DPIDR_REVISION_OFFSET 28U
#define ADIV5_DP_DPIDR_REVISION_MASK   (0xfU << ADIV5_DP_DPIDR_REVISION_OFFSET)
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

/* AP Control and Status Word (CSW) */
#define ADIV5_AP_CSW_DBGSWENABLE (1U << 31U)
/* Bits 30:24 - Prot, Implementation defined and bus dependant */
/* For AXI3, AXI4: */
#define ADIV5_AP_CSW_AXI3_4_PROT_MASK 0x7f000000U
/* For APB4, APB5, AXI5: */
#define ADIV5_AP_CSW_AXI5_PROT_MASK 0x70000000U
/* For all AXI and APB4 + APB5: */
#define ADIV5_AP_CSW_AXI_PROT_NS   (1U << 29U) /* Set if the request should be non-secure */
#define ADIV5_AP_CSW_AXI_PROT_PRIV (1U << 28U) /* Request is privileged */
/* Bit 15 - MTE (Memory Tagging Enable) for AXI busses */
#define ADIV5_AP_CSW_AXI_MTE (1U << 15U)
/* For AHB3, AHB5: */
#define ADIV5_AP_CSW_AHB_HNONSEC    (1U << 30U) /* Must be set for ABH3 to operate correctly */
#define ADIV5_AP_CSW_AHB_MASTERTYPE (1U << 29U) /* AHB-AP as requester if set, secondary ID if not */
#define ADIV5_AP_CSW_AHB_HPROT_MASK 0x1f000000U
#define ADIV5_AP_CSW_AHB_HPROT_PRIV (1U << 25U) /* Request is privileged */
#define ADIV5_AP_CSW_AHB_HPROT_DATA (1U << 24U) /* Request is a data access */
/* For APB2 and APB3, bits 23 thorugh 30 are reserved */
/* For APB4 and APB5: */
#define ADIV5_AP_CSW_APB_PPROT_MASK 0x70000000U
#define ADIV5_AP_CSW_APB_PPROT_PRIV (1U << 28U) /* Request is privileged */
#define ADIV5_AP_CSW_APB_PPROT_NS   (1U << 29U) /* Set if the request should be non-secure */
/* Bit 23 - SPIDEN on most bus types */
#define ADIV5_AP_CSW_SPIDEN (1U << 23U) /* Secure Invasive Debugging Enable */
/* Bits 22:16 - Reserved */
/* Bits 15:12 - Type, must be zero */
/* Bit 15 on AHB5 w/ enhanced HPROT control - Request is sharable */
/* Bits 11:8 - Mode, must be zero */
#define ADIV5_AP_CSW_TRINPROG       (1U << 7U)
#define ADIV5_AP_CSW_AP_ENABLED     (1U << 6U)
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
#define ADIV5_AP_BASE_FORMAT   (1U << 1U)

#define ADIV5_AP_BASE_PRESENT_NO_ENTRY (0U << 0U)
#define ADIV5_AP_BASE_FORMAT_LEGACY    (0U << 1U)
#define ADIV5_AP_BASE_FORMAT_ADIV5     (1U << 1U)
#define ADIV5_AP_BASE_NOT_PRESENT      0xffffffffU

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

#define ADIV5_AP_IDR_CLASS_JTAG 0U
#define ADIV5_AP_IDR_CLASS_COM  1U
#define ADIV5_AP_IDR_CLASS_MEM  8U

#define ADIV5_AP_IDR_TYPE_AHB3       1U
#define ADIV5_AP_IDR_TYPE_APB2_3     2U
#define ADIV5_AP_IDR_TYPE_AXI3_4     4U
#define ADIV5_AP_IDR_TYPE_AHB5       5U
#define ADIV5_AP_IDR_TYPE_APB4_5     6U
#define ADIV5_AP_IDR_TYPE_AXI5       7U
#define ADIV5_AP_IDR_TYPE_AHB5_HPROT 8U

#define ADIV5_AP_CFG_LARGE_ADDRESS (1U << 1U)

#define ADIV5_AP_FLAGS_64BIT   (1U << 0U)
#define ADIV5_AP_FLAGS_HAS_MEM (1U << 1U)

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

#define JTAG_IDCODE_PARTNO_DPV0 0xba00U

/* Constants for the DP's quirks field */
#define ADIV5_DP_QUIRK_MINDP    (1U << 0U) /* DP is a minimal DP implementation */
#define ADIV5_DP_QUIRK_DUPED_AP (1U << 1U) /* DP has only 1 AP but the address decoding is bugged */
/* This one is not a quirk, but the field's a convinient place to store this */
#define ADIV5_AP_ACCESS_BANKED (1U << 7U) /* Last AP access was done using the banked interface */

/* JTAG DP discovery handler */
void adiv5_jtag_dp_handler(uint8_t dev_index);

/* SWD multi-drop DP discovery handler */
void adiv5_swd_multidrop_scan(adiv5_debug_port_s *dp, uint32_t targetid);

/* DP and AP discovery functions */
void adiv5_dp_init(adiv5_debug_port_s *dp);
adiv5_access_port_s *adiv5_new_ap(adiv5_debug_port_s *dp, uint8_t apsel);

/* AP lifetime management functions */
void adiv5_ap_ref(adiv5_access_port_s *ap);
void adiv5_ap_unref(adiv5_access_port_s *ap);

#if PC_HOSTED == 1
/* BMDA interposition functions for DP setup */
void bmda_adiv5_dp_init(adiv5_debug_port_s *dp);
void bmda_jtag_dp_init(adiv5_debug_port_s *dp);
bool bmda_swd_dp_init(adiv5_debug_port_s *dp);

/* BMDA interposition function for JTAG device setup */
void bmda_add_jtag_dev(uint32_t dev_index, const jtag_dev_s *jtag_dev);
#endif

/* Data transfer value packing/unpacking helper functions */
void *adiv5_unpack_data(void *dest, target_addr32_t src, uint32_t data, align_e align);
const void *adiv5_pack_data(target_addr32_t dest, const void *src, uint32_t *data, align_e align);

/* ADIv5 high-level memory write function */
void adiv5_mem_write(adiv5_access_port_s *ap, target_addr64_t dest, const void *src, size_t len);

/* ADIv5 low-level logical operation functions for memory access */
void adiv5_mem_access_setup(adiv5_access_port_s *ap, target_addr64_t addr, align_e align);
void adiv5_mem_write_bytes(adiv5_access_port_s *ap, target_addr64_t dest, const void *src, size_t len, align_e align);
void advi5_mem_read_bytes(adiv5_access_port_s *ap, void *dest, target_addr64_t src, size_t len);
/* ADIv5 logical operation functions for AP register I/O */
void adiv5_ap_reg_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value);
uint32_t adiv5_ap_reg_read(adiv5_access_port_s *ap, uint16_t addr);

/* ADIv5 DP logical operation function for reading DPIDR safely */
uint32_t adiv5_dp_read_dpidr(adiv5_debug_port_s *dp);

/* SWD low-level ADIv5 routines */
bool adiv5_swd_write_no_check(uint16_t addr, uint32_t data);
uint32_t adiv5_swd_read_no_check(uint16_t addr);
uint32_t adiv5_swd_read(adiv5_debug_port_s *dp, uint16_t addr);
uint32_t adiv5_swd_raw_access(adiv5_debug_port_s *dp, uint8_t rnw, uint16_t addr, uint32_t value);
uint32_t adiv5_swd_clear_error(adiv5_debug_port_s *dp, bool protocol_recovery);
void adiv5_swd_abort(adiv5_debug_port_s *dp, uint32_t abort);

/* JTAG low-level ADIv5 routines */
uint32_t adiv5_jtag_read(adiv5_debug_port_s *dp, uint16_t addr);
uint32_t adiv5_jtag_raw_access(adiv5_debug_port_s *dp, uint8_t rnw, uint16_t addr, uint32_t value);
uint32_t adiv5_jtag_clear_error(adiv5_debug_port_s *dp, bool protocol_recovery);
void adiv5_jtag_abort(adiv5_debug_port_s *dp, uint32_t abort);

#endif /* TARGET_ADIV5_H */
