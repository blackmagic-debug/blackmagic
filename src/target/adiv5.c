/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
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

/* This file implements the transport generic functions.
 * See the following ARM Reference Documents:
 *
 * ARM Debug Interface v5 Architecure Specification, ARM IHI 0031E
 */
#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "adiv5.h"
#include "cortexm.h"
#include "exception.h"

/* All this should probably be defined in a dedicated ADIV5 header, so that they
 * are consistently named and accessible when needed in the codebase.
 */

/* ROM table CIDR values */
#define CIDR0_OFFSET    0xFF0 /* DBGCID0 */
#define CIDR1_OFFSET    0xFF4 /* DBGCID1 */
#define CIDR2_OFFSET    0xFF8 /* DBGCID2 */
#define CIDR3_OFFSET    0xFFC /* DBGCID3 */

/* Component class ID register can be broken down into the following logical
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
#define CID_PREAMBLE    0xB105000D
#define CID_CLASS_MASK  0x0000F000
#define CID_CLASS_SHIFT 12
/* The following enum is based on the Component Class value table 13-3 of the
 * ADIv5 standard.
 */
enum cid_class {
	cidc_gvc = 0x0,    /* Generic verification component*/
	cidc_romtab = 0x1, /* ROM Table, std. layout (ADIv5 Chapter 14) */
	/* 0x2 - 0x8 */    /* Reserved */
	cidc_dc = 0x9,     /* Debug component, std. layout (CoreSight Arch. Spec.) */
	/* 0xA */          /* Reserved */
	cidc_ptb = 0xB,    /* Peripheral Test Block (PTB) */
	/* 0xC */          /* Reserved */
	cidc_dess = 0xD,   /* OptimoDE Data Engine SubSystem (DESS) component */
	cidc_gipc = 0xE,   /* Generic IP Component */
	cidc_pcp = 0xF,    /* PrimeCell peripheral */
	cidc_unknown = 0x10
};

#ifdef ENABLE_DEBUG
/* The reserved ones only have an R in them, to save a bit of space. */
static const char * const cidc_debug_strings[] =
{
	[cidc_gvc] =     "Generic verification component",           /* 0x0 */
	[cidc_romtab] =  "ROM Table",                                /* 0x1 */
	[0x2 ... 0x8] =  "R",                                        /* 0x2 - 0x8 */
	[cidc_dc] =      "Debug component",                          /* 0x9 */
	[0xA] =          "R",                                        /* 0xA */
	[cidc_ptb] =     "Peripheral Test Block",                    /* 0xB */
	[0xC] =          "R",                                        /* 0xC */
	[cidc_dess] =    "OptimoDE Data Engine SubSystem component", /* 0xD */
	[cidc_gipc] =    "Generic IP component",                     /* 0xE */
	[cidc_pcp] =     "PrimeCell peripheral",                     /* 0xF */
	[cidc_unknown] = "Unknown component class"                   /* 0x10 */
};
#endif

#define PIDR0_OFFSET  0xFE0 /* DBGPID0 */
#define PIDR1_OFFSET  0xFE4 /* DBGPID1 */
#define PIDR2_OFFSET  0xFE8 /* DBGPID2 */
#define PIDR3_OFFSET  0xFEC /* DBGPID3 */
#define PIDR4_OFFSET  0xFD0 /* DBGPID4 */
#define PIDR5_OFFSET  0xFD4 /* DBGPID5 (Reserved) */
#define PIDR6_OFFSET  0xFD8 /* DBGPID6 (Reserved) */
#define PIDR7_OFFSET  0xFDC /* DBGPID7 (Reserved) */
#define PIDR_REV_MASK 0x0FFF00000ULL /* Revision bits. */
#define PIDR_PN_MASK  0x000000FFFULL /* Part number bits. */
#define PIDR_ARM_BITS 0x4000BB000ULL /* These make up the ARM JEP-106 code. */

enum arm_arch {
	aa_nosupport,
	aa_cortexm,
	aa_cortexa,
	aa_end
};

#ifdef ENABLE_DEBUG
#define PIDR_PN_BIT_STRINGS(...) __VA_ARGS__
#else
#define PIDR_PN_BIT_STRINGS(...)
#endif

