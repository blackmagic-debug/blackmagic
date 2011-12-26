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
#include <stdio.h>

#include "general.h"
#include "jtagtap.h"
#include "jtag_scan.h"
#include "adiv5.h"
#include "target.h"

#include "cortexm3.h"
#include "lmi.h"
#include "stm32_tgt.h"
#include "nxp_tgt.h"

static char cm3_driver_str[] = "ARM Cortex-M3";

/* Private peripheral bus base address */
#define CM3_PPB_BASE	0xE0000000

#define CM3_SCS_BASE	(CM3_PPB_BASE + 0xE000)

#define CM3_AIRCR	(CM3_SCS_BASE + 0xD0C)
#define CM3_CFSR	(CM3_SCS_BASE + 0xD28)
#define CM3_HFSR	(CM3_SCS_BASE + 0xD2C)
#define CM3_DFSR	(CM3_SCS_BASE + 0xD30)
#define CM3_DHCSR	(CM3_SCS_BASE + 0xDF0)
#define CM3_DCRSR	(CM3_SCS_BASE + 0xDF4)
#define CM3_DCRDR	(CM3_SCS_BASE + 0xDF8)
#define CM3_DEMCR	(CM3_SCS_BASE + 0xDFC)

#define CM3_FPB_BASE	(CM3_PPB_BASE + 0x2000)

/* ARM Literature uses FP_*, we use CM3_FPB_* consistently */
#define CM3_FPB_CTRL	(CM3_FPB_BASE + 0x000)
#define CM3_FPB_REMAP	(CM3_FPB_BASE + 0x004)
#define CM3_FPB_COMP(i)	(CM3_FPB_BASE + 0x008 + (4*(i)))

#define CM3_DWT_BASE	(CM3_PPB_BASE + 0x1000)

#define CM3_DWT_CTRL	(CM3_DWT_BASE + 0x000)
#define CM3_DWT_COMP(i)	(CM3_DWT_BASE + 0x020 + (0x10*(i)))
#define CM3_DWT_MASK(i)	(CM3_DWT_BASE + 0x024 + (0x10*(i)))
#define CM3_DWT_FUNC(i)	(CM3_DWT_BASE + 0x028 + (0x10*(i)))

/* Application Interrupt and Reset Control Register (AIRCR) */
#define CM3_AIRCR_VECTKEY	(0x05FA << 16)
/* Bits 31:16 - Read as VECTKETSTAT, 0xFA05 */
#define CM3_AIRCR_ENDIANESS	(1 << 15)
/* Bits 15:11 - Unused, reserved */
#define CM3_AIRCR_PRIGROUP	(7 << 8)
/* Bits 7:3 - Unused, reserved */
#define CM3_AIRCR_SYSRESETREQ	(1 << 2)
#define CM3_AIRCR_VECTCLRACTIVE	(1 << 1)
#define CM3_AIRCR_VECTRESET	(1 << 0)

/* HardFault Status Register (HFSR) */
#define CM3_HFSR_DEBUGEVT	(1 << 31)
#define CM3_HFSR_FORCED		(1 << 30)
/* Bits 29:2 - Not specified */
#define CM3_HFSR_VECTTBL	(1 << 1)
/* Bits 0 - Reserved */

/* Debug Fault Status Register (DFSR) */
/* Bits 31:5 - Reserved */
#define CM3_DFSR_RESETALL	0x1F
#define CM3_DFSR_EXTERNAL	(1 << 4)
#define CM3_DFSR_VCATCH		(1 << 3)
#define CM3_DFSR_DWTTRAP	(1 << 2)
#define CM3_DFSR_BKPT		(1 << 1)
#define CM3_DFSR_HALTED		(1 << 0)

/* Debug Halting Control and Status Register (DHCSR) */
/* This key must be written to bits 31:16 for write to take effect */
#define CM3_DHCSR_DBGKEY	0xA05F0000
/* Bits 31:26 - Reserved */
#define CM3_DHCSR_S_RESET_ST	(1 << 25)
#define CM3_DHCSR_S_RETIRE_ST	(1 << 24)
/* Bits 23:20 - Reserved */
#define CM3_DHCSR_S_LOCKUP	(1 << 19)
#define CM3_DHCSR_S_SLEEP	(1 << 18)
#define CM3_DHCSR_S_HALT	(1 << 17)
#define CM3_DHCSR_S_REGRDY	(1 << 16)
/* Bits 15:6 - Reserved */
#define CM3_DHCSR_C_SNAPSTALL	(1 << 5)	/* v7m only */
/* Bit 4 - Reserved */
#define CM3_DHCSR_C_MASKINTS	(1 << 3)
#define CM3_DHCSR_C_STEP	(1 << 2)
#define CM3_DHCSR_C_HALT	(1 << 1)
#define CM3_DHCSR_C_DEBUGEN	(1 << 0)

