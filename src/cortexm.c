/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
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

/* This file implements debugging functionality specific to ARM
 * the Cortex-M3 core.  This should be generic to ARMv7-M as it is
 * implemented according to the "ARMv7-M Architectue Reference Manual",
 * ARM doc DDI0403C.
 *
 * Also supports Cortex-M0 / ARMv6-M
 *
 * Issues:
 * There are way too many magic numbers used here.
 */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "general.h"
#include "jtagtap.h"
#include "jtag_scan.h"
#include "adiv5.h"
#include "target.h"
#include "command.h"
#include "gdb_packet.h"

static char cortexm_driver_str[] = "ARM Cortex-M";

static bool cortexm_vector_catch(target *t, int argc, char *argv[]);

const struct command_s cortexm_cmd_list[] = {
	{"vector_catch", (cmd_handler)cortexm_vector_catch, "Catch exception vectors"},
	{NULL, NULL, NULL}
};

/* target options recognised by the Cortex-M target */
#define	TOPT_FLAVOUR_V6M	(1<<0)	/* if not set, target is assumed to be v7m */
#define	TOPT_FLAVOUR_V7MF	(1<<1)	/* if set, floating-point enabled. */

/* Private peripheral bus base address */
#define CORTEXM_PPB_BASE	0xE0000000

#define CORTEXM_SCS_BASE	(CORTEXM_PPB_BASE + 0xE000)

#define CORTEXM_AIRCR		(CORTEXM_SCS_BASE + 0xD0C)
#define CORTEXM_CFSR		(CORTEXM_SCS_BASE + 0xD28)
#define CORTEXM_HFSR		(CORTEXM_SCS_BASE + 0xD2C)
#define CORTEXM_DFSR		(CORTEXM_SCS_BASE + 0xD30)
#define CORTEXM_CPACR		(CORTEXM_SCS_BASE + 0xD88)
#define CORTEXM_DHCSR		(CORTEXM_SCS_BASE + 0xDF0)
#define CORTEXM_DCRSR		(CORTEXM_SCS_BASE + 0xDF4)
#define CORTEXM_DCRDR		(CORTEXM_SCS_BASE + 0xDF8)
#define CORTEXM_DEMCR		(CORTEXM_SCS_BASE + 0xDFC)

#define CORTEXM_FPB_BASE	(CORTEXM_PPB_BASE + 0x2000)

/* ARM Literature uses FP_*, we use CORTEXM_FPB_* consistently */
#define CORTEXM_FPB_CTRL	(CORTEXM_FPB_BASE + 0x000)
#define CORTEXM_FPB_REMAP	(CORTEXM_FPB_BASE + 0x004)
#define CORTEXM_FPB_COMP(i)	(CORTEXM_FPB_BASE + 0x008 + (4*(i)))

#define CORTEXM_DWT_BASE	(CORTEXM_PPB_BASE + 0x1000)

#define CORTEXM_DWT_CTRL	(CORTEXM_DWT_BASE + 0x000)
#define CORTEXM_DWT_COMP(i)	(CORTEXM_DWT_BASE + 0x020 + (0x10*(i)))
#define CORTEXM_DWT_MASK(i)	(CORTEXM_DWT_BASE + 0x024 + (0x10*(i)))
#define CORTEXM_DWT_FUNC(i)	(CORTEXM_DWT_BASE + 0x028 + (0x10*(i)))

/* Application Interrupt and Reset Control Register (AIRCR) */
#define CORTEXM_AIRCR_VECTKEY		(0x05FA << 16)
/* Bits 31:16 - Read as VECTKETSTAT, 0xFA05 */
#define CORTEXM_AIRCR_ENDIANESS		(1 << 15)
/* Bits 15:11 - Unused, reserved */
#define CORTEXM_AIRCR_PRIGROUP		(7 << 8)
/* Bits 7:3 - Unused, reserved */
#define CORTEXM_AIRCR_SYSRESETREQ	(1 << 2)
#define CORTEXM_AIRCR_VECTCLRACTIVE	(1 << 1)
#define CORTEXM_AIRCR_VECTRESET		(1 << 0)

/* HardFault Status Register (HFSR) */
#define CORTEXM_HFSR_DEBUGEVT		(1 << 31)
#define CORTEXM_HFSR_FORCED		(1 << 30)
/* Bits 29:2 - Not specified */
#define CORTEXM_HFSR_VECTTBL		(1 << 1)
/* Bits 0 - Reserved */

/* Debug Fault Status Register (DFSR) */
/* Bits 31:5 - Reserved */
#define CORTEXM_DFSR_RESETALL		0x1F
#define CORTEXM_DFSR_EXTERNAL		(1 << 4)
#define CORTEXM_DFSR_VCATCH		(1 << 3)
#define CORTEXM_DFSR_DWTTRAP		(1 << 2)
#define CORTEXM_DFSR_BKPT		(1 << 1)
#define CORTEXM_DFSR_HALTED		(1 << 0)