/* The part number list was adopted from OpenOCD:
 * https://sourceforge.net/p/openocd/code/ci/406f4/tree/src/target/arm_adi_v5.c#l932
 *
 * The product ID register consists of several parts. For a full description
 * refer to ARM Debug Interface v5 Architecture Specification. Based on the
 * document the pidr is 64 bit long and has the following interpratiation:
 * |7   ID7 reg   0|7   ID6 reg   0|7   ID5 reg   0|7   ID4 reg   0|
 * |0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0|0| | | | |1|0|0|0|
 * |63           56|55           48|47           40|39   36|35   32|
 * \_______________________ ______________________/\___ __/\___ ___/
 *                         V                           V       V
 *                    Reserved, RAZ                   4KB      |
 *                                                   count     |
 *                                                          JEP-106
 *                                                     Continuation Code
 *
 * |7   ID3 reg   0|7   ID2 reg   0|7   ID1 reg   0|7   ID0 reg   0|
 * | | | | | | | | | | | | |1|0|1|1|1|0|1|1| | | | | | | | | | | | |
 * |31   28|27   24|23   20|||18   |     12|11     |              0|
 * \___ __/\__ ___/\___ __/ |\______ _____/\___________ ___________/
 *     V      V        V    |       V                  V
 *  RevAnd    |    Revision |    JEP-106          Part number
 *            |             |    ID code
 *        Customer          19
 *        modified          `- JEP-106 code is used
 *
 * JEP-106 is a JEDEC standard assigning manufacturer IDs to different
 * manufacturers in case of ARM the full code consisting of the JEP-106
 * Continuation code followed by the code used bit and the JEP-106 code itself
 * results in the code 0x4BB. These are the bits filled in the above bit table.
 *
 * We left out some of the Part numbers included in OpenOCD, we only include
 * the ones that have ARM as the designer.
 */
