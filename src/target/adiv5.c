/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2018-2021 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
 * This file implements transport generic ADIv5 functions.
 *
 * See the following ARM Reference Documents:
 * - ARM Debug Interface v5 Architecture Specification, ARM IHI 0031E
 */
#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "target_probe.h"
#include "jep106.h"
#include "adiv5.h"
#include "cortexm.h"
#include "cortex_internal.h"
#include "exception.h"
#if PC_HOSTED == 1
#include "bmp_hosted.h"
#endif

/*
 * All this should probably be defined in a dedicated ADIV5 header, so that they
 * are consistently named and accessible when needed in the codebase.
 */

/*
 * This value is taken from the ADIv5 spec table C1-2
 * "AP Identification types for an AP designed by Arm"
 * §C1.3 pg146. This defines a AHB3 AP when the class value is 8
 */
#define ARM_AP_TYPE_AHB3 1U

/* ROM table CIDR values */
#define CIDR0_OFFSET 0xff0U /* DBGCID0 */
#define CIDR1_OFFSET 0xff4U /* DBGCID1 */
#define CIDR2_OFFSET 0xff8U /* DBGCID2 */
#define CIDR3_OFFSET 0xffcU /* DBGCID3 */

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

#define PIDR0_OFFSET 0xfe0U /* DBGPID0 */
#define PIDR1_OFFSET 0xfe4U /* DBGPID1 */
#define PIDR2_OFFSET 0xfe8U /* DBGPID2 */
#define PIDR3_OFFSET 0xfecU /* DBGPID3 */
#define PIDR4_OFFSET 0xfd0U /* DBGPID4 */
#define PIDR5_OFFSET 0xfd4U /* DBGPID5 (Reserved) */
#define PIDR6_OFFSET 0xfd8U /* DBGPID6 (Reserved) */
#define PIDR7_OFFSET 0xfdcU /* DBGPID7 (Reserved) */

#define PIDR_JEP106_CONT_OFFSET 32U                                         /*JEP-106 Continuation Code offset */
#define PIDR_JEP106_CONT_MASK   (UINT64_C(0xf) << PIDR_JEP106_CONT_OFFSET)  /*JEP-106 Continuation Code mask */
#define PIDR_REV_OFFSET         20U                                         /* Revision bits offset */
#define PIDR_REV_MASK           (UINT64_C(0xfff) << PIDR_REV_OFFSET)        /* Revision bits mask */
#define PIDR_JEP106_USED_OFFSET 19U                                         /* JEP-106 code used flag offset */
#define PIDR_JEP106_USED        (UINT64_C(1) << PIDR_JEP106_USED_OFFSET)    /* JEP-106 code used flag */
#define PIDR_JEP106_CODE_OFFSET 12U                                         /* JEP-106 code offset */
#define PIDR_JEP106_CODE_MASK   (UINT64_C(0x7f) << PIDR_JEP106_CODE_OFFSET) /* JEP-106 code mask */
#define PIDR_PN_MASK            UINT64_C(0xfff)                             /* Part number */

#define DEVTYPE_OFFSET 0xfccU /* CoreSight Device Type Register */
#define DEVARCH_OFFSET 0xfbcU /* CoreSight Device Architecture Register */

#define DEVTYPE_MASK        0x000000ffU
#define DEVARCH_PRESENT     (1U << 20U)
#define DEVARCH_ARCHID_MASK 0x0000ffffU

typedef enum arm_arch {
	aa_nosupport,
	aa_cortexm,
	aa_cortexa,
	aa_cortexr,
	aa_end
} arm_arch_e;

#if ENABLE_DEBUG == 1
#define ARM_COMPONENT_STR(...) __VA_ARGS__
#else
#define ARM_COMPONENT_STR(...)
#endif

/*
 * The part number list was adopted from OpenOCD:
 * https://sourceforge.net/p/openocd/code/ci/406f4/tree/src/target/arm_adi_v5.c#l932
 *
 * The product ID register consists of several parts. For a full description
 * refer to ARM Debug Interface v5 Architecture Specification. Based on the
 * document the pidr is 64 bit long and has the following interpratiation:
 * |7   ID7 reg   0|7   ID6 reg   0|7   ID5 reg   0|7   ID4 reg   0|
 * |0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0| | | | | | | | |
 * |63           56|55           48|47           40|39   36|35   32|
 * \_______________________ ______________________/\___ __/\___ ___/
 *                         V                           V       V
 *                    Reserved, RAZ                   4KB      |
 *                                                   count     |
 *                                                          JEP-106
 *                                                     Continuation Code (only valid for JEP-106 codes)
 *
 * |7   ID3 reg   0|7   ID2 reg   0|7   ID1 reg   0|7   ID0 reg   0|
 * | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
 * |31   28|27   24|23   20|||18   |     12|11     |              0|
 * \___ __/\__ ___/\___ __/ |\______ _____/\___________ ___________/
 *     V      V        V    |       V                  V
 *  RevAnd    |    Revision |  JEP-106 ID         Part number
 *            |             |  (no parity)
 *        Customer          19
 *        modified          `- JEP-106 code is used
 *
 * only a subset of Part numbers are listed,
 * the ones that have ARM as the designer code.
 *
 * To properly identify ADIv6 CoreSight components, two additional fields,
 * DEVTYPE and ARCHID are read.
 * The dev_type and arch_id values in the table below were found in the
 * corresponding logic in pyOCD:
 * https://github.com/mbedmicro/pyOCD/blob/master/pyocd/coresight/component_ids.py
 *
 * Additional reference on the DEVTYPE and DEVARCH registers can be found in the
 * ARM CoreSight Architecture Specification v3.0, sections B2.3.4 and B2.3.8.
 */
