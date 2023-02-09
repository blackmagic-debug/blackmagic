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

static void riscv32_mem_read(target_s *target, void *dest, target_addr_t src, size_t len);

#define STRINGIFY(x) #x
#define PROBE(x)                                  \
	do {                                          \
		DEBUG_INFO("Calling " STRINGIFY(x) "\n"); \
		if ((x)(target))                          \
			return true;                          \
	} while (0)

bool riscv32_probe(target_s *const target)
{
	target->core = "rv32";
	target->mem_read = riscv32_mem_read;

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
