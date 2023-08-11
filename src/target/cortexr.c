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

/*
 * This file implements support for Cortex-R family processors.
 *
 * References:
 * DDI0406C - ARM Architecture Reference Manual for ARMv7-A/R
 *   https://documentation-service.arm.com/static/5f8daeb7f86e16515cdb8c4e
 * DDI0363G - Cortex-R4 and Cortex-R4F Technical Reference Manual
 *   https://documentation-service.arm.com/static/5f0358e8dbdee951c1cd6f3b
 */

#include "general.h"
#include "exception.h"
#include "adiv5.h"
#include "target.h"
#include "target_internal.h"
#include "target_probe.h"
#include "jep106.h"
#include "cortex.h"
#include "cortex_internal.h"
#include "gdb_packet.h"

typedef struct cortexr_priv {
	/* Base core information */
	cortex_priv_s base;
	/* Watchpoint unit information */
	uint8_t hw_watchpoint_max;
	/* Breakpoint unit information */
	uint8_t hw_breakpoint_max;

	/* Core registers cache */
	struct {
		uint32_t r[16U];
		uint32_t cpsr;
		uint32_t spsr[5U];
		uint64_t d[16U];
		uint32_t fpcsr;
	} core_regs;
} cortexr_priv_s;

#define CORTEXR_DBG_IDR   0x000U
#define CORTEXR_DBG_WFAR  0x018U
#define CORTEXR_DBG_VCR   0x01cU
#define CORTEXR_DBG_DSCCR 0x028U
#define CORTEXR_DBG_DTRTX 0x080U
#define CORTEXR_DBG_ITR   0x084U
#define CORTEXR_DBG_DSCR  0x088U
#define CORTEXR_DBG_DTRRX 0x08cU
#define CORTEXR_DBG_DRCR  0x090U
#define CORTEXR_DBG_BVR   0x100U
#define CORTEXR_DBG_BCR   0x140U
#define CORTEXR_DBG_WVR   0x180U
#define CORTEXR_DBG_WCR   0x1c0U

#define CORTEXR_CPUID 0xd00U
#define CORTEXR_CTR   0xd04U

#define CORTEXR_DBG_IDR_BREAKPOINT_MASK  0xfU
#define CORTEXR_DBG_IDR_BREAKPOINT_SHIFT 24U
#define CORTEXR_DBG_IDR_WATCHPOINT_MASK  0xfU
#define CORTEXR_DBG_IDR_WATCHPOINT_SHIFT 28U

#define CORTEXR_DBG_DSCR_HALTED           (1U << 0U)
#define CORTEXR_DBG_DSCR_RESTARTED        (1U << 1U)
#define CORTEXR_DBG_DSCR_MOE_MASK         0x0000003cU
#define CORTEXR_DBG_DSCR_MOE_HALT_REQUEST 0x00000000U
#define CORTEXR_DBG_DSCR_MOE_BREAKPOINT   0x00000004U
#define CORTEXR_DBG_DSCR_MOE_ASYNC_WATCH  0x00000008U
#define CORTEXR_DBG_DSCR_MOE_BKPT_INSN    0x0000000cU
#define CORTEXR_DBG_DSCR_MOE_EXTERNAL_DBG 0x00000010U
#define CORTEXR_DBG_DSCR_MOE_VEC_CATCH    0x00000014U
#define CORTEXR_DBG_DSCR_MOE_SYNC_WATCH   0x00000028U
#define CORTEXR_DBG_DSCR_ITR_ENABLE       (1U << 13U)
#define CORTEXR_DBG_DSCR_INSN_COMPLETE    (1U << 24U)
#define CORTEXR_DBG_DSCR_DTR_READ_READY   (1U << 29U)
#define CORTEXR_DBG_DSCR_DTR_WRITE_DONE   (1U << 30U)

#define CORTEXR_DBG_DRCR_HALT_REQ           (1U << 0U)
#define CORTEXR_DBG_DRCR_RESTART_REQ        (1U << 1U)
#define CORTEXR_DBG_DRCR_CLR_STICKY_EXC     (1U << 2U)
#define CORTEXR_DBG_DRCR_CLR_STICKY_PIPEADV (1U << 3U)
#define CORTEXR_DBG_DRCR_CANCEL_BUS_REQ     (1U << 4U)

