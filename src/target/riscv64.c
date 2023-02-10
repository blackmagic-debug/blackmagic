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
#include "riscv_debug.h"

typedef struct riscv64_regs {
	uint64_t gprs[32];
	uint64_t pc;
} riscv64_regs_s;

static void riscv64_regs_read(target_s *target, void *data);
static void riscv64_mem_read(target_s *target, void *dest, target_addr_t src, size_t len);

bool riscv64_probe(target_s *const target)
{
	target->core = "rv64";
	/* Provide the length of a suitable registers structure */
	target->regs_size = sizeof(riscv64_regs_s);
	target->regs_read = riscv64_regs_read;
	target->mem_read = riscv64_mem_read;

	return false;
}

static void riscv64_regs_read(target_s *const target, void *const data)
{
	riscv_hart_s *const hart = riscv_hart_struct(target);
	riscv64_regs_s *const regs = (riscv64_regs_s *)data;
	const size_t gprs_count = hart->extensions & RV_ISA_EXT_EMBEDDED ? 16U : 32U;
	for (size_t gpr = 0; gpr < gprs_count; ++gpr) {
		// TODO: handle when this fails..
		riscv_csr_read(hart, RV_GPR_BASE + gpr, &regs->gprs[gpr]);
	}
	riscv_csr_read(hart, RV_DPC, &regs->pc);
}

void riscv64_unpack_data(
	void *const dest, const uint32_t data_low, const uint32_t data_high, const uint8_t access_width)
{
	switch (access_width) {
	case RV_MEM_ACCESS_8_BIT:
	case RV_MEM_ACCESS_16_BIT:
	case RV_MEM_ACCESS_32_BIT:
		riscv32_unpack_data(dest, data_low, access_width);
		break;
	case RV_MEM_ACCESS_64_BIT: {
		uint64_t value = ((uint64_t)data_high << 32U) | data_low;
		memcpy(dest, &value, sizeof(value));
		break;
	}
	}
}

/* XXX: target_addr_t supports only 32-bit addresses, artificially limiting this function */
static void riscv64_mem_read(target_s *const target, void *const dest, const target_addr_t src, const size_t len)
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
	if (!riscv_dm_write(hart->dbg_module, RV_DM_DATA2, src) || !riscv_dm_write(hart->dbg_module, RV_DM_DATA3, 0U))
		return;
	uint8_t *const data = (uint8_t *)dest;
	for (size_t offset = 0; offset < len; offset += access_length) {
		/* Execute the read */
		if (!riscv_dm_write(hart->dbg_module, RV_DM_ABST_COMMAND, command) || !riscv_command_wait_complete(hart))
			return;
		/* Extract back the data from arg0 */
		uint32_t value_low = 0;
		uint32_t value_high = 0;
		if (!riscv_dm_read(hart->dbg_module, RV_DM_DATA0, &value_low) ||
			!riscv_dm_read(hart->dbg_module, RV_DM_DATA1, &value_high))
			return;
		riscv64_unpack_data(data + offset, value_low, value_high, access_width);
	}
}