/* Debug Halting Control and Status Register (DHCSR) */
/* This key must be written to bits 31:16 for write to take effect */
#define CORTEXM_DHCSR_DBGKEY		0xA05F0000
/* Bits 31:26 - Reserved */
#define CORTEXM_DHCSR_S_RESET_ST	(1 << 25)
#define CORTEXM_DHCSR_S_RETIRE_ST	(1 << 24)
/* Bits 23:20 - Reserved */
#define CORTEXM_DHCSR_S_LOCKUP		(1 << 19)
#define CORTEXM_DHCSR_S_SLEEP		(1 << 18)
#define CORTEXM_DHCSR_S_HALT		(1 << 17)
#define CORTEXM_DHCSR_S_REGRDY		(1 << 16)
/* Bits 15:6 - Reserved */
#define CORTEXM_DHCSR_C_SNAPSTALL	(1 << 5)	/* v7m only */
/* Bit 4 - Reserved */
#define CORTEXM_DHCSR_C_MASKINTS	(1 << 3)
#define CORTEXM_DHCSR_C_STEP		(1 << 2)
#define CORTEXM_DHCSR_C_HALT		(1 << 1)
#define CORTEXM_DHCSR_C_DEBUGEN		(1 << 0)

/* Debug Core Register Selector Register (DCRSR) */
#define CORTEXM_DCRSR_REGWnR		0x00010000
#define CORTEXM_DCRSR_REGSEL_MASK	0x0000001F
#define CORTEXM_DCRSR_REGSEL_XPSR	0x00000010
#define CORTEXM_DCRSR_REGSEL_MSP	0x00000011
#define CORTEXM_DCRSR_REGSEL_PSP	0x00000012

/* Debug Exception and Monitor Control Register (DEMCR) */
/* Bits 31:25 - Reserved */
#define CORTEXM_DEMCR_TRCENA		(1 << 24)
/* Bits 23:20 - Reserved */
#define CORTEXM_DEMCR_MON_REQ		(1 << 19)	/* v7m only */
#define CORTEXM_DEMCR_MON_STEP		(1 << 18)	/* v7m only */
#define CORTEXM_DEMCR_VC_MON_PEND	(1 << 17)	/* v7m only */
#define CORTEXM_DEMCR_VC_MON_EN		(1 << 16)	/* v7m only */
/* Bits 15:11 - Reserved */
#define CORTEXM_DEMCR_VC_HARDERR	(1 << 10)
#define CORTEXM_DEMCR_VC_INTERR		(1 << 9)	/* v7m only */
#define CORTEXM_DEMCR_VC_BUSERR		(1 << 8)	/* v7m only */
#define CORTEXM_DEMCR_VC_STATERR	(1 << 7)	/* v7m only */
#define CORTEXM_DEMCR_VC_CHKERR		(1 << 6)	/* v7m only */
#define CORTEXM_DEMCR_VC_NOCPERR	(1 << 5)	/* v7m only */
#define CORTEXM_DEMCR_VC_MMERR		(1 << 4)	/* v7m only */
/* Bits 3:1 - Reserved */
#define CORTEXM_DEMCR_VC_CORERESET	(1 << 0)

/* Flash Patch and Breakpoint Control Register (FP_CTRL) */
/* Bits 32:15 - Reserved */
/* Bits 14:12 - NUM_CODE2 */	/* v7m only */
/* Bits 11:8 - NUM_LIT */	/* v7m only */
/* Bits 7:4 - NUM_CODE1 */
/* Bits 3:2 - Unspecified */
#define CORTEXM_FPB_CTRL_KEY		(1 << 1)
#define CORTEXM_FPB_CTRL_ENABLE		(1 << 0)

/* Data Watchpoint and Trace Mask Register (DWT_MASKx) */
#define CORTEXM_DWT_MASK_BYTE		(0 << 0)
#define CORTEXM_DWT_MASK_HALFWORD	(1 << 0)
#define CORTEXM_DWT_MASK_WORD		(3 << 0)

/* Data Watchpoint and Trace Function Register (DWT_FUNCTIONx) */
#define CORTEXM_DWT_FUNC_MATCHED	(1 << 24)
#define CORTEXM_DWT_FUNC_DATAVSIZE_WORD	(2 << 10)	/* v7m only */
#define CORTEXM_DWT_FUNC_FUNC_READ	(5 << 0)
#define CORTEXM_DWT_FUNC_FUNC_WRITE	(6 << 0)
#define CORTEXM_DWT_FUNC_FUNC_ACCESS	(7 << 0)

/* Signals returned by cortexm_halt_wait() */
#define SIGINT 2
#define SIGTRAP 5
#define SIGSEGV 11

static bool cortexm_attach(struct target_s *target);
static void cortexm_detach(struct target_s *target);

static int cortexm_regs_read(struct target_s *target, void *data);
static int cortexm_regs_write(struct target_s *target, const void *data);
static int cortexm_pc_write(struct target_s *target, const uint32_t val);

static void cortexm_reset(struct target_s *target);
static void cortexm_halt_resume(struct target_s *target, bool step);
static int cortexm_halt_wait(struct target_s *target);
static void cortexm_halt_request(struct target_s *target);
static int cortexm_fault_unwind(struct target_s *target);

static int cortexm_set_hw_bp(struct target_s *target, uint32_t addr);
static int cortexm_clear_hw_bp(struct target_s *target, uint32_t addr);

static int cortexm_set_hw_wp(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len);
static int cortexm_clear_hw_wp(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len);

static int cortexm_check_hw_wp(struct target_s *target, uint32_t *addr);

#define CORTEXM_MAX_WATCHPOINTS	4	/* architecture says up to 15, no implementation has > 4 */
#define CORTEXM_MAX_BREAKPOINTS	6	/* architecture says up to 127, no implementation has > 6 */

static int cortexm_hostio_request(target *t);
static void cortexm_hostio_reply(target *t, int32_t retcode, uint32_t errcode);