static const struct {
	uint16_t part_number;
	uint8_t dev_type;
	uint16_t arch_id;
	arm_arch_e arch;
	cid_class_e cidc;
#if ENABLE_DEBUG == 1
	const char *type;
	const char *full;
#endif
} arm_component_lut[] = {
	{0x000, 0x00, 0, aa_cortexm, cidc_gipc, ARM_COMPONENT_STR("Cortex-M3 SCS", "(System Control Space)")},
	{0x001, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M3 ITM", "(Instrumentation Trace Module)")},
	{0x002, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M3 DWT", "(Data Watchpoint and Trace)")},
	{0x003, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M3 FBP", "(Flash Patch and Breakpoint)")},
	{0x008, 0x00, 0, aa_cortexm, cidc_gipc, ARM_COMPONENT_STR("Cortex-M0 SCS", "(System Control Space)")},
	{0x00a, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M0 DWT", "(Data Watchpoint and Trace)")},
	{0x00b, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M0 BPU", "(Breakpoint Unit)")},
	{0x00c, 0x00, 0, aa_cortexm, cidc_gipc, ARM_COMPONENT_STR("Cortex-M4 SCS", "(System Control Space)")},
	{0x00d, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight ETM11", "(Embedded Trace)")},
	{0x00e, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M7 FBP", "(Flash Patch and Breakpoint)")},
	{0x101, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("System TSGEN", "(Time Stamp Generator)")},
	{0x471, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M0  ROM", "(Cortex-M0 ROM)")},
	{0x490, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A15 GIC", "(Generic Interrupt Controller)")},
	{0x4c0, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M0+ ROM", "(Cortex-M0+ ROM)")},
	{0x4c3, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M3 ROM", "(Cortex-M3 ROM)")},
	{0x4c4, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M4 ROM", "(Cortex-M4 ROM)")},
	{0x4c7, 0x00, 0, aa_nosupport, cidc_unknown,
		ARM_COMPONENT_STR("Cortex-M7 PPB", "(Cortex-M7 Private Peripheral Bus ROM Table)")},
	{0x4c8, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M7 ROM", "(Cortex-M7 ROM)")},
	{0x906, 0x14, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight CTI", "(Cross Trigger)")},
	{0x907, 0x21, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight ETB", "(Trace Buffer)")},
	{0x908, 0x12, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight CSTF", "(Trace Funnel)")},
	{0x910, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight ETM9", "(Embedded Trace)")},
	{0x912, 0x11, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight TPIU", "(Trace Port Interface Unit)")},
	{0x913, 0x43, 0, aa_nosupport, cidc_unknown,
		ARM_COMPONENT_STR("CoreSight ITM", "(Instrumentation Trace Macrocell)")},
	{0x914, 0x11, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight SWO", "(Single Wire Output)")},
	{0x917, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight HTM", "(AHB Trace Macrocell)")},
	{0x920, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight ETM11", "(Embedded Trace)")},
	{0x921, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A8 ETM", "(Embedded Trace)")},
	{0x922, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A8 CTI", "(Cross Trigger)")},
	{0x923, 0x11, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M3 TPIU", "(Trace Port Interface Unit)")},
	{0x924, 0x13, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M3 ETM", "(Embedded Trace)")},
	{0x925, 0x13, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M4 ETM", "(Embedded Trace)")},
	{0x930, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-R4 ETM", "(Embedded Trace)")},
	{0x932, 0x31, 0x0a31, aa_nosupport, cidc_unknown,
		ARM_COMPONENT_STR("CoreSight MTB-M0+", "(Simple Execution Trace)")},
	{0x941, 0x00, 0, aa_nosupport, cidc_unknown,
		ARM_COMPONENT_STR("CoreSight TPIU-Lite", "(Trace Port Interface Unit)")},
	{0x950, 0x13, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A9 PTM", "(Program Trace Macrocell)")},
	{0x955, 0x00, 0, aa_nosupport, cidc_unknown,
		ARM_COMPONENT_STR("CoreSight Component", "(unidentified Cortex-A5 component)")},
	{0x956, 0x13, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A7 ETM", "(Embedded Trace)")},
	{0x95f, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A15 PTM", "(Program Trace Macrocell)")},
	{0x961, 0x32, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight TMC", "(Trace Memory Controller)")},
	{0x961, 0x21, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight TMC", "(Trace Buffer)")},
	{0x962, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight STM", "(System Trace Macrocell)")},
	{0x963, 0x63, 0x0a63, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight STM", "(System Trace Macrocell)")},
	{0x975, 0x13, 0x4a13, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M7 ETM", "(Embedded Trace)")},
	{0x9a0, 0x16, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("CoreSight PMU", "(Performance Monitoring Unit)")},
	{0x9a1, 0x11, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M4 TPIU", "(Trace Port Interface Unit)")},
	{0x9a6, 0x14, 0x1a14, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M0+ CTI", "(Cross Trigger Interface)")},
	{0x9a9, 0x11, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M7 TPIU", "(Trace Port Interface Unit)")},
	{0x9a5, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A5 ETM", "(Embedded Trace)")},
	{0x9a7, 0x16, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A7 PMU", "(Performance Monitor Unit)")},
	{0x9af, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A15 PMU", "(Performance Monitor Unit)")},
	{0xc05, 0x00, 0, aa_cortexa, cidc_dc, ARM_COMPONENT_STR("Cortex-A5 Debug", "(Debug Unit)")},
	{0xc07, 0x15, 0, aa_cortexa, cidc_dc, ARM_COMPONENT_STR("Cortex-A7 Debug", "(Debug Unit)")},
	{0xc08, 0x00, 0, aa_cortexa, cidc_dc, ARM_COMPONENT_STR("Cortex-A8 Debug", "(Debug Unit)")},
	{0xc09, 0x15, 0, aa_cortexa, cidc_dc, ARM_COMPONENT_STR("Cortex-A9 Debug", "(Debug Unit)")},
	{0xc0f, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A15 Debug", "(Debug Unit)")}, /* support? */
	{0xc14, 0x15, 0, aa_cortexr, cidc_unknown, ARM_COMPONENT_STR("Cortex-R4", "(Debug Unit)")},
	{0xcd0, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Atmel DSU", "(Device Service Unit)")},
	{0xd20, 0x00, 0x2a04, aa_cortexm, cidc_gipc, ARM_COMPONENT_STR("Cortex-M23", "(System Control Space)")},
	{0xd20, 0x11, 0, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M23", "(Trace Port Interface Unit)")},
	{0xd20, 0x13, 0, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M23", "(Embedded Trace)")},
	{0xd20, 0x31, 0x0a31, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M23", "(Micro Trace Buffer)")},
	{0xd20, 0x00, 0x1a02, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M23", "(Data Watchpoint and Trace)")},
	{0xd20, 0x00, 0x1a03, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M23", "(Breakpoint Unit)")},
	{0xd20, 0x14, 0x1a14, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M23", "(Cross Trigger)")},
	{0xd21, 0x00, 0x2a04, aa_cortexm, cidc_gipc, ARM_COMPONENT_STR("Cortex-M33", "(System Control Space)")},
	{0xd21, 0x31, 0x0a31, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M33", "(Micro Trace Buffer)")},
	{0xd21, 0x43, 0x1a01, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M33", "(Instrumentation Trace Macrocell)")},
	{0xd21, 0x00, 0x1a02, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M33", "(Data Watchpoint and Trace)")},
	{0xd21, 0x00, 0x1a03, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M33", "(Breakpoint Unit)")},
	{0xd21, 0x14, 0x1a14, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M33", "(Cross Trigger)")},
	{0xd21, 0x13, 0x4a13, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M33", "(Embedded Trace)")},
	{0xd21, 0x11, 0, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M33", "(Trace Port Interface Unit)")},
	{0x132, 0x31, 0x0a31, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 MTB", "(Execution Trace)")},
	{0x132, 0x43, 0x1a01, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 ITM", "(Instrumentation Trace Module)")},
	{0x132, 0x00, 0x1a02, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 DWT", "(Data Watchpoint and Trace)")},
	{0x132, 0x00, 0x1a03, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 BPU", "(Breakpoint Unit)")},
	{0x132, 0x14, 0x1a14, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 CTI", "(Cross Trigger)")},
	{0x132, 0x00, 0x2a04, aa_cortexm, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 SCS", "(System Control Space)")},
	{0x132, 0x13, 0x4a13, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 ETM", "(Embedded Trace)")},
	{0x132, 0x11, 0, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 TPIU", "(Trace Port Interface Unit)")},
	{0x9a3, 0x13, 0, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("nRF NTB", "(Nordic Trace Buffer)")},
	{0xfff, 0x00, 0, aa_end, cidc_unknown, ARM_COMPONENT_STR("end", "end")},
};

#if ENABLE_DEBUG == 1
static const char *adiv5_arm_ap_type_string(const uint8_t ap_type, const uint8_t ap_class)
{
	/*
	 * Values taken from ADIv5 spec §C1.3 pg146.
	 * table C1-2 "AP Identification types for an AP designed by Arm"
	 */

	/* All types except 0 are only valid for ap_class == 0x8 */
	if (ap_class == 0x8U || ap_type == 0U) {
		switch (ap_type) {
		case 0U:
			/* Type 0 APs are determined by the class code */
			if (ap_class == 0U)
				return "JTAG-AP";
			if (ap_class == 1U)
				return "COM-AP";
			break;
		case 0x1U:
			return "AHB3-AP";
		case 0x2U:
			return "APB2/3-AP";
		/* 0x3 is not defined */
		case 0x4U:
			return "AXI3/4-AP";
		case 0x5U:
			return "AHB5-AP";
		case 0x6U:
			return "APB4/5-AP";
		case 0x7U:
			return "AXI5-AP";
		case 0x8U:
			return "AHB5-AP";
		default:
			break;
		}
	}
	return "Unknown";
}

static const char *adiv5_cid_class_string(const cid_class_e cid_class)
{
	switch (cid_class) {
	case cidc_gvc:
		return "Generic verification component";
	case cidc_romtab:
		return "ROM Table";
	case cidc_dc:
		return "Debug component";
	case cidc_ptb:
		return "Peripheral Test Block";
	case cidc_dess:
		return "OptimoDE Data Engine SubSystem component";
	case cidc_gipc:
		return "Generic IP component";
	case cidc_sys:
		return "Non STD System component";
	default:
		return "Unknown component"; /* Noted as reserved in the spec */
	}
};
#endif

/* Used to probe for a protected SAMX5X device */
#define SAMX5X_DSU_CTRLSTAT 0x41002100U
#define SAMX5X_STATUSB_PROT (1U << 16U)

void adiv5_ap_ref(adiv5_access_port_s *ap)
{
	if (ap->refcnt == 0)
		ap->dp->refcnt++;
	ap->refcnt++;
}

static void adiv5_dp_unref(adiv5_debug_port_s *dp)
{
	if (--(dp->refcnt) == 0)
		free(dp);
}

void adiv5_ap_unref(adiv5_access_port_s *ap)
{
	if (--(ap->refcnt) == 0) {
		adiv5_dp_unref(ap->dp);
		free(ap);
	}
}

static uint32_t adiv5_mem_read32(adiv5_access_port_s *ap, uint32_t addr)
{
	uint32_t ret;
	adiv5_mem_read(ap, &ret, addr, sizeof(ret));
	return ret;
}

static uint32_t adiv5_ap_read_id(adiv5_access_port_s *ap, uint32_t addr)
{
	uint32_t res = 0;
	uint8_t data[16];
	adiv5_mem_read(ap, data, addr, sizeof(data));
	for (size_t i = 0; i < 4U; ++i)
		res |= (uint32_t)data[4U * i] << (i * 8U);
	return res;
}

static uint64_t adiv5_ap_read_pidr(adiv5_access_port_s *ap, uint32_t addr)
{
	uint64_t pidr = adiv5_ap_read_id(ap, addr + PIDR4_OFFSET);
	pidr = pidr << 32U | adiv5_ap_read_id(ap, addr + PIDR0_OFFSET);
	return pidr;
}

/*
 * This function tries to halt Cortex-M processors.
 * To handle WFI and other sleep states, it does this in as tight a loop as it can,
 * either using the TRNCNT bits, or, if on a minimal DP implementation by doing the
 * memory writes as fast as possible.
 */
static uint32_t cortexm_initial_halt(adiv5_access_port_s *ap)
{
	/* Read the current CTRL/STATUS register value to use in the non-minimal DP case */
	const uint32_t ctrlstat = adiv5_dp_read(ap->dp, ADIV5_DP_CTRLSTAT);

	platform_timeout_s halt_timeout;
	platform_timeout_set(&halt_timeout, cortexm_wait_timeout);

	/* Setup to read/write DHCSR */
	/* ap_mem_access_setup() uses ADIV5_AP_CSW_ADDRINC_SINGLE which is undesirable for our use here */
	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw | ADIV5_AP_CSW_SIZE_WORD);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_TAR, CORTEXM_DHCSR);
	/* Write (and do a dummy read of) DHCSR to ensure debug is enabled */
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DRW, CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_DEBUGEN);
	adiv5_dp_read(ap->dp, ADIV5_DP_RDBUFF);

	bool reset_seen = false;
	while (!platform_timeout_is_expired(&halt_timeout)) {
		uint32_t dhcsr;

		/* If we're not on a minimal DP implementation, use TRNCNT to help */
		if (!(ap->dp->quirks & ADIV5_DP_QUIRK_MINDP)) {
			/* Ask the AP to repeatedly retry the write to DHCSR */
			adiv5_dp_low_access(
				ap->dp, ADIV5_LOW_WRITE, ADIV5_DP_CTRLSTAT, ctrlstat | ADIV5_DP_CTRLSTAT_TRNCNT(0xfffU));
		}
		/* Repeatedly try to halt the processor */
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DRW,
			CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_DEBUGEN | CORTEXM_DHCSR_C_HALT);
		dhcsr = adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_AP_DRW, 0);

		/*
		 * If we are on a minimal DP implementation, then we have to do things a little differently
		 * so the reads behave consistently. If we use raw accesses as above, then on some parts the
		 * data we want to read will be returned in the first raw access, and on others the read
		 * will do nothing (return 0) and instead need RDBUFF read to get the data.
		 */
		if ((ap->dp->quirks & ADIV5_DP_QUIRK_MINDP)
#if PC_HOSTED == 1
			&& bmda_probe_info.type != PROBE_TYPE_CMSIS_DAP
#endif
		)
			dhcsr = adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_DP_RDBUFF, 0);

		/*
		 * Check how we did, handling some errata along the way.
		 * On STM32F7 parts, invalid DHCSR reads of 0xffffffff and 0xa05f0000 may happen,
		 * so filter those out (we check for the latter by checking the reserved bits are 0)
		 */
		if (dhcsr == 0xffffffffU || (dhcsr & 0xf000fff0U) != 0)
			continue;
		/* Now we've got some confidence we've got a good read, check for resets */
		if ((dhcsr & CORTEXM_DHCSR_S_RESET_ST) && !reset_seen) {
			if (connect_assert_nrst)
				return dhcsr;
			reset_seen = true;
			continue;
		}
		/* And finally check if halt succeeded */
		if ((dhcsr & (CORTEXM_DHCSR_S_HALT | CORTEXM_DHCSR_C_DEBUGEN)) ==
			(CORTEXM_DHCSR_S_HALT | CORTEXM_DHCSR_C_DEBUGEN))
			return dhcsr;
	}

	return 0U;
}

/*
 * Prepare the core to read the ROM tables, PIDR, etc
 *
 * Because of various errata, failing to halt the core is considered
 * a hard error. We also need to set the debug exception and monitor
 * control register (DEMCR) up but save its value to restore later,
 * and release the core from reset when connecting under reset.
 *
 * Example errata for STM32F7:
 * - fails reading romtable in WFI
 * - fails with some AP accesses when romtable is read under reset.
 * - fails reading some ROMTABLE entries w/o TRCENA
 * - fails reading outside SYSROM when halted from WFI and DBGMCU_CR not set.
 *
 * Example errata for STM32F0
 * - fails reading DBGMCU when under reset
 */
static bool cortexm_prepare(adiv5_access_port_s *ap)
{
#if PC_HOSTED == 1 || ENABLE_DEBUG == 1
	uint32_t start_time = platform_time_ms();
#endif
	uint32_t dhcsr = cortexm_initial_halt(ap);
	if (!dhcsr) {
		DEBUG_ERROR("Halt via DHCSR(%08" PRIx32 "): failure after %" PRIu32 "ms\nTry again with longer "
					"timeout or connect under reset\n",
			adiv5_mem_read32(ap, CORTEXM_DHCSR), platform_time_ms() - start_time);
		return false;
	}
	/* Clear any residual WAIT fault code to keep things in good state for the next steps */
	ap->dp->fault = 0;
	DEBUG_INFO("Halt via DHCSR(%08" PRIx32 "): success after %" PRIu32 "ms\n", dhcsr, platform_time_ms() - start_time);
	/* Save the old value of DEMCR and enable the DWT, and both vector table debug bits */
	ap->ap_cortexm_demcr = adiv5_mem_read32(ap, CORTEXM_DEMCR);
	const uint32_t demcr = CORTEXM_DEMCR_TRCENA | CORTEXM_DEMCR_VC_HARDERR | CORTEXM_DEMCR_VC_CORERESET;
	adiv5_mem_write(ap, CORTEXM_DEMCR, &demcr, sizeof(demcr));
	/* Having setup DEMCR, try to observe the core being released from reset */
	platform_timeout_s reset_timeout;
	platform_timeout_set(&reset_timeout, cortexm_wait_timeout);
	/* Deassert the physical reset line */
	platform_nrst_set_val(false);
	while (true) {
		/* Read back DHCSR and check if the reset status bit is still set */
		dhcsr = adiv5_mem_read32(ap, CORTEXM_DHCSR);
		if ((dhcsr & CORTEXM_DHCSR_S_RESET_ST) == 0)
			break;
		/* If it is and we timeout, turn that into an error */
		if (platform_timeout_is_expired(&reset_timeout)) {
			DEBUG_ERROR("Error releasing from reset\n");
			return false;
		}
	}
	/* Core is now in a good state */
	return true;
}

static cid_class_e adiv5_class_from_cid(const uint16_t part_number, const uint16_t arch_id, const cid_class_e cid_class)
{
	/*
	 * Cortex-M23 and 33 incorrectly list their SCS's as a debug component,
	 * but they're a generic IP component, so we adjust the cid_class.
	 */
	if ((part_number == 0xd20U || part_number == 0xd21U) && arch_id == 0x2a04U && cid_class == cidc_dc)
		return cidc_gipc;
	return cid_class;
}

/*
 * Return true if we find a debuggable device.
 * NOLINTNEXTLINE(misc-no-recursion)
 */
static void adiv5_component_probe(
	adiv5_access_port_s *ap, uint32_t addr, const size_t recursion, const uint32_t num_entry)
{
	(void)num_entry;

	addr &= 0xfffff000U; /* Mask out base address */
	if (addr == 0)       /* No rom table on this AP */
		return;

	const volatile uint32_t cidr = adiv5_ap_read_id(ap, addr + CIDR0_OFFSET);
	if (ap->dp->fault) {
		DEBUG_ERROR("Error reading CIDR on AP%u: %u\n", ap->apsel, ap->dp->fault);
		return;
	}

#if ENABLE_DEBUG == 1
	char *indent = alloca(recursion + 1U);

	for (size_t i = 0; i < recursion; i++)
		indent[i] = ' ';
	indent[recursion] = 0;
#endif

	if (adiv5_dp_error(ap->dp)) {
		DEBUG_ERROR("%sFault reading ID registers\n", indent);
		return;
	}

	/* CIDR preamble sanity check */
	if ((cidr & ~CID_CLASS_MASK) != CID_PREAMBLE) {
		DEBUG_WARN("%s%" PRIu32 " 0x%08" PRIx32 ": 0x%08" PRIx32 " <- does not match preamble (0x%08" PRIx32 ")\n",
			indent, num_entry, addr, cidr, CID_PREAMBLE);
		return;
	}

	/* Extract Component ID class nibble */
	const uint32_t cid_class = (cidr & CID_CLASS_MASK) >> CID_CLASS_SHIFT;
	const uint64_t pidr = adiv5_ap_read_pidr(ap, addr);

	uint16_t designer_code;
	if (pidr & PIDR_JEP106_USED) {
		/* (OFFSET - 8) because we want it on bits 11:8 of new code, see "JEP-106 code list" */
		designer_code = (pidr & PIDR_JEP106_CONT_MASK) >> (PIDR_JEP106_CONT_OFFSET - 8U) |
			(pidr & PIDR_JEP106_CODE_MASK) >> PIDR_JEP106_CODE_OFFSET;

		if (designer_code == JEP106_MANUFACTURER_ERRATA_STM32WX || designer_code == JEP106_MANUFACTURER_ERRATA_CS) {
			/**
			 * see 'JEP-106 code list' for context, here we are aliasing codes that are non compliant with the
			 * JEP-106 standard to their expected codes, this is later used to determine the correct probe function.
			 */
			DEBUG_WARN("Patching Designer code 0x%03" PRIx16 " -> 0x%03u\n", designer_code, JEP106_MANUFACTURER_STM);
			designer_code = JEP106_MANUFACTURER_STM;
		}
	} else {
		/* legacy ascii code */
		designer_code = (pidr & PIDR_JEP106_CODE_MASK) >> PIDR_JEP106_CODE_OFFSET | ASCII_CODE_FLAG;
	}

	/* Extract part number from the part id register. */
	const uint16_t part_number = pidr & PIDR_PN_MASK;

	/* ROM table */
	if (cid_class == cidc_romtab) {
		if (recursion == 0) {
			ap->designer_code = designer_code;
			ap->partno = part_number;

			if (ap->designer_code == JEP106_MANUFACTURER_ATMEL && ap->partno == 0xcd0U) {
				uint32_t ctrlstat = adiv5_mem_read32(ap, SAMX5X_DSU_CTRLSTAT);
				if (ctrlstat & SAMX5X_STATUSB_PROT) {
					/* A protected SAMx5x device is found.
					 * Handle it here, as access only to limited memory region
					 * is allowed
					 */
					cortexm_probe(ap);
					return;
				}
			}
		}

#ifndef DEBUG_WARN_IS_NOOP
		/* Check SYSMEM bit */
		const uint32_t memtype = adiv5_mem_read32(ap, addr | ADIV5_ROM_MEMTYPE) & ADIV5_ROM_MEMTYPE_SYSMEM;
		if (adiv5_dp_error(ap->dp))
			DEBUG_ERROR("Fault reading ROM table entry\n");
#endif
		DEBUG_INFO("ROM: Table BASE=0x%" PRIx32 " SYSMEM=0x%08" PRIx32 ", Manufacturer %03x Partno %03x\n", addr,
			memtype, designer_code, part_number);

		for (uint32_t i = 0; i < 960U; i++) {
			adiv5_dp_error(ap->dp);

			uint32_t entry = adiv5_mem_read32(ap, addr + i * 4U);
			if (adiv5_dp_error(ap->dp)) {
				DEBUG_ERROR("%sFault reading ROM table entry %" PRIu32 "\n", indent, i);
				break;
			}

			if (entry == 0)
				break;

			if (!(entry & ADIV5_ROM_ROMENTRY_PRESENT)) {
				DEBUG_INFO("%s%" PRIu32 " Entry 0x%" PRIx32 " -> Not present\n", indent, i, entry);
				continue;
			}

			/* Probe recursively */
			adiv5_component_probe(ap, addr + (entry & ADIV5_ROM_ROMENTRY_OFFSET), recursion + 1U, i);
		}
		DEBUG_INFO("%sROM: Table END\n", indent);

	} else {
		if (designer_code != JEP106_MANUFACTURER_ARM && designer_code != JEP106_MANUFACTURER_ARM_CHINA) {
			/* non arm components not supported currently */
			DEBUG_WARN("%s%" PRIu32 " 0x%" PRIx32 ": 0x%08" PRIx32 "%08" PRIx32 " Non ARM component ignored\n",
				indent + 1, num_entry, addr, (uint32_t)(pidr >> 32U), (uint32_t)pidr);
			DEBUG_TARGET("%s -> designer: %x, part no: %x\n", indent, designer_code, part_number);
			return;
		}

		/* ADIv5: For CoreSight components, read DEVTYPE and ARCHID */
		uint16_t arch_id = 0;
		uint8_t dev_type = 0;
		if (cid_class == cidc_dc) {
			dev_type = adiv5_mem_read32(ap, addr + DEVTYPE_OFFSET) & DEVTYPE_MASK;

			uint32_t devarch = adiv5_mem_read32(ap, addr + DEVARCH_OFFSET);

			if (devarch & DEVARCH_PRESENT)
				arch_id = devarch & DEVARCH_ARCHID_MASK;
		}

		/* Find the part number in our part list and run the appropriate probe routine if applicable. */
		size_t i;
		for (i = 0; arm_component_lut[i].arch != aa_end; i++) {
			if (arm_component_lut[i].part_number != part_number || arm_component_lut[i].dev_type != dev_type ||
				arm_component_lut[i].arch_id != arch_id)
				continue;

			DEBUG_INFO("%s%" PRIu32 " 0x%" PRIx32 ": %s - %s %s (PIDR = 0x%08" PRIx32 "%08" PRIx32 " DEVTYPE = 0x%02x "
					   "ARCHID = 0x%04x)\n",
				indent + 1, num_entry, addr, adiv5_cid_class_string(cid_class), arm_component_lut[i].type,
				arm_component_lut[i].full, (uint32_t)(pidr >> 32U), (uint32_t)pidr, dev_type, arch_id);

			const cid_class_e adjusted_class = adiv5_class_from_cid(part_number, arch_id, cid_class);
			/* Perform sanity check, if we know what to expect as * component ID class. */
			if (arm_component_lut[i].cidc != cidc_unknown && adjusted_class != arm_component_lut[i].cidc)
				DEBUG_WARN("%s\"%s\" expected, got \"%s\"\n", indent + 1,
					adiv5_cid_class_string(arm_component_lut[i].cidc), adiv5_cid_class_string(adjusted_class));

			switch (arm_component_lut[i].arch) {
			case aa_cortexm:
				DEBUG_INFO("%s-> cortexm_probe\n", indent + 1);
				cortexm_probe(ap);
				break;
			case aa_cortexa:
				DEBUG_INFO("%s-> cortexa_probe\n", indent + 1);
				cortexa_probe(ap, addr);
				break;
			case aa_cortexr:
				DEBUG_INFO("%s-> cortexr_probe\n", indent + 1);
				cortexr_probe(ap, addr);
				break;
			default:
				break;
			}
			break;
		}
		if (arm_component_lut[i].arch == aa_end) {
			DEBUG_WARN("%s%" PRIu32 " 0x%" PRIx32 ": %s - Unknown (PIDR = 0x%08" PRIx32 "%08" PRIx32 " DEVTYPE = "
					   "0x%02x ARCHID = 0x%04x)\n",
				indent, num_entry, addr, adiv5_cid_class_string(cid_class), (uint32_t)(pidr >> 32U), (uint32_t)pidr,
				dev_type, arch_id);
		}
	}
}

adiv5_access_port_s *adiv5_new_ap(adiv5_debug_port_s *dp, uint8_t apsel)
{
	adiv5_access_port_s tmpap = {0};
	/* Assume valid and try to read IDR */
	tmpap.dp = dp;
	tmpap.apsel = apsel;
	tmpap.idr = adiv5_ap_read(&tmpap, ADIV5_AP_IDR);
	tmpap.base = adiv5_ap_read(&tmpap, ADIV5_AP_BASE);
	/*
	 * Check the Debug Base Address register. See ADIv5
	 * Specification C2.6.1
	 */
	if (tmpap.base == 0xffffffffU) {
		/*
		 * Debug Base Address not present in this MEM-AP
		 * No debug entries... useless AP
		 * AP0 on STM32MP157C reads 0x00000002
		 */
		return NULL;
	}

	if (!tmpap.idr) /* IDR Invalid */
		return NULL;
	tmpap.csw = adiv5_ap_read(&tmpap, ADIV5_AP_CSW);
	// XXX: We might be able to use the type field in ap->idr to determine if the AP supports TrustZone
	tmpap.csw &= ~(ADIV5_AP_CSW_SIZE_MASK | ADIV5_AP_CSW_ADDRINC_MASK | ADIV5_AP_CSW_MTE | ADIV5_AP_CSW_HNOSEC);
	tmpap.csw |= ADIV5_AP_CSW_DBGSWENABLE;

	if (tmpap.csw & ADIV5_AP_CSW_TRINPROG) {
		DEBUG_ERROR("AP %3u: Transaction in progress. AP is not usable!\n", apsel);
		return NULL;
	}

	/* It's valid to so create a heap copy */
	adiv5_access_port_s *ap = malloc(sizeof(*ap));
	if (!ap) { /* malloc failed: heap exhaustion */
		DEBUG_ERROR("malloc: failed in %s\n", __func__);
		return NULL;
	}

	memcpy(ap, &tmpap, sizeof(*ap));

#if ENABLE_DEBUG == 1
	/* Grab the config register to get a complete set */
	uint32_t cfg = adiv5_ap_read(ap, ADIV5_AP_CFG);
	DEBUG_INFO("AP %3u: IDR=%08" PRIx32 " CFG=%08" PRIx32 " BASE=%08" PRIx32 " CSW=%08" PRIx32, apsel, ap->idr, cfg,
		ap->base, ap->csw);
	/* Decode the AP designer code */
	uint16_t designer = ADIV5_AP_IDR_DESIGNER(ap->idr);
	designer = (designer & ADIV5_DP_DESIGNER_JEP106_CONT_MASK) << 1U | (designer & ADIV5_DP_DESIGNER_JEP106_CODE_MASK);
	/* If this is an ARM-designed AP, map the AP type. Otherwise display "Unknown" */
	const char *const ap_type = designer == JEP106_MANUFACTURER_ARM ?
		adiv5_arm_ap_type_string(ADIV5_AP_IDR_TYPE(ap->idr), ADIV5_AP_IDR_CLASS(ap->idr)) :
		"Unknown";
	/* Display the AP's type, variant and revision information */
	DEBUG_INFO(" (%s var%" PRIx32 " rev%" PRIx32 ")\n", ap_type, ADIV5_AP_IDR_VARIANT(ap->idr),
		ADIV5_AP_IDR_REVISION(ap->idr));
#endif
	adiv5_ap_ref(ap);
	return ap;
}

/* No real AP on RP2040. Special setup.*/
static void rp_rescue_setup(adiv5_debug_port_s *dp)
{
	adiv5_access_port_s *ap = calloc(1, sizeof(*ap));
	if (!ap) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}
	ap->dp = dp;

	rp_rescue_probe(ap);
}

static void adiv5_dp_clear_sticky_errors(adiv5_debug_port_s *dp)
{
	/*
	 * For DPv1+ APs, this is done by writing through the ABORT register.
	 * For DPv0 APs, this must be done by writing a 1 back to the appropriate
	 * CTRL/STATUS register bit
	 */
	if (dp->version)
		adiv5_dp_abort(dp, ADIV5_DP_ABORT_STKERRCLR);
	else
		/* For JTAG-DPs (which all DPv0 DPs are), use the adiv5_jtagdp_error routine */
		adiv5_dp_error(dp);
}

/* Keep the TRY_CATCH funkiness contained to avoid clobbering and reduce the need for volatiles */
uint32_t adiv5_dp_read_dpidr(adiv5_debug_port_s *const dp)
{
	volatile uint32_t dpidr = 0;
	volatile exception_s e;
	TRY_CATCH (e, EXCEPTION_ALL) {
		dpidr = adiv5_dp_low_access(dp, ADIV5_LOW_READ, ADIV5_DP_DPIDR, 0U);
	}
	if (e.type)
		return 0;
	return dpidr;
}

#define S32K344_TARGET_PARTNO        0x995cU
#define S32K3xx_APB_AP               1U
#define S32K3xx_AHB_AP               4U
#define S32K3xx_MDM_AP               6U
#define S32K3xx_SDA_AP               7U
#define S32K3xx_SDA_AP_DBGENCTR      ADIV5_AP_REG(0x80U)
#define S32K3xx_SDA_AP_DBGENCTR_MASK 0x300000f0U

static bool s32k3xx_dp_prepare(adiv5_debug_port_s *const dp)
{
	/* Is this an S32K344? */
	if (dp->target_partno != S32K344_TARGET_PARTNO)
		return false;

	adiv5_dp_abort(dp, ADIV5_DP_ABORT_DAPABORT);

	/* SDA_AP has various flags we must enable before we can have debug access, so
	 * start with it and enable them */
	adiv5_access_port_s *sda_ap = adiv5_new_ap(dp, S32K3xx_SDA_AP);
	if (!sda_ap)
		return false;
	adiv5_ap_write(sda_ap, S32K3xx_SDA_AP_DBGENCTR, S32K3xx_SDA_AP_DBGENCTR_MASK);
	adiv5_ap_unref(sda_ap);

	/* If we try to access an invalid AP the S32K3 will hard fault, so we must
	 * statically enumerate the APs we expect */
	adiv5_access_port_s *apb_ap = adiv5_new_ap(dp, S32K3xx_APB_AP);
	if (!apb_ap)
		return false;
	adiv5_component_probe(apb_ap, apb_ap->base, 0, 0);
	adiv5_ap_unref(apb_ap);

	adiv5_access_port_s *ahb_ap = adiv5_new_ap(dp, S32K3xx_AHB_AP);
	if (!ahb_ap)
		return false;
	adiv5_component_probe(ahb_ap, ahb_ap->base, 0, 0);

	cortexm_prepare(ahb_ap);
	for (target_s *target = target_list; target; target = target->next) {
		if (!connect_assert_nrst && target->priv_free == cortex_priv_free) {
			adiv5_access_port_s *target_ap = cortex_ap(target);
			if (target_ap == ahb_ap)
				target_halt_resume(target, false);
		}
	}

	adiv5_ap_unref(ahb_ap);

	adiv5_access_port_s *mdm_ap = adiv5_new_ap(dp, S32K3xx_MDM_AP);
	if (!mdm_ap)
		return false;
	adiv5_component_probe(mdm_ap, mdm_ap->base, 0, 0);
	adiv5_ap_unref(mdm_ap);

	return true;
}

void adiv5_dp_init(adiv5_debug_port_s *const dp)
{
	/*
	 * We have to initialise the DP routines up front before any adiv5_* functions are called or
	 * bad things happen under BMDA (particularly CMSIS-DAP)
	 */
	dp->ap_write = firmware_ap_write;
	dp->ap_read = firmware_ap_read;
	dp->mem_read = advi5_mem_read_bytes;
	dp->mem_write = adiv5_mem_write_bytes;
#if PC_HOSTED == 1
	bmda_adiv5_dp_init(dp);
#endif

	/*
	 * Start by assuming DP v1 or later.
	 * this may not be true for JTAG-DP (we attempt to detect this with the part ID code)
	 * in such cases (DPv0) DPIDR is not implemented and reads are UNPREDICTABLE.
	 *
	 * for SWD-DP, we are guaranteed to be DP v1 or later.
	 */
	if (dp->designer_code != JEP106_MANUFACTURER_ARM || dp->partno != JTAG_IDCODE_PARTNO_DPv0) {
		const uint32_t dpidr = adiv5_dp_read_dpidr(dp);
		if (!dpidr) {
			DEBUG_ERROR("Failed to read DPIDR\n");
			free(dp);
			return;
		}

		dp->version = (dpidr & ADIV5_DP_DPIDR_VERSION_MASK) >> ADIV5_DP_DPIDR_VERSION_OFFSET;

		/*
		 * The code in the DPIDR is in the form
		 * Bits 10:7 - JEP-106 Continuation code
		 * Bits 6:0 - JEP-106 Identity code
		 * here we convert it to our internal representation, See JEP-106 code list
		 *
		 * note: this is the code of the designer not the implementer, we expect it to be ARM
		 */
		const uint16_t designer = (dpidr & ADIV5_DP_DPIDR_DESIGNER_MASK) >> ADIV5_DP_DPIDR_DESIGNER_OFFSET;
		dp->designer_code =
			(designer & ADIV5_DP_DESIGNER_JEP106_CONT_MASK) << 1U | (designer & ADIV5_DP_DESIGNER_JEP106_CODE_MASK);
		dp->partno = (dpidr & ADIV5_DP_DPIDR_PARTNO_MASK) >> ADIV5_DP_DPIDR_PARTNO_OFFSET;

		/* Minimal Debug Port (MINDP) functions implemented */
		dp->quirks = (dpidr >> ADIV5_DP_DPIDR_MINDP_OFFSET) & ADIV5_DP_QUIRK_MINDP;

		/*
		 * Check DPIDR validity
		 * Designer code 0 is not a valid JEP-106 code
		 * Version 0 is reserved for DPv0 which does not implement DPIDR
		 * Bit 0 of DPIDR is read as 1
		 */
		if (dp->designer_code != 0U && dp->version > 0U && (dpidr & 1U)) {
			DEBUG_INFO("DP DPIDR 0x%08" PRIx32 " (v%x %srev%" PRIu32 ") designer 0x%x partno 0x%x\n", dpidr,
				dp->version, (dp->quirks & ADIV5_DP_QUIRK_MINDP) ? "MINDP " : "",
				(dpidr & ADIV5_DP_DPIDR_REVISION_MASK) >> ADIV5_DP_DPIDR_REVISION_OFFSET, dp->designer_code,
				dp->partno);
		} else {
			DEBUG_WARN("Invalid DPIDR %08" PRIx32 " assuming DPv0\n", dpidr);
			dp->version = 0U;
			dp->designer_code = 0U;
			dp->partno = 0U;
			dp->quirks = 0U;
		}
	} else if (dp->version == 0)
		/* DP v0 */
		DEBUG_WARN("DPv0 detected based on JTAG IDCode\n");

	/*
	 * Ensure that whatever previous accesses happened to this DP before we
	 * scanned the chain and found it, the sticky error bit is cleared
	 */
	adiv5_dp_clear_sticky_errors(dp);

	if (dp->version >= 2) {
		/* TARGETID is on bank 2 */
		adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK2);
		const uint32_t targetid = adiv5_dp_read(dp, ADIV5_DP_TARGETID);
		adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK0);

		/* Use TARGETID register to identify target */
		const uint16_t tdesigner = (targetid & ADIV5_DP_TARGETID_TDESIGNER_MASK) >> ADIV5_DP_TARGETID_TDESIGNER_OFFSET;

		/* convert it to our internal representation, See JEP-106 code list */
		dp->target_designer_code =
			(tdesigner & ADIV5_DP_DESIGNER_JEP106_CONT_MASK) << 1U | (tdesigner & ADIV5_DP_DESIGNER_JEP106_CODE_MASK);

		dp->target_partno = (targetid & ADIV5_DP_TARGETID_TPARTNO_MASK) >> ADIV5_DP_TARGETID_TPARTNO_OFFSET;

		dp->target_revision = (targetid & ADIV5_DP_TARGETID_TREVISION_MASK) >> ADIV5_DP_TARGETID_TREVISION_OFFSET;

		DEBUG_INFO("TARGETID 0x%08" PRIx32 " designer 0x%x partno 0x%x\n", targetid, dp->target_designer_code,
			dp->target_partno);

		dp->targetsel = dp->instance << ADIV5_DP_TARGETSEL_TINSTANCE_OFFSET |
			(targetid & (ADIV5_DP_TARGETID_TDESIGNER_MASK | ADIV5_DP_TARGETID_TPARTNO_MASK)) | 1U;
	}

	if (dp->designer_code == JEP106_MANUFACTURER_RASPBERRY && dp->partno == 0x2U) {
		rp_rescue_setup(dp);
		return;
	}

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);

	/* Start by resetting the DP control state so the debug domain powers down */
	adiv5_dp_write(dp, ADIV5_DP_CTRLSTAT, 0U);
	uint32_t status = ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK;
	/* Wait for the acknowledgements to go low */
	while (status & (ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK)) {
		status = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT);
		if (platform_timeout_is_expired(&timeout)) {
			DEBUG_WARN("adiv5: power-down failed\n");
			break;
		}
	}

	platform_timeout_set(&timeout, 201);
	/* Write request for system and debug power up */
	adiv5_dp_write(dp, ADIV5_DP_CTRLSTAT, ADIV5_DP_CTRLSTAT_CSYSPWRUPREQ | ADIV5_DP_CTRLSTAT_CDBGPWRUPREQ);
	/* Wait for acknowledge */
	status = 0U;
	while (status != (ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK)) {
		platform_delay(10);
		status =
			adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT) & (ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK);
		if (status == (ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK))
			break;
		if (platform_timeout_is_expired(&timeout)) {
			DEBUG_WARN("adiv5: power-up failed\n");
			free(dp); /* No AP that referenced this DP so long*/
			return;
		}
	}
	/* At this point due to the guaranteed power domain restart, the APs are all up and in their reset state. */

	if (dp->target_designer_code == JEP106_MANUFACTURER_NXP)
		lpc55_dp_prepare(dp);

	if (dp->target_designer_code == JEP106_MANUFACTURER_NORDIC && dp->target_partno == 0x90U) {
		if (!nrf91_dp_prepare(dp)) {
			/* device is in secure state, only show rescue target */
			return;
		}
	}

	/* Probe for APs on this DP */
	size_t invalid_aps = 0;
	dp->refcnt++;

	if (dp->target_designer_code == JEP106_MANUFACTURER_FREESCALE) {
		/* S32K3XX will requires special handling, do so and skip the AP enumeration */
		if (s32k3xx_dp_prepare(dp)) {
			adiv5_dp_unref(dp);
			return;
		}
	}

	for (size_t i = 0; i < 256U && invalid_aps < 8U; ++i) {
		adiv5_access_port_s *ap = adiv5_new_ap(dp, i);
		if (ap == NULL) {
			/* Clear sticky errors in case scanning for this AP triggered any */
			adiv5_dp_clear_sticky_errors(dp);
			/*
			 * We have probably found all APs on this DP so no need to keep looking.
			 * Continue with rest of init function down below.
			 */
			if (++invalid_aps == 8U)
				break;

			continue;
		}

		kinetis_mdm_probe(ap);
		nrf51_mdm_probe(ap);
		efm32_aap_probe(ap);
		lpc55_dmap_probe(ap);

		/* Try to prepare the AP if it seems to be a AHB (memory) AP */
		if (!ap->apsel && ADIV5_AP_IDR_CLASS(ap->idr) == 8U && ADIV5_AP_IDR_TYPE(ap->idr) == ARM_AP_TYPE_AHB3) {
			if (!cortexm_prepare(ap))
				DEBUG_WARN("adiv5: Failed to prepare AP, results may be unpredictable\n");
		}

		/* The rest should only be added after checking ROM table */
		adiv5_component_probe(ap, ap->base, 0, 0);
		/*
		 * Having completed discovery on this AP, if we're not in connect-under-reset mode,
		 * and now that we're done with this AP's ROM tables, look for the target and resume the core.
		 */
		for (target_s *target = target_list; target; target = target->next) {
			if (!connect_assert_nrst && target->priv_free == cortex_priv_free) {
				adiv5_access_port_s *target_ap = cortex_ap(target);
				if (target_ap == ap)
					target_halt_resume(target, false);
			}
		}

		/*
		 * Due to the Tiva TM4C1294KCDT (among others) repeating the single AP ad-nauseum,
		 * this check is needed so that we bail rather than repeating the same AP ~256 times.
		 */
		if (ap->dp->quirks & ADIV5_DP_QUIRK_DUPED_AP) {
			adiv5_ap_unref(ap);
			adiv5_dp_unref(dp);
			return;
		}

		adiv5_ap_unref(ap);
	}
	adiv5_dp_unref(dp);
}

/* Program the CSW and TAR for sequential access at a given width */
void ap_mem_access_setup(adiv5_access_port_s *ap, uint32_t addr, align_e align)
{
	uint32_t csw = ap->csw | ADIV5_AP_CSW_ADDRINC_SINGLE;

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
	/* Select AP bank 0 and write CSW */
	adiv5_ap_write(ap, ADIV5_AP_CSW, csw);
	/* Then write TAR which is in the same AP bank */
	adiv5_dp_write(ap->dp, ADIV5_AP_TAR, addr);
}

/* Unpack data from the source uint32_t value based on data alignment and source address */
void *adiv5_unpack_data(void *const dest, const uint32_t src, const uint32_t data, const align_e align)
{
	switch (align) {
	case ALIGN_8BIT: {
		/*
		 * Mask off the bottom 2 bits of the address to figure out which byte of data to use
		 * then multiply that by 8 and shift the data down by the result to pick one of the 4 possible bytes
		 */
		uint8_t value = (data >> (8U * (src & 3U))) & 0xffU;
		/* Then memcpy() the result to the destination buffer (this avoids doing a possibly UB cast) */
		memcpy(dest, &value, sizeof(value));
		break;
	}
	case ALIGN_16BIT: {
		/*
		 * Mask off the 2nd bit of the address to figure out which 16 bits of data to use
		 * then multiply that by 8 and shift the data down by the result to pick one of the 2 possible 16-bit blocks
		 */
		uint16_t value = (data >> (8U * (src & 2U))) & 0xffffU;
		/* Then memcpy() the result to the destination buffer (this avoids unaligned write issues) */
		memcpy(dest, &value, sizeof(value));
		break;
	}
	case ALIGN_64BIT:
	case ALIGN_32BIT:
		/*
		 * When using 32- or 64-bit alignment, we don't have to do anything special, just memcpy() the data to the
		 * destination buffer (this avoids issues with unaligned writes and UB casts)
		 */
		memcpy(dest, &data, sizeof(data));
		break;
	}
	return (uint8_t *)dest + (1 << align);
}

/* Pack data from the source value into a uint32_t based on data alignment and source address */
const void *adiv5_pack_data(const uint32_t dest, const void *const src, uint32_t *const data, const align_e align)
{
	switch (align) {
	case ALIGN_8BIT: {
		uint8_t value;
		/* Copy the data to pack in from the source buffer */
		memcpy(&value, src, sizeof(value));
		/* Then shift it up to the appropriate byte in data based on the bottom 2 bits of the destination address */
		*data = (uint32_t)value << (8U * (dest & 3U));
		break;
	}
	case ALIGN_16BIT: {
		uint16_t value;
		/* Copy the data to pack in from the source buffer (avoids unaligned read issues) */
		memcpy(&value, src, sizeof(value));
		/* Then shift it up to the appropriate 16-bit block in data based on the 2nd bit of the destination address */
		*data = (uint32_t)value << (8U * (dest & 2U));
		break;
	}
	default:
		/*
		 * 32- and 64-bit aligned reads don't need to do anything special beyond using memcpy()
		 * to avoid doing  an unaligned read of src, or any UB casts.
		 */
		memcpy(data, src, sizeof(*data));
		break;
	}
	return (const uint8_t *)src + (1 << align);
}

void advi5_mem_read_bytes(adiv5_access_port_s *const ap, void *dest, uint32_t src, size_t len)
{
	uint32_t osrc = src;
	const align_e align = MIN_ALIGN(src, len);

	if (len == 0)
		return;

	len >>= align;
	ap_mem_access_setup(ap, src, align);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
	while (--len) {
		const uint32_t value = adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
		dest = adiv5_unpack_data(dest, src, value, align);

		src += 1U << align;
		/* Check for 10 bit address overflow */
		if ((src ^ osrc) & 0xfffffc00U) {
			osrc = src;
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_TAR, src);
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
		}
	}
	const uint32_t value = adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_DP_RDBUFF, 0);
	adiv5_unpack_data(dest, src, value, align);
}

void adiv5_mem_write_bytes(adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align)
{
	uint32_t odest = dest;

	len >>= align;
	ap_mem_access_setup(ap, dest, align);
	while (len--) {
		uint32_t value = 0;
		src = adiv5_pack_data(dest, src, &value, align);
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DRW, value);

		dest += 1U << align;
		/* Check for 10 bit address overflow */
		if ((dest ^ odest) & 0xfffffc00U) {
			odest = dest;
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_TAR, dest);
		}
	}
	/* Make sure this write is complete by doing a dummy read */
	adiv5_dp_read(ap->dp, ADIV5_DP_RDBUFF);
}

void firmware_ap_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value)
{
	adiv5_dp_recoverable_access(
		ap->dp, ADIV5_LOW_WRITE, ADIV5_DP_SELECT, ((uint32_t)ap->apsel << 24U) | (addr & 0xf0U));
	adiv5_dp_write(ap->dp, addr, value);
}

uint32_t firmware_ap_read(adiv5_access_port_s *ap, uint16_t addr)
{
	uint32_t ret;
	adiv5_dp_recoverable_access(
		ap->dp, ADIV5_LOW_WRITE, ADIV5_DP_SELECT, ((uint32_t)ap->apsel << 24U) | (addr & 0xf0U));
	ret = adiv5_dp_read(ap->dp, addr);
	return ret;
}

void adiv5_mem_write(adiv5_access_port_s *const ap, const uint32_t dest, const void *const src, const size_t len)
{
	const align_e align = MIN_ALIGN(dest, len);
	adiv5_mem_write_sized(ap, dest, src, len, align);
}