/*
 * Instruction encodings for reading/writing the program counter to/from r0,
 * and reading/writing CPSR to/from r0
 */
#define ARM_MOV_R0_PC_INSN   0xe1a0000fU
#define ARM_MOV_PC_R0_INSN   0xe1a0f000U
#define ARM_MRS_R0_CPSR_INSN 0xe10f0000U
#define ARM_MSR_CPSR_R0_INSN 0xe12ff000U

/* CPSR register definitions */
#define CORTEXR_CPSR_THUMB (1U << 5U)

/*
 * Instruction encodings for reading/writing the VFPv3 float registers
 * to/from r0 and r1 and reading/writing FPSCR to/from r0
 */
#define ARM_VMRS_R0_FPCSR_INSN 0xeef10a10
#define ARM_VMSR_FPCSR_R0_INSN 0xeee10a10
#define ARM_VMOV_R0_R1_DN_INSN 0xec510b10
#define ARM_VMOV_DN_R0_R1_INSN 0xec410b10

/*
 * Instruction encodings for the coprocessor interface
 * MRC -> Move to ARM core register from Coprocessor (DDI0406C §A8.8.108, pg493)
 * MCR -> Move to Coprocessor from ARM core register (DDI0406C §A8.8.99, pg477)
 */
#define ARM_MRC_INSN 0xee100010U
#define ARM_MCR_INSN 0xee000010U
/*
 * Encodes a core <=> coprocessor access for use with the MRC and MCR instructions.
 * opc1 -> Coprocessor-specific opcode 1
 *   rt -> ARM core register to use for the transfer
 *  crn -> Primary coprocessor register
 *  crm -> Additional coprocessor register
 * opc2 -> Coprocessor-specific opcode 2
 */
#define ENCODE_CP_ACCESS(coproc, opc1, rt, crn, crm, opc2) \
	(((opc1) << 21U) | ((crn) << 16U) | ((rt) << 12U) | ((coproc) << 8U) | ((opc2) << 5U) | (crm))
/* Packs a CRn and CRm value for the coprocessor IO rouines below to unpack */
#define ENCODE_CP_REG(n, m, opc1, opc2) \
	((((n)&0xfU) << 4U) | ((m)&0xfU) | (((opc1)&0x7U) << 8U) | (((opc2)&0x7U) << 12U))

/* Coprocessor register definitions */
#define CORTEXR_CPACR 15U, ENCODE_CP_REG(1U, 0U, 0U, 2U)

#define CORTEXR_CPACR_CP10_FULL_ACCESS 0x00300000U
#define CORTEXR_CPACR_CP11_FULL_ACCESS 0x00c00000U

#define TOPT_FLAVOUR_FLOAT (1U << 1U) /* If set, core has a hardware FPU */

static target_halt_reason_e cortexr_halt_poll(target_s *target, target_addr_t *watch);
static void cortexr_halt_request(target_s *target);
static void cortexr_halt_resume(target_s *target, bool step);

static void cortexr_mem_read(target_s *const target, void *const dest, const target_addr_t src, const size_t len)
{
	adiv5_mem_read(cortex_ap(target), dest, src, len);
}

static void cortexr_mem_write(target_s *const target, const target_addr_t dest, const void *const src, const size_t len)
{
	adiv5_mem_write(cortex_ap(target), dest, src, len);
}

static void cortexr_run_insn(target_s *const target, const uint32_t insn)
{
	/* Issue the requested instruction to the core */
	cortex_dbg_write32(target, CORTEXR_DBG_ITR, insn);
	/* Poll for the instruction to complete */
	while (!(cortex_dbg_read32(target, CORTEXR_DBG_DSCR) & CORTEXR_DBG_DSCR_INSN_COMPLETE))
		continue;
}

static uint32_t cortexr_run_read_insn(target_s *const target, const uint32_t insn)
{
	/* Issue the requested instruction to the core */
	cortex_dbg_write32(target, CORTEXR_DBG_ITR, insn);
	/* Poll for the instruction to complete and the data to become ready in the DTR */
	while ((cortex_dbg_read32(target, CORTEXR_DBG_DSCR) &
			   (CORTEXR_DBG_DSCR_INSN_COMPLETE | CORTEXR_DBG_DSCR_DTR_READ_READY)) !=
		(CORTEXR_DBG_DSCR_INSN_COMPLETE | CORTEXR_DBG_DSCR_DTR_READ_READY))
		continue;
	/* Read back the DTR to complete the read */
	return cortex_dbg_read32(target, CORTEXR_DBG_DTRRX);
}

