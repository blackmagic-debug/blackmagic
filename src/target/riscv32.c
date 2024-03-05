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
#include "adiv5.h"

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

static size_t riscv32_reg_read(target_s *target, uint32_t c, void *data, size_t max);
static size_t riscv32_reg_write(target_s *target, uint32_t c, const void *data, size_t max);
static void riscv32_regs_read(target_s *target, void *data);
static void riscv32_regs_write(target_s *target, const void *data);

static int riscv32_breakwatch_set(target_s *target, breakwatch_s *breakwatch);
static int riscv32_breakwatch_clear(target_s *target, breakwatch_s *breakwatch);

bool riscv32_probe(target_s *const target)
{
	/* Finish setting up the target structure with generic rv32 functions */
	target->core = "rv32";
	/* Provide the length of a suitable registers structure */
	target->regs_size = sizeof(riscv32_regs_s);
	target->regs_read = riscv32_regs_read;
	target->regs_write = riscv32_regs_write;
	target->reg_write = riscv32_reg_write;
	target->reg_read = riscv32_reg_read;
	target->mem_read = riscv32_mem_read;
	target->mem_write = riscv32_mem_write;

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

static void riscv32_regs_write(target_s *const target, const void *const data)
{
	/* Grab the hart structure and figure out how many registers need reading out */
	riscv_hart_s *const hart = riscv_hart_struct(target);
	riscv32_regs_s *const regs = (riscv32_regs_s *)data;
	const size_t gprs_count = hart->extensions & RV_ISA_EXT_EMBEDDED ? 16U : 32U;
	/* Loop through writing out the GPRs, except for the first which is always 0 */
	for (size_t gpr = 1; gpr < gprs_count; ++gpr) {
		// TODO: handle when this fails..
		riscv_csr_write(hart, RV_GPR_BASE + gpr, &regs->gprs[gpr]);
	}
	/* Special access to poke in the program counter that will be executed on resuming the hart */
	riscv_csr_write(hart, RV_DPC, &regs->pc);
}

static inline size_t riscv32_bool_to_4(const bool ret)
{
	return ret ? 4 : 0;
}

static size_t riscv32_reg_read(target_s *target, const uint32_t reg, void *data, const size_t max)
{
	/* We may be called with a buffer larger then necessary, so only error if there is too little space */
	if (max < 4)
		return 0;
	/* Grab the hart structure  */
	riscv_hart_s *const hart = riscv_hart_struct(target);
	if (reg < 32)
		return riscv32_bool_to_4(riscv_csr_read(hart, RV_GPR_BASE + reg, data));
	if (reg == 32)
		return riscv32_bool_to_4(riscv_csr_read(hart, RV_DPC, data));
	if (reg >= RV_CSR_GDB_OFFSET)
		return riscv32_bool_to_4(riscv_csr_read(hart, reg - RV_CSR_GDB_OFFSET, data));
	if (reg >= RV_FPU_GDB_OFFSET)
		return riscv32_bool_to_4(riscv_csr_read(hart, RV_FP_BASE + reg - RV_FPU_GDB_OFFSET, data));
	return 0;
}

static size_t riscv32_reg_write(target_s *const target, const uint32_t reg, const void *data, const size_t max)
{
	if (max != 4)
		return 0;
	/* Grab the hart structure  */
	riscv_hart_s *const hart = riscv_hart_struct(target);
	if (reg < 32)
		return riscv32_bool_to_4(riscv_csr_write(hart, RV_GPR_BASE + reg, data));
	if (reg == 32)
		return riscv32_bool_to_4(riscv_csr_write(hart, RV_DPC, data));
	if (reg >= RV_CSR_GDB_OFFSET)
		return riscv32_bool_to_4(riscv_csr_write(hart, reg - RV_CSR_GDB_OFFSET, data));
	if (reg >= RV_FPU_GDB_OFFSET)
		return riscv32_bool_to_4(riscv_csr_write(hart, RV_FP_BASE + reg - RV_FPU_GDB_OFFSET, data));
	return 0;
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

/* Takes in data from src, based on the access width, to be written to abstract command arg0 and packs it */
uint32_t riscv32_pack_data(const void *const src, const uint8_t access_width)
{
	switch (access_width) {
	case RV_MEM_ACCESS_8_BIT: {
		uint8_t value = 0;
		memcpy(&value, src, sizeof(value));
		return value;
	}
	case RV_MEM_ACCESS_16_BIT: {
		uint16_t value = 0;
		memcpy(&value, src, sizeof(value));
		return value;
	}
	case RV_MEM_ACCESS_32_BIT: {
		uint32_t value = 0;
		memcpy(&value, src, sizeof(value));
		return value;
	}
	}
	return 0;
}

static void riscv32_abstract_mem_read(
	riscv_hart_s *const hart, void *const dest, const target_addr_t src, const size_t len)
{
	/* Figure out the maximal width of access to perform, up to the bitness of the target */
	const uint8_t access_width = riscv_mem_access_width(hart, src, len);
	const uint8_t access_length = 1U << access_width;
	/* Build the access command */
	const uint32_t command = RV_DM_ABST_CMD_ACCESS_MEM | RV_ABST_READ | (access_width << RV_ABST_MEM_ACCESS_SHIFT) |
		(access_length < len ? RV_ABST_MEM_ADDR_POST_INC : 0U);
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

static void riscv32_abstract_mem_write(
	riscv_hart_s *const hart, const target_addr_t dest, const void *const src, const size_t len)
{
	/* Figure out the maxmial width of access to perform, up to the bitness of the target */
	const uint8_t access_width = riscv_mem_access_width(hart, dest, len);
	const uint8_t access_length = 1U << access_width;
	/* Build the access command */
	const uint32_t command = RV_DM_ABST_CMD_ACCESS_MEM | RV_ABST_WRITE | (access_width << RV_ABST_MEM_ACCESS_SHIFT) |
		(access_length < len ? RV_ABST_MEM_ADDR_POST_INC : 0U);
	/* Write the address to write to arg1 */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_DATA1, dest))
		return;
	const uint8_t *const data = (const uint8_t *)src;
	for (size_t offset = 0; offset < len; offset += access_length) {
		/* Pack the data to write into arg0 */
		uint32_t value = riscv32_pack_data(data + offset, access_width);
		if (!riscv_dm_write(hart->dbg_module, RV_DM_DATA0, value))
			return;
		/* Execute the write */
		if (!riscv_dm_write(hart->dbg_module, RV_DM_ABST_COMMAND, command) || !riscv_command_wait_complete(hart))
			return;
	}
}

static void riscv_sysbus_check(riscv_hart_s *const hart)
{
	uint32_t status = 0;
	/* Read back the system bus status */
	if (!riscv_dm_read(hart->dbg_module, RV_DM_SYSBUS_CTRLSTATUS, &status))
		return;
	/* Store the result and reset the value in the control/status register */
	hart->status = (status >> 12U) & RISCV_HART_OTHER;
	if (!riscv_dm_write(hart->dbg_module, RV_DM_SYSBUS_CTRLSTATUS, RISCV_HART_OTHER << 12U))
		return;
	/* If something goes wrong, tell the user */
	if (hart->status != RISCV_HART_NO_ERROR)
		DEBUG_WARN("memory access failed: %u\n", hart->status);
}

static void riscv32_sysbus_mem_native_read(riscv_hart_s *const hart, void *const dest, const target_addr_t src,
	const size_t len, const uint8_t access_width, const uint8_t access_length)
{
	/* Build the access command */
	const uint32_t command = ((uint32_t)access_width << RV_SYSBUS_MEM_ACCESS_SHIFT) | RV_SYSBUS_MEM_READ_ON_ADDR |
		(access_length < len ? RV_SYSBUS_MEM_ADDR_POST_INC | RV_SYSBUS_MEM_READ_ON_DATA : 0U);
	/*
	 * Write the command setup to the access control register
	 * Then set up the read by writing the address to the address register
	 */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_SYSBUS_CTRLSTATUS, command) ||
		!riscv_dm_write(hart->dbg_module, RV_DM_SYSBUS_ADDR0, src))
		return;
	uint8_t *const data = (uint8_t *)dest;
	for (size_t offset = 0; offset < len; offset += access_length) {
		uint32_t status = RV_SYSBUS_STATUS_BUSY;
		/* Wait for the current read cycle to complete */
		while (status & RV_SYSBUS_STATUS_BUSY) {
			if (!riscv_dm_read(hart->dbg_module, RV_DM_SYSBUS_CTRLSTATUS, &status))
				return;
		}
		/* If this would be the last read, clean up the access control register */
		if (offset + access_length == len && (command & RV_SYSBUS_MEM_ADDR_POST_INC)) {
			if (!riscv_dm_write(hart->dbg_module, RV_DM_SYSBUS_CTRLSTATUS, 0))
				return;
		}
		uint32_t value = 0;
		/* Read back and unpack the data for this block */
		if (!riscv_dm_read(hart->dbg_module, RV_DM_SYSBUS_DATA0, &value))
			return;
		riscv32_unpack_data(data + offset, value, access_width);
	}
	riscv_sysbus_check(hart);
}

