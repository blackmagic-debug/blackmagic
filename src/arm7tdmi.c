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

/* This file implements the ARM7TDMI target support using the JTAG
 * interface as described in ARM7TDMI Technical Reference Manual,
 * ARM Document DDI 0210C
 */

#include "general.h"
#include "platform.h"
#include "target.h"
#include "jtag_scan.h"
#include "jtagtap.h"

#include <stdlib.h>
#include <string.h>

/* TODO:
 * Skeleton target.
 * EmbeddedICE registers, halt/resume target.
 * Check target mode on halt, switch to ARM if needed.
 * Read registers on halt, restore on resume. Give GDB cached copy.
 * System speed access, read/write memory.
 * Misaligned/byte memory access.
 * Breakpoint support.
 * Watchpoint support.
 * Funnies: abort on breakpointed instruction, etc.
 * Flash programming for STR73x and LPC2xxx.
 */
static const char arm7_driver_str[] = "ARM7TDMI";

/* ARM7 JTAG IR values */
#define ARM7_IR_EXTEST		0x0
#define ARM7_IR_SCAN_N		0x2
#define ARM7_IR_SAMPLE_PRELOAD	0x3
#define ARM7_IR_RESTART		0x4
#define ARM7_IR_CLAMP		0x5
#define ARM7_IR_HIGHZ		0x7
#define ARM7_IR_CLAMPZ		0x9
#define ARM7_IR_INTEST		0xC
#define ARM7_IR_IDCODE		0xE
#define ARM7_IR_BYPASS		0xF

/* ARM7 SCAN_N scan chain values */
#define ARM7_SCANN_BOUNDARY	0
#define ARM7_SCANN_DBUS		1
#define ARM7_SCANN_EICE		2

/* EmbeddedICE-RT Register addresses */
#define ARM7_EICE_DEBUG_CTRL		0x00
#define ARM7_EICE_DEBUG_STAT		0x01
#define ARM7_EICE_ABORT_STAT		0x02
#define ARM7_EICE_COMMS_CTRL		0x04
#define ARM7_EICE_COMMS_DATA		0x05
#define ARM7_EICE_WATCH_ADDR(x)		(0x08 + (8 * (x))
#define ARM7_EICE_WATCH_ADDR_MASK(x)	(0x09 + (8 * (x))
#define ARM7_EICE_WATCH_DATA(x)		(0x0A + (8 * (x))
#define ARM7_EICE_WATCH_DATA_MASK(x)	(0x0B + (8 * (x))
#define ARM7_EICE_WATCH_CTRL(x)		(0x0C + (8 * (x))
#define ARM7_EICE_WATCH_CTRL_MASK(x)	(0x0D + (8 * (x))

/* Read/write bit in EmbeddedICE-RT scan chain */
#define ARM7_EICE_READ 		(0uLL << 37)
#define ARM7_EICE_WRITE		(1uLL << 37)

/* Debug Control Register bits */
#define ARM7_EICE_DEBUG_CTRL_EICE_DISABLE	(1 << 5)
#define ARM7_EICE_DEBUG_CTRL_MONITOR		(1 << 4)
/* Bit 3 - Reserved */
#define ARM7_EICE_DEBUG_CTRL_INTDIS		(1 << 2)
#define ARM7_EICE_DEBUG_CTRL_DBGRQ		(1 << 1)
#define ARM7_EICE_DEBUG_CTRL_DBGACK		(1 << 0)

/* Debug Status Register bits */
#define ARM7_EICE_DEBUG_STAT_TBIT		(1 << 4)
#define ARM7_EICE_DEBUG_STAT_NMREQ		(1 << 3)
#define ARM7_EICE_DEBUG_STAT_INTDIS		(1 << 2)
#define ARM7_EICE_DEBUG_STAT_DBGRQ		(1 << 1)
#define ARM7_EICE_DEBUG_STAT_DBGACK		(1 << 0)