static void cortexr_run_write_insn(target_s *const target, const uint32_t insn, const uint32_t data)
{
	/* Set up the data in the DTR for the transaction */
	cortex_dbg_write32(target, CORTEXR_DBG_DTRTX, data);
	/* Poll for the data to become ready in the DTR */
	while (!(cortex_dbg_read32(target, CORTEXR_DBG_DSCR) & CORTEXR_DBG_DSCR_DTR_WRITE_DONE))
		continue;
	/* Issue the requested instruction to the core */
	cortex_dbg_write32(target, CORTEXR_DBG_ITR, insn);
	/* Poll for the instruction to complete and the data to be consumed from the DTR */
	while ((cortex_dbg_read32(target, CORTEXR_DBG_DSCR) &
			   (CORTEXR_DBG_DSCR_INSN_COMPLETE | CORTEXR_DBG_DSCR_DTR_WRITE_DONE)) != CORTEXR_DBG_DSCR_INSN_COMPLETE)
		continue;
}

static inline uint32_t cortexr_core_reg_read(target_s *const target, const uint8_t reg)
{
	/* If the register is a GPR and not the program counter, use a "simple" MCR to read */
	if (reg < 15U)
		/* Build an issue a core to coprocessor transfer for the requested register and read back the result */
		return cortexr_run_read_insn(target, ARM_MCR_INSN | ENCODE_CP_ACCESS(14, 0, reg, 0, 5, 0));
	/* If the register is the program counter, we first have to extract it to r0 */
	else if (reg == 15U) {
		cortexr_run_insn(target, ARM_MOV_R0_PC_INSN);
		return cortexr_core_reg_read(target, 0U);
	}
	return 0U;
}

static void cortexr_core_regs_save(target_s *const target)
{
	cortexr_priv_s *const priv = (cortexr_priv_s *)target->priv;
	/* Save out r0-r15 in that order (r15, aka pc, clobbers r0) */
	for (size_t i = 0U; i < ARRAY_LENGTH(priv->core_regs.r); ++i)
		priv->core_regs.r[i] = cortexr_core_reg_read(target, i);
	/* Read CPSR to r0 and retrieve it */
	cortexr_run_insn(target, ARM_MRS_R0_CPSR_INSN);
	priv->core_regs.cpsr = cortexr_core_reg_read(target, 0U);
	/* Adjust the program counter according to the mode */
	priv->core_regs.r[CORTEX_REG_PC] -= (priv->core_regs.cpsr & CORTEXR_CPSR_THUMB) ? 4U : 8U;
}

static void cortexr_float_regs_save(target_s *const target)
{
	cortexr_priv_s *const priv = (cortexr_priv_s *)target->priv;
	/* Read FPCSR to r0 and retrieve it */
	cortexr_run_insn(target, ARM_VMRS_R0_FPCSR_INSN);
	priv->core_regs.fpcsr = cortexr_core_reg_read(target, 0U);
	/* Now step through each double-precision float register, reading it back to r0,r1 */
	for (size_t i = 0; i < ARRAY_LENGTH(priv->core_regs.d); ++i) {
		/* The float register to read slots into the bottom 4 bits of the instruction */
		cortexr_run_insn(target, ARM_VMOV_R0_R1_DN_INSN | i);
		/* Read back the data */
		const uint32_t d_low = cortexr_core_reg_read(target, 0U);
		const uint32_t d_high = cortexr_core_reg_read(target, 1U);
		/* Reassemble it as a full 64-bit value */
		priv->core_regs.d[i] = d_low | ((uint64_t)d_high << 32U);
	}
}