static void riscv32_sysbus_mem_adjusted_read(riscv_hart_s *const hart, void *const dest, const target_addr_t src,
	const uint8_t access_length, const uint8_t access_width, const uint8_t native_access_length)
{
	const target_addr_t alignment = ~(native_access_length - 1U);
	/*
	 * On a 32-bit target the only possible widths are 8- 16- and 32-bit, so after the adjustment loop,
	 * there are only and exactly 2 possible cases to handle here: 16- and 32-bit access.
	 */
	switch (access_width) {
	case RV_MEM_ACCESS_16_BIT: {
		uint16_t value = 0;
		/* Run the 16-bit native read, storing the result in `value` */
		riscv32_sysbus_mem_native_read(
			hart, &value, src & alignment, native_access_length, RV_MEM_ACCESS_16_BIT, native_access_length);
		/* Having completed the read, unpack the data (we only care about a single byte in the access) */
		adiv5_unpack_data(dest, src, value, ALIGN_8BIT);
		break;
	}
	case RV_MEM_ACCESS_32_BIT: {
		uint32_t value = 0;
		/* Run the 32-bit native read, storing the result in `value` */
		riscv32_sysbus_mem_native_read(
			hart, &value, src & alignment, native_access_length, RV_MEM_ACCESS_32_BIT, native_access_length);

		char *data = (char *)dest;
		/* Figure out from the access length the initial unpack and adjustment */
		const uint8_t adjustment = access_length & (uint8_t)~1U;
		/* Having completed the read, unpack the first part of the data (two bytes) */
		if (adjustment)
			data = (char *)adiv5_unpack_data(data, src, value, ALIGN_16BIT);
		/* Now unpack the remaining byte if necessary */
		if (access_length & 1U)
			adiv5_unpack_data(data, src + adjustment, value, ALIGN_8BIT);
		break;
	}
	}
}