#define ARM7_OP_NOP		0xE1A00000

struct target_arm7_s {
	target t;
	jtag_dev_t *jtag;
	uint32_t reg_cache[16];
};

/* FIXME: Remove: */
static void do_nothing(void)
{
}

static bool arm7_attach(struct target_s *target);
static int arm7_regs_read(struct target_s *target, void *data);
static int arm7_regs_write(struct target_s *target, const void *data);
static void arm7_halt_request(struct target_s *target);
static int arm7_halt_wait(struct target_s *target);
static void arm7_halt_resume(struct target_s *target, bool step);

void arm7tdmi_jtag_handler(jtag_dev_t *dev)
{
	struct target_arm7_s *tj = (void*)target_new(sizeof(*tj));
	target *t = (target *)tj;

	t->driver = arm7_driver_str;
	tj->jtag = dev;

	/* Setup mandatory virtual methods */
	t->attach = arm7_attach;
	t->detach = (void *)do_nothing;
	t->check_error = (void *)do_nothing;
	t->mem_read_words = (void *)do_nothing;
	t->mem_write_words = (void *)do_nothing;
	t->mem_read_bytes = (void *)do_nothing;
	t->mem_write_bytes = (void *)do_nothing;
	t->regs_size = 16 * sizeof(uint32_t);
	t->regs_read = (void *)arm7_regs_read;
	t->regs_write = (void *)arm7_regs_write;
	t->pc_write = (void *)do_nothing;
	t->reset = (void *)do_nothing;
	t->halt_request = arm7_halt_request;
	t->halt_wait = arm7_halt_wait;
	t->halt_resume = arm7_halt_resume;

	/* TODO: Breakpoint and watchpoint functions. */
	/* TODO: Fault unwinder. */
	/* TODO: Memory map / Flash programming. */
}

static void arm7_select_scanchain(struct target_arm7_s *target, uint8_t chain)
{
	jtag_dev_write_ir(target->jtag, ARM7_IR_SCAN_N);
	jtag_dev_shift_dr(target->jtag, NULL, &chain, 4);
	jtag_dev_write_ir(target->jtag, ARM7_IR_INTEST);
}

static void arm7_eice_write(struct target_arm7_s *target,
				uint8_t addr, uint32_t value)
{
	uint64_t val = ((uint64_t)addr << 32) | value | ARM7_EICE_WRITE;

	arm7_select_scanchain(target, ARM7_SCANN_EICE);
	jtag_dev_shift_dr(target->jtag, NULL, (uint8_t *)&val, 38);
	DEBUG("eice_write(%d, 0x%08X)\n", addr, value);
}

static uint32_t arm7_eice_read(struct target_arm7_s *target, uint8_t addr)
{
	uint64_t val = ((uint64_t)addr << 32) | ARM7_EICE_READ;

	arm7_select_scanchain(target, ARM7_SCANN_EICE);
	jtag_dev_shift_dr(target->jtag, NULL, (uint8_t *)&val, 38);
	jtag_dev_shift_dr(target->jtag, (uint8_t *)&val, (uint8_t *)&val, 38);
	DEBUG("eice_read(%d, 0x%08X)\n", addr, (uint32_t)val);

	return (uint32_t)val;
}

/* Execute a single instruction at debug speed.
 * Performs datalen data bus accesses after the op to capture data.
 */
static void arm7_op_debug(struct target_arm7_s *t, uint32_t op, uint32_t *data,
			int datalen)
{
	uint64_t tmp;
	/* FIXME: This routine is broken.
	 * This process isn't very well documented.  Maybe NOPs need to
	 * be shifted into pipeline before data is read out.
	 */
	DEBUG("op_debug(0x%08X)\n", op);
	arm7_select_scanchain(t, ARM7_SCANN_DBUS);
	tmp = op;
	jtag_dev_shift_dr(t->jtag, NULL, (const uint8_t*)&tmp, 33);
	while(datalen--) {
		tmp = *data;
		jtag_dev_shift_dr(t->jtag, (uint8_t*)&tmp, (uint8_t*)&tmp, 33);
		*data = (uint32_t)tmp;
		DEBUG("\t0x%08X\n", *data);
		data++;
	}
}

