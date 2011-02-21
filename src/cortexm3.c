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

static char cm3_driver_str[] = "ARM Cortex-M3";

static void cm3_attach(struct target_s *target);
static void cm3_detach(struct target_s *target);

static int ap_regs_read(struct target_s *target, void *data);
static int ap_regs_write(struct target_s *target, const void *data);
static int ap_pc_write(struct target_s *target, const uint32_t val);

static void cm3_reset(struct target_s *target);
static void ap_halt_resume(struct target_s *target, uint8_t step);
static int ap_halt_wait(struct target_s *target);
static void ap_halt_request(struct target_s *target);
static int cm3_fault_unwind(struct target_s *target);

static int cm3_set_hw_bp(struct target_s *target, uint32_t addr);
static int cm3_clear_hw_bp(struct target_s *target, uint32_t addr);

static int cm3_set_hw_wp(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len);
static int cm3_clear_hw_wp(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len);

static int cm3_check_hw_wp(struct target_s *target, uint32_t *addr);

/* Watchpoint unit status */
static struct wp_unit_s {
	uint32_t addr;
	uint8_t type;
	uint8_t size;
} hw_watchpoint[4];

/* Breakpoint unit status */
static uint32_t hw_breakpoint[6];

int
cm3_probe(struct target_s *target)
{
	target->driver = cm3_driver_str;

	target->attach = cm3_attach;
	target->detach = cm3_detach;

	/* Should probe here to make sure it's Cortex-M3 */
	target->regs_read = ap_regs_read;
	target->regs_write = ap_regs_write;
//	target->pc_read = ap_pc_read;
	target->pc_write = ap_pc_write;

	target->reset = cm3_reset;
	target->halt_request = ap_halt_request;
	target->halt_wait = ap_halt_wait;
	target->halt_resume = ap_halt_resume;
	target->fault_unwind = cm3_fault_unwind;
	target->regs_size = 16<<2;

	/* if not STM32 try LMI */
	if(stm32_probe(target) != 0)
		lmi_probe(target);

	return 0;
}

static void
cm3_attach(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;
	int i;

	target_halt_request(target);
	while(!target_halt_wait(target));

	/* Request halt on reset */
	/* TRCENA | VC_CORERESET */
	adiv5_ap_mem_write(t->ap, 0xE000EDFC, 0x01000401); 

	/* Reset DFSR flags */
	adiv5_ap_mem_write(t->ap, 0xE000ED30UL, 0x1F);

	/* Clear any stale breakpoints */
	for(i = 0; i < 6; i++) {
		adiv5_ap_mem_write(t->ap, 0xE0002008 + i*4, 0);
		hw_breakpoint[i] = 0;
	}

	/* Clear any stale watchpoints */
	for(i = 0; i < 4; i++) {
		adiv5_ap_mem_write(t->ap, 0xE0001028 + i*0x10, 0);
		hw_watchpoint[i].type = 0;
	}

	/* Flash Patch Control Register: set ENABLE */
	adiv5_ap_mem_write(t->ap, 0xE0002000, 3);
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
	int i;

	/* Clear any stale breakpoints */
	for(i = 0; i < 6; i++)
		adiv5_ap_mem_write(t->ap, 0xE0002008 + i*4, 0);

	/* Clear any stale watchpoints */
	for(i = 0; i < 4; i++) 
		adiv5_ap_mem_write(t->ap, 0xE0001028 + i*0x10, 0);

	/* Disable debug */
	adiv5_ap_mem_write(t->ap, 0xE000EDF0UL, 0xA05F0000UL);
}

static int
ap_regs_read(struct target_s *target, void *data)
{
	struct target_ap_s *t = (void *)target;
	uint32_t *regs = data;
	int i;

	adiv5_ap_write(t->ap, 0x00, 0xA2000052);
	adiv5_dp_low_access(t->ap->dp, 1, 0, 0x04, 0xE000EDF0);
	adiv5_ap_write(t->ap, 0x14, 0); /* Required to switch banks */
	*regs++ = adiv5_dp_read_ap(t->ap->dp, 0x18);
	for(i = 1; i < 16; i++) {
		adiv5_dp_low_access(t->ap->dp, 1, 0, 0x14, i);
		*regs++ = adiv5_dp_read_ap(t->ap->dp, 0x18);
	}

	return 0;
}

static int
ap_regs_write(struct target_s *target, const void *data)
{
	struct target_ap_s *t = (void *)target;
	const uint32_t *regs = data;
	int i;

	adiv5_ap_write(t->ap, 0x00, 0xA2000052);
	adiv5_dp_low_access(t->ap->dp, 1, 0, 0x04, 0xE000EDF0);
	adiv5_ap_write(t->ap, 0x18, *regs++); /* Required to switch banks */
	adiv5_dp_low_access(t->ap->dp, 1, 0, 0x14, 0x10000);
	for(i = 1; i < 16; i++) {
		adiv5_dp_low_access(t->ap->dp, 1, 0, 0x18, *regs++);
		adiv5_dp_low_access(t->ap->dp, 1, 0, 0x14, i | 0x10000);
	}

	return 0;
}

static int
ap_pc_write(struct target_s *target, const uint32_t val)
{
	struct target_ap_s *t = (void *)target;

	adiv5_ap_write(t->ap, 0x00, 0xA2000052);
	adiv5_dp_low_access(t->ap->dp, 1, 0, 0x04, 0xE000EDF0);

	adiv5_ap_write(t->ap, 0x18, val); /* Required to switch banks */
	adiv5_dp_low_access(t->ap->dp, 1, 0, 0x14, 0x1000F);

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
	adiv5_ap_mem_write(t->ap, 0xE000ED0C, 0x05FA0004);
	adiv5_ap_mem_write(t->ap, 0xE000ED0C, 0x05FA0001);

	/* FIXME: poll for release from reset! */
	for(int i = 0; i < 10000; i++) asm("nop");

	/* Reset DFSR flags */
	adiv5_ap_mem_write(t->ap, 0xE000ED30UL, 0x1F);
}