static void riscv32_sysbus_mem_read(
	riscv_hart_s *const hart, void *const dest, const target_addr_t src, const size_t len)
{
	/* Figure out the maxmial width of access to perform, up to the bitness of the target */
	const uint8_t access_width = riscv_mem_access_width(hart, src, len);
	const uint8_t access_length = (uint8_t)(1U << access_width);
	/* Check if the access is a natural/native width */
	if (hart->flags & access_length) {
		riscv32_sysbus_mem_native_read(hart, dest, src, len, access_width, access_length);
		return;
	}

	/* If we were unable to do this using a native access, find the next largest supported access width */
	uint8_t native_access_width = access_width;
	while (!((hart->flags >> native_access_width) & 1U) && native_access_width < RV_MEM_ACCESS_32_BIT)
		++native_access_width;
	const uint8_t native_access_length = (uint8_t)(1U << native_access_width);

	/* Figure out how much the length is getting adjusted by in the first read to make it aligned */
	const target_addr_t length_adjustment = src & (native_access_length - 1U);
	/*
	 * Having done this, figure out how long the resulting read actually is so we can fill enough of the
	 * destination buffer with a single read
	 */
	const uint8_t read_length =
		len + length_adjustment <= native_access_length ? len : native_access_length - length_adjustment;

	/* Do the initial adjusted access */
	size_t remainder = len;
	target_addr_t address = src;
	uint8_t *data = (uint8_t *)dest;
	riscv32_sysbus_mem_adjusted_read(hart, data, address, read_length, native_access_width, native_access_length);

	/* After doing the initial access, adjust the location of the next and do any follow-up accesses required */
	remainder -= read_length;
	address += read_length;
	data += read_length;

	/*
	 * Now we're aligned to the wider access width, do another set of reads if there's
	 * any remainder. Do this till we either reach nothing left, or we have another small left-over amount
	 */
	if (!remainder)
		return;
	const size_t amount = remainder & ~(native_access_length - 1U);
	if (amount)
		riscv32_sysbus_mem_native_read(hart, data, address, amount, native_access_width, native_access_length);
	remainder -= amount;
	address += (uint32_t)amount;
	data += amount;

	/* If there's any data left to read, do another adjusted access to grab it */
	if (remainder)
		riscv32_sysbus_mem_adjusted_read(hart, data, address, remainder, native_access_width, native_access_length);
}

