/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "target_probe.h"
#include "jep106.h"
#include "riscv_debug.h"
#include "gdb_packet.h"

typedef struct riscv32_regs {
	uint32_t gprs[32];
	uint32_t pc;
} riscv32_regs_s;

/* This defines a match trigger that's for an address or data location */
#define RV32_MATCH_ADDR_DATA_TRIGGER 0x20000000U
/* A dmode of 1 restricts the writability of the trigger to debug mode only */
#define RV32_MATCH_DMODE_DEBUG 0x08000000U
/* Match when the processor tries to execute the location */
#define RV32_MATCH_EXECUTE 0x00000004U
/* Match when the processor tries to read the location */
#define RV32_MATCH_READ 0x00000001U
/* Match when the processor tries to write the location */
#define RV32_MATCH_WRITE 0x00000002U
/* Define that the match should occur in all/any mode */
#define RV32_MATCH_ANY_MODE 0x00000058U
/* Set the match action to raise a breakpoint exception */
#define RV32_MATCH_ACTION_EXCEPTION 0x00000000U
/* Set the match action to enter debug mode */
#define RV32_MATCH_ACTION_DEBUG_MODE 0x00001000U
/* These two define whether the match should be performed on the address, or specific data */
#define RV32_MATCH_ADDR 0x00000000U
#define RV32_MATCH_DATA 0x00080000U
/* These two define the match timing (before-or-after operation execution) */
#define RV32_MATCH_BEFORE 0x00000000U
#define RV32_MATCH_AFTER  0x00040000U

static void riscv32_regs_read(target_s *target, void *data);
static void riscv32_mem_read(target_s *target, void *dest, target_addr_t src, size_t len);

static int riscv32_breakwatch_set(target_s *target, breakwatch_s *breakwatch);
static int riscv32_breakwatch_clear(target_s *target, breakwatch_s *breakwatch);

#define STRINGIFY(x) #x
#define PROBE(x)                                  \
	do {                                          \
		DEBUG_INFO("Calling " STRINGIFY(x) "\n"); \
		if ((x)(target))                          \
			return true;                          \
	} while (0)

bool riscv32_probe(target_s *const target)
{
	/* Finish setting up the target structure with generic rv32 functions */
	target->core = "rv32";
	/* Provide the length of a suitable registers structure */
	target->regs_size = sizeof(riscv32_regs_s);
	target->regs_read = riscv32_regs_read;
	target->mem_read = riscv32_mem_read;

	target->breakwatch_set = riscv32_breakwatch_set;
	target->breakwatch_clear = riscv32_breakwatch_clear;

	switch (target->designer_code) {
	case JEP106_MANUFACTURER_RV_GIGADEVICE:
		PROBE(gd32vf1_probe);
		break;
	}
#if PC_HOSTED == 0
	gdb_outf("Please report unknown device with Designer 0x%x\n", target->designer_code);
#else
	DEBUG_WARN("Please report unknown device with Designer 0x%x\n", target->designer_code);
#endif
#undef PROBE
	return false;
}

static void riscv32_regs_read(target_s *const target, void *const data)
{
	/* Grab the hart structure and figure out how many registers need reading out */
	riscv_hart_s *const hart = riscv_hart_struct(target);
	riscv32_regs_s *const regs = (riscv32_regs_s *)data;
	const size_t gprs_count = hart->extensions & RV_ISA_EXT_EMBEDDED ? 16U : 32U;
	/* Loop through reading out the GPRs */
	for (size_t gpr = 0; gpr < gprs_count; ++gpr) {
		// TODO: handle when this fails..
		riscv_csr_read(hart, RV_GPR_BASE + gpr, &regs->gprs[gpr]);
	}
	/* Special access to grab the program counter that would be executed on resuming the hart */
	riscv_csr_read(hart, RV_DPC, &regs->pc);
}

/* Takes in data from abstract command arg0 and, based on the access width, unpacks it to dest */
void riscv32_unpack_data(void *const dest, const uint32_t data, const uint8_t access_width)
{
	switch (access_width) {
	case RV_MEM_ACCESS_8_BIT: {
		const uint8_t value = data & 0xffU;
		memcpy(dest, &value, sizeof(value));
		break;
	}
	case RV_MEM_ACCESS_16_BIT: {
		const uint16_t value = data & 0xffffU;
		memcpy(dest, &value, sizeof(value));
		break;
	}
	case RV_MEM_ACCESS_32_BIT:
		memcpy(dest, &data, sizeof(data));
		break;
	}
}