static void 
ap_halt_request(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;

	adiv5_ap_mem_write(t->ap, 0xE000EDF0UL, 0xA05F0003UL);
}

static int
ap_halt_wait(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;

	return adiv5_ap_mem_read(t->ap, 0xE000EDF0UL) & 0x20000;
}

static void 
ap_halt_resume(struct target_s *target, uint8_t step)
{
	struct target_ap_s *t = (void *)target;
	static uint8_t old_step = 0;

	/* Disable interrupts while single stepping... */
	if(step != old_step) {
		adiv5_ap_mem_write(t->ap, 0xE000EDF0UL, 
			step?0xA05F000FUL:0xA05F0003UL);
		old_step = step;
	}

	adiv5_ap_mem_write(t->ap, 0xE000EDF0UL, step?0xA05F000DUL:0xA05F0001UL);
}

static int cm3_fault_unwind(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;
	uint32_t dfsr = adiv5_ap_mem_read(t->ap, 0xE000ED30UL); //DFSR
	//gdb_outf("DFSR = 0x%08X\n", dfsr);
	adiv5_ap_mem_write(t->ap, 0xE000ED30UL, dfsr);/* write back to reset */
	if(dfsr & (1 << 3)) {	// VCATCH
		/* Unwind exception */
		uint32_t regs[16];
		uint32_t stack[8];
		/* Read registers for post-exception stack pointer */
		ap_regs_read(target, regs);
		gdb_outf("SP pre exception 0x%08X\n", regs[13]);
		/* Read stack for pre-exception registers */
		target_mem_read_words(target, stack, regs[13], 8 << 2); 
		regs[0] = stack[0];
		regs[1] = stack[1];
		regs[2] = stack[2];
		regs[3] = stack[3];
		regs[12] = stack[4];
		regs[14] = stack[5];
		regs[15] = stack[6];
		gdb_outf("PC pre exception 0x%08X\n", regs[15]);
		/* FIXME: stack[7] contains xPSR when this is supported */

		/* Write pre-exception registers back to core */
		ap_regs_write(target, regs);

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
	int i;

	val |= (addr & 2)?0x80000000:0x40000000;
	val |= 1;

	for(i = 0; i < 6; i++) 
		if((hw_breakpoint[i] & 1) == 0) break;
	
	if(i == 6) return -1;

	hw_breakpoint[i] = addr | 1;

	adiv5_ap_mem_write(t->ap, 0xE0002008 + i*4, val);

	return 0;
}

static int
cm3_clear_hw_bp(struct target_s *target, uint32_t addr)
{
	struct target_ap_s *t = (void *)target;
	int i;

	for(i = 0; i < 6; i++)
		if((hw_breakpoint[i] & ~1) == addr) break;

	if(i == 6) return -1;

	hw_breakpoint[i] = 0;

	adiv5_ap_mem_write(t->ap, 0xE0002008 + i*4, 0);

	return 0; 
}


/* The following routines implement hardware watchpoints.
 * The Data Watch and Trace (DWT) system is used. */

static int
cm3_set_hw_wp(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len)
{
	struct target_ap_s *t = (void *)target;
	int i;

	switch(len) { /* Convert bytes size to mask size */
		case 1: len = 0; break;
		case 2: len = 1; break;
		case 4: len = 2; break;
		default:
			return -1;
	}

	switch(type) { /* Convert gdb type to function type */
		case 2: type = 6; break;
		case 3: type = 5; break;
		case 4: type = 7; break;
		default:
			return -1;
	}

	for(i = 0; i < 4; i++) 
		if((hw_watchpoint[i].type) == 0) break;
	
	if(i == 4) return -2;

	hw_watchpoint[i].type = type;
	hw_watchpoint[i].addr = addr;
	hw_watchpoint[i].size = len;

	adiv5_ap_mem_write(t->ap, 0xE0001020 + i*0x10, addr);
	adiv5_ap_mem_write(t->ap, 0xE0001024 + i*0x10, len);
	adiv5_ap_mem_write(t->ap, 0xE0001028 + i*0x10, 0x800 | type);

	return 0;
}

static int
cm3_clear_hw_wp(struct target_s *target, uint8_t type, uint32_t addr, uint8_t len)
{
	struct target_ap_s *t = (void *)target;
	int i;

	switch(len) {
		case 1: len = 0; break;
		case 2: len = 1; break;
		case 4: len = 2; break;
		default:
			return -1;
	}

	switch(type) {
		case 2: type = 6; break;
		case 3: type = 5; break;
		case 4: type = 7; break;
		default:
			return -1;
	}

	for(i = 0; i < 4; i++)
		if((hw_watchpoint[i].addr == addr) &&
		   (hw_watchpoint[i].type == type) &&
		   (hw_watchpoint[i].size == len)) break;

	if(i == 4) return -2;

	hw_watchpoint[i].type = 0;

	adiv5_ap_mem_write(t->ap, 0xE0001028 + i*0x10, 0);

	return 0;
}

static int
cm3_check_hw_wp(struct target_s *target, uint32_t *addr)
{
	struct target_ap_s *t = (void *)target;
	int i;

	for(i = 0; i < 4; i++)
		/* if SET and MATCHED then break */
		if(hw_watchpoint[i].type && 
		   (adiv5_ap_mem_read(t->ap, 0xE0001028 + i*0x10) & 0x01000000))
			break;

	if(i == 4) return 0;

	*addr = hw_watchpoint[i].addr;
	return 1;
}