/* Debug Core Register Selector Register (DCRSR) */
#define CM3_DCRSR_REGSEL_MASK	0x0000001F
#define CM3_DCRSR_REGSEL_XPSR	0x00000010
#define CM3_DCRSR_REGSEL_MSP	0x00000011
#define CM3_DCRSR_REGSEL_PSP	0x00000012

/* Debug Exception and Monitor Control Register (DEMCR) */
/* Bits 31:25 - Reserved */
#define CM3_DEMCR_TRCENA	(1 << 24)
/* Bits 23:20 - Reserved */
#define CM3_DEMCR_MON_REQ	(1 << 19)	/* v7m only */
#define CM3_DEMCR_MON_STEP	(1 << 18)	/* v7m only */
#define CM3_DEMCR_VC_MON_PEND	(1 << 17)	/* v7m only */
#define CM3_DEMCR_VC_MON_EN	(1 << 16)	/* v7m only */
/* Bits 15:11 - Reserved */
#define CM3_DEMCR_VC_HARDERR	(1 << 10)
#define CM3_DEMCR_VC_INTERR	(1 << 9)	/* v7m only */
#define CM3_DEMCR_VC_BUSERR	(1 << 8)	/* v7m only */
#define CM3_DEMCR_VC_STATERR	(1 << 7)	/* v7m only */
#define CM3_DEMCR_VC_CHKERR	(1 << 6)	/* v7m only */
#define CM3_DEMCR_VC_NOCPERR	(1 << 5)	/* v7m only */
#define CM3_DEMCR_VC_MMERR	(1 << 4)	/* v7m only */
/* Bits 3:1 - Reserved */
#define CM3_DEMCR_VC_CORERESET	(1 << 0)

/* Flash Patch and Breakpoint Control Register (FP_CTRL) */
/* Bits 32:15 - Reserved */
/* Bits 14:12 - NUM_CODE2 */	/* v7m only */
/* Bits 11:8 - NUM_LIT */	/* v7m only */
/* Bits 7:4 - NUM_CODE1 */
/* Bits 3:2 - Unspecified */
#define CM3_FPB_CTRL_KEY	(1 << 1)
#define CM3_FPB_CTRL_ENABLE	(1 << 0)

/* Data Watchpoint and Trace Mask Register (DWT_MASKx) */
#define CM3_DWT_MASK_BYTE	(0 << 0)
#define CM3_DWT_MASK_HALFWORD	(1 << 0)
#define CM3_DWT_MASK_WORD	(3 << 0)

/* Data Watchpoint and Trace Function Register (DWT_FUNCTIONx) */
#define CM3_DWT_FUNC_MATCHED		(1 << 24)
#define CM3_DWT_FUNC_DATAVSIZE_WORD	(2 << 10)	/* v7m only */
#define CM3_DWT_FUNC_FUNC_READ		(5 << 0)
#define CM3_DWT_FUNC_FUNC_WRITE		(6 << 0)
#define CM3_DWT_FUNC_FUNC_ACCESS	(7 << 0)

static void cm3_attach(struct target_s *target);
static void cm3_detach(struct target_s *target);

static int cm3_regs_read(struct target_s *target, void *data);
static int cm3_regs_write(struct target_s *target, const void *data);
static int cm3_pc_write(struct target_s *target, const uint32_t val);

static void cm3_reset(struct target_s *target);
static void cm3_halt_resume(struct target_s *target, uint8_t step);
static int cm3_halt_wait(struct target_s *target);
static void cm3_halt_request(struct target_s *target);
static int cm3_fault_unwind(struct target_s *target);

static int cm3_set_hw_bp(struct target_s *target, uint32_t addr);
static int cm3_clear_hw_bp(struct target_s *target, uint32_t addr);

static int cm3_set_hw_wp(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len);
static int cm3_clear_hw_wp(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len);

