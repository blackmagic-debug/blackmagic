/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2018-2021 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

/*
 * This file implements version-independant ADI functions.
 *
 * See the following ARM Reference Documents:
 * ARM Debug Interface v5 Architecture Specification, IHI0031 ver. g
 * - https://developer.arm.com/documentation/ihi0031/latest/
 * ARM Debug Interface v6 Architecture Specification, IHI0074 ver. e
 * - https://developer.arm.com/documentation/ihi0074/latest/
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "jep106.h"
#include "adi.h"
#include "adiv5.h"
#include "adiv6.h"
#include "cortex.h"
#include "cortex_internal.h"

/* Used to probe for a protected SAMX5X device */
#define SAMX5X_DSU_CTRLSTAT 0x41002100U
#define SAMX5X_STATUSB_PROT (1U << 16U)

#define ID_SAMx5x 0xcd0U

#if ENABLE_DEBUG == 1
#define ARM_COMPONENT_STR(...) __VA_ARGS__
#else
#define ARM_COMPONENT_STR(...)
#endif

/*
 * The product ID register consists of several parts. For a full description
 * refer to the ADIv5 and ADIv6 specifications.
 * The PIDR is 64-bit and has the following interpratiation:
 * |7    reg 7    0|7    reg 6    0|7    reg 5    0|7    reg 4    0|
 * |0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0| | | | | | | | |
 * |63           56|55           48|47           40|39   36|35   32|
 * \_______________________ ______________________/\___ __/\___ ___/
 *                         V                           V       V
 *                    Reserved, RAZ                   4KB      |
 *                                                   count     |
 *                                                          JEP-106
 *                                                     Continuation Code (only valid for JEP-106 codes)
 *
 * |7    reg 3    0|7    reg 2    0|7    reg 1    0|7    reg 0    0|
 * | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | | |
 * |31   28|27   24|23   20|||18   |     12|11     |              0|
 * \___ __/\__ ___/\___ __/ |\______ _____/\___________ ___________/
 *     V      V        V    |       V                  V
 *  RevAnd    |    Revision |  JEP-106 ID         Part number
 *            |             |  (no parity)
 *        Customer          19
 *        modified          `- JEP-106 code is used
 *
 * Only a subset of part numbers are listed. These all have ARM as the designer code.
 *
 * To properly identify CoreSight components, two additional fields - DEVTYPE and ARCHID - are read.
 *
 * Additional reference on the DEVTYPE and DEVARCH registers can be found in the
 * ARM CoreSight Architecture Specification v3.0, §B2.3.4 and §B2.3.8.
 */