struct cortexm_priv {
	bool stepping;
	bool on_bkpt;
	/* Watchpoint unit status */
	struct wp_unit_s {
		uint32_t addr;
		uint8_t type;
		uint8_t size;
	} hw_watchpoint[CORTEXM_MAX_WATCHPOINTS];
	unsigned hw_watchpoint_max;
	/* Breakpoint unit status */
	uint32_t hw_breakpoint[CORTEXM_MAX_BREAKPOINTS];
	unsigned hw_breakpoint_max;
	/* Copy of DEMCR for vector-catch */
	uint32_t demcr;
	/* Semihosting state */
	uint32_t syscall;
	uint32_t errno;
	uint32_t byte_count;
};

/* Register number tables */
static const uint32_t regnum_cortex_m[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,	/* standard r0-r15 */
	0x10,	/* xpsr */
	0x11,	/* msp */
	0x12,	/* psp */
	0x14	/* special */
};

static const uint32_t regnum_cortex_mf[] = {
	0x21,	/* fpscr */
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,	/* s0-s7 */
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,	/* s8-s15 */
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,	/* s16-s23 */
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,	/* s24-s31 */
};

static const char tdesc_cortex_m[] =
	"<?xml version=\"1.0\"?>"
	"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
	"<target>"
	"  <architecture>arm</architecture>"
	"  <feature name=\"org.gnu.gdb.arm.m-profile\">"
	"    <reg name=\"r0\" bitsize=\"32\"/>"
	"    <reg name=\"r1\" bitsize=\"32\"/>"
	"    <reg name=\"r2\" bitsize=\"32\"/>"
	"    <reg name=\"r3\" bitsize=\"32\"/>"
	"    <reg name=\"r4\" bitsize=\"32\"/>"
	"    <reg name=\"r5\" bitsize=\"32\"/>"
	"    <reg name=\"r6\" bitsize=\"32\"/>"
	"    <reg name=\"r7\" bitsize=\"32\"/>"
	"    <reg name=\"r8\" bitsize=\"32\"/>"
	"    <reg name=\"r9\" bitsize=\"32\"/>"
	"    <reg name=\"r10\" bitsize=\"32\"/>"
	"    <reg name=\"r11\" bitsize=\"32\"/>"
	"    <reg name=\"r12\" bitsize=\"32\"/>"
	"    <reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\"/>"
	"    <reg name=\"lr\" bitsize=\"32\" type=\"code_ptr\"/>"
	"    <reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>"
	"    <reg name=\"xpsr\" bitsize=\"32\"/>"
	"    <reg name=\"msp\" bitsize=\"32\" save-restore=\"no\" type=\"data_ptr\"/>"
	"    <reg name=\"psp\" bitsize=\"32\" save-restore=\"no\" type=\"data_ptr\"/>"
	"    <reg name=\"special\" bitsize=\"32\" save-restore=\"no\"/>"
	"  </feature>"
	"</target>";

static const char tdesc_cortex_mf[] =
	"<?xml version=\"1.0\"?>"
	"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
	"<target>"
	"  <architecture>arm</architecture>"
	"  <feature name=\"org.gnu.gdb.arm.m-profile\">"
	"    <reg name=\"r0\" bitsize=\"32\"/>"
	"    <reg name=\"r1\" bitsize=\"32\"/>"
	"    <reg name=\"r2\" bitsize=\"32\"/>"
	"    <reg name=\"r3\" bitsize=\"32\"/>"
	"    <reg name=\"r4\" bitsize=\"32\"/>"
	"    <reg name=\"r5\" bitsize=\"32\"/>"
	"    <reg name=\"r6\" bitsize=\"32\"/>"
	"    <reg name=\"r7\" bitsize=\"32\"/>"
	"    <reg name=\"r8\" bitsize=\"32\"/>"
	"    <reg name=\"r9\" bitsize=\"32\"/>"
	"    <reg name=\"r10\" bitsize=\"32\"/>"
	"    <reg name=\"r11\" bitsize=\"32\"/>"
	"    <reg name=\"r12\" bitsize=\"32\"/>"
	"    <reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\"/>"
	"    <reg name=\"lr\" bitsize=\"32\" type=\"code_ptr\"/>"
	"    <reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>"
	"    <reg name=\"xpsr\" bitsize=\"32\"/>"
	"    <reg name=\"msp\" bitsize=\"32\" save-restore=\"no\" type=\"data_ptr\"/>"
	"    <reg name=\"psp\" bitsize=\"32\" save-restore=\"no\" type=\"data_ptr\"/>"
	"    <reg name=\"special\" bitsize=\"32\" save-restore=\"no\"/>"
	"  </feature>"
	"  <feature name=\"org.gnu.gdb.arm.vfp\">"
	"    <reg name=\"fpscr\" bitsize=\"32\"/>"
	"    <reg name=\"d0\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d1\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d2\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d3\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d4\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d5\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d6\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d7\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d8\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d9\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d10\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d11\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d12\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d13\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d14\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d15\" bitsize=\"64\" type=\"float\"/>"
	"  </feature>"
	"</target>";

#define REG_SP		13
#define REG_LR		14
#define REG_PC		15
#define REG_XPSR	16
#define REG_MSP		17
#define REG_PSP		18
#define REG_SPECIAL	19