static int cm3_check_hw_wp(struct target_s *target, uint32_t *addr);

/* Watchpoint unit status */
#define CM3_MAX_WATCHPOINTS	4	/* architecture says up to 15, no implementation has > 4 */
static struct wp_unit_s {
	uint32_t addr;
	uint8_t type;
	uint8_t size;
} hw_watchpoint[CM3_MAX_WATCHPOINTS];
static unsigned hw_watchpoint_max;

/* Breakpoint unit status */
#define CM3_MAX_BREAKPOINTS	6	/* architecture says up to 127, no implementation has > 6 */
static uint32_t hw_breakpoint[CM3_MAX_BREAKPOINTS];
static unsigned hw_breakpoint_max;

/* Register number tables */
static uint32_t regnum_cortex_m[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,	/* standard r0-r15 */
	0x10,	/* xpsr */
	0x11,	/* msp */
	0x12,	/* psp */
	0x14	/* special */
};
#if 0
/* XXX: need some way for a specific CPU to indicate it has FP registers */
static uint32_t regnum_cortex_mf[] = {
	0x21,	/* fpscr */
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,	/* s0-s7 */
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,	/* s8-s15 */
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,	/* s16-s23 */
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,	/* s24-s31 */
};
#endif

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

int
cm3_probe(struct target_s *target)
{
	target->driver = cm3_driver_str;

	target->attach = cm3_attach;
	target->detach = cm3_detach;

	/* Should probe here to make sure it's Cortex-M3 */
	target->tdesc = tdesc_cortex_m;
	target->regs_read = cm3_regs_read;
	target->regs_write = cm3_regs_write;
	target->pc_write = cm3_pc_write;

	target->reset = cm3_reset;
	target->halt_request = cm3_halt_request;
	target->halt_wait = cm3_halt_wait;
	target->halt_resume = cm3_halt_resume;
	target->fault_unwind = cm3_fault_unwind;
	target->regs_size = sizeof(regnum_cortex_m);	/* XXX: detect FP extension */

	if(stm32_probe(target) == 0) return 0;
	if(stm32f4_probe(target) == 0) return 0;
	if(lpc11xx_probe(target) == 0) return 0;
	/* if not STM32 try LMI which I don't know how to detect reliably */
	lmi_probe(target);

	return 0;
}

static void
cm3_attach(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;
	unsigned i;
	uint32_t r;

	target_halt_request(target);
	while(!target_halt_wait(target));

	/* Request halt on reset */
	adiv5_ap_mem_write(t->ap, CM3_DEMCR, 
			CM3_DEMCR_TRCENA | CM3_DEMCR_VC_HARDERR | 
			CM3_DEMCR_VC_CORERESET); 

	/* Reset DFSR flags */
	adiv5_ap_mem_write(t->ap, CM3_DFSR, CM3_DFSR_RESETALL);

	/* size the break/watchpoint units */
	hw_breakpoint_max = CM3_MAX_BREAKPOINTS;
	r = adiv5_ap_mem_read(t->ap, CM3_FPB_CTRL);
	if (((r >> 4) & 0xf) < hw_breakpoint_max)	/* only look at NUM_COMP1 */
		hw_breakpoint_max = (r >> 4) & 0xf;
	hw_watchpoint_max = CM3_MAX_WATCHPOINTS;
	r = adiv5_ap_mem_read(t->ap, CM3_DWT_CTRL);
	if ((r >> 28) > hw_watchpoint_max)
		hw_watchpoint_max = r >> 28;

	/* Clear any stale breakpoints */
	for(i = 0; i < hw_breakpoint_max; i++) {
		adiv5_ap_mem_write(t->ap, CM3_FPB_COMP(i), 0);
		hw_breakpoint[i] = 0;
	}

	/* Clear any stale watchpoints */
	for(i = 0; i < hw_watchpoint_max; i++) {
		adiv5_ap_mem_write(t->ap, CM3_DWT_FUNC(i), 0);
		hw_watchpoint[i].type = 0;
	}

	/* Flash Patch Control Register: set ENABLE */
	adiv5_ap_mem_write(t->ap, CM3_FPB_CTRL, 
			CM3_FPB_CTRL_KEY | CM3_FPB_CTRL_ENABLE);
	target->set_hw_bp = cm3_set_hw_bp;
	target->clear_hw_bp = cm3_clear_hw_bp;

	/* Data Watchpoint and Trace */
	target->set_hw_wp = cm3_set_hw_wp;
	target->clear_hw_wp = cm3_clear_hw_wp;
	target->check_hw_wp = cm3_check_hw_wp;
}