static void riscv32_sysbus_mem_native_write(riscv_hart_s *const hart, const target_addr_t dest, const void *const src,
	const size_t len, const uint8_t access_width, const uint8_t access_length)
{
	/* Build the access command */
	const uint32_t command = ((uint32_t)access_width << RV_SYSBUS_MEM_ACCESS_SHIFT) |
		(access_length < len ? RV_SYSBUS_MEM_ADDR_POST_INC : 0U);
	/*
	 * Write the command setup to the access control register
	 * Then set up the write by writing the address to the address register
	 */
	if (!riscv_dm_write(hart->dbg_module, RV_DM_SYSBUS_CTRLSTATUS, command) ||
		!riscv_dm_write(hart->dbg_module, RV_DM_SYSBUS_ADDR0, dest))
		return;
	const uint8_t *const data = (const uint8_t *)src;
	for (size_t offset = 0; offset < len; offset += access_length) {
		/* Pack the data for this block and write it */
		const uint32_t value = riscv32_pack_data(data + offset, access_width);
		if (!riscv_dm_write(hart->dbg_module, RV_DM_SYSBUS_DATA0, value))
			return;

		uint32_t status = RV_SYSBUS_STATUS_BUSY;
		/* Wait for the current write cycle to complete */
		while (status & RV_SYSBUS_STATUS_BUSY) {
			if (!riscv_dm_read(hart->dbg_module, RV_DM_SYSBUS_CTRLSTATUS, &status))
				return;
		}
	}
	riscv_sysbus_check(hart);
}

static void riscv32_sysbus_mem_adjusted_write(riscv_hart_s *const hart, const target_addr_t dest, const void *const src,
	const uint8_t access_length, const uint8_t access_width, const uint8_t native_access_length)
{
	const target_addr_t alignment = ~(native_access_length - 1U);
	/*
	 * On a 32-bit target the only possible widths are 8- 16- and 32-bit, so after the adjustment loop,
	 * there are only and exactly 2 possible cases to handle here: 16- and 32-bit access.
	 * The basic premise here is that we have to read to correctly write - to do a N bit write with a
	 * wider access primitive, we first have to read back what's at the target aligned location, replace
	 * the correct set of bits in the target value, then write the new combined value back
	 */
	switch (access_width) {
	case RV_MEM_ACCESS_16_BIT: {
		uint16_t value = 0;
		/* Start by reading 16 bits */
		riscv32_sysbus_mem_native_read(
			hart, &value, dest & alignment, native_access_length, RV_MEM_ACCESS_16_BIT, native_access_length);
		/* Now replace the part to write (must be done on the widened version of the value) */
		uint32_t widened_value = value;
		/*
		 * Note that to get here we're doing a 2 byte access for 1 byte so we only care about a single byte
		 * replacement. We also have to constrain the replacement to only happen in the lower 16 bits.
		 */
		adiv5_pack_data(dest & ~2U, src, &widened_value, ALIGN_8BIT);
		value = (uint16_t)widened_value;
		/* And finally write the new value back */
		riscv32_sysbus_mem_native_write(
			hart, dest & alignment, &value, native_access_length, RV_MEM_ACCESS_16_BIT, native_access_length);
		break;
	}
	case RV_MEM_ACCESS_32_BIT: {
		uint32_t value = 0;
		/* Start by reading 32 bits */
		riscv32_sysbus_mem_native_read(
			hart, &value, dest & alignment, native_access_length, RV_MEM_ACCESS_32_BIT, native_access_length);

		/* Now replace the part to write */
		const char *data = (const char *)src;
		/* Figure out from the access length the initial pack and adjustment */
		const uint8_t adjustment = access_length & (uint8_t)~1U;
		if (adjustment)
			data = (const char *)adiv5_pack_data(dest, data, &value, ALIGN_16BIT);
		/* Now pack the remaining byte if necessary */
		if (access_length & 1)
			adiv5_pack_data(dest + adjustment, data, &value, ALIGN_8BIT);
		/* And finally write the new value back */
		riscv32_sysbus_mem_native_write(
			hart, dest & alignment, &value, native_access_length, RV_MEM_ACCESS_32_BIT, native_access_length);
		break;
	}
	}
}