bool
cortexm_probe(struct target_s *target)
{
	target->driver = cortexm_driver_str;

	target->attach = cortexm_attach;
	target->detach = cortexm_detach;

	/* Should probe here to make sure it's Cortex-M3 */
	target->tdesc = tdesc_cortex_m;
	target->regs_read = cortexm_regs_read;
	target->regs_write = cortexm_regs_write;
	target->pc_write = cortexm_pc_write;

	target->reset = cortexm_reset;
	target->halt_request = cortexm_halt_request;
	target->halt_wait = cortexm_halt_wait;
	target->halt_resume = cortexm_halt_resume;
	target->regs_size = sizeof(regnum_cortex_m);

	target->hostio_reply = cortexm_hostio_reply;

	target_add_commands(target, cortexm_cmd_list, cortexm_driver_str);

	/* Probe for FP extension */
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t cpacr = adiv5_ap_mem_read(ap, CORTEXM_CPACR);
	cpacr |= 0x00F00000; /* CP10 = 0b11, CP11 = 0b11 */
	adiv5_ap_mem_write(ap, CORTEXM_CPACR, cpacr);
	if (adiv5_ap_mem_read(ap, CORTEXM_CPACR) == cpacr) {
		target->target_options |= TOPT_FLAVOUR_V7MF;
		target->regs_size += sizeof(regnum_cortex_mf);
		target->tdesc = tdesc_cortex_mf;
	}

	struct cortexm_priv *priv = calloc(1, sizeof(*priv));
	ap->priv = priv;
	ap->priv_free = free;

	/* Default vectors to catch */
	priv->demcr = CORTEXM_DEMCR_TRCENA | CORTEXM_DEMCR_VC_HARDERR |
			CORTEXM_DEMCR_VC_CORERESET;

#define PROBE(x) \
	do { if ((x)(target)) return true; else target_check_error(target); } while (0)

	PROBE(stm32f1_probe);
	PROBE(stm32f4_probe);
	PROBE(stm32l1_probe);
	PROBE(lpc11xx_probe);
	PROBE(lpc43xx_probe);
	PROBE(sam3x_probe);
	PROBE(nrf51_probe);
	PROBE(lmi_probe);
#undef PROBE

	return true;
}

static bool
cortexm_attach(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	struct cortexm_priv *priv = ap->priv;
	unsigned i;
	uint32_t r;
	int tries;

	/* Clear any pending fault condition */
	target_check_error(target);

	target_halt_request(target);
	tries = 10;
	while(!connect_assert_srst && !target_halt_wait(target) && --tries)
		platform_delay(2);
	if(!tries)
		return false;

	/* Request halt on reset */
	adiv5_ap_mem_write(ap, CORTEXM_DEMCR, priv->demcr);

	/* Reset DFSR flags */
	adiv5_ap_mem_write(ap, CORTEXM_DFSR, CORTEXM_DFSR_RESETALL);

	/* size the break/watchpoint units */
	priv->hw_breakpoint_max = CORTEXM_MAX_BREAKPOINTS;
	r = adiv5_ap_mem_read(ap, CORTEXM_FPB_CTRL);
	if (((r >> 4) & 0xf) < priv->hw_breakpoint_max)	/* only look at NUM_COMP1 */
		priv->hw_breakpoint_max = (r >> 4) & 0xf;
	priv->hw_watchpoint_max = CORTEXM_MAX_WATCHPOINTS;
	r = adiv5_ap_mem_read(ap, CORTEXM_DWT_CTRL);
	if ((r >> 28) > priv->hw_watchpoint_max)
		priv->hw_watchpoint_max = r >> 28;

	/* Clear any stale breakpoints */
	for(i = 0; i < priv->hw_breakpoint_max; i++) {
		adiv5_ap_mem_write(ap, CORTEXM_FPB_COMP(i), 0);
		priv->hw_breakpoint[i] = 0;
	}

	/* Clear any stale watchpoints */
	for(i = 0; i < priv->hw_watchpoint_max; i++) {
		adiv5_ap_mem_write(ap, CORTEXM_DWT_FUNC(i), 0);
		priv->hw_watchpoint[i].type = 0;
	}

	/* Flash Patch Control Register: set ENABLE */
	adiv5_ap_mem_write(ap, CORTEXM_FPB_CTRL,
			CORTEXM_FPB_CTRL_KEY | CORTEXM_FPB_CTRL_ENABLE);
	target->set_hw_bp = cortexm_set_hw_bp;
	target->clear_hw_bp = cortexm_clear_hw_bp;

	/* Data Watchpoint and Trace */
	target->set_hw_wp = cortexm_set_hw_wp;
	target->clear_hw_wp = cortexm_clear_hw_wp;
	target->check_hw_wp = cortexm_check_hw_wp;

	if(connect_assert_srst)
		jtagtap_srst(false);

	return true;
}

static void
cortexm_detach(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	struct cortexm_priv *priv = ap->priv;
	unsigned i;

	/* Clear any stale breakpoints */
	for(i = 0; i < priv->hw_breakpoint_max; i++)
		adiv5_ap_mem_write(ap, CORTEXM_FPB_COMP(i), 0);

	/* Clear any stale watchpoints */
	for(i = 0; i < priv->hw_watchpoint_max; i++)
		adiv5_ap_mem_write(ap, CORTEXM_DWT_FUNC(i), 0);

	/* Disable debug */
	adiv5_ap_mem_write(ap, CORTEXM_DHCSR, CORTEXM_DHCSR_DBGKEY);
}