static void
cm3_detach(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;
	unsigned i;

	/* Clear any stale breakpoints */
	for(i = 0; i < hw_breakpoint_max; i++)
		adiv5_ap_mem_write(t->ap, CM3_FPB_COMP(i), 0);

	/* Clear any stale watchpoints */
	for(i = 0; i < hw_watchpoint_max; i++) 
		adiv5_ap_mem_write(t->ap, CM3_DWT_FUNC(i), 0);

	/* Disable debug */
	adiv5_ap_mem_write(t->ap, CM3_DHCSR, CM3_DHCSR_DBGKEY);
}

static int
cm3_regs_read(struct target_s *target, void *data)
{
	struct target_ap_s *t = (void *)target;
	uint32_t *regs = data;
	unsigned i;

	/* FIXME: Describe what's really going on here */
	adiv5_ap_write(t->ap, ADIV5_AP_CSW, 0xA2000052);

	/* Map the banked data registers (0x10-0x1c) to the
	 * debug registers DHCSR, DCRSR, DCRDR and DEMCR respectively */
	adiv5_dp_low_access(t->ap->dp, 1, 0, ADIV5_AP_TAR, CM3_DHCSR);

	/* Walk the regnum_cortex_m array, reading the registers it 
	 * calls out. */
	adiv5_ap_write(t->ap, ADIV5_AP_DB(1), regnum_cortex_m[0]); /* Required to switch banks */
	*regs++ = adiv5_dp_read_ap(t->ap->dp, ADIV5_AP_DB(2));
	for(i = 1; i < sizeof(regnum_cortex_m) / 4; i++) {
		adiv5_dp_low_access(t->ap->dp, 1, 0, ADIV5_AP_DB(1), regnum_cortex_m[i]);
		*regs++ = adiv5_dp_read_ap(t->ap->dp, ADIV5_AP_DB(2));
	}

	return 0;
}

static int
cm3_regs_write(struct target_s *target, const void *data)
{
	struct target_ap_s *t = (void *)target;
	const uint32_t *regs = data;
	unsigned i;

	/* FIXME: Describe what's really going on here */
	adiv5_ap_write(t->ap, ADIV5_AP_CSW, 0xA2000052);

	/* Map the banked data registers (0x10-0x1c) to the
	 * debug registers DHCSR, DCRSR, DCRDR and DEMCR respectively */
	adiv5_dp_low_access(t->ap->dp, 1, 0, ADIV5_AP_TAR, CM3_DHCSR);

	/* Walk the regnum_cortex_m array, writing the registers it 
	 * calls out. */
	adiv5_ap_write(t->ap, ADIV5_AP_DB(2), *regs++); /* Required to switch banks */
	adiv5_dp_low_access(t->ap->dp, 1, 0, ADIV5_AP_DB(1), 0x10000 | regnum_cortex_m[0]);
	for(i = 1; i < sizeof(regnum_cortex_m) / 4; i++) {
		adiv5_dp_low_access(t->ap->dp, 1, 0, ADIV5_AP_DB(2), *regs++);
		adiv5_dp_low_access(t->ap->dp, 1, 0, ADIV5_AP_DB(1), 
					0x10000 | regnum_cortex_m[i]);
	}

	return 0;
}

static int
cm3_pc_write(struct target_s *target, const uint32_t val)
{
	struct target_ap_s *t = (void *)target;

	adiv5_ap_write(t->ap, ADIV5_AP_CSW, 0xA2000052);
	adiv5_dp_low_access(t->ap->dp, 1, 0, ADIV5_AP_TAR, CM3_DHCSR);

	adiv5_ap_write(t->ap, ADIV5_AP_DB(2), val); /* Required to switch banks */
	adiv5_dp_low_access(t->ap->dp, 1, 0, ADIV5_AP_DB(1), 0x1000F);

	return 0;
}

/* The following three routines implement target halt/resume
 * using the core debug registers in the NVIC. */