static inline void cortexr_core_reg_write(target_s *const target, const uint8_t reg, const uint32_t value)
{
	/* If the register is a GPR and not the program counter, use a "simple" MCR to read */
	if (reg < 15U)
		/* Build and issue a coprocessor to core transfer for the requested register and send the new data */
		cortexr_run_write_insn(target, ARM_MRC_INSN | ENCODE_CP_ACCESS(14, 0, reg, 0, 5, 0), value);
	/* If the register is the program counter, we first have to write it to r0 */
	else if (reg == 15U) {
		cortexr_core_reg_write(target, 0U, value);
		cortexr_run_insn(target, ARM_MOV_PC_R0_INSN);
	}
}

static void cortexr_core_regs_restore(target_s *const target)
{
	cortexr_priv_s *const priv = (cortexr_priv_s *)target->priv;
	/* Load the value for CPSR to r0 and then shove it back into place */
	cortexr_core_reg_write(target, 0U, priv->core_regs.cpsr);
	cortexr_run_insn(target, ARM_MSR_CPSR_R0_INSN);
	/* Fix up the program counter for the mode */
	if (priv->core_regs.cpsr & CORTEXR_CPSR_THUMB)
		priv->core_regs.r[CORTEX_REG_PC] |= 1U;
	/* Restore r1-15 in that order. Ignore r0 for the moment as it gets clobbered repeatedly */
	for (size_t i = 1U; i < ARRAY_LENGTH(priv->core_regs.r); ++i)
		cortexr_core_reg_write(target, i, priv->core_regs.r[i]);

	/* Now we're done with the rest of the registers, restore r0 */
	cortexr_core_reg_write(target, 0U, priv->core_regs.r[0U]);
}

static void cortexr_float_regs_restore(target_s *const target)
{
	const cortexr_priv_s *const priv = (cortexr_priv_s *)target->priv;
	/* Step through each double-precision float register, writing it back via r0,r1 */
	for (size_t i = 0; i < ARRAY_LENGTH(priv->core_regs.d); ++i) {
		/* Load the low 32 bits into r0, and the high into r1 */
		cortexr_core_reg_write(target, 0U, priv->core_regs.d[i] & UINT32_MAX);
		cortexr_core_reg_write(target, 1U, priv->core_regs.d[i] >> 32U);
		/* The float register to write slots into the bottom 4 bits of the instruction */
		cortexr_run_insn(target, ARM_VMOV_DN_R0_R1_INSN | i);
	}
	/* Load the value for FPCSR to r0 and then shove it back into place */
	cortexr_core_reg_write(target, 0U, priv->core_regs.fpcsr);
	cortexr_run_insn(target, ARM_VMSR_FPCSR_R0_INSN);
}

static uint32_t cortexr_coproc_read(target_s *const target, const uint8_t coproc, const uint16_t op)
{
	/*
	 * Perform a read of a coprocessor - which one (between 0 and 15) is given by the coproc parameter
	 * and which register of the coprocessor to read and the operands required is given by op.
	 * This follows the steps laid out in DDI0406C §C6.4.1 pg2109
	 *
	 * Encode the MCR (Move to ARM core register from Coprocessor) instruction in ARM ISA format
	 * using core reg r0 as the read target.
	 */
	cortexr_run_insn(target,
		ARM_MRC_INSN |
			ENCODE_CP_ACCESS(coproc & 0xfU, (op >> 8U) & 0x7U, 0U, (op >> 4U) & 0xfU, op & 0xfU, (op >> 12U) & 0x7U));
	return cortexr_core_reg_read(target, 0U);
}

static void cortexr_coproc_write(target_s *const target, const uint8_t coproc, const uint16_t op, const uint32_t value)
{
	/*
	 * Perform a write of a coprocessor - which one (between 0 and 15) is given by the coproc parameter
	 * and which register of the coprocessor to write and the operands required is given by op.
	 * This follows the steps laid out in DDI0406C §C6.4.1 pg2109
	 *
	 * Encode the MRC (Move to Coprocessor from ARM core register) instruction in ARM ISA format
	 * using core reg r0 as the write data source.
	 */
	cortexr_core_reg_write(target, 0U, value);
	cortexr_run_insn(target,
		ARM_MCR_INSN |
			ENCODE_CP_ACCESS(coproc & 0xfU, (op >> 8U) & 0x7U, 0U, (op >> 4U) & 0xfU, op & 0xfU, (op >> 12U) & 0x7U));
}