static int
cortexm_regs_read(struct target_s *target, void *data)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t *regs = data;
	unsigned i;

	/* FIXME: Describe what's really going on here */
	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_WORD | ADIV5_AP_CSW_ADDRINC_SINGLE);

	/* Map the banked data registers (0x10-0x1c) to the
	 * debug registers DHCSR, DCRSR, DCRDR and DEMCR respectively */
	adiv5_dp_low_access(ap->dp, 1, 0, ADIV5_AP_TAR, CORTEXM_DHCSR);

	/* Walk the regnum_cortex_m array, reading the registers it
	 * calls out. */
	adiv5_ap_write(ap, ADIV5_AP_DB(1), regnum_cortex_m[0]); /* Required to switch banks */
	*regs++ = adiv5_dp_read_ap(ap->dp, ADIV5_AP_DB(2));
	for(i = 1; i < sizeof(regnum_cortex_m) / 4; i++) {
		adiv5_dp_low_access(ap->dp, 1, 0, ADIV5_AP_DB(1), regnum_cortex_m[i]);
		*regs++ = adiv5_dp_read_ap(ap->dp, ADIV5_AP_DB(2));
	}
	if (target->target_options & TOPT_FLAVOUR_V7MF)
		for(i = 0; i < sizeof(regnum_cortex_mf) / 4; i++) {
			adiv5_dp_low_access(ap->dp, 1, 0, ADIV5_AP_DB(1), regnum_cortex_mf[i]);
			*regs++ = adiv5_dp_read_ap(ap->dp, ADIV5_AP_DB(2));
		}

	return 0;
}

static int
cortexm_regs_write(struct target_s *target, const void *data)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	const uint32_t *regs = data;
	unsigned i;

	/* FIXME: Describe what's really going on here */
	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw |
		ADIV5_AP_CSW_SIZE_WORD | ADIV5_AP_CSW_ADDRINC_SINGLE);

	/* Map the banked data registers (0x10-0x1c) to the
	 * debug registers DHCSR, DCRSR, DCRDR and DEMCR respectively */
	adiv5_dp_low_access(ap->dp, 1, 0, ADIV5_AP_TAR, CORTEXM_DHCSR);

	/* Walk the regnum_cortex_m array, writing the registers it
	 * calls out. */
	adiv5_ap_write(ap, ADIV5_AP_DB(2), *regs++); /* Required to switch banks */
	adiv5_dp_low_access(ap->dp, 1, 0, ADIV5_AP_DB(1), 0x10000 | regnum_cortex_m[0]);
	for(i = 1; i < sizeof(regnum_cortex_m) / 4; i++) {
		adiv5_dp_low_access(ap->dp, 1, 0, ADIV5_AP_DB(2), *regs++);
		adiv5_dp_low_access(ap->dp, 1, 0, ADIV5_AP_DB(1),
					0x10000 | regnum_cortex_m[i]);
	}
	if (target->target_options & TOPT_FLAVOUR_V7MF)
		for(i = 0; i < sizeof(regnum_cortex_mf) / 4; i++) {
			adiv5_dp_low_access(ap->dp, 1, 0, ADIV5_AP_DB(2), *regs++);
			adiv5_dp_low_access(ap->dp, 1, 0, ADIV5_AP_DB(1),
						0x10000 | regnum_cortex_mf[i]);
		}

	return 0;
}

static uint32_t
cortexm_pc_read(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	adiv5_ap_mem_write(ap, CORTEXM_DCRSR, 0x0F);
	return adiv5_ap_mem_read(ap, CORTEXM_DCRDR);

	return 0;
}

static int
cortexm_pc_write(struct target_s *target, const uint32_t val)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	adiv5_ap_mem_write(ap, CORTEXM_DCRDR, val);
	adiv5_ap_mem_write(ap, CORTEXM_DCRSR, CORTEXM_DCRSR_REGWnR | 0x0F);

	return 0;
}

/* The following three routines implement target halt/resume
 * using the core debug registers in the NVIC. */
static void
cortexm_reset(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	jtagtap_srst(true);
	jtagtap_srst(false);

	/* Read DHCSR here to clear S_RESET_ST bit before reset */
	adiv5_ap_mem_read(ap, CORTEXM_DHCSR);

	/* Request system reset from NVIC: SRST doesn't work correctly */
	/* This could be VECTRESET: 0x05FA0001 (reset only core)
	 *          or SYSRESETREQ: 0x05FA0004 (system reset)
	 */
	adiv5_ap_mem_write(ap, CORTEXM_AIRCR,
			CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_SYSRESETREQ);

	/* Poll for release from reset */
	while(adiv5_ap_mem_read(ap, CORTEXM_DHCSR) & CORTEXM_DHCSR_S_RESET_ST);

	/* Reset DFSR flags */
	adiv5_ap_mem_write(ap, CORTEXM_DFSR, CORTEXM_DFSR_RESETALL);
}

static void
cortexm_halt_request(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	ap->dp->allow_timeout = false;
	adiv5_ap_mem_write(ap, CORTEXM_DHCSR,
		CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_HALT | CORTEXM_DHCSR_C_DEBUGEN);
}

static int
cortexm_halt_wait(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	struct cortexm_priv *priv = ap->priv;
	if (!(adiv5_ap_mem_read(ap, CORTEXM_DHCSR) & CORTEXM_DHCSR_S_HALT))
		return 0;

	ap->dp->allow_timeout = false;

	/* We've halted.  Let's find out why. */
	uint32_t dfsr = adiv5_ap_mem_read(ap, CORTEXM_DFSR);
	adiv5_ap_mem_write(ap, CORTEXM_DFSR, dfsr); /* write back to reset */

	if ((dfsr & CORTEXM_DFSR_VCATCH) && cortexm_fault_unwind(target))
		return SIGSEGV;

	/* Remember if we stopped on a breakpoint */
	priv->on_bkpt = dfsr & (CORTEXM_DFSR_BKPT);
	if (priv->on_bkpt) {
		/* If we've hit a programmed breakpoint, check for semihosting
		 * call. */
		uint32_t pc = cortexm_pc_read(target);
		uint16_t bkpt_instr;
		target_mem_read_bytes(target, (uint8_t *)&bkpt_instr, pc, 2);
		if (bkpt_instr == 0xBEAB) {
			int n = cortexm_hostio_request(target);
			if (n > 0) {
				target_halt_resume(target, priv->stepping);
				return 0;
			} else if (n < 0) {
				return -1;
			}
		}
	}

	if (dfsr & (CORTEXM_DFSR_BKPT | CORTEXM_DFSR_DWTTRAP))
		return SIGTRAP;

	if (dfsr & CORTEXM_DFSR_HALTED)
		return priv->stepping ? SIGTRAP : SIGINT;

	return SIGTRAP;

}