static void 
cm3_reset(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;

	jtagtap_srst();

	/* Request system reset from NVIC: SRST doesn't work correctly */
	/* This could be VECTRESET: 0x05FA0001 (reset only core)
	 *          or SYSRESETREQ: 0x05FA0004 (system reset)
	 */
	adiv5_ap_mem_write(t->ap, CM3_AIRCR,
			CM3_AIRCR_VECTKEY | CM3_AIRCR_SYSRESETREQ);

	/* Poll for release from reset */
	while(adiv5_ap_mem_read(t->ap, CM3_AIRCR) & 
			(CM3_AIRCR_VECTRESET | CM3_AIRCR_SYSRESETREQ));

	/* Reset DFSR flags */
	adiv5_ap_mem_write(t->ap, CM3_DFSR, CM3_DFSR_RESETALL);
}

static void 
cm3_halt_request(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;

	adiv5_ap_mem_write(t->ap, CM3_DHCSR, 
		CM3_DHCSR_DBGKEY | CM3_DHCSR_C_HALT | CM3_DHCSR_C_DEBUGEN);
}

static int
cm3_halt_wait(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;

	return adiv5_ap_mem_read(t->ap, CM3_DHCSR) & CM3_DHCSR_S_HALT;
}

static void 
cm3_halt_resume(struct target_s *target, uint8_t step)
{
	struct target_ap_s *t = (void *)target;
	static uint8_t old_step = 0;
	uint32_t dhcsr = CM3_DHCSR_DBGKEY | CM3_DHCSR_C_DEBUGEN;

	if(step) dhcsr |= CM3_DHCSR_C_STEP | CM3_DHCSR_C_MASKINTS;

	/* Disable interrupts while single stepping... */
	if(step != old_step) {
		adiv5_ap_mem_write(t->ap, CM3_DHCSR, dhcsr | CM3_DHCSR_C_HALT);
		old_step = step;
	}

	adiv5_ap_mem_write(t->ap, CM3_DHCSR, dhcsr);
}

static int cm3_fault_unwind(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;
	uint32_t dfsr = adiv5_ap_mem_read(t->ap, CM3_DFSR);
	uint32_t hfsr = adiv5_ap_mem_read(t->ap, CM3_HFSR);
	uint32_t cfsr = adiv5_ap_mem_read(t->ap, CM3_CFSR);
	adiv5_ap_mem_write(t->ap, CM3_DFSR, dfsr);/* write back to reset */
	adiv5_ap_mem_write(t->ap, CM3_HFSR, hfsr);/* write back to reset */
	adiv5_ap_mem_write(t->ap, CM3_CFSR, cfsr);/* write back to reset */
	/* We check for FORCED in the HardFault Status Register or 
	 * for a configurable fault to avoid catching core resets */
	if((dfsr & CM3_DFSR_VCATCH) && ((hfsr & CM3_HFSR_FORCED) || cfsr)) {
		/* Unwind exception */
		uint32_t regs[target->regs_size];
		uint32_t stack[8];
		uint32_t retcode, framesize;
		/* Read registers for post-exception stack pointer */
		target_regs_read(target, regs);
		/* save retcode currently in lr */
		retcode = regs[14];
		/* Read stack for pre-exception registers */
		target_mem_read_words(target, stack, regs[13], sizeof(stack)); 
		regs[14] = stack[5];	/* restore LR to pre-exception state */
		regs[15] = stack[6];	/* restore PC to pre-exception state */

		/* adjust stack to pop exception state */
		framesize = (retcode & (1<<4)) ? 0x68 : 0x20;	/* check for basic vs. extended frame */
		if (stack[7] & (1<<9))				/* check for stack alignment fixup */
			framesize += 4;
		regs[13] += framesize;

		/* FIXME: stack[7] contains xPSR when this is supported */
		/* although, if we caught the exception it will be unchanged */

		/* Reset exception state to allow resuming from restored
		 * state.
		 */
		adiv5_ap_mem_write(t->ap, CM3_AIRCR, 
				CM3_AIRCR_VECTKEY | CM3_AIRCR_VECTCLRACTIVE);

		/* Write pre-exception registers back to core */
		target_regs_write(target, regs);

		return 1;
	}
	return 0;
}

/* The following routines implement hardware breakpoints.
 * The Flash Patch and Breakpoint (FPB) system is used. */