static void riscv32_sysbus_mem_write(
	riscv_hart_s *const hart, const target_addr_t dest, const void *const src, const size_t len)
{
	/* Figure out the maxmial width of access to perform, up to the bitness of the target */
	const uint8_t access_width = riscv_mem_access_width(hart, dest, len);
	const uint8_t access_length = 1U << access_width;
	/* Check if the access is a natural/native width */
	if (hart->flags & access_length) {
		riscv32_sysbus_mem_native_write(hart, dest, src, len, access_width, access_length);
		return;
	}

	/* If we were unable to do this using a native access, find the next largest supported access width */
	uint8_t native_access_width = access_width;
	while (!((hart->flags >> native_access_width) & 1U) && native_access_width < RV_MEM_ACCESS_32_BIT)
		++native_access_width;
	const uint8_t native_access_length = (uint8_t)(1U << native_access_width);

	/* Figure out how much the length is getting adjusted by in the first write to make it aligned */
	const target_addr_t length_adjustment = dest & (native_access_length - 1U);
	/*
	 * Having done this, figure out how long the resulting write actually is so we can fill enough of the
	 * destination buffer with a single write
	 */
	const uint8_t write_length =
		len + length_adjustment <= native_access_length ? len : native_access_length - length_adjustment;

	/* Do the initial adjusted access */
	size_t remainder = len;
	target_addr_t address = dest;
	const uint8_t *data = (const uint8_t *)src;
	riscv32_sysbus_mem_adjusted_write(hart, address, data, write_length, native_access_width, native_access_length);

	/* After doing the initial access, adjust the location of the next and do any follow-up accesses required */
	remainder -= write_length;
	address += write_length;
	data += write_length;

	/*
	 * Now we're aligned to the wider access width, do another set of writes if there's
	 * any remainder. Do this till we either reach nothing left, or we have another small left-over amount
	 */
	if (!remainder)
		return;
	const size_t amount = remainder & ~(native_access_length - 1U);
	if (amount)
		riscv32_sysbus_mem_native_write(hart, address, data, amount, native_access_width, native_access_length);
	remainder -= amount;
	address += (uint32_t)amount;
	data += amount;

	/* If there's any data left to write, do another adjusted access to perform it */
	if (remainder)
		riscv32_sysbus_mem_adjusted_write(hart, address, data, remainder, native_access_width, native_access_length);
}

void riscv32_mem_read(target_s *const target, void *const dest, const target_addr64_t src, const size_t len)
{
	/* If we're asked to do a 0-byte read, do nothing */
	if (!len) {
		DEBUG_PROTO("%s: @ %08" PRIx32 " len %zu\n", __func__, (uint32_t)src, len);
		return;
	}

	riscv_hart_s *const hart = riscv_hart_struct(target);
	if (hart->flags & RV_HART_FLAG_MEMORY_SYSBUS)
		riscv32_sysbus_mem_read(hart, dest, src, len);
	else
		riscv32_abstract_mem_read(hart, dest, src, len);

#if ENABLE_DEBUG
	DEBUG_PROTO("%s: @ %08" PRIx32 " len %zu:", __func__, (uint32_t)src, len);
#ifndef DEBUG_PROTO_IS_NOOP
	const uint8_t *const data = (const uint8_t *)dest;
#endif
	for (size_t offset = 0; offset < len; ++offset) {
		if (offset == 16U)
			break;
		DEBUG_PROTO(" %02x", data[offset]);
	}
	if (len > 16U)
		DEBUG_PROTO(" ...");
	DEBUG_PROTO("\n");
#endif
}

void riscv32_mem_write(target_s *const target, const target_addr64_t dest, const void *const src, const size_t len)
{
#if ENABLE_DEBUG
	DEBUG_PROTO("%s: @ %" PRIx32 " len %zu:", __func__, (uint32_t)dest, len);
#ifndef DEBUG_PROTO_IS_NOOP
	const uint8_t *const data = (const uint8_t *)src;
#endif
	for (size_t offset = 0; offset < len; ++offset) {
		if (offset == 16U)
			break;
		DEBUG_PROTO(" %02x", data[offset]);
	}
	if (len > 16U)
		DEBUG_PROTO(" ...");
	DEBUG_PROTO("\n");
#endif
	/* If we're asked to do a 0-byte read, do nothing */
	if (!len)
		return;

	riscv_hart_s *const hart = riscv_hart_struct(target);
	if (hart->flags & RV_HART_FLAG_MEMORY_SYSBUS)
		riscv32_sysbus_mem_write(hart, dest, src, len);
	else
		riscv32_abstract_mem_write(hart, dest, src, len);
}

/*
 * The following can be used as a key for understanding the various return results from the breakwatch functions:
 * 0 -> success
 * 1 -> not supported
 * -1 -> an error occurred
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