static const arm_coresight_component_s arm_component_lut[] = {
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
	{0x471, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M0 ROM", "(Cortex-M0 ROM)")},
	{0x490, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A15 GIC", "(Generic Interrupt Controller)")},
	{0x4c0, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M0+ ROM", "(Cortex-M0+ ROM)")},
	{0x4c3, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M3 ROM", "(Cortex-M3 ROM)")},
	{0x4c4, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M4 ROM", "(Cortex-M4 ROM)")},
	{0x4c7, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M7 PPB", "(Cortex-M7 PPB ROM Table)")},
	{0x4c8, 0x00, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M7 ROM", "(Cortex-M7 ROM)")},
	{0x000, 0x00, 0x0af7, aa_rom_table, cidc_dc, ARM_COMPONENT_STR("CoreSight ROM", "(ROM Table)")},
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
	{0x921, 0x13, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A8 ETM", "(Embedded Trace)")},
	{0x922, 0x14, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A8 CTI", "(Cross Trigger)")},
	{0x923, 0x11, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M3 TPIU", "(Trace Port Interface Unit)")},
	{0x924, 0x13, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M3 ETM", "(Embedded Trace)")},
	{0x925, 0x13, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-M4 ETM", "(Embedded Trace)")},
	{0x930, 0x13, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-R4 ETM", "(Embedded Trace)")},
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
	{0x9a5, 0x13, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A5 ETM", "(Embedded Trace)")},
	{0x9a7, 0x16, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A7 PMU", "(Performance Monitor Unit)")},
	{0x9af, 0x16, 0, aa_nosupport, cidc_unknown, ARM_COMPONENT_STR("Cortex-A15 PMU", "(Performance Monitor Unit)")},
	{0xc05, 0x15, 0, aa_cortexa, cidc_dc, ARM_COMPONENT_STR("Cortex-A5", "(Debug Unit)")},
	{0xc07, 0x15, 0, aa_cortexa, cidc_dc, ARM_COMPONENT_STR("Cortex-A7", "(Debug Unit)")},
	{0xc08, 0x15, 0, aa_cortexa, cidc_dc, ARM_COMPONENT_STR("Cortex-A8", "(Debug Unit)")},
	{0xc09, 0x15, 0, aa_cortexa, cidc_dc, ARM_COMPONENT_STR("Cortex-A9", "(Debug Unit)")},
	{0xc0f, 0x15, 0, aa_cortexa, cidc_unknown, ARM_COMPONENT_STR("Cortex-A15", "(Debug Unit)")},
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
	{0xd22, 0x00, 0x2a04, aa_cortexm, cidc_dc, ARM_COMPONENT_STR("Cortex-M55", "(Ssystem Control Space)")},
	{0xd22, 0x00, 0x1a02, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M55", "(Data Watchpoint and Trace)")},
	{0xd22, 0x00, 0x1a03, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M55", "(Breakpoint Unit)")},
	{0xd22, 0x43, 0x1a01, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M55", "(Instrumentation Trace Macrocell)")},
	{0xd22, 0x13, 0x4a13, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M55", "(Embedded Trace)")},
	{0xd22, 0x16, 0x0a06, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M55", "(Performance Monitoring Unit)")},
	{0xd22, 0x14, 0x1a14, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("Cortex-M55", "(Cross Trigger)")},
	{0x132, 0x31, 0x0a31, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 MTB", "(Execution Trace)")},
	{0x132, 0x43, 0x1a01, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 ITM", "(Instrumentation Trace Module)")},
	{0x132, 0x00, 0x1a02, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 DWT", "(Data Watchpoint and Trace)")},
	{0x132, 0x00, 0x1a03, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 BPU", "(Breakpoint Unit)")},
	{0x132, 0x14, 0x1a14, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 CTI", "(Cross Trigger)")},
	{0x132, 0x00, 0x2a04, aa_cortexm, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 SCS", "(System Control Space)")},
	{0x132, 0x13, 0x4a13, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 ETM", "(Embedded Trace)")},
	{0x132, 0x11, 0, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("STAR-MC1 TPIU", "(Trace Port Interface Unit)")},
	{0x9a3, 0x13, 0, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("nRF NTB", "(Nordic Trace Buffer)")},
	{0x9e2, 0x00, 0x0a17, aa_access_port, cidc_dc, ARM_COMPONENT_STR("ADIv6 MEM-APv2", "(Memory Access Port)")},
	{0x9e3, 0x00, 0x0a17, aa_access_port, cidc_dc, ARM_COMPONENT_STR("ADIv6 MEM-APv2", "(Memory Access Port)")},
	{0x193, 0x00, 0x0000, aa_nosupport, cidc_sys, ARM_COMPONENT_STR("CoreSight TSG", "(Timestamp Generator)")},
	{0x9e4, 0x00, 0x0a17, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("CoreSight MTE", "(Memory Tagging Extensioon)")},
	{0x9e7, 0x11, 0x0000, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("CoreSight TPIU", "(Trace Port Interface Unit)")},
	{0x9e8, 0x21, 0x0000, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("CoreSight TCM", "(Trace Memory Controller)")},
	{0x9eb, 0x12, 0x0000, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("CoreSight ATBF", "(ATB Funnel)")},
	{0x9ec, 0x22, 0x0000, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("CoreSight ATBR", "(ATB Replicator)")},
	{0x9ed, 0x14, 0x1a14, aa_nosupport, cidc_dc, ARM_COMPONENT_STR("CoreSight CTI", "(Cross Trigger Interface)")},
	{0x9ee, 0x00, 0x0000, aa_nosupport, cidc_dc,
		ARM_COMPONENT_STR("CoreSight CATU", "(CoreSight Address Translation Unit)")},
	{0xfff, 0x00, 0, aa_end, cidc_unknown, ARM_COMPONENT_STR("end", "end")},
};

#if ENABLE_DEBUG == 1
static const char *adi_arm_ap_type_string(const uint8_t ap_type, const uint8_t ap_class)
{
	/*
	 * Values taken from ADIv5 spec §C1.3 pg146.
	 * table C1-2 "AP Identification types for an AP designed by Arm"
	 */

	/* All types except 0 are only valid for ap_class == 0x8 */
	if (ap_class == ADIV5_AP_IDR_CLASS_MEM || ap_type == 0U) {
		switch (ap_type) {
		case 0U:
			/* Type 0 APs are determined by the class code */
			if (ap_class == ADIV5_AP_IDR_CLASS_JTAG)
				return "JTAG-AP";
			if (ap_class == ADIV5_AP_IDR_CLASS_COM)
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

static const char *adi_cid_class_string(const cid_class_e cid_class)
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

uint16_t adi_designer_from_pidr(const uint64_t pidr)
{
	uint16_t designer_code;
	if (pidr & PIDR_JEP106_USED) {
		/* (OFFSET - 8) because we want it on bits 11:8 of new code, see "JEP-106 code list" */
		designer_code = ((pidr & PIDR_JEP106_CONT_MASK) >> (PIDR_JEP106_CONT_OFFSET - 8U)) |
			((pidr & PIDR_JEP106_CODE_MASK) >> PIDR_JEP106_CODE_OFFSET);

	} else {
		/* legacy ascii code */
		designer_code = ((pidr & PIDR_JEP106_CODE_MASK) >> PIDR_JEP106_CODE_OFFSET) | ASCII_CODE_FLAG;
	}

	if (designer_code == JEP106_MANUFACTURER_ERRATA_STM32WX || designer_code == JEP106_MANUFACTURER_ERRATA_CS ||
		designer_code == JEP106_MANUFACTURER_ERRATA_CS_ASCII) {
		/**
         * see 'JEP-106 code list' for context, here we are aliasing codes that are non compliant with the
         * JEP-106 standard to their expected codes, this is later used to determine the correct probe function.
         */
		DEBUG_WARN("Patching Designer code %03x -> %03x\n", designer_code, JEP106_MANUFACTURER_STM);
		designer_code = JEP106_MANUFACTURER_STM;
	}
	return designer_code;
}

static cid_class_e adi_class_from_cid(const uint16_t part_number, const uint16_t arch_id, const cid_class_e cid_class)
{
	/*
	 * Cortex-M23 and 33 incorrectly list their SCS's as a debug component,
	 * but they're a generic IP component, so we adjust the cid_class.
	 */
	if ((part_number == 0xd20U || part_number == 0xd21U) && arch_id == 0x2a04U && cid_class == cidc_dc)
		return cidc_gipc;
	return cid_class;
}

const arm_coresight_component_s *adi_lookup_component(const target_addr64_t base_address, const uint32_t entry_number,
	const char *const indent, const uint8_t cid_class, const uint64_t pidr, const uint8_t dev_type,
	const uint16_t arch_id)
{
#if defined(DEBUG_WARN_IS_NOOP) && defined(DEBUG_ERROR_IS_NOOP)
	(void)indent;
	(void)base_address;
	(void)entry_number;
#endif

	const uint16_t part_number = arch_id == DEVARCH_ARCHID_ROMTABLE_V0 ? 0U : (pidr & PIDR_PN_MASK);
	for (size_t index = 0; arm_component_lut[index].arch != aa_end; ++index) {
		if (arm_component_lut[index].part_number != part_number || arm_component_lut[index].dev_type != dev_type ||
			arm_component_lut[index].arch_id != arch_id)
			continue;

		DEBUG_INFO("%s%" PRIu32 " 0x%0" PRIx32 "%08" PRIx32 ": %s - %s %s (PIDR = 0x%02" PRIx32 "%08" PRIx32 " DEVTYPE "
				   "= 0x%02x "
				   "ARCHID = 0x%04x)\n",
			indent + 1, entry_number, (uint32_t)(base_address >> 32U), (uint32_t)base_address,
			adi_cid_class_string(cid_class), arm_component_lut[index].type, arm_component_lut[index].full,
			(uint32_t)(pidr >> 32U), (uint32_t)pidr, dev_type, arch_id);

		const cid_class_e adjusted_class = adi_class_from_cid(part_number, arch_id, cid_class);
		/* Perform sanity check, if we know what to expect as * component ID class. */
		if (arm_component_lut[index].cidc != cidc_unknown && adjusted_class != arm_component_lut[index].cidc)
			DEBUG_WARN("%s\"%s\" expected, got \"%s\"\n", indent + 1,
				adi_cid_class_string(arm_component_lut[index].cidc), adi_cid_class_string(adjusted_class));
		return &arm_component_lut[index];
	}

	DEBUG_WARN("%s%" PRIu32 " 0x%0" PRIx32 "%08" PRIx32 ": %s - Unknown (PIDR = 0x%02" PRIx32 "%08" PRIx32 " DEVTYPE = "
			   "0x%02x ARCHID = 0x%04x)\n",
		indent, entry_number, (uint32_t)(base_address >> 32U), (uint32_t)base_address, adi_cid_class_string(cid_class),
		(uint32_t)(pidr >> 32U), (uint32_t)pidr, dev_type, arch_id);
	return NULL;
}

static void adi_display_ap(const adiv5_access_port_s *const ap)
{
#if ENABLE_DEBUG == 1
	const uint8_t ap_type = ADIV5_AP_IDR_TYPE(ap->idr);
	const uint8_t ap_class = ADIV5_AP_IDR_CLASS(ap->idr);
	const uint16_t designer = adi_decode_designer(ADIV5_AP_IDR_DESIGNER(ap->idr));
	/* If this is an ARM-designed AP, map the AP type. Otherwise display "Unknown" */
	const char *const ap_type_name =
		designer == JEP106_MANUFACTURER_ARM ? adi_arm_ap_type_string(ap_type, ap_class) : "Unknown";
	/* Display the AP's type, variant and revision information */
	DEBUG_INFO(" (%s var%" PRIx32 " rev%" PRIx32 ")\n", ap_type_name, ADIV5_AP_IDR_VARIANT(ap->idr),
		ADIV5_AP_IDR_REVISION(ap->idr));
#else
	(void)ap;
#endif
}

static bool adi_configure_mem_ap(adiv5_access_port_s *const ap)
{
	const uint8_t ap_type = ADIV5_AP_IDR_TYPE(ap->idr);

	/* Grab the config, base and CSW registers */
	const uint32_t cfg = adiv5_ap_read(ap, ADIV5_AP_CFG);
	ap->csw = adiv5_ap_read(ap, ADIV5_AP_CSW);
	/* This reads the lower half of BASE */
	ap->base = adiv5_ap_read(ap, ADIV5_AP_BASE_LOW);
	const uint8_t base_flags = (uint8_t)ap->base & (ADIV5_AP_BASE_FORMAT | ADIV5_AP_BASE_PRESENT);
	/* Check if this is a 64-bit AP */
	if (cfg & ADIV5_AP_CFG_LARGE_ADDRESS) {
		/* If this base value is invalid for a LPAE MEM-AP, bomb out here */
		if (base_flags == (ADIV5_AP_BASE_FORMAT_LEGACY | ADIV5_AP_BASE_PRESENT_NO_ENTRY)) {
			DEBUG_INFO(" -> Invalid\n");
			return false;
		}
		/* Otherwise note this is a 64-bit AP and read the high part */
		ap->flags |= ADIV5_AP_FLAGS_64BIT;
		ap->base |= (uint64_t)adiv5_ap_read(ap, ADIV5_AP_BASE_HIGH) << 32U;
	}
	/* Check the Debug Base Address register for not-present. See ADIv5 Specification C2.6.1 */
	if (base_flags == (ADIV5_AP_BASE_FORMAT_ADIV5 | ADIV5_AP_BASE_PRESENT_NO_ENTRY) ||
		(!(ap->flags & ADIV5_AP_FLAGS_64BIT) && (uint32_t)ap->base == ADIV5_AP_BASE_NOT_PRESENT)) {
		bool ignore_not_present = false;
		/*
			 * Debug Base Address not present in this MEM-AP
			 * No debug entries... useless AP
			 * AP0 on STM32MP157C reads 0x00000002
			 *
			 * NB: MSPM0 parts erroneously set BASE.P = 0 despite there being
			 * valid debug components on AP0, so we have to have an exception
			 * for this part family.
			 */
		if (ap->dp->target_designer_code == JEP106_MANUFACTURER_TEXAS && ap->base == 0xf0000002U)
			ignore_not_present = true;

		else if (ap->dp->target_designer_code == JEP106_MANUFACTURER_NORDIC && ap->base != 0x00000002U)
			ignore_not_present = true;

		if (!ignore_not_present) {
			DEBUG_INFO(" -> Not Present\n");
			return false;
		}
	}
	/* Make sure we only pay attention to the base address, not the presence and format bits */
	ap->base &= ADIV5_AP_BASE_BASEADDR;
	/* Check if the AP is disabled, skipping it if that is the case */
	if ((ap->csw & ADIV5_AP_CSW_AP_ENABLED) == 0U) {
		DEBUG_INFO(" -> Disabled\n");
		return false;
	}

	/* Apply bus-common fixups to the CSW value */
	ap->csw &= ~(ADIV5_AP_CSW_SIZE_MASK | ADIV5_AP_CSW_ADDRINC_MASK);
	ap->csw |= ADIV5_AP_CSW_DBGSWENABLE;

	switch (ap_type) {
	case ADIV5_AP_IDR_TYPE_APB2_3:
		/* We have no prot modes on APB2 and APB3 */
		break;
	case ADIV5_AP_IDR_TYPE_AXI3_4:
		/* XXX: Handle AXI4 w/ ACE-Lite which makes Mode and Type do ~things~™ (§E1.3.1, pg237) */
		/* Clear any existing prot modes and disable memory tagging */
		ap->csw &= ~(ADIV5_AP_CSW_AXI3_4_PROT_MASK | ADIV5_AP_CSW_AXI_MTE);
		/* Check if secure access is allowed and enable it if so */
		if (ap->csw & ADIV5_AP_CSW_SPIDEN)
			ap->csw &= ~ADIV5_AP_CSW_AXI_PROT_NS;
		else
			ap->csw |= ADIV5_AP_CSW_AXI_PROT_NS;
		/* Always privileged accesses */
		ap->csw |= ADIV5_AP_CSW_AXI_PROT_PRIV;
		break;
	case ADIV5_AP_IDR_TYPE_AXI5:
		/* Clear any existing prot modes and disable memory tagging */
		ap->csw &= ~(ADIV5_AP_CSW_AXI5_PROT_MASK | ADIV5_AP_CSW_AXI_MTE);
		/* Check if secure access is allowed and enable it if so */
		if (ap->csw & ADIV5_AP_CSW_SPIDEN)
			ap->csw &= ~ADIV5_AP_CSW_AXI_PROT_NS;
		else
			ap->csw |= ADIV5_AP_CSW_AXI_PROT_NS;
		/* Always privileged accesses */
		ap->csw |= ADIV5_AP_CSW_AXI_PROT_PRIV;
		break;
	case ADIV5_AP_IDR_TYPE_AHB3:
	case ADIV5_AP_IDR_TYPE_AHB5:
	case ADIV5_AP_IDR_TYPE_AHB5_HPROT:
		/* Clear any existing HPROT modes */
		ap->csw &= ~ADIV5_AP_CSW_AHB_HPROT_MASK;
		/*
			 * Ensure that MasterType is set to generate transactions as requested from the AHB-AP,
			 * and that we generate privileged data requests via the HPROT bits
			 */
		ap->csw |= ADIV5_AP_CSW_AHB_MASTERTYPE | ADIV5_AP_CSW_AHB_HPROT_DATA | ADIV5_AP_CSW_AHB_HPROT_PRIV;
		/* Check to see if secure access is supported and allowed */
		if (ap->csw & ADIV5_AP_CSW_SPIDEN)
			ap->csw &= ~ADIV5_AP_CSW_AHB_HNONSEC;
		else
			ap->csw |= ADIV5_AP_CSW_AHB_HNONSEC;
		break;
	case ADIV5_AP_IDR_TYPE_APB4_5:
		/* Clear any existing prot modes and disable memory tagging */
		ap->csw &= ~ADIV5_AP_CSW_APB_PPROT_MASK;
		/* Check if secure access is allowed and enable it if so */
		if (ap->csw & ADIV5_AP_CSW_SPIDEN)
			ap->csw &= ~ADIV5_AP_CSW_APB_PPROT_NS;
		else
			ap->csw |= ADIV5_AP_CSW_APB_PPROT_NS;
		ap->csw |= ADIV5_AP_CSW_APB_PPROT_PRIV;
		break;
	default:
		DEBUG_ERROR("Unhandled AP type %u\n", ap_type);
	}

	if (cfg & ADIV5_AP_CFG_LARGE_ADDRESS)
		DEBUG_INFO(" CFG=%08" PRIx32 " BASE=%08" PRIx32 "%08" PRIx32 " CSW=%08" PRIx32, cfg,
			(uint32_t)(ap->base >> 32U), (uint32_t)ap->base, ap->csw);
	else
		DEBUG_INFO(" CFG=%08" PRIx32 " BASE=%08" PRIx32 " CSW=%08" PRIx32, cfg, (uint32_t)ap->base, ap->csw);

	if (ap->csw & ADIV5_AP_CSW_TRINPROG) {
		DEBUG_ERROR("AP %3u: Transaction in progress. AP is not usable!\n", ap->apsel);
		return false;
	}

	return true;
}

bool adi_configure_ap(adiv5_access_port_s *const ap)
{
	/* Grab the ID register and make sure the value is sane (non-zero) */
	ap->idr = adiv5_ap_read(ap, ADIV5_AP_IDR);
	if (!ap->idr)
		return false;
	const uint8_t ap_type = ADIV5_AP_IDR_TYPE(ap->idr);
	const uint8_t ap_class = ADIV5_AP_IDR_CLASS(ap->idr);
	DEBUG_INFO("AP %3u: IDR=%08" PRIx32, ap->apsel, ap->idr);
	/* If this is a MEM-AP */
	if (ap_class == ADIV5_AP_IDR_CLASS_MEM && ap_type >= 1U && ap_type <= 8U) {
		if (!adi_configure_mem_ap(ap)) {
			return false;
		}
	}

	adi_display_ap(ap);
	return true;
}

void adi_ap_resume_cores(adiv5_access_port_s *const ap)
{
	/*
	 * If we're not in connect-under-reset mode, and now that we're done with this AP's
	 * ROM tables, look for any created targets and resume the core associated with it.
	 */
	for (target_s *target = target_list; target; target = target->next) {
		if (!connect_assert_nrst && target->priv_free == cortex_priv_free) {
			adiv5_access_port_s *target_ap = cortex_ap(target);
			if (target_ap == ap)
				target_halt_resume(target, false);
		}
	}
}

/* Program the CSW and TAR for sequential access at a given width */
void adi_ap_mem_access_setup(adiv5_access_port_s *const ap, const target_addr64_t addr, const align_e align)
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
	if (ap->flags & ADIV5_AP_FLAGS_64BIT)
		adiv5_dp_write(ap->dp, ADIV5_AP_TAR_HIGH, (uint32_t)(addr >> 32U));
	adiv5_dp_write(ap->dp, ADIV5_AP_TAR_LOW, (uint32_t)addr);
}

void adi_ap_banked_access_setup(adiv5_access_port_s *base_ap)
{
	/* Check which ADI version this is for, v5 only requires we set up the DP's SELECT register */
	if (base_ap->dp->version <= 2U)
		/* Configure the bank selection to the appropriate AP register bank */
		adiv5_dp_write(base_ap->dp, ADIV5_DP_SELECT, ((uint32_t)base_ap->apsel << 24U) | (ADIV5_AP_DB(0) & 0x00f0U));
	else {
		/* ADIv6 requires we set up the DP's SELECT1 and SELECT registers to correctly acccess the AP */
		adiv6_access_port_s *const ap = (adiv6_access_port_s *)base_ap;
		/* Set SELECT1 in the DP up first */
		adiv5_dp_write(base_ap->dp, ADIV5_DP_SELECT, ADIV5_DP_BANK5);
		adiv5_dp_write(base_ap->dp, ADIV6_DP_SELECT1, (uint32_t)(ap->ap_address >> 32U));
		/* Now set up SELECT in the DP */
		adiv5_dp_write(base_ap->dp, ADIV5_DP_SELECT, (uint32_t)ap->ap_address | (ADIV5_AP_DB(0) & ADIV6_AP_BANK_MASK));
	}
}

static uint32_t adi_ap_read_id(adiv5_access_port_s *ap, uint32_t addr)
{
	uint32_t res = 0;
	uint8_t data[16];
	adiv5_mem_read(ap, data, addr, sizeof(data));
	for (size_t i = 0; i < 4U; ++i)
		res |= (uint32_t)data[4U * i] << (i * 8U);
	return res;
}

static uint64_t adi_ap_read_pidr(adiv5_access_port_s *ap, uint32_t addr)
{
	const uint32_t pidr_upper = adi_ap_read_id(ap, addr + PIDR4_OFFSET);
	const uint32_t pidr_lower = adi_ap_read_id(ap, addr + PIDR0_OFFSET);
	return ((uint64_t)pidr_upper << 32U) | (uint64_t)pidr_lower;
}

uint32_t adi_mem_read32(adiv5_access_port_s *const ap, const target_addr32_t addr)
{
	uint32_t ret;
	adiv5_mem_read(ap, &ret, addr, sizeof(ret));
	return ret;
}

void adi_mem_write32(adiv5_access_port_s *const ap, const target_addr32_t addr, const uint32_t value)
{
	adiv5_mem_write(ap, addr, &value, sizeof(value));
}

static void adi_parse_adi_rom_table(adiv5_access_port_s *const ap, const target_addr32_t base_address,
	const size_t recursion_depth, const char *const indent, const uint64_t pidr)
{
#if defined(DEBUG_WARN_IS_NOOP) && defined(DEBUG_ERROR_IS_NOOP)
	(void)indent;
#endif

	/* Extract the designer code and part number from the part ID register */
	const uint16_t designer_code = adi_designer_from_pidr(pidr);
	const uint16_t part_number = pidr & PIDR_PN_MASK;

	if (recursion_depth == 0U) {
		ap->designer_code = designer_code;
		ap->partno = part_number;

		if (ap->designer_code == JEP106_MANUFACTURER_ATMEL && ap->partno == ID_SAMx5x) {
			uint32_t ctrlstat = adi_mem_read32(ap, SAMX5X_DSU_CTRLSTAT);
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

	/* Check SYSMEM bit */
	const bool memtype = adi_mem_read32(ap, base_address + ADI_ROM_MEMTYPE) & ADI_ROM_MEMTYPE_SYSMEM;
	if (adiv5_dp_error(ap->dp))
		DEBUG_ERROR("Fault reading ROM table entry\n");
	else if (memtype)
		ap->flags |= ADIV5_AP_FLAGS_HAS_MEM;
	DEBUG_INFO("ROM Table: BASE=0x%" PRIx32 " SYSMEM=%u, Manufacturer %03x Partno %03x (PIDR = 0x%02" PRIx32
			   "%08" PRIx32 ")\n",
		base_address, memtype, designer_code, part_number, (uint32_t)(pidr >> 32U), (uint32_t)pidr);

	for (uint32_t i = 0; i < 960U; i++) {
		adiv5_dp_error(ap->dp);

		uint32_t entry = adi_mem_read32(ap, base_address + i * 4U);
		if (adiv5_dp_error(ap->dp)) {
			DEBUG_ERROR("%sFault reading ROM table entry %" PRIu32 "\n", indent, i);
			break;
		}

		if (entry == 0)
			break;

		if (!(entry & ADI_ROM_ROMENTRY_PRESENT)) {
			DEBUG_INFO("%s%" PRIu32 " Entry 0x%08" PRIx32 " -> Not present\n", indent, i, entry);
			continue;
		}

		/* Probe recursively */
		adi_ap_component_probe(ap, base_address + (entry & ADI_ROM_ROMENTRY_OFFSET), recursion_depth + 1U, i);
	}
	DEBUG_INFO("%sROM Table: END\n", indent);
}

static bool adi_reset_resources(adiv5_access_port_s *const ap, const target_addr64_t base_address)
{
	/* Read out power request ID register 0 and check if power control is actually implemented */
	const uint8_t pridr0 = adi_mem_read32(ap, base_address + CORESIGHT_ROM_PRIDR0) & 0x3fU;
	if ((pridr0 & CORESIGHT_ROM_PRIDR0_VERSION_MASK) != CORESIGHT_ROM_PRIDR0_VERSION_NOT_IMPL)
		ap->flags |= ADIV6_DP_FLAGS_HAS_PWRCTRL;
	/* Now try and perform a debug reset request */
	if (pridr0 & CORESIGHT_ROM_PRIDR0_HAS_DBG_RESET_REQ) {
		platform_timeout_s timeout;
		platform_timeout_set(&timeout, 250);

		adi_mem_write32(ap, base_address + CORESIGHT_ROM_DBGRSTRR, CORESIGHT_ROM_DBGRST_REQ);
		/* While the reset request is in progress */
		while (adi_mem_read32(ap, base_address + CORESIGHT_ROM_DBGRSTRR) & CORESIGHT_ROM_DBGRST_REQ) {
			/* Check if it's been acknowledge, and if it has, deassert the request */
			if (adi_mem_read32(ap, base_address + CORESIGHT_ROM_DBGRSTAR) & CORESIGHT_ROM_DBGRST_REQ)
				adi_mem_write32(ap, base_address + CORESIGHT_ROM_DBGRSTRR, 0U);
			/* Check if the reset has timed out */
			if (platform_timeout_is_expired(&timeout)) {
				DEBUG_WARN("adi: debug reset failed\n");
				adi_mem_write32(ap, base_address + CORESIGHT_ROM_DBGRSTRR, 0U);
				break;
			}
		}
	}
	/* Regardless of what happened, extract whether system reset is supported this way */
	if (pridr0 & CORESIGHT_ROM_PRIDR0_HAS_SYS_RESET_REQ)
		ap->flags |= ADIV6_DP_FLAGS_HAS_SYSRESETREQ;
	return true;
}

static inline uint64_t adi_read_coresight_rom_entry(
	adiv5_access_port_s *const ap, const uint8_t rom_format, const target_addr64_t entry_address)
{
	const uint32_t entry_lower = adi_mem_read32(ap, entry_address);
	if (rom_format == CORESIGHT_ROM_DEVID_FORMAT_32BIT)
		return entry_lower;
	const uint32_t entry_upper = adi_mem_read32(ap, entry_address + 4U);
	return ((uint64_t)entry_upper << 32U) | (uint64_t)entry_lower;
}

static void adi_parse_coresight_v0_rom_table(adiv5_access_port_s *const ap, const target_addr64_t base_address,
	const uint64_t recursion_depth, const char *const indent, const uint64_t pidr)
{
	/* Extract the designer code and part number from the part ID register */
	const uint16_t designer_code = adi_designer_from_pidr(pidr);
	const uint16_t part_number = pidr & PIDR_PN_MASK;

#if defined(DEBUG_INFO_IS_NOOP)
	(void)indent;
	(void)designer_code;
	(void)part_number;
#endif

	/* Now we know we're in a CoreSight v0 ROM table, read out the device ID field and set up the memory flag on the AP */
	const uint8_t dev_id = adi_mem_read32(ap, base_address + CORESIGHT_ROM_DEVID) & 0x7fU;

	if (adiv5_dp_error(ap->dp))
		DEBUG_ERROR("Fault reading ROM table DEVID\n");

	if (dev_id & CORESIGHT_ROM_DEVID_SYSMEM)
		ap->flags |= ADIV5_AP_FLAGS_HAS_MEM;
	const uint8_t rom_format = dev_id & CORESIGHT_ROM_DEVID_FORMAT;

	/* Check if the power control registers are available, and if they are try to reset all debug resources */
	if ((dev_id & CORESIGHT_ROM_DEVID_HAS_POWERREQ) && !adi_reset_resources(ap, base_address))
		return;

	DEBUG_INFO("%sROM Table: BASE=0x%0" PRIx32 "%08" PRIx32 " SYSMEM=%u, Manufacturer %03x Partno %03x (PIDR = "
			   "0x%02" PRIx32 "%08" PRIx32 ")\n",
		indent, (uint32_t)(base_address >> 32U), (uint32_t)base_address, 0U, designer_code, part_number,
		(uint32_t)(pidr >> 32U), (uint32_t)pidr);

	/* ROM table has at most 512 entries when 32-bit and 256 entries when 64-bit */
	const uint32_t max_entries = rom_format == CORESIGHT_ROM_DEVID_FORMAT_32BIT ? 512U : 256U;
	const size_t entry_shift = rom_format == CORESIGHT_ROM_DEVID_FORMAT_32BIT ? 2U : 3U;
	for (uint32_t index = 0; index < max_entries; ++index) {
		adiv5_dp_error(ap->dp);

		/* Start by reading out the entry */
		const uint64_t entry = adi_read_coresight_rom_entry(ap, rom_format, base_address + (index << entry_shift));

		if (adiv5_dp_error(ap->dp)) {
			DEBUG_ERROR("Fault reading ROM table entry %" PRIu32 "\n", index);
			break;
		}

		const uint8_t presence = entry & CORESIGHT_ROM_ROMENTRY_ENTRY_MASK;
		/* Check if the entry is valid */
		if (presence == CORESIGHT_ROM_ROMENTRY_ENTRY_FINAL)
			break;
		/* Check for an entry to skip */
		if (presence == CORESIGHT_ROM_ROMENTRY_ENTRY_NOT_PRESENT) {
			DEBUG_INFO("%s%" PRIu32 " Entry 0x%0" PRIx32 "%08" PRIx32 " -> Not present\n", indent, index,
				(uint32_t)(entry >> 32U), (uint32_t)entry);
			continue;
		}
		/* Check that the entry isn't invalid */
		if (presence == CORESIGHT_ROM_ROMENTRY_ENTRY_INVALID) {
			DEBUG_INFO("%s%" PRIu32 " Entry invalid\n", indent, index);
			continue;
		}
		/* Got a good entry? great! Figure out if it has a power domain to cycle and what the address offset is */
		const target_addr64_t offset = entry & CORESIGHT_ROM_ROMENTRY_OFFSET_MASK;
		if ((ap->flags & ADIV6_DP_FLAGS_HAS_PWRCTRL) & entry & CORESIGHT_ROM_ROMENTRY_POWERID_VALID) {
			const uint8_t power_domain_offset =
				((entry & CORESIGHT_ROM_ROMENTRY_POWERID_MASK) >> CORESIGHT_ROM_ROMENTRY_POWERID_SHIFT) << 2U;
			/* Check if the power control register for this domain is present */
			if (adi_mem_read32(ap, base_address + CORESIGHT_ROM_DBGPCR_BASE + power_domain_offset) &
				CORESIGHT_ROM_DBGPCR_PRESENT) {
				/* And if it is, ask the domain to power up */
				adi_mem_write32(
					ap, base_address + CORESIGHT_ROM_DBGPCR_BASE + power_domain_offset, CORESIGHT_ROM_DBGPCR_PWRREQ);
				/* Then spin for a little waiting for the domain to become powered usefully */
				platform_timeout_s timeout;
				platform_timeout_set(&timeout, 250);
				while (!(adi_mem_read32(ap, base_address + CORESIGHT_ROM_DBGPSR_BASE + power_domain_offset) &
					CORESIGHT_ROM_DBGPSR_STATUS_ON)) {
					if (platform_timeout_is_expired(&timeout)) {
						DEBUG_WARN("adi: power-up failed\n");
						return;
					}
				}
			}
		}

		/* Now recursively probe the component */
		adi_ap_component_probe(ap, base_address + offset, recursion_depth + 1U, index);
	}

	DEBUG_INFO("%sROM Table: END\n", indent);
}

/* Return true if we find a debuggable device. */
void adi_ap_component_probe(
	adiv5_access_port_s *ap, target_addr64_t base_address, const size_t recursion, const uint32_t entry_number)
{
#ifdef DEBUG_WARN_IS_NOOP
	(void)entry_number;
#endif

	const uint32_t cidr = adi_ap_read_id(ap, base_address + CIDR0_OFFSET);
	if (ap->dp->fault) {
		DEBUG_ERROR("Error reading CIDR on AP%u: %u\n", ap->apsel, ap->dp->fault);
		return;
	}

#if ENABLE_DEBUG == 1
	char *const indent = alloca(recursion + 1U);

	for (size_t i = 0; i < recursion; i++)
		indent[i] = ' ';
	indent[recursion] = 0;
#else
	const char *const indent = " ";
#endif

	if (adiv5_dp_error(ap->dp)) {
		DEBUG_ERROR("%sFault reading ID registers\n", indent);
		return;
	}

	/* CIDR preamble sanity check */
	if ((cidr & ~CID_CLASS_MASK) != CID_PREAMBLE) {
		DEBUG_WARN("%s%" PRIu32 " 0x%0" PRIx32 "%08" PRIx32 ": 0x%08" PRIx32 " <- does not match preamble (0x%08" PRIx32
				   ")\n",
			indent, entry_number, (uint32_t)(base_address >> 32U), (uint32_t)base_address, cidr, CID_PREAMBLE);
		return;
	}

	/* Extract Component ID class nibble */
	const uint8_t cid_class = (cidr & CID_CLASS_MASK) >> CID_CLASS_SHIFT;

	/* Read out the peripheral ID register */
	const uint64_t pidr = adi_ap_read_pidr(ap, base_address);

	/* ROM table */
	if (cid_class == cidc_romtab) {
		/* Validate that the SIZE field is 0 per the spec */
		if (pidr & PIDR_SIZE_MASK) {
			DEBUG_ERROR("Fault reading ROM table\n");
			return;
		}
		adi_parse_adi_rom_table(ap, base_address, recursion, indent, pidr);
	} else {
		/* Extract the designer code from the part ID register */
		const uint16_t designer_code = adi_designer_from_pidr(pidr);

		if (designer_code != JEP106_MANUFACTURER_ARM && designer_code != JEP106_MANUFACTURER_ARM_CHINA) {
#ifndef DEBUG_TARGET_IS_NOOP
			const uint16_t part_number = pidr & PIDR_PN_MASK;
#endif
			/* non-ARM components are not supported currently */
			DEBUG_WARN("%s%" PRIu32 " 0x%0" PRIx32 "%08" PRIx32 ": 0x%02" PRIx32 "%08" PRIx32 " Non-ARM component "
					   "ignored\n",
				indent + 1, entry_number, (uint32_t)(base_address >> 32U), (uint32_t)base_address,
				(uint32_t)(pidr >> 32U), (uint32_t)pidr);
			DEBUG_TARGET("%s -> designer: %x, part no: %x\n", indent, designer_code, part_number);
			return;
		}

		/* Check if this is a CoreSight component */
		uint8_t dev_type = 0;
		uint16_t arch_id = 0;
		if (cid_class == cidc_dc) {
			/* Read out the component's identification information */
			const uint32_t devarch = adi_mem_read32(ap, base_address + CORESIGHT_ROM_DEVARCH);
			dev_type = adi_mem_read32(ap, base_address + CORESIGHT_ROM_DEVTYPE) & DEVTYPE_MASK;

			if (devarch & DEVARCH_PRESENT)
				arch_id = devarch & DEVARCH_ARCHID_MASK;
		}

		/* Look the component up and dispatch to a probe routine accordingly */
		const arm_coresight_component_s *const component =
			adi_lookup_component(base_address, entry_number, indent, cid_class, pidr, dev_type, arch_id);
		if (component == NULL)
			return;

		switch (component->arch) {
		case aa_cortexm:
			DEBUG_INFO("%s-> cortexm_probe\n", indent + 1);
			cortexm_probe(ap);
			break;
		case aa_cortexa:
			DEBUG_INFO("%s-> cortexa_probe\n", indent + 1);
			cortexa_probe(ap, base_address);
			break;
		case aa_cortexr:
			DEBUG_INFO("%s-> cortexr_probe\n", indent + 1);
			cortexr_probe(ap, base_address);
			break;
		/* Handle when the component is a CoreSight component ROM table */
		case aa_rom_table:
			if (pidr & PIDR_SIZE_MASK)
				DEBUG_ERROR("Fault reading ROM table\n");
			else
				adi_parse_coresight_v0_rom_table(ap, base_address, recursion, indent, pidr);
			break;
		default:
			break;
		}
	}
}