static int
cm3_set_hw_bp(struct target_s *target, uint32_t addr)
{
	struct target_ap_s *t = (void *)target;
	uint32_t val = addr & 0x1FFFFFFC;
	unsigned i;

	val |= (addr & 2)?0x80000000:0x40000000;
	val |= 1;

	for(i = 0; i < hw_breakpoint_max; i++) 
		if((hw_breakpoint[i] & 1) == 0) break;
	
	if(i == hw_breakpoint_max) return -1;

	hw_breakpoint[i] = addr | 1;

	adiv5_ap_mem_write(t->ap, CM3_FPB_COMP(i), val);

	return 0;
}

static int
cm3_clear_hw_bp(struct target_s *target, uint32_t addr)
{
	struct target_ap_s *t = (void *)target;
	unsigned i;

	for(i = 0; i < hw_breakpoint_max; i++)
		if((hw_breakpoint[i] & ~1) == addr) break;

	if(i == hw_breakpoint_max) return -1;

	hw_breakpoint[i] = 0;

	adiv5_ap_mem_write(t->ap, CM3_FPB_COMP(i), 0);

	return 0; 
}


/* The following routines implement hardware watchpoints.
 * The Data Watch and Trace (DWT) system is used. */

static int
cm3_set_hw_wp(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len)
{
	struct target_ap_s *t = (void *)target;
	unsigned i;

	switch(len) { /* Convert bytes size to mask size */
		case 1: len = CM3_DWT_MASK_BYTE; break;
		case 2: len = CM3_DWT_MASK_HALFWORD; break;
		case 4: len = CM3_DWT_MASK_WORD; break;
		default:
			return -1;
	}

	switch(type) { /* Convert gdb type to function type */
		case 2: type = CM3_DWT_FUNC_FUNC_WRITE; break;
		case 3: type = CM3_DWT_FUNC_FUNC_READ; break;
		case 4: type = CM3_DWT_FUNC_FUNC_ACCESS; break;
		default:
			return -1;
	}

	for(i = 0; i < hw_watchpoint_max; i++) 
		if((hw_watchpoint[i].type) == 0) break;
	
	if(i == hw_watchpoint_max) return -2;

	hw_watchpoint[i].type = type;
	hw_watchpoint[i].addr = addr;
	hw_watchpoint[i].size = len;

	adiv5_ap_mem_write(t->ap, CM3_DWT_COMP(i), addr);
	adiv5_ap_mem_write(t->ap, CM3_DWT_MASK(i), len);
	adiv5_ap_mem_write(t->ap, CM3_DWT_FUNC(i), type |
			((target->target_options & TOPT_FLAVOUR_V6M) ? 0: CM3_DWT_FUNC_DATAVSIZE_WORD));

	return 0;
}

static int
cm3_clear_hw_wp(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len)
{
	struct target_ap_s *t = (void *)target;
	unsigned i;

	switch(len) {
		case 1: len = CM3_DWT_MASK_BYTE; break;
		case 2: len = CM3_DWT_MASK_HALFWORD; break;
		case 4: len = CM3_DWT_MASK_WORD; break;
		default:
			return -1;
	}

	switch(type) {
		case 2: type = CM3_DWT_FUNC_FUNC_WRITE; break;
		case 3: type = CM3_DWT_FUNC_FUNC_READ; break;
		case 4: type = CM3_DWT_FUNC_FUNC_ACCESS; break;
		default:
			return -1;
	}

	for(i = 0; i < hw_watchpoint_max; i++)
		if((hw_watchpoint[i].addr == addr) &&
		   (hw_watchpoint[i].type == type) &&
		   (hw_watchpoint[i].size == len)) break;

	if(i == hw_watchpoint_max) return -2;

	hw_watchpoint[i].type = 0;

	adiv5_ap_mem_write(t->ap, CM3_DWT_FUNC(i), 0);

	return 0;
}

static int
cm3_check_hw_wp(struct target_s *target, uint32_t *addr)
{
	struct target_ap_s *t = (void *)target;
	unsigned i;

	for(i = 0; i < hw_watchpoint_max; i++)
		/* if SET and MATCHED then break */
		if(hw_watchpoint[i].type && 
		   (adiv5_ap_mem_read(t->ap, CM3_DWT_FUNC(i)) & 
					CM3_DWT_FUNC_MATCHED))
			break;

	if(i == hw_watchpoint_max) return 0;

	*addr = hw_watchpoint[i].addr;
	return 1;
}