static void
cortexm_halt_resume(struct target_s *target, bool step)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	struct cortexm_priv *priv = ap->priv;
	uint32_t dhcsr = CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_DEBUGEN;

	if(step) dhcsr |= CORTEXM_DHCSR_C_STEP | CORTEXM_DHCSR_C_MASKINTS;

	/* Disable interrupts while single stepping... */
	if(step != priv->stepping) {
		adiv5_ap_mem_write(ap, CORTEXM_DHCSR, dhcsr | CORTEXM_DHCSR_C_HALT);
		priv->stepping = step;
	}

	if (priv->on_bkpt) {
		uint32_t pc = cortexm_pc_read(target);
		if ((adiv5_ap_mem_read_halfword(ap, pc) & 0xFF00) == 0xBE00)
			cortexm_pc_write(target, pc + 2);
	}

	adiv5_ap_mem_write(ap, CORTEXM_DHCSR, dhcsr);
	ap->dp->allow_timeout = true;
}

static int cortexm_fault_unwind(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t hfsr = adiv5_ap_mem_read(ap, CORTEXM_HFSR);
	uint32_t cfsr = adiv5_ap_mem_read(ap, CORTEXM_CFSR);
	adiv5_ap_mem_write(ap, CORTEXM_HFSR, hfsr);/* write back to reset */
	adiv5_ap_mem_write(ap, CORTEXM_CFSR, cfsr);/* write back to reset */
	/* We check for FORCED in the HardFault Status Register or
	 * for a configurable fault to avoid catching core resets */
	if((hfsr & CORTEXM_HFSR_FORCED) || cfsr) {
		/* Unwind exception */
		uint32_t regs[target->regs_size / 4];
		uint32_t stack[8];
		uint32_t retcode, framesize;
		/* Read registers for post-exception stack pointer */
		target_regs_read(target, regs);
		/* save retcode currently in lr */
		retcode = regs[REG_LR];
		bool spsel = retcode & (1<<2);
		bool fpca = !(retcode & (1<<4));
		/* Read stack for pre-exception registers */
		uint32_t sp = spsel ? regs[REG_PSP] : regs[REG_MSP];
		target_mem_read_words(target, stack, sp, sizeof(stack));
		regs[REG_LR] = stack[5];	/* restore LR to pre-exception state */
		regs[REG_PC] = stack[6];	/* restore PC to pre-exception state */

		/* adjust stack to pop exception state */
		framesize = fpca ? 0x68 : 0x20;	/* check for basic vs. extended frame */
		if (stack[7] & (1<<9))				/* check for stack alignment fixup */
			framesize += 4;

		if (spsel) {
			regs[REG_SPECIAL] |= 0x4000000;
			regs[REG_SP] = regs[REG_PSP] += framesize;
		} else {
			regs[REG_SP] = regs[REG_MSP] += framesize;
		}

		if (fpca)
			regs[REG_SPECIAL] |= 0x2000000;

		/* FIXME: stack[7] contains xPSR when this is supported */
		/* although, if we caught the exception it will be unchanged */

		/* Reset exception state to allow resuming from restored
		 * state.
		 */
		adiv5_ap_mem_write(ap, CORTEXM_AIRCR,
				CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_VECTCLRACTIVE);

		/* Write pre-exception registers back to core */
		target_regs_write(target, regs);

		return 1;
	}
	return 0;
}

/* The following routines implement hardware breakpoints.
 * The Flash Patch and Breakpoint (FPB) system is used. */

static int
cortexm_set_hw_bp(struct target_s *target, uint32_t addr)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	struct cortexm_priv *priv = ap->priv;
	uint32_t val = addr & 0x1FFFFFFC;
	unsigned i;

	val |= (addr & 2)?0x80000000:0x40000000;
	val |= 1;

	for(i = 0; i < priv->hw_breakpoint_max; i++)
		if((priv->hw_breakpoint[i] & 1) == 0) break;

	if(i == priv->hw_breakpoint_max) return -1;

	priv->hw_breakpoint[i] = addr | 1;

	adiv5_ap_mem_write(ap, CORTEXM_FPB_COMP(i), val);

	return 0;
}

static int
cortexm_clear_hw_bp(struct target_s *target, uint32_t addr)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	struct cortexm_priv *priv = ap->priv;
	unsigned i;

	for(i = 0; i < priv->hw_breakpoint_max; i++)
		if((priv->hw_breakpoint[i] & ~1) == addr) break;

	if(i == priv->hw_breakpoint_max) return -1;

	priv->hw_breakpoint[i] = 0;

	adiv5_ap_mem_write(ap, CORTEXM_FPB_COMP(i), 0);

	return 0;
}


/* The following routines implement hardware watchpoints.
 * The Data Watch and Trace (DWT) system is used. */