bool cortexr_probe(adiv5_access_port_s *const ap, const target_addr_t base_address)
{
	target_s *target = target_new();
	if (!target)
		return false;

	adiv5_ap_ref(ap);
	if (ap->dp->version >= 2 && ap->dp->target_designer_code != 0) {
		/* Use TARGETID register to identify target */
		target->designer_code = ap->dp->target_designer_code;
		target->part_id = ap->dp->target_partno;
	} else {
		/* Use AP DESIGNER and AP PARTNO to identify target */
		target->designer_code = ap->designer_code;
		target->part_id = ap->partno;
	}

	cortexr_priv_s *const priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}

	target->priv = priv;
	target->priv_free = cortex_priv_free;
	priv->base.ap = ap;
	priv->base.base_addr = base_address;

	target->check_error = cortex_check_error;
	target->mem_read = cortexr_mem_read;
	target->mem_write = cortexr_mem_write;

	target->driver = "ARM Cortex-R";

	target->halt_request = cortexr_halt_request;
	target->halt_poll = cortexr_halt_poll;
	target->halt_resume = cortexr_halt_resume;

	/* Try to halt the target core */
	target_halt_request(target);
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);
	target_halt_reason_e reason = TARGET_HALT_RUNNING;
	while (!platform_timeout_is_expired(&timeout) && reason == TARGET_HALT_RUNNING)
		reason = target_halt_poll(target, NULL);
	/* If we did not succeed, we must abort at this point. */
	if (reason == TARGET_HALT_FAULT || reason == TARGET_HALT_ERROR)
		return false;

	cortex_read_cpuid(target);
	/* The format of the debug identification register is described in DDI0406C §C11.11.15 pg2217 */
	const uint32_t debug_id = cortex_dbg_read32(target, CORTEXR_DBG_IDR);
	priv->hw_breakpoint_max = ((debug_id >> CORTEXR_DBG_IDR_BREAKPOINT_SHIFT) & CORTEXR_DBG_IDR_BREAKPOINT_MASK) + 1U;
	priv->hw_watchpoint_max = ((debug_id >> CORTEXR_DBG_IDR_WATCHPOINT_SHIFT) & CORTEXR_DBG_IDR_WATCHPOINT_MASK) + 1U;
	DEBUG_TARGET("%s %s core has %u breakpoint and %u watchpoint units available\n", target->driver, target->core,
		priv->hw_breakpoint_max, priv->hw_watchpoint_max);

	/* Probe for FP extension. */
	uint32_t cpacr = cortexr_coproc_read(target, CORTEXR_CPACR);
	cpacr |= CORTEXR_CPACR_CP10_FULL_ACCESS | CORTEXR_CPACR_CP11_FULL_ACCESS;
	cortexr_coproc_write(target, CORTEXR_CPACR, cpacr);
	const bool core_has_fpu = cortexr_coproc_read(target, CORTEXR_CPACR) == cpacr;
	DEBUG_TARGET("%s: FPU present? %s\n", __func__, core_has_fpu ? "yes" : "no");

	if (core_has_fpu) {
		target->target_options |= TOPT_FLAVOUR_FLOAT;
		target->regs_size += sizeof(uint32_t) * CORTEX_FLOAT_REG_COUNT;
		cortexr_float_regs_save(target);
	}

	/* Check cache type */
	const uint32_t cache_type = cortex_dbg_read32(target, CORTEXR_CTR);
	if (cache_type >> CORTEX_CTR_FORMAT_SHIFT == CORTEX_CTR_FORMAT_ARMv7) {
		/* If there is an ICache defined, decompress its length to a uint32_t count */
		if (cache_type & CORTEX_CTR_ICACHE_LINE_MASK)
			priv->base.icache_line_length = CORTEX_CTR_ICACHE_LINE(cache_type);
		/* If there is a DCache defined, decompress its length to a uint32_t count */
		if ((cache_type >> CORTEX_CTR_DCACHE_LINE_SHIFT) & CORTEX_CTR_DCACHE_LINE_MASK)
			priv->base.dcache_line_length = CORTEX_CTR_DCACHE_LINE(cache_type);

		DEBUG_TARGET("%s: ICache line length = %u, DCache line length = %u\n", __func__,
			priv->base.icache_line_length << 2U, priv->base.dcache_line_length << 2U);
	} else
		target_check_error(target);