static const struct {
	uint16_t part_number;
	enum arm_arch arch;
	enum cid_class cidc;
#ifdef ENABLE_DEBUG
	const char *type;
	const char *full;
#endif
} pidr_pn_bits[] = {
	{0x000, aa_cortexm,   cidc_gipc,    PIDR_PN_BIT_STRINGS("Cortex-M3 SCS",  "(System Control Space)")},
	{0x001, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M3 ITM",  "(Instrumentation Trace Module)")},
	{0x002, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M3 DWT",  "(Data Watchpoint and Trace)")},
	{0x003, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M3 FBP",  "(Flash Patch and Breakpoint)")},
	{0x008, aa_cortexm,   cidc_gipc,    PIDR_PN_BIT_STRINGS("Cortex-M0 SCS",  "(System Control Space)")},
	{0x00a, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M0 DWT",  "(Data Watchpoint and Trace)")},
	{0x00b, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M0 BPU",  "(Breakpoint Unit)")},
	{0x00c, aa_cortexm,   cidc_gipc,    PIDR_PN_BIT_STRINGS("Cortex-M4 SCS",  "(System Control Space)")},
	{0x00d, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight ETM11", "(Embedded Trace)")},
	{0x00e, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M7 FBP",  "(Flash Patch and Breakpoint)")},
	{0x101, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("System TSGEN",   "(Time Stamp Generator)")},
	{0x490, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-A15 GIC", "(Generic Interrupt Controller)")},
	{0x4c7, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M7 PPB",  "(Private Peripheral Bus ROM Table)")},
	{0x906, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight CTI",  "(Cross Trigger)")},
	{0x907, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight ETB",  "(Trace Buffer)")},
	{0x908, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight CSTF", "(Trace Funnel)")},
	{0x910, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight ETM9", "(Embedded Trace)")},
	{0x912, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight TPIU", "(Trace Port Interface Unit)")},
	{0x913, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight ITM",  "(Instrumentation Trace Macrocell)")},
	{0x914, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight SWO",  "(Single Wire Output)")},
	{0x917, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight HTM",  "(AHB Trace Macrocell)")},
	{0x920, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight ETM11", "(Embedded Trace)")},
	{0x921, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-A8 ETM",  "(Embedded Trace)")},
	{0x922, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-A8 CTI",  "(Cross Trigger)")},
	{0x923, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M3 TPIU", "(Trace Port Interface Unit)")},
	{0x924, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M3 ETM",  "(Embedded Trace)")},
	{0x925, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M4 ETM",  "(Embedded Trace)")},
	{0x930, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-R4 ETM",  "(Embedded Trace)")},
	{0x932, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight MTB-M0+",  "(Simple Execution Trace)")},
	{0x941, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight TPIU-Lite", "(Trace Port Interface Unit)")},
	{0x950, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight Component", "(unidentified Cortex-A9 component)")},
	{0x955, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight Component", "(unidentified Cortex-A5 component)")},
	{0x956, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-A7 ETM",  "(Embedded Trace)")},
	{0x95f, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-A15 PTM", "(Program Trace Macrocell)")},
	{0x961, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight TMC",  "(Trace Memory Controller)")},
	{0x962, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight STM",  "(System Trace Macrocell)")},
	{0x963, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight STM",  "(System Trace Macrocell)")},
	{0x975, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M7 ETM",  "(Embedded Trace)")},
	{0x9a0, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("CoreSight PMU",  "(Performance Monitoring Unit)")},
	{0x9a1, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M4 TPIU", "(Trace Port Interface Unit)")},
	{0x9a9, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M7 TPIU", "(Trace Port Interface Unit)")},
	{0x9a5, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-A5 ETM",  "(Embedded Trace)")},
	{0x9a7, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-A7 PMU",  "(Performance Monitor Unit)")},
	{0x9af, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-A15 PMU", "(Performance Monitor Unit)")},
	{0xc05, aa_cortexa,   cidc_dc,      PIDR_PN_BIT_STRINGS("Cortex-A5 Debug", "(Debug Unit)")},
	{0xc07, aa_cortexa,   cidc_dc,      PIDR_PN_BIT_STRINGS("Cortex-A7 Debug", "(Debug Unit)")},
	{0xc08, aa_cortexa,   cidc_dc,      PIDR_PN_BIT_STRINGS("Cortex-A8 Debug", "(Debug Unit)")},
	{0xc09, aa_cortexa,   cidc_dc,      PIDR_PN_BIT_STRINGS("Cortex-A9 Debug", "(Debug Unit)")},
	{0xc0f, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-A15 Debug", "(Debug Unit)")}, /* support? */
	{0xc14, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-R4 Debug", "(Debug Unit)")}, /* support? */
	{0xcd0, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Atmel DSU", "(Device Service Unit)")},
	{0xd21, aa_nosupport, cidc_unknown, PIDR_PN_BIT_STRINGS("Cortex-M33", "()")}, /* support? */
	{0xfff, aa_end,       cidc_unknown, PIDR_PN_BIT_STRINGS("end", "end")}
};

extern bool cortexa_probe(ADIv5_AP_t *apb, uint32_t debug_base);

static void adiv5_dp_ref(ADIv5_DP_t *dp)
{
	dp->refcnt++;
}

void adiv5_ap_ref(ADIv5_AP_t *ap)
{
	ap->refcnt++;
}

static void adiv5_dp_unref(ADIv5_DP_t *dp)
{
	if (--(dp->refcnt) == 0)
		free(dp);
}

void adiv5_ap_unref(ADIv5_AP_t *ap)
{
	if (--(ap->refcnt) == 0) {
		adiv5_dp_unref(ap->dp);
		free(ap);
	}
}

static uint32_t adiv5_mem_read32(ADIv5_AP_t *ap, uint32_t addr)
{
	uint32_t ret;
	adiv5_mem_read(ap, &ret, addr, sizeof(ret));
	return ret;
}

static uint32_t adiv5_ap_read_id(ADIv5_AP_t *ap, uint32_t addr)
{
	uint32_t res = 0;
	for (int i = 0; i < 4; i++) {
		uint32_t x = adiv5_mem_read32(ap, addr + 4 * i);
		res |= (x & 0xff) << (i * 8);
	}
	return res;
}

uint64_t adiv5_ap_read_pidr(ADIv5_AP_t *ap, uint32_t addr)
{
	uint64_t pidr = adiv5_ap_read_id(ap, addr + PIDR4_OFFSET);
	pidr = pidr << 32 |     adiv5_ap_read_id(ap, addr + PIDR0_OFFSET);
	return pidr;
}

static bool adiv5_component_probe(ADIv5_AP_t *ap, uint32_t addr, int recursion, int num_entry)
{
	(void) num_entry;
	addr &= ~3;
	uint64_t pidr = adiv5_ap_read_pidr(ap, addr);
	uint32_t cidr = adiv5_ap_read_id(ap, addr + CIDR0_OFFSET);
	bool res = false;
#if defined(ENABLE_DEBUG)
	char indent[recursion + 1];

	for(int i = 0; i < recursion; i++) indent[i] = ' ';
	indent[recursion] = 0;
#endif

	if (adiv5_dp_error(ap->dp)) {
		DEBUG("%sFault reading ID registers\n", indent);
		return false;
	}

	/* CIDR preamble sanity check */
	if ((cidr & ~CID_CLASS_MASK) != CID_PREAMBLE) {
		DEBUG("%s%d 0x%08" PRIx32": 0x%08" PRIx32
			  " <- does not match preamble (0x%X)\n",
			  indent + 1, num_entry, addr, cidr, CID_PREAMBLE);
		return false;
	}

	/* Extract Component ID class nibble */
	uint32_t cid_class = (cidr & CID_CLASS_MASK) >> CID_CLASS_SHIFT;

	/* ROM table */
	if (cid_class == cidc_romtab) {
		/* Check SYSMEM bit */
#if defined(ENABLE_DEBUG)
		uint32_t memtype = adiv5_mem_read32(ap, addr | ADIV5_ROM_MEMTYPE) &
			ADIV5_ROM_MEMTYPE_SYSMEM;

		if (adiv5_dp_error(ap->dp)) {
			DEBUG("Fault reading ROM table entry\n");
		}

		DEBUG("ROM: Table BASE=0x%" PRIx32 " SYSMEM=0x%" PRIx32 ", PIDR 0x%02"
			  PRIx32 "%08" PRIx32 "\n", addr, memtype, (uint32_t)(pidr >> 32),
			  (uint32_t)pidr);
#endif

		for (int i = 0; i < 960; i++) {
			adiv5_dp_error(ap->dp);
			uint32_t entry = adiv5_mem_read32(ap, addr + i*4);
			if (adiv5_dp_error(ap->dp)) {
				DEBUG("%sFault reading ROM table entry %d\n", indent, i);
				break;
			}

			if (entry == 0)
				break;

			if (!(entry & ADIV5_ROM_ROMENTRY_PRESENT)) {
				DEBUG("%s%d Entry 0x%" PRIx32 " -> Not present\n", indent,
					  i, entry);
				continue;
			}

			/* Probe recursively */
			res |= adiv5_component_probe(
				ap, addr + (entry & ADIV5_ROM_ROMENTRY_OFFSET),
				recursion + 1, i);
		}
		DEBUG("%sROM: Table END\n", indent);
	} else {
		/* Check if the component was designed by ARM, we currently do not support,
		 * any components by other designers.
		 */
		if ((pidr & ~(PIDR_REV_MASK | PIDR_PN_MASK)) != PIDR_ARM_BITS) {
			DEBUG("%s0x%" PRIx32 ": 0x%02" PRIx32 "%08" PRIx32
				  " <- does not match ARM JEP-106\n",
				  indent, addr, (uint32_t)(pidr >> 32), (uint32_t)pidr);
			return false;
		}

		/* Extract part number from the part id register. */
		uint16_t part_number = pidr & PIDR_PN_MASK;
		/* Find the part number in our part list and run the appropriate probe
		 * routine if applicable.
		 */
		int i;
		for (i = 0; pidr_pn_bits[i].arch != aa_end; i++) {
			if (pidr_pn_bits[i].part_number == part_number) {
				DEBUG("%s%d 0x%" PRIx32 ": %s - %s %s (PIDR = 0x%02" PRIx32
					  "%08" PRIx32 ")",
					  indent + 1, num_entry, addr,
					  cidc_debug_strings[cid_class],
					  pidr_pn_bits[i].type, pidr_pn_bits[i].full,
					  (uint32_t)(pidr >> 32), (uint32_t)pidr);
				/* Perform sanity check, if we know what to expect as
				 * component ID class.
				 */
				if ((pidr_pn_bits[i].cidc != cidc_unknown) &&
				    (cid_class != pidr_pn_bits[i].cidc)) {
					DEBUG("%sWARNING: \"%s\" !match expected \"%s\"\n", indent + 1,
					      cidc_debug_strings[cid_class],
					      cidc_debug_strings[pidr_pn_bits[i].cidc]);
				}
				res = true;
				switch (pidr_pn_bits[i].arch) {
				case aa_cortexm:
					DEBUG("%s-> cortexm_probe\n", indent + 1);
					cortexm_probe(ap, false);
					break;
				case aa_cortexa:
					DEBUG("\n -> cortexa_probe\n");
					cortexa_probe(ap, addr);
					break;
				default:
					DEBUG("\n");
					break;
				}
				break;
			}
		}
		if (pidr_pn_bits[i].arch == aa_end) {
			DEBUG("%s0x%" PRIx32 ": %s - Unknown (PIDR = 0x%02" PRIx32
				  "%08" PRIx32 ")\n",
				  indent, addr, cidc_debug_strings[cid_class],
				  (uint32_t)(pidr >> 32), (uint32_t)pidr);
		}
	}
	return res;
}

ADIv5_AP_t *adiv5_new_ap(ADIv5_DP_t *dp, uint8_t apsel)
{
	ADIv5_AP_t *ap, tmpap;
	/* Assume valid and try to read IDR */
	memset(&tmpap, 0, sizeof(tmpap));
	tmpap.dp = dp;
	tmpap.apsel = apsel;
	tmpap.idr = adiv5_ap_read(&tmpap, ADIV5_AP_IDR);
	tmpap.base = adiv5_ap_read(&tmpap, ADIV5_AP_BASE);

	if(!tmpap.idr) /* IDR Invalid */
		return NULL;
	/* It's valid to so create a heap copy */
	ap = malloc(sizeof(*ap));
	if (!ap) {			/* malloc failed: heap exhaustion */
		DEBUG("malloc: failed in %s\n", __func__);
		return NULL;
	}

	memcpy(ap, &tmpap, sizeof(*ap));
	adiv5_dp_ref(dp);

	ap->base = adiv5_ap_read(ap, ADIV5_AP_BASE);
	ap->csw = adiv5_ap_read(ap, ADIV5_AP_CSW) &
		~(ADIV5_AP_CSW_SIZE_MASK | ADIV5_AP_CSW_ADDRINC_MASK);

	if (ap->csw & ADIV5_AP_CSW_TRINPROG) {
		DEBUG("AP transaction in progress.  Target may not be usable.\n");
		ap->csw &= ~ADIV5_AP_CSW_TRINPROG;
	}

#if defined(ENABLE_DEBUG)
	uint32_t cfg = adiv5_ap_read(ap, ADIV5_AP_CFG);
	DEBUG("AP %3d: IDR=%08"PRIx32" CFG=%08"PRIx32" BASE=%08"PRIx32" CSW=%08"PRIx32"\n",
	      apsel, ap->idr, cfg, ap->base, ap->csw);
#endif
	return ap;
}

void adiv5_dp_init(ADIv5_DP_t *dp)
{
	volatile bool probed = false;
	volatile uint32_t ctrlstat = 0;
	adiv5_dp_ref(dp);
#if PC_HOSTED  == 1
	platform_adiv5_dp_defaults(dp);
	if (!dp->ap_write)
		dp->ap_write = firmware_ap_write;
	if (!dp->ap_read)
		dp->ap_read = firmware_ap_read;
	if (!dp->mem_read)
		dp->mem_read = firmware_mem_read;
	if (!dp->mem_write_sized)
		dp->mem_write_sized = firmware_mem_write_sized;
#else
	dp->ap_write = firmware_ap_write;
	dp->ap_read = firmware_ap_read;
	dp->mem_read = firmware_mem_read;
	dp->mem_write_sized = firmware_mem_write_sized;
#endif
	volatile struct exception e;
	TRY_CATCH (e, EXCEPTION_TIMEOUT) {
		ctrlstat = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT);
	}
	if (e.type) {
		DEBUG("DP not responding!  Trying abort sequence...\n");
		adiv5_dp_abort(dp, ADIV5_DP_ABORT_DAPABORT);
		ctrlstat = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT);
	}

	/* Write request for system and debug power up */
	adiv5_dp_write(dp, ADIV5_DP_CTRLSTAT,
			ctrlstat |= ADIV5_DP_CTRLSTAT_CSYSPWRUPREQ |
				ADIV5_DP_CTRLSTAT_CDBGPWRUPREQ);
	/* Wait for acknowledge */
	while(((ctrlstat = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT)) &
		(ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK)) !=
		(ADIV5_DP_CTRLSTAT_CSYSPWRUPACK | ADIV5_DP_CTRLSTAT_CDBGPWRUPACK));

	/* This AP reset logic is described in ADIv5, but fails to work
	 * correctly on STM32.	CDBGRSTACK is never asserted, and we
	 * just wait forever.  This scenario is described in B2.4.1
	 * so we have a timeout mechanism in addition to the sensing one.
	 *
	 * Write request for debug reset */
	adiv5_dp_write(dp, ADIV5_DP_CTRLSTAT,
				   ctrlstat |= ADIV5_DP_CTRLSTAT_CDBGRSTREQ);

	platform_timeout timeout;
	platform_timeout_set(&timeout, 101);
	/* Write request for debug reset release */
	adiv5_dp_write(dp, ADIV5_DP_CTRLSTAT,
				   ctrlstat &= ~ADIV5_DP_CTRLSTAT_CDBGRSTREQ);
	/* Wait for acknowledge */
	while(1) {
		platform_delay(20);
		ctrlstat = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT);
		if (ctrlstat & ADIV5_DP_CTRLSTAT_CDBGRSTACK) {
			DEBUG("RESET_SEQ succeeded.\n");
			break;
		}
		if (platform_timeout_is_expired(&timeout)) {
			DEBUG("RESET_SEQ failed\n");
			break;
		}
	}

	uint32_t dp_idcode = adiv5_dp_read(dp, ADIV5_DP_IDCODE);
	if ((dp_idcode & ADIV5_DP_VERSION_MASK) == ADIV5_DPv2) {
		/* Read TargetID. Can be done with device in WFI, sleep or reset!*/
		adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK2);
		dp->targetid = adiv5_dp_read(dp, ADIV5_DP_CTRLSTAT);
		adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK0);
		DEBUG("TARGETID %08" PRIx32 "\n", dp->targetid);
	}
	/* Probe for APs on this DP */
	uint32_t last_base = 0;
	int void_aps = 0;
	for(int i = 0; (i < 256) && (void_aps < 8); i++) {
		ADIv5_AP_t *ap = NULL;
#if PC_HOSTED == 1
		if ((!dp->ap_setup) || dp->ap_setup(i))
			ap = adiv5_new_ap(dp, i);
#else
		ap = adiv5_new_ap(dp, i);
#endif
		if (ap == NULL) {
			void_aps++;
#if PC_HOSTED == 1
			if (dp->ap_cleanup)
				dp->ap_cleanup(i);
#endif
			if (i == 0)
				return;
			else
				continue;
		}
		if (ap->base == last_base) {
			DEBUG("AP %d: Duplicate base\n", i);
#if PC_HOSTED == 1
			if (dp->ap_cleanup)
				dp->ap_cleanup(i);
#endif
			free(ap);
			/* FIXME: Should we expect valid APs behind duplicate ones? */
			return;
		}
		last_base = ap->base;
		extern void kinetis_mdm_probe(ADIv5_AP_t *);
		kinetis_mdm_probe(ap);

		extern void nrf51_mdm_probe(ADIv5_AP_t *);
		nrf51_mdm_probe(ap);

		extern void efm32_aap_probe(ADIv5_AP_t *);
		efm32_aap_probe(ap);

		/* Check the Debug Base Address register. See ADIv5
		 * Specification C2.6.1 */
		if (!(ap->base & ADIV5_AP_BASE_PRESENT) ||
			(ap->base == 0xffffffff)) {
			/* Debug Base Address not present in this MEM-AP */
			/* No debug entries... useless AP */
			adiv5_ap_unref(ap);
			continue;
		}

		/* Should probe further here to make sure it's a valid target.
		 * AP should be unref'd if not valid.
		 */

		/* The rest should only be added after checking ROM table */
		probed |= adiv5_component_probe(ap, ap->base, 0, 0);
		if (!probed && (dp->idcode & 0xfff) == 0x477) {
			DEBUG("-> cortexm_probe forced\n");
			cortexm_probe(ap, true);
			probed = true;
		}
	}
	adiv5_dp_unref(dp);
}

#define ALIGNOF(x) (((x) & 3) == 0 ? ALIGN_WORD : \
                    (((x) & 1) == 0 ? ALIGN_HALFWORD : ALIGN_BYTE))

/* Program the CSW and TAR for sequencial access at a given width */
static void ap_mem_access_setup(ADIv5_AP_t *ap, uint32_t addr, enum align align)
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
	adiv5_ap_write(ap, ADIV5_AP_CSW, csw);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_TAR, addr);
}

/* Extract read data from data lane based on align and src address */
void * extract(void *dest, uint32_t src, uint32_t val, enum align align)
{
	switch (align) {
	case ALIGN_BYTE:
		*(uint8_t *)dest = (val >> ((src & 0x3) << 3) & 0xFF);
		break;
	case ALIGN_HALFWORD:
		*(uint16_t *)dest = (val >> ((src & 0x2) << 3) & 0xFFFF);
		break;
	case ALIGN_DWORD:
	case ALIGN_WORD:
		*(uint32_t *)dest = val;
		break;
	}
	return (uint8_t *)dest + (1 << align);
}

void firmware_mem_read(ADIv5_AP_t *ap, void *dest, uint32_t src, size_t len)
{
	uint32_t tmp;
	uint32_t osrc = src;
	enum align align = MIN(ALIGNOF(src), ALIGNOF(len));

	if (len == 0)
		return;

	len >>= align;
	ap_mem_access_setup(ap, src, align);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
	while (--len) {
		tmp = adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
		dest = extract(dest, src, tmp, align);

		src += (1 << align);
		/* Check for 10 bit address overflow */
		if ((src ^ osrc) & 0xfffffc00) {
			osrc = src;
			adiv5_dp_low_access(ap->dp,
					ADIV5_LOW_WRITE, ADIV5_AP_TAR, src);
			adiv5_dp_low_access(ap->dp,
					ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
		}
	}
	tmp = adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_DP_RDBUFF, 0);
	extract(dest, src, tmp, align);
}

void firmware_mem_write_sized(ADIv5_AP_t *ap, uint32_t dest, const void *src,
							size_t len, enum align align)
{
	uint32_t odest = dest;

	len >>= align;
	ap_mem_access_setup(ap, dest, align);
	while (len--) {
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
		src = (uint8_t *)src + (1 << align);
		dest += (1 << align);
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DRW, tmp);

		/* Check for 10 bit address overflow */
		if ((dest ^ odest) & 0xfffffc00) {
			odest = dest;
			adiv5_dp_low_access(ap->dp,
					ADIV5_LOW_WRITE, ADIV5_AP_TAR, dest);
		}
	}
}

void firmware_ap_write(ADIv5_AP_t *ap, uint16_t addr, uint32_t value)
{
	adiv5_dp_write(ap->dp, ADIV5_DP_SELECT,
			((uint32_t)ap->apsel << 24)|(addr & 0xF0));
	adiv5_dp_write(ap->dp, addr, value);
}

uint32_t firmware_ap_read(ADIv5_AP_t *ap, uint16_t addr)
{
	uint32_t ret;
	adiv5_dp_write(ap->dp, ADIV5_DP_SELECT,
			((uint32_t)ap->apsel << 24)|(addr & 0xF0));
	ret = adiv5_dp_read(ap->dp, addr);
	return ret;
}

void adiv5_mem_write(ADIv5_AP_t *ap, uint32_t dest, const void *src, size_t len)
{
	enum align align = MIN(ALIGNOF(dest), ALIGNOF(len));
	adiv5_mem_write_sized(ap, dest, src, len, align);
}
