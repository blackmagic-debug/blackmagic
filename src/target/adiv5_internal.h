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

#define ADIV5_APnDP     0x1000U
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

/* ADIv5 SWD to Dormant State sequence */
#define ADIV5_SWD_TO_DORMANT_SEQUENCE 0xe3bcU /* 16 bits, LSB (MSB: 0x3dc7) */

/* ADIv5 JTAG to Dormant Sequence */
#define ADIV5_JTAG_TO_DORMANT_SEQUENCE0 0x1fU       /* 5 bits */
#define ADIV5_JTAG_TO_DORMANT_SEQUENCE1 0x33bbbbbaU /* 31 bits, LSB  (MSB : 0x2eeeeee6) */
#define ADIV5_JTAG_TO_DORMANT_SEQUENCE2 0xffU       /* 8 bits  */

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
#define ADIV5_DP_BANK5 5U

/*
 * ADIv5 MEM-AP Registers
 *
 * The upper 4 bits of the uint16_t are used to encode A[11:8] for ADIv6.
 * XXX: We would use the form <0b000 APnDP A[11:0]> instead of <A[11:8] 0b000 APnDP A[7:0]>,
 * however this would be incompatible with older firmware and the remote protocol.
 * This should be adjusted because we can do some encoding shenanigans to make that work
 * in BMDA, but this has been chosen to make something work in the immediate present.
 */
#define ADIV5_AP_CSW      ADIV5_AP_REG(0xd00U)
#define ADIV5_AP_TAR_LOW  ADIV5_AP_REG(0xd04U)
#define ADIV5_AP_TAR_HIGH ADIV5_AP_REG(0xd08U)
/* 0x08 - Reserved */
#define ADIV5_AP_DRW   ADIV5_AP_REG(0xd0cU)
#define ADIV5_AP_DB(x) ADIV5_AP_REG(0xd10U + (4U * (x)))
/* 0x20:0xec - Reserved */
#define ADIV5_AP_BASE_HIGH ADIV5_AP_REG(0xdf0U)
#define ADIV5_AP_CFG       ADIV5_AP_REG(0xdf4U)
#define ADIV5_AP_BASE_LOW  ADIV5_AP_REG(0xdf8U)
#define ADIV5_AP_IDR       ADIV5_AP_REG(0xdfcU)

/* ROM table CIDR values */
#define CIDR0_OFFSET 0xff0U /* DBGCID0 */
#define CIDR1_OFFSET 0xff4U /* DBGCID1 */
#define CIDR2_OFFSET 0xff8U /* DBGCID2 */
#define CIDR3_OFFSET 0xffcU /* DBGCID3 */

#define PIDR0_OFFSET 0xfe0U /* DBGPID0 */
#define PIDR1_OFFSET 0xfe4U /* DBGPID1 */
#define PIDR2_OFFSET 0xfe8U /* DBGPID2 */
#define PIDR3_OFFSET 0xfecU /* DBGPID3 */
#define PIDR4_OFFSET 0xfd0U /* DBGPID4 */
#define PIDR5_OFFSET 0xfd4U /* DBGPID5 (Reserved) */
#define PIDR6_OFFSET 0xfd8U /* DBGPID6 (Reserved) */
#define PIDR7_OFFSET 0xfdcU /* DBGPID7 (Reserved) */

/* CoreSight ROM registers */
#define CORESIGHT_ROM_PRIDR0      0xc00U
#define CORESIGHT_ROM_DBGRSTRR    0xc10U
#define CORESIGHT_ROM_DBGRSTAR    0xc14U
#define CORESIGHT_ROM_DBGPCR_BASE 0xa00U
#define CORESIGHT_ROM_DBGPSR_BASE 0xa80U
#define CORESIGHT_ROM_DEVARCH     0xfbcU
#define CORESIGHT_ROM_DEVID       0xfc8U
#define CORESIGHT_ROM_DEVTYPE     0xfccU

#define CORESIGHT_ROM_PRIDR0_VERSION_MASK      (0xfU << 0U)
#define CORESIGHT_ROM_PRIDR0_VERSION_NOT_IMPL  0x0U
#define CORESIGHT_ROM_PRIDR0_HAS_DBG_RESET_REQ (1U << 4U)
#define CORESIGHT_ROM_PRIDR0_HAS_SYS_RESET_REQ (1U << 5U)
#define CORESIGHT_ROM_DBGPCR_PRESENT           (1U << 0U)
#define CORESIGHT_ROM_DBGPCR_PWRREQ            (1U << 1U)
#define CORESIGHT_ROM_DBGPSR_STATUS_ON         (1U << 0)
#define CORESIGHT_ROM_DBGRST_REQ               (1U << 0U)
#define CORESIGHT_ROM_DEVID_FORMAT             (0xfU << 0U)
#define CORESIGHT_ROM_DEVID_FORMAT_32BIT       0U
#define CORESIGHT_ROM_DEVID_FORMAT_64BIT       1U
#define CORESIGHT_ROM_DEVID_SYSMEM             (1U << 4U)
#define CORESIGHT_ROM_DEVID_HAS_POWERREQ       (1U << 5U)