static int
cortexm_set_hw_wp(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	struct cortexm_priv *priv = ap->priv;
	unsigned i;

	switch(len) { /* Convert bytes size to mask size */
		case 1: len = CORTEXM_DWT_MASK_BYTE; break;
		case 2: len = CORTEXM_DWT_MASK_HALFWORD; break;
		case 4: len = CORTEXM_DWT_MASK_WORD; break;
		default:
			return -1;
	}

	switch(type) { /* Convert gdb type to function type */
		case 2: type = CORTEXM_DWT_FUNC_FUNC_WRITE; break;
		case 3: type = CORTEXM_DWT_FUNC_FUNC_READ; break;
		case 4: type = CORTEXM_DWT_FUNC_FUNC_ACCESS; break;
		default:
			return -1;
	}

	for(i = 0; i < priv->hw_watchpoint_max; i++)
		if((priv->hw_watchpoint[i].type == 0) &&
		   ((adiv5_ap_mem_read(ap, CORTEXM_DWT_FUNC(i)) & 0xF) == 0))
			break;

	if(i == priv->hw_watchpoint_max) return -2;

	priv->hw_watchpoint[i].type = type;
	priv->hw_watchpoint[i].addr = addr;
	priv->hw_watchpoint[i].size = len;

	adiv5_ap_mem_write(ap, CORTEXM_DWT_COMP(i), addr);
	adiv5_ap_mem_write(ap, CORTEXM_DWT_MASK(i), len);
	adiv5_ap_mem_write(ap, CORTEXM_DWT_FUNC(i), type |
			((target->target_options & TOPT_FLAVOUR_V6M) ? 0: CORTEXM_DWT_FUNC_DATAVSIZE_WORD));

	return 0;
}

static int
cortexm_clear_hw_wp(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	struct cortexm_priv *priv = ap->priv;
	unsigned i;

	switch(len) {
		case 1: len = CORTEXM_DWT_MASK_BYTE; break;
		case 2: len = CORTEXM_DWT_MASK_HALFWORD; break;
		case 4: len = CORTEXM_DWT_MASK_WORD; break;
		default:
			return -1;
	}

	switch(type) {
		case 2: type = CORTEXM_DWT_FUNC_FUNC_WRITE; break;
		case 3: type = CORTEXM_DWT_FUNC_FUNC_READ; break;
		case 4: type = CORTEXM_DWT_FUNC_FUNC_ACCESS; break;
		default:
			return -1;
	}

	for(i = 0; i < priv->hw_watchpoint_max; i++)
		if((priv->hw_watchpoint[i].addr == addr) &&
		   (priv->hw_watchpoint[i].type == type) &&
		   (priv->hw_watchpoint[i].size == len)) break;

	if(i == priv->hw_watchpoint_max) return -2;

	priv->hw_watchpoint[i].type = 0;

	adiv5_ap_mem_write(ap, CORTEXM_DWT_FUNC(i), 0);

	return 0;
}

static int
cortexm_check_hw_wp(struct target_s *target, uint32_t *addr)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	struct cortexm_priv *priv = ap->priv;
	unsigned i;

	for(i = 0; i < priv->hw_watchpoint_max; i++)
		/* if SET and MATCHED then break */
		if(priv->hw_watchpoint[i].type &&
		   (adiv5_ap_mem_read(ap, CORTEXM_DWT_FUNC(i)) &
					CORTEXM_DWT_FUNC_MATCHED))
			break;

	if(i == priv->hw_watchpoint_max) return 0;

	*addr = priv->hw_watchpoint[i].addr;
	return 1;
}

static bool cortexm_vector_catch(target *t, int argc, char *argv[])
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);
	struct cortexm_priv *priv = ap->priv;
	const char *vectors[] = {"reset", NULL, NULL, NULL, "mm", "nocp",
				"chk", "stat", "bus", "int", "hard"};
	uint32_t tmp = 0;
	unsigned i, j;

	if ((argc < 3) || ((argv[1][0] != 'e') && (argv[1][0] != 'd'))) {
		gdb_out("usage: monitor vector_catch (enable|disable) "
			"(hard|int|bus|stat|chk|nocp|mm|reset)\n");
	} else {
		for (j = 0; j < argc; j++)
			for (i = 0; i < sizeof(vectors) / sizeof(char*); i++) {
				if (vectors[i] && !strcmp(vectors[i], argv[j]))
					tmp |= 1 << i;
			}

		if (argv[1][0] == 'e')
			priv->demcr |= tmp;
		else
			priv->demcr &= ~tmp;

		adiv5_ap_mem_write(ap, CORTEXM_DEMCR, priv->demcr);
	}

	gdb_out("Catching vectors: ");
	for (i = 0; i < sizeof(vectors) / sizeof(char*); i++) {
		if (!vectors[i])
			continue;
		if (priv->demcr & (1 << i))
			gdb_outf("%s ", vectors[i]);
	}
	gdb_out("\n");
	return true;
}


/* Semihosting support */
/* ARM Semihosting syscall numbers, from ARM doc DUI0471C, Chapter 8 */
#define SYS_CLOSE	0x02
#define SYS_CLOCK	0x10
#define SYS_ELAPSED	0x30
#define SYS_ERRNO	0x13
#define SYS_FLEN	0x0C
#define SYS_GET_CMDLINE	0x15
#define SYS_HEAPINFO	0x16
#define SYS_ISERROR	0x08
#define SYS_ISTTY	0x09
#define SYS_OPEN	0x01
#define SYS_READ	0x06
#define SYS_READC	0x07
#define SYS_REMOVE	0x0E
#define SYS_RENAME	0x0F
#define SYS_SEEK	0x0A
#define SYS_SYSTEM	0x12
#define SYS_TICKFREQ	0x31
#define SYS_TIME	0x11
#define SYS_TMPNAM	0x0D
#define SYS_WRITE	0x05
#define SYS_WRITEC	0x03
#define SYS_WRITE0	0x04