#if PC_HOSTED == 0
	gdb_outf("Please report unknown device with Designer 0x%x Part ID 0x%x\n", target->designer_code, target->part_id);
#else
	DEBUG_WARN(
		"Please report unknown device with Designer 0x%x Part ID 0x%x\n", target->designer_code, target->part_id);
#endif
	return true;
}

static void cortexr_halt_request(target_s *const target)
{
	volatile exception_s error;
	TRY_CATCH (error, EXCEPTION_TIMEOUT) {
		cortex_dbg_write32(target, CORTEXR_DBG_DRCR, CORTEXR_DBG_DRCR_HALT_REQ);
	}
	if (error.type)
		tc_printf(target, "Timeout sending interrupt, is target in WFI?\n");
}

static target_halt_reason_e cortexr_halt_poll(target_s *const target, target_addr_t *const watch)
{
	volatile uint32_t dscr = 0;
	volatile exception_s error;
	TRY_CATCH (error, EXCEPTION_ALL) {
		/* If this times out because the target is in WFI then the target is still running. */
		dscr = cortex_dbg_read32(target, CORTEXR_DBG_DSCR);
	}
	switch (error.type) {
	case EXCEPTION_ERROR:
		/* Things went seriously wrong and there is no recovery from this... */
		target_list_free();
		return TARGET_HALT_ERROR;
	case EXCEPTION_TIMEOUT:
		/* Timeout isn't actually a problem and probably means target is in WFI */
		return TARGET_HALT_RUNNING;
	}

	/* Check that the core actually halted */
	if (!(dscr & CORTEXR_DBG_DSCR_HALTED))
		return TARGET_HALT_RUNNING;

	/* Make sure ITR is enabled */
	cortex_dbg_write32(target, CORTEXR_DBG_DSCR, dscr | CORTEXR_DBG_DSCR_ITR_ENABLE);

	/* Save the target core's registers as debugging operations clobber them */
	cortexr_core_regs_save(target);
	if (target->target_options & TOPT_FLAVOUR_FLOAT)
		cortexr_float_regs_save(target);

	target_halt_reason_e reason = TARGET_HALT_FAULT;
	/* Determine why we halted exactly from the Method Of Entry bits */
	switch (dscr & CORTEXR_DBG_DSCR_MOE_MASK) {
	case CORTEXR_DBG_DSCR_MOE_HALT_REQUEST:
		reason = TARGET_HALT_REQUEST;
		break;
	case CORTEXR_DBG_DSCR_MOE_EXTERNAL_DBG:
	case CORTEXR_DBG_DSCR_MOE_BREAKPOINT:
	case CORTEXR_DBG_DSCR_MOE_BKPT_INSN:
	case CORTEXR_DBG_DSCR_MOE_VEC_CATCH:
		reason = TARGET_HALT_BREAKPOINT;
		break;
	case CORTEXR_DBG_DSCR_MOE_ASYNC_WATCH:
	case CORTEXR_DBG_DSCR_MOE_SYNC_WATCH:
		/* TODO: determine the watchpoint we hit */
		(void)watch;
		reason = TARGET_HALT_WATCHPOINT;
		break;
	}
	/* Check if we halted because we were actually single-stepping */
	return reason;
}

static void cortexr_halt_resume(target_s *const target, const bool step)
{
	(void)step;
	/* Restore the core's registers so the running program doesn't know we've been in there */
	if (target->target_options & TOPT_FLAVOUR_FLOAT)
		cortexr_float_regs_restore(target);
	cortexr_core_regs_restore(target);

	/* Ask to resume the core */
	cortex_dbg_write32(target, CORTEXR_DBG_DRCR, CORTEXR_DBG_DRCR_CLR_STICKY_EXC | CORTEXR_DBG_DRCR_RESTART_REQ);

	/* Then poll for when the core actually resumes */
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);
	uint32_t status = CORTEXR_DBG_DSCR_HALTED;
	while (!(status & CORTEXR_DBG_DSCR_RESTARTED) && !platform_timeout_is_expired(&timeout))
		status = cortex_dbg_read32(target, CORTEXR_DBG_DSCR);
}