#define CORESIGHT_ROM_ROMENTRY_ENTRY_MASK        (0x3U << 0U)
#define CORESIGHT_ROM_ROMENTRY_ENTRY_FINAL       0U
#define CORESIGHT_ROM_ROMENTRY_ENTRY_INVALID     1U
#define CORESIGHT_ROM_ROMENTRY_ENTRY_NOT_PRESENT 2U
#define CORESIGHT_ROM_ROMENTRY_ENTRY_PRESENT     3U
#define CORESIGHT_ROM_ROMENTRY_POWERID_VALID     (1U << 2U)
#define CORESIGHT_ROM_ROMENTRY_POWERID_SHIFT     4U
#define CORESIGHT_ROM_ROMENTRY_POWERID_MASK      (0x1fU << CORESIGHT_ROM_ROMENTRY_POWERID_SHIFT)
#define CORESIGHT_ROM_ROMENTRY_OFFSET_MASK       UINT64_C(0xfffffffffffff000)

/*
 * Component class ID register can be broken down into the following logical
 * interpretation of the 32bit value consisting of the least significant bytes
 * of the 4 CID registers:
 * |7   ID3 reg   0|7   ID2 reg   0|7   ID1 reg   0|7   ID0 reg   0|
 * |1|0|1|1|0|0|0|1|0|0|0|0|0|1|0|1| | | | |0|0|0|0|0|0|0|0|1|1|0|1|
 * |31           24|23           16|15   12|11     |              0|
 * \_______________ ______________/\___ __/\___________ ___________/
 *                 V                   V               V
 *             Preamble            Component       Preamble
 *                                   Class
 * \_______________________________ _______________________________/
 *                                 V
 *                           Component ID
 */
#define CID_PREAMBLE    UINT32_C(0xb105000d)
#define CID_CLASS_MASK  UINT32_C(0x0000f000)
#define CID_CLASS_SHIFT 12U

#define PIDR_JEP106_CONT_OFFSET 32U                                         /*JEP-106 Continuation Code offset */
#define PIDR_JEP106_CONT_MASK   (UINT64_C(0xf) << PIDR_JEP106_CONT_OFFSET)  /*JEP-106 Continuation Code mask */
#define PIDR_REV_OFFSET         20U                                         /* Revision bits offset */
#define PIDR_REV_MASK           (UINT64_C(0xfff) << PIDR_REV_OFFSET)        /* Revision bits mask */
#define PIDR_JEP106_USED_OFFSET 19U                                         /* JEP-106 code used flag offset */
#define PIDR_JEP106_USED        (UINT64_C(1) << PIDR_JEP106_USED_OFFSET)    /* JEP-106 code used flag */
#define PIDR_JEP106_CODE_OFFSET 12U                                         /* JEP-106 code offset */
#define PIDR_JEP106_CODE_MASK   (UINT64_C(0x7f) << PIDR_JEP106_CODE_OFFSET) /* JEP-106 code mask */
#define PIDR_PN_MASK            UINT64_C(0xfff)                             /* Part number */
#define PIDR_SIZE_OFFSET        36U
#define PIDR_SIZE_MASK          (UINT64_C(0xf) << PIDR_SIZE_OFFSET)

#define DEVTYPE_MASK               0x000000ffU
#define DEVARCH_ARCHID_MASK        0xffffU
#define DEVARCH_ARCHID_ROMTABLE_V0 0x0af7U
#define DEVARCH_PRESENT            (1U << 20U)

#define SWD_ACK_OK          0x01U
#define SWD_ACK_WAIT        0x02U
#define SWD_ACK_FAULT       0x04U
#define SWD_ACK_NO_RESPONSE 0x07U

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

#if CONFIG_BMDA == 1
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

	/* TARGETID designer and partno, present on DPv2+ */
	uint16_t target_designer_code;
	uint16_t target_partno;

	/* DPv3+ bus address width */
	uint8_t address_width;
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

/* The following enum is based on the Component Class value table 13-3 of the ADIv5 specification. */
typedef enum cid_class {
	cidc_gvc = 0x0,     /* Generic verification component*/
	cidc_romtab = 0x1,  /* ROM Table, std. layout (ADIv5 Chapter 14) */
	/* 0x2 - 0x8 */     /* Reserved */
	cidc_dc = 0x9,      /* Debug component, std. layout (CoreSight Arch. Spec.) */
	/* 0xa */           /* Reserved */
	cidc_ptb = 0xb,     /* Peripheral Test Block (PTB) */
	/* 0xc */           /* Reserved */
	cidc_dess = 0xd,    /* OptimoDE Data Engine SubSystem (DESS) component */
	cidc_gipc = 0xe,    /* Generic IP Component */
	cidc_sys = 0xf,     /* CoreLink, PrimeCell, or other system component with no standard register layout */
	cidc_unknown = 0x10 /* Not a valid component class */
} cid_class_e;

/* Enumeration of CoreSight component architectures */
typedef enum arm_arch {
	aa_nosupport,
	aa_cortexm,
	aa_cortexa,
	aa_cortexr,
	aa_rom_table,
	aa_access_port,
	aa_end
} arm_arch_e;

/* Structure defining an ARM CoreSight component of some kind */
typedef struct arm_coresight_component {
	uint16_t part_number;
	uint8_t dev_type;
	uint16_t arch_id;
	arm_arch_e arch;
	cid_class_e cidc;
#if ENABLE_DEBUG == 1
	const char *type;
	const char *full;
#endif
} arm_coresight_component_s;

/* Helper for building an ADIv5 request */
uint8_t make_packet_request(uint8_t rnw, uint16_t addr);

#endif /* TARGET_ADIV5_INTERNAL_H */
