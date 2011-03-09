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
#define ARM7_EICE_DEBUG_STAT		0x00
#define ARM7_EICE_DEBUG_CTRL		0x01
#define ARM7_EICE_ABORT_STAT		0x02
#define ARM7_EICE_COMMS_CTRL		0x04
#define ARM7_EICE_COMMS_DATA		0x05
#define ARM7_EICE_WATCH_ADDR(x)		(0x08 + (8 * (x))
#define ARM7_EICE_WATCH_ADDR_MASK(x)	(0x09 + (8 * (x))
#define ARM7_EICE_WATCH_DATA(x)		(0x0A + (8 * (x))
#define ARM7_EICE_WATCH_DATA_MASK(x)	(0x0B + (8 * (x))
#define ARM7_EICE_WATCH_CTRL(x)		(0x0C + (8 * (x))
#define ARM7_EICE_WATCH_CTRL_MASK(x)	(0x0D + (8 * (x))

struct target_arm7_s {
	target t;
	jtag_dev_t *jtag;
	uint32_t reg_cache[16];
};

/* FIXME: Remove: */
static void do_nothing(void)
{
}

static void arm7_halt_request(struct target_s *target);
static int arm7_halt_wait(struct target_s *target);
static void arm7_halt_resume(struct target_s *target, uint8_t step);

void arm7tdmi_jtag_handler(jtag_dev_t *dev)
{
	struct target_arm7_s *tj = calloc(1, sizeof(*tj));
	target *t = (target *)tj;

	t->driver = arm7_driver_str;
	tj->jtag = dev;

	/* Setup mandatory virtual methods */
	t->attach = (void *)do_nothing;
	t->detach = (void *)do_nothing;
	t->check_error = (void *)do_nothing;
	t->mem_read_words = (void *)do_nothing;
	t->mem_write_words = (void *)do_nothing;
	t->mem_read_bytes = (void *)do_nothing;
	t->mem_write_bytes = (void *)do_nothing;
	t->regs_size = 16 * 4;
	t->regs_read = (void *)do_nothing;
	t->regs_write = (void *)do_nothing;
	t->pc_write = (void *)do_nothing;
	t->reset = (void *)do_nothing;
	t->halt_request = arm7_halt_request;
	t->halt_wait = arm7_halt_wait;
	t->halt_resume = arm7_halt_resume;
	
	/* TODO: Breakpoint and watchpoint functions. */
	/* TODO: Fault unwinder. */
	/* TODO: Memory map / Flash programming. */

	t->next = target_list;
	target_list = t;
}

static void arm7_halt_request(struct target_s *target)
{
	(void)target;
}

static int arm7_halt_wait(struct target_s *target)
{
	(void)target;
	return 1;
}

static void arm7_halt_resume(struct target_s *target, uint8_t step)
{
	(void)target;
	(void)step;
}