/* Execute a single instruction at system speed.  */
static void arm7_op_system(struct target_arm7_s *t, uint32_t op)
{
	uint64_t tmp;
	arm7_select_scanchain(t, ARM7_SCANN_DBUS);
	tmp = op | (1uLL << 32);
	jtag_dev_shift_dr(t->jtag, NULL, (const uint8_t*)&tmp, 33);
}

static void arm7_halt_request(struct target_s *target)
{
	struct target_arm7_s *t = (struct target_arm7_s *)target;

	arm7_eice_write(t, ARM7_EICE_DEBUG_CTRL, ARM7_EICE_DEBUG_CTRL_DBGRQ);
}

static int arm7_halt_wait(struct target_s *target)
{
	struct target_arm7_s *t = (struct target_arm7_s *)target;
	int stat = arm7_eice_read(t, ARM7_EICE_DEBUG_STAT);

	if(!(stat & ARM7_EICE_DEBUG_STAT_DBGACK))
		return 0;

	/* We are halted, so switch to ARM mode if needed. */
	if(stat & ARM7_EICE_DEBUG_STAT_TBIT) {
		/* This sequence switches to ARM mode:
		 * 6000  STR R0, [R0]	; Save R0 before use
		 * 4678  MOV R0, PC	; Copy PC into R0
		 * 6000  STR R0, [R0]	; Now save the PC in R0
		 * 4778  BX PC		; Jump into ARM state
		 * 46c0  MOV R8, R8	; NOP
		 * 46c0  MOV R8, R8	; NOP
		 */
		/* FIXME: Switch to ARM mode. */
	}

	/* Fetch core register values */
	/* E880FFFF  STM R0, {R0-R15} */
	arm7_op_debug(t, 0xE880FFFF, t->reg_cache, 16);

	return 1;
}

static void arm7_halt_resume(struct target_s *target, bool step)
{
	struct target_arm7_s *t = (struct target_arm7_s *)target;

	if(step) {
		/* FIXME: Set breakpoint on any instruction to single step. */
	}

	/* Restore core registers. */
	/* E890FFFF  LDM R0, {R0-R15} */
	arm7_op_debug(t, 0xE890FFFF, t->reg_cache, 16);

	/* Release DBGRQ */
	arm7_eice_write(t, ARM7_EICE_DEBUG_CTRL, 0);
	/* This sequence restores PC if no other instructions issued in
	 * debug mode...
	 * 0 E1A00000; MOV R0, R0
	 * 1 E1A00000; MOV R0, R0
	 * 0 EAFFFFFA; B -6
	 * FIXME: Add adjustment for other opcodes.
	 */
	arm7_op_debug(t, ARM7_OP_NOP, NULL, 0);
	arm7_op_system(t, ARM7_OP_NOP);
	arm7_op_debug(t, 0xEAFFFFF8, NULL, 0);

	jtag_dev_write_ir(t->jtag, ARM7_IR_RESTART);
}

static bool arm7_attach(struct target_s *target)
{
	int tries = 0;
	target_halt_request(target);
	while(!target_halt_wait(target) && --tries)
		platform_delay(2);
	if(!tries)
		return false;
	return true;
}

static int arm7_regs_read(struct target_s *target, void *data)
{
	struct target_arm7_s *t = (struct target_arm7_s *)target;
	memcpy(data, t->reg_cache, target->regs_size);
	return 0;
}

static int arm7_regs_write(struct target_s *target, const void *data)
{
	struct target_arm7_s *t = (struct target_arm7_s *)target;
	memcpy(t->reg_cache, data, target->regs_size);
	return 0;
}

