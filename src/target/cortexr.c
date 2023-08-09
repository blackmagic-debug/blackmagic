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
#include "adiv5.h"
#include "target.h"
#include "target_internal.h"
#include "target_probe.h"
#include "jep106.h"
#include "cortex.h"
#include "cortex_internal.h"

typedef struct cortexr_priv {
	/* Base core information */
	cortex_priv_s base;
} cortexr_priv_s;

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

#define CORTEXR_DBG_DSCR_INSN_COMPLETE  (1U << 24U)
#define CORTEXR_DBG_DSCR_DTR_READ_READY (1U << 29U)
#define CORTEXR_DBG_DSCR_DTR_WRITE_DONE (1U << 30U)

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

static inline uint32_t cortexr_core_reg_read(target_s *const target, const uint8_t reg)
{
	/* Build an issue a core to coprocessor transfer for the requested register and read back the result */
	return cortexr_run_read_insn(target, ARM_MCR_INSN | ENCODE_CP_ACCESS(14, 0, reg, 0, 5, 0));
}

uint32_t cortexr_coproc_read(target_s *const target, const uint8_t coproc, const uint16_t op)
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

	cortex_read_cpuid(target);

	return true;
}