#define FILEIO_O_RDONLY		0
#define FILEIO_O_WRONLY		1
#define FILEIO_O_RDWR		2
#define FILEIO_O_APPEND		0x008
#define FILEIO_O_CREAT		0x200
#define FILEIO_O_TRUNC		0x400

#define FILEIO_SEEK_SET		0
#define FILEIO_SEEK_CUR		1
#define FILEIO_SEEK_END		2

static int cortexm_hostio_request(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);
	struct cortexm_priv *priv = ap->priv;
	uint32_t arm_regs[t->regs_size];
	uint32_t params[4];

	target_regs_read(t, arm_regs);
	target_mem_read_words(t, params, arm_regs[1], sizeof(params));
	priv->syscall = arm_regs[0];

	DEBUG("syscall 0x%x (%x %x %x %x)\n", priv->syscall,
			params[0], params[1], params[2], params[3]);
	switch (priv->syscall) {
	case SYS_OPEN:{	/* open */
		/* Translate stupid fopen modes to open flags.
		 * See DUI0471C, Table 8-3 */
                const uint32_t flags[] = {
			FILEIO_O_RDONLY,	/* r, rb */
			FILEIO_O_RDWR,		/* r+, r+b */
			FILEIO_O_WRONLY | FILEIO_O_CREAT | FILEIO_O_TRUNC,/*w*/
			FILEIO_O_RDWR | FILEIO_O_CREAT | FILEIO_O_TRUNC,/*w+*/
			FILEIO_O_WRONLY | FILEIO_O_CREAT | FILEIO_O_APPEND,/*a*/
			FILEIO_O_RDWR | FILEIO_O_CREAT | FILEIO_O_APPEND,/*a+*/
		};
		uint32_t pflag = flags[params[1] >> 1];
		char filename[4];

		target_mem_read_bytes(t, filename, params[0], sizeof(filename));
		/* handle requests for console i/o */
		if (!strcmp(filename, ":tt")) {
			if (pflag == FILEIO_O_RDONLY)
				arm_regs[0] = STDIN_FILENO;
			else if (pflag & FILEIO_O_TRUNC)
				arm_regs[0] = STDOUT_FILENO;
			else
				arm_regs[0] = STDERR_FILENO;
			arm_regs[0]++;
			target_regs_write(t, arm_regs);
			return 1;
		}

		gdb_putpacket_f("Fopen,%08X/%X,%08X,%08X",
				params[0], params[2] + 1,
				pflag, 0644);
		break;
		}
	case SYS_CLOSE:	/* close */
		gdb_putpacket_f("Fclose,%08X", params[0] - 1);
		break;
	case SYS_READ:	/* read */
		priv->byte_count = params[2];
		gdb_putpacket_f("Fread,%08X,%08X,%08X",
				params[0] - 1, params[1], params[2]);
		break;
	case SYS_WRITE:	/* write */
		priv->byte_count = params[2];
		gdb_putpacket_f("Fwrite,%08X,%08X,%08X",
				params[0] - 1, params[1], params[2]);
		break;
	case SYS_WRITEC: /* writec */
		gdb_putpacket_f("Fwrite,2,%08X,1", arm_regs[1]);
		break;
	case SYS_ISTTY:	/* isatty */
		gdb_putpacket_f("Fisatty,%08X", params[0] - 1);
		break;
	case SYS_SEEK:	/* lseek */
		gdb_putpacket_f("Flseek,%08X,%08X,%08X",
				params[0] - 1, params[1], FILEIO_SEEK_SET);
		break;
	case SYS_RENAME:/* rename */
		gdb_putpacket_f("Frename,%08X/%X,%08X/%X",
				params[0] - 1, params[1] + 1,
				params[2], params[3] + 1);
		break;
	case SYS_REMOVE:/* unlink */
		gdb_putpacket_f("Funlink,%08X/%X", params[0] - 1,
				params[1] + 1);
		break;
	case SYS_SYSTEM:/* system */
		gdb_putpacket_f("Fsystem,%08X/%X", params[0] - 1,
				params[1] + 1);
		break;

	case SYS_FLEN:	/* Not supported, fake success */
		priv->errno = 0;
		return 1;

	case SYS_ERRNO: /* Return last errno from GDB */
		arm_regs[0] = priv->errno;
		target_regs_write(t, arm_regs);
		return 1;

	case SYS_TIME:	/* gettimeofday */
		/* FIXME How do we use gdb's gettimeofday? */
	default:
		return 0;
	}

	return -1;
}

static void cortexm_hostio_reply(target *t, int32_t retcode, uint32_t errcode)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);
	struct cortexm_priv *priv = ap->priv;
	uint32_t arm_regs[t->regs_size];

	DEBUG("syscall return ret=%d errno=%d\n", retcode, errcode);
	target_regs_read(t, arm_regs);
	if (((priv->syscall == SYS_READ) || (priv->syscall == SYS_WRITE)) &&
	    (retcode > 0))
		retcode = priv->byte_count - retcode;
	if ((priv->syscall == SYS_OPEN) && (retcode != -1))
		retcode++;
	arm_regs[0] = retcode;
	target_regs_write(t, arm_regs);
	priv->errno = errcode;
}