static void riscv32_mem_read(target_s *const target, void *const dest, const target_addr_t src, const size_t len)
{
	DEBUG_TARGET("Performing %zu byte read of %08" PRIx32 "\n", len, src);
	/* If we're asked to do a 0-byte read, do nothing */
	if (!len)
		return;
	riscv_hart_s *const hart = riscv_hart_struct(target);
	/* Figure out the maxmial width of access to perform, up to the bitness of the target */
	const uint8_t access_width = riscv_mem_access_width(hart, src, len);
	const uint8_t access_length = 1U << access_width;
	/* Build the access command */
	const uint32_t command = RV_DM_ABST_CMD_ACCESS_MEM | RV_ABST_READ | (access_width << RV_MEM_ACCESS_SHIFT) |
		(access_length < len ? RV_MEM_ADDR_POST_INC : 0U);
	/* Write the address to read to arg1 */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_DATA1, src))
		return;
	uint8_t *const data = (uint8_t *)dest;
	for (size_t offset = 0; offset < len; offset += access_length) {
		/* Execute the read */
		if (!riscv_dm_write(hart->dbg_module, RV_DM_ABST_COMMAND, command) || !riscv_command_wait_complete(hart))
			return;
		/* Extract back the data from arg0 */
		uint32_t value = 0;
		if (!riscv_dm_read(hart->dbg_module, RV_DM_DATA0, &value))
			return;
		riscv32_unpack_data(data + offset, value, access_width);
	}
}

/*
 * The following can be used as a key for understanding the various return results from the breakwatch functions:
 * 0 -> success
 * 1 -> not supported
 * -1 -> an error occured
 */

static int riscv32_breakwatch_set(target_s *const target, breakwatch_s *const breakwatch)
{
	riscv_hart_s *const hart = riscv_hart_struct(target);
	size_t trigger = 0;
	/* Find the first unused trigger slot */
	for (; trigger < hart->triggers; ++trigger) {
		const uint32_t trigger_use = hart->trigger_uses[trigger];
		/* Make sure it's unused and that it supports breakwatch mode */
		if ((trigger_use & RV_TRIGGER_MODE_MASK) == RISCV_TRIGGER_MODE_UNUSED &&
			(trigger_use & RV_TRIGGER_SUPPORT_BREAKWATCH))
			break;
	}
	/* If none was available, return an error */
	if (trigger == hart->triggers)
		return -1;

	/* Build the mcontrol config for the requested breakwatch type */
	uint32_t config = RV32_MATCH_ADDR_DATA_TRIGGER | RV32_MATCH_DMODE_DEBUG | RV32_MATCH_ANY_MODE |
		RV32_MATCH_ACTION_DEBUG_MODE | RV32_MATCH_ADDR | riscv_breakwatch_match_size(breakwatch->size);
	// RV32_MATCH_DATA (bit 19)
	riscv_trigger_state_e mode = RISCV_TRIGGER_MODE_WATCHPOINT;
	switch (breakwatch->type) {
	case TARGET_BREAK_HARD:
		config |= RV32_MATCH_EXECUTE | RV32_MATCH_BEFORE;
		mode = RISCV_TRIGGER_MODE_BREAKPOINT;
		break;
	case TARGET_WATCH_READ:
		config |= RV32_MATCH_READ | RV32_MATCH_AFTER;
		break;
	case TARGET_WATCH_WRITE:
		config |= RV32_MATCH_WRITE | RV32_MATCH_BEFORE;
		break;
	case TARGET_WATCH_ACCESS:
		config |= RV32_MATCH_READ | RV32_MATCH_WRITE | RV32_MATCH_AFTER;
		break;
	default:
		/* If the breakwatch type is not one of the above, tell the debugger we don't support it */
		return 1;
	}
	/* Grab the address to set the breakwatch on and configure the hardware */
	const uint32_t address = breakwatch->addr;
	const bool result = riscv_config_trigger(hart, trigger, mode, &config, &address);
	/* If configuration succeeds, store the trigger index in the breakwatch structure */
	if (result)
		breakwatch->reserved[0] = trigger;
	/* Return based on whether setting up the hardware worked or not */
	return result ? 0 : -1;
}

static int riscv32_breakwatch_clear(target_s *const target, breakwatch_s *const breakwatch)
{
	riscv_hart_s *const hart = riscv_hart_struct(target);
	const uint32_t config = RV32_MATCH_ADDR_DATA_TRIGGER;
	const uint32_t address = 0;
	return riscv_config_trigger(hart, breakwatch->reserved[0], RISCV_TRIGGER_MODE_UNUSED, &config, &address) ? 0 : -1;
}
