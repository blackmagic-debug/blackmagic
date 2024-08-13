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
#include "cortexar.h"
#include "cortex_internal.h"
#include "gdb_reg.h"
#include "gdb_packet.h"
#include "maths_utils.h"
#include "buffer_utils.h"

#include <assert.h>

typedef struct cortexar_priv {
	/* Base core information */
	cortex_priv_s base;

	/* Core registers cache */
	struct {
		uint32_t r[16U];
		uint32_t cpsr;
		uint32_t spsr[5U];
		uint64_t d[16U];
		uint32_t fpcsr;
	} core_regs;

	/* Fault status/address cache */
	uint32_t fault_status;
	uint32_t fault_address;

	/* Control and status information */
	uint8_t core_status;
} cortexar_priv_s;

#define CORTEXAR_DBG_IDR   0x000U /* ID register */
#define CORTEXAR_DBG_WFAR  0x018U
#define CORTEXAR_DBG_VCR   0x01cU /* Vector Catch register */
#define CORTEXAR_DBG_DSCCR 0x028U
#define CORTEXAR_DBG_DTRTX 0x080U /* DBGDTRRXext */
#define CORTEXAR_DBG_ITR   0x084U /* Instruction register */
#define CORTEXAR_DBG_DSCR  0x088U /* Debug Status and Control register */
#define CORTEXAR_DBG_DTRRX 0x08cU /* DBGDTRTXext */
#define CORTEXAR_DBG_DRCR  0x090U /* Debug Run Control register */
#define CORTEXAR_DBG_BVR   0x100U
#define CORTEXAR_DBG_BCR   0x140U
#define CORTEXAR_DBG_WVR   0x180U
#define CORTEXAR_DBG_WCR   0x1c0U
#define CORTEXAR_DBG_OSLAR 0x300U /* OS Lock Access register */
#define CORTEXAR_DBG_OSLSR 0x304U /* OS Lock Status register */
#define CORTEXAR_DBG_OSSRR 0x308U /* OS Save/Restore register */
#define CORTEXAR_DBG_OSDLR 0x30cU /* OS Double-Lock register */
#define CORTEXAR_DBG_PRCR  0x310U /* Power and Reset Control register */
#define CORTEXAR_DBG_PRSR  0x314U /* Power and Reset Status register */

#define CORTEXAR_CPUID 0xd00U
#define CORTEXAR_CTR   0xd04U
#define CORTEXAR_PFR1  0xd24U
#define CORTEXAR_MMFR0 0xd30U

#define CORTEXAR_DBG_IDR_BREAKPOINT_MASK  0xfU
#define CORTEXAR_DBG_IDR_BREAKPOINT_SHIFT 24U
#define CORTEXAR_DBG_IDR_WATCHPOINT_MASK  0xfU
#define CORTEXAR_DBG_IDR_WATCHPOINT_SHIFT 28U

#define CORTEXAR_DBG_DSCR_HALTED             (1U << 0U)
#define CORTEXAR_DBG_DSCR_RESTARTED          (1U << 1U)
#define CORTEXAR_DBG_DSCR_MOE_MASK           0x0000003cU
#define CORTEXAR_DBG_DSCR_MOE_HALT_REQUEST   0x00000000U
#define CORTEXAR_DBG_DSCR_MOE_BREAKPOINT     0x00000004U
#define CORTEXAR_DBG_DSCR_MOE_ASYNC_WATCH    0x00000008U
#define CORTEXAR_DBG_DSCR_MOE_BKPT_INSN      0x0000000cU
#define CORTEXAR_DBG_DSCR_MOE_EXTERNAL_DBG   0x00000010U
#define CORTEXAR_DBG_DSCR_MOE_VEC_CATCH      0x00000014U
#define CORTEXAR_DBG_DSCR_MOE_SYNC_WATCH     0x00000028U
#define CORTEXAR_DBG_DSCR_SYNC_DATA_ABORT    (1U << 6U)
#define CORTEXAR_DBG_DSCR_INTERRUPT_DISABLE  (1U << 11U)
#define CORTEXAR_DBG_DSCR_ITR_ENABLE         (1U << 13U)
#define CORTEXAR_DBG_DSCR_HALTING_DBG_ENABLE (1U << 14U)
#define CORTEXAR_DBG_DCSR_DCC_MASK           0x00300000U
#define CORTEXAR_DBG_DCSR_DCC_NORMAL         0x00000000U
#define CORTEXAR_DBG_DCSR_DCC_STALL          0x00100000U
#define CORTEXAR_DBG_DCSR_DCC_FAST           0x00200000U
#define CORTEXAR_DBG_DSCR_INSN_COMPLETE      (1U << 24U)
#define CORTEXAR_DBG_DSCR_DTR_READ_READY     (1U << 29U)
#define CORTEXAR_DBG_DSCR_DTR_WRITE_DONE     (1U << 30U)

#define CORTEXAR_DBG_DRCR_HALT_REQ           (1U << 0U)
#define CORTEXAR_DBG_DRCR_RESTART_REQ        (1U << 1U)
#define CORTEXAR_DBG_DRCR_CLR_STICKY_EXC     (1U << 2U)
#define CORTEXAR_DBG_DRCR_CLR_STICKY_PIPEADV (1U << 3U)
#define CORTEXAR_DBG_DRCR_CANCEL_BUS_REQ     (1U << 4U)

#define CORTEXAR_DBG_BCR_ENABLE                      0x00000001U
#define CORTEXAR_DBG_BCR_TYPE_UNLINKED_INSN_MATCH    0x00000000U
#define CORTEXAR_DBG_BCR_TYPE_UNLINKED_INSN_MISMATCH 0x00400000U
#define CORTEXAR_DBG_BCR_ALL_MODES                   0x00002006U
#define CORTEXAR_DBG_BCR_BYTE_SELECT_ALL             0x000001e0U
#define CORTEXAR_DBG_BCR_BYTE_SELECT_LOW_HALF        0x00000060U
#define CORTEXAR_DBG_BCR_BYTE_SELECT_HIGH_HALF       0x00000180U

#define CORTEXAR_DBG_WCR_ENABLE             0x00000001U
#define CORTEXAR_DBG_WCR_MATCH_ON_LOAD      0x00000008U
#define CORTEXAR_DBG_WCR_MATCH_ON_STORE     0x00000010U
#define CORTEXAR_DBG_WCR_MATCH_ANY_ACCESS   0x00000018U
#define CORTEXAR_DBG_WCR_ALL_MODES          0x00002006U
#define CORTEXAR_DBG_WCR_BYTE_SELECT_OFFSET 5U
#define CORTEXAR_DBG_WCR_BYTE_SELECT_MASK   0x00001fe0U
#define CORTEXAR_DBG_WCR_BYTE_SELECT(x) \
	(((x) << CORTEXAR_DBG_WCR_BYTE_SELECT_OFFSET) & CORTEXAR_DBG_WCR_BYTE_SELECT_MASK)

#define CORTEXAR_DBG_OSLSR_OS_LOCK_MODEL         0x00000009U
#define CORTEXAR_DBG_OSLSR_OS_LOCK_MODEL_FULL    0x00000001U
#define CORTEXAR_DBG_OSLSR_OS_LOCK_MODEL_PARTIAL 0x00000008U
#define CORTEXAR_DBG_OSLSR_LOCKED                (1U << 1U)

#define CORTEXAR_DBG_PRCR_CORE_POWER_DOWN_REQ  (1U << 0U)
#define CORTEXAR_DBG_PRCR_CORE_WARM_RESET_REQ  (1U << 1U)
#define CORTEXAR_DBG_PRCR_HOLD_CORE_WARM_RESET (1U << 2U)
#define CORTEXAR_DBG_PRCR_CORE_POWER_UP_REQ    (1U << 3U)

#define CORTEXAR_DBG_PRSR_POWERED_UP   (1U << 0U)
#define CORTEXAR_DBG_PRSR_STICKY_PD    (1U << 1U)
#define CORTEXAR_DBG_PRSR_RESET_ACTIVE (1U << 2U)
#define CORTEXAR_DBG_PRSR_STICKY_RESET (1U << 3U)
#define CORTEXAR_DBG_PRSR_HALTED       (1U << 4U)
#define CORTEXAR_DBG_PRSR_OS_LOCK      (1U << 5U)
#define CORTEXAR_DBG_PRSR_DOUBLE_LOCK  (1U << 6U)

/*
 * Instruction encodings for reading/writing the program counter to/from r0,
 * reading/writing CPSR to/from r0, and reading/writing the SPSRs to/from r0.
 */
#define ARM_MOV_R0_PC_INSN   0xe1a0000fU
#define ARM_MOV_PC_R0_INSN   0xe1a0f000U
#define ARM_MRS_R0_CPSR_INSN 0xe10f0000U
#define ARM_MSR_CPSR_R0_INSN 0xe12ff000U
#define ARM_MRS_R0_SPSR_INSN 0xe1400200U
#define ARM_MSR_SPSR_R0_INSN 0xe160f200U

/* CPSR register definitions */
#define CORTEXAR_CPSR_MODE_MASK 0xffffffe0U
#define CORTEXAR_CPSR_MODE_USER 0x00000010U
#define CORTEXAR_CPSR_MODE_SVC  0x00000013U
#define CORTEXAR_CPSR_MODE_MON  0x00000016U
#define CORTEXAR_CPSR_MODE_ABRT 0x00000017U
#define CORTEXAR_CPSR_MODE_HYP  0x0000001aU
#define CORTEXAR_CPSR_MODE_SYS  0x0000001fU
#define CORTEXAR_CPSR_THUMB     (1U << 5U)

/* CPSR remap position for GDB XML mapping */
#define CORTEXAR_CPSR_GDB_REMAP_POS 25U

/* Banked register offsets for when using using the DB{0,3} interface */
enum {
	CORTEXAR_BANKED_DTRTX,
	CORTEXAR_BANKED_ITR,
	CORTEXAR_BANKED_DCSR,
	CORTEXAR_BANKED_DTRRX
};

/*
 * Table of encodings for the banked SPSRs - These are encoded in the following format:
 * Bits[0]: SYSm[0]
 * Bits[15:12]: SYSm[4:1]
 * This allows these values to simply be shifted up a little to put them in the right spot
 * for use in the banked MRS/MSR instructions.
 */
static const uint16_t cortexar_spsr_encodings[5] = {
	0xc001U, /* FIQ */
	0x1000U, /* IRQ */
	0x5000U, /* SVC */
	0x9000U, /* ABT */
	0xd000U, /* UND */
};

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
/* Packs a CRn and CRm value for the coprocessor IO routines below to unpack */
#define ENCODE_CP_REG(n, m, opc1, opc2) \
	((((n)&0xfU) << 4U) | ((m)&0xfU) | (((opc1)&0x7U) << 8U) | (((opc2)&0x7U) << 12U))

/*
 * Instruction encodings for coprocessor load/store
 * LDC -> Load Coprocessor (DDI0406C §A8.8.56, pg393)
 * STC -> Store Corprocessor (DDI0406C §A8.8.119, pg663)
 */
#define ARM_LDC_INSN 0xec100000U
#define ARM_STC_INSN 0xec000000U
/*
 * Pre-encoded LDC/STC operands for getting data in and out of the core
 * The first is a LDC encoded to move [r0] to the debug DTR and then increment r0 by 4
 * (`LDC p14, c5, [r0], #+4`, Preincrement = 0, Unindexed = 1, Doublelength = 0, Writeback = 1)
 * The immediate is encoded shifted right by 2, and the reads are done 32 bits at a time.
 * The second is a STC encoded to move from the debug DTR to [r0] and then increment r0 by 4
 * (`STC p14, c5, [r0], #+4`, Preincrement = 0, Unindexed = 1, Doublelength = 0, Writeback = 1)
 * As with read, the immediate is encoded shifted right by 2 and writes are done 32 bits at a time.
 */
#define ARM_LDC_R0_POSTINC4_DTRTX_INSN (ARM_LDC_INSN | 0x00a05e01U)
#define ARM_STC_DTRRX_R0_POSTINC4_INSN (ARM_STC_INSN | 0x00a05e01U)

/*
 * Instruction encodings for indirect loads and stores of data via the CPU
 * LDRB -> Load Register Byte (immediate) (DDI0406C §A8.8.69, pg419)
 * LDRH -> Load Register Halfword (immediate) (DDI0406C §A8.8.81, pg443)
 * STRB -> Store Register Byte (immediate) (DDI0406C §A8.8.208, pg681)
 * STRH -> Store Register Halfword (immediate) (DDI0406C §A8.8.218, pg701)
 *
 * The first is `LDRB r1, [r0], #+1` to load a uint8_t from [r0] into r1 and increment the
 * address in r0 by 1, writing the new address back to r0.
 * The second is `LDRH r1, [r0], #+2` to load a uint16_t from [r0] into r1 and increment
 * the address in r0 by 2, writing the new address back to r0.
 * The third is `STRB r1, [r0], #+1` to store a uint8_t to [r0] from r1 and increment the
 * address in r0 by 1, writing the new address back to r0.
 * The fourth is `STRH r1, [r0], #+2` to store a uint16_t to [r0] from r1 and increment
 * the address in r0 by 2, writing the new address back to r0.
 */
#define ARM_LDRB_R0_R1_INSN 0xe4f01001U
#define ARM_LDRH_R0_R1_INSN 0xe0f010b2U
#define ARM_STRB_R1_R0_INSN 0xe4e01001U
#define ARM_STRH_R1_R0_INSN 0xe0e010b2U

/* Instruction encodings for synchronisation barriers */
#define ARM_ISB_INSN 0xe57ff06fU

/* Coprocessor register definitions */

/* Co-Processor Access Control Register */
#define CORTEXAR_CPACR 15U, ENCODE_CP_REG(1U, 0U, 0U, 2U)
/* Current Cache Size ID Register */
#define CORTEXAR_CCSIDR 15U, ENCODE_CP_REG(0U, 0U, 1U, 0U)
/* Cache Level ID Register */
#define CORTEXAR_CLIDR 15U, ENCODE_CP_REG(0U, 0U, 1U, 1U)
/* Cache Size Selection Register */
#define CORTEXAR_CSSELR 15U, ENCODE_CP_REG(0U, 0U, 2U, 0U)
/* Data Fault Status Register */
#define CORTEXAR_DFSR 15U, ENCODE_CP_REG(5U, 0U, 0U, 0U)
/* Data Fault Address Register */
#define CORTEXAR_DFAR 15U, ENCODE_CP_REG(6U, 0U, 0U, 0U)
/* Physical Address Register */
#define CORTEXAR_PAR32 15U, ENCODE_CP_REG(7U, 4U, 0U, 0U)
/* Instruction Cache Invalidate ALL to Unification */
#define CORTEXAR_ICIALLU 15U, ENCODE_CP_REG(7U, 5U, 0U, 0U)
/* Data Cache Clean + Invalidate by Set/Way to Unification */
#define CORTEXAR_DCCISW 15U, ENCODE_CP_REG(7U, 14U, 0U, 2U)
/* Address Translate Stage 1 Current state PL1 Read */
#define CORTEXAR_ATS1CPR 15U, ENCODE_CP_REG(7U, 8U, 0U, 0U)

#define CORTEXAR_CPACR_CP10_FULL_ACCESS 0x00300000U
#define CORTEXAR_CPACR_CP11_FULL_ACCESS 0x00c00000U

#define CORTEXAR_CLIDR_LEVEL_OF_COHERENCE_MASK  0x07000000U
#define CORTEXAR_CLIDR_LEVEL_OF_COHERENCE_SHIFT 24U

#define CORTEXAR_CACHE_MASK   0x07U
#define CORTEXAR_ICACHE_MASK  0x01U
#define CORTEXAR_DCACHE_MASK  0x02U
#define CORTEXAR_HAS_NO_CACHE 0x00U
#define CORTEXAR_HAS_ICACHE   0x01U
#define CORTEXAR_HAS_DCACHE   0x02U
#define CORTEXAR_HAS_UCACHE   0x04U

#define CORTEXAR_PAR32_FAULT 0x00000001U

#define CORTEXAR_PFR1_SEC_EXT_MASK  0x000000f0U
#define CORTEXAR_PFR1_VIRT_EXT_MASK 0x0000f000U

#define CORTEXAR_MMFR0_VMSA_MASK 0x0000000fU
#define CORTEXAR_MMFR0_PMSA_MASK 0x000000f0U

#define TOPT_FLAVOUR_FLOAT    (1U << 1U) /* If set, core has a hardware FPU */
#define TOPT_FLAVOUR_SEC_EXT  (1U << 2U) /* If set, core has security extensions */
#define TOPT_FLAVOUR_VIRT_EXT (1U << 3U) /* If set, core has virtualisation extensions */
#define TOPT_FLAVOUR_VIRT_MEM (1U << 4U) /* If set, core uses the virtual memory model, not protected */

#define CORTEXAR_STATUS_DATA_FAULT        (1U << 0U)
#define CORTEXAR_STATUS_MMU_FAULT         (1U << 1U)
#define CORTEXAR_STATUS_FAULT_CACHE_VALID (1U << 2U)

/*
 * Fields for Cortex-R special-purpose registers, used in the generation of GDB's target description XML.
 * The general-purpose registers r0-r12 and the vector floating point (VFP) registers d0-d15 all follow a
 * very regular format, so we only need to store fields for the special-purpose registers.
 * The array for each SPR field have the same order as each other, making each of these pseudo
 * 'associative array's.
 */

/* Cortex-R special-purpose register name strings */
static const char *cortexr_spr_names[] = {
	"sp",
	"lr",
	"pc",
	"cpsr",
};

/* Cortex-R special-purpose register types */
static const gdb_reg_type_e cortexr_spr_types[] = {
	GDB_TYPE_DATA_PTR,    /* sp */
	GDB_TYPE_CODE_PTR,    /* lr */
	GDB_TYPE_CODE_PTR,    /* pc */
	GDB_TYPE_UNSPECIFIED, /* cpsr */
};

/* clang-format off */
static_assert(ARRAY_LENGTH(cortexr_spr_types) == ARRAY_LENGTH(cortexr_spr_names),
	"SPR array length mistmatch! SPR type array should have the same length as SPR name array."
);
/* clang-format on */

static bool cortexar_check_error(target_s *target);
static void cortexar_mem_read(target_s *target, void *dest, target_addr64_t src, size_t len);
static void cortexar_mem_write(target_s *target, target_addr64_t dest, const void *src, size_t len);

static void cortexar_regs_read(target_s *target, void *data);
static void cortexar_regs_write(target_s *target, const void *data);
static size_t cortexar_reg_read(target_s *target, uint32_t reg, void *data, size_t max);
static size_t cortexar_reg_write(target_s *target, uint32_t reg, const void *data, size_t max);

static void cortexar_reset(target_s *target);
static target_halt_reason_e cortexar_halt_poll(target_s *target, target_addr_t *watch);
static void cortexar_halt_request(target_s *target);
static void cortexar_halt_resume(target_s *target, bool step);

static int cortexar_breakwatch_set(target_s *target, breakwatch_s *breakwatch);
static int cortexar_breakwatch_clear(target_s *target, breakwatch_s *breakwatch);
static void cortexar_config_breakpoint(target_s *target, size_t slot, uint32_t mode, target_addr_t addr);

bool cortexar_attach(target_s *target);
void cortexar_detach(target_s *target);

static const char *cortexar_target_description(target_s *target);

static void cortexar_banked_dcc_mode(target_s *const target)
{
	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	if (!(priv->base.ap->dp->quirks & ADIV5_AP_ACCESS_BANKED)) {
		priv->base.ap->dp->quirks |= ADIV5_AP_ACCESS_BANKED;
		/* Configure the AP to put {DBGDTR{TX,RX},DBGITR,DBGDCSR} in banked data registers window */
		adiv5_mem_access_setup(priv->base.ap, priv->base.base_addr + CORTEXAR_DBG_DTRTX, ALIGN_32BIT);
		/* Selecting AP bank 1 to finish switching into banked mode */
		adiv5_dp_write(priv->base.ap->dp, ADIV5_DP_SELECT, ((uint32_t)priv->base.ap->apsel << 24U) | 0x10U);
	}
}

static bool cortexar_check_data_abort(target_s *const target, const uint32_t status)
{
	/* If the instruction triggered a synchronous data abort, signal failure having cleared it */
	if (status & CORTEXAR_DBG_DSCR_SYNC_DATA_ABORT) {
		cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
		priv->core_status |= CORTEXAR_STATUS_DATA_FAULT;
		cortex_dbg_write32(target, CORTEXAR_DBG_DRCR, CORTEXAR_DBG_DRCR_CLR_STICKY_EXC);
	}
	return !(status & CORTEXAR_DBG_DSCR_SYNC_DATA_ABORT);
}

static bool cortexar_run_insn(target_s *const target, const uint32_t insn)
{
	const cortexar_priv_s *const priv = (const cortexar_priv_s *)target->priv;
	/* Make sure we're in banked mode */
	cortexar_banked_dcc_mode(target);
	/* Issue the requested instruction to the core */
	adiv5_dp_write(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_ITR), insn);
	/* Poll for the instruction to complete */
	uint32_t status = 0;
	while (!(status & CORTEXAR_DBG_DSCR_INSN_COMPLETE))
		status = adiv5_dp_read(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DCSR));
	/* Check if the instruction triggered a synchronous data abort */
	return cortexar_check_data_abort(target, status);
}

static bool cortexar_run_read_insn(target_s *const target, const uint32_t insn, uint32_t *const result)
{
	const cortexar_priv_s *const priv = (const cortexar_priv_s *)target->priv;
	/* Make sure we're in banked mode */
	cortexar_banked_dcc_mode(target);
	/* Issue the requested instruction to the core */
	adiv5_dp_write(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_ITR), insn);
	/* Poll for the instruction to complete and the data to become ready in the DTR */
	uint32_t status = 0;
	while ((status & (CORTEXAR_DBG_DSCR_INSN_COMPLETE | CORTEXAR_DBG_DSCR_DTR_READ_READY)) !=
		(CORTEXAR_DBG_DSCR_INSN_COMPLETE | CORTEXAR_DBG_DSCR_DTR_READ_READY)) {
		status = adiv5_dp_read(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DCSR));
		/* Check if the instruction triggered a synchronous data abort */
		if (!cortexar_check_data_abort(target, status))
			return false;
	}
	/* Read back the DTR to complete the read and signal success */
	*result = adiv5_dp_read(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DTRRX));
	return true;
}

static bool cortexar_run_write_insn(target_s *const target, const uint32_t insn, const uint32_t data)
{
	const cortexar_priv_s *const priv = (const cortexar_priv_s *)target->priv;
	/* Make sure we're in banked mode */
	cortexar_banked_dcc_mode(target);
	/* Set up the data in the DTR for the transaction */
	adiv5_dp_write(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DTRTX), data);
	/* Poll for the data to become ready in the DTR */
	while (!(adiv5_dp_read(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DCSR)) & CORTEXAR_DBG_DSCR_DTR_WRITE_DONE))
		continue;
	/* Issue the requested instruction to the core */
	adiv5_dp_write(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_ITR), insn);
	/* Poll for the instruction to complete and the data to be consumed from the DTR */
	uint32_t status = 0;
	while ((status & (CORTEXAR_DBG_DSCR_INSN_COMPLETE | CORTEXAR_DBG_DSCR_DTR_WRITE_DONE)) !=
		CORTEXAR_DBG_DSCR_INSN_COMPLETE) {
		status = adiv5_dp_read(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DCSR));
		/* Check if the instruction triggered a synchronous data abort */
		if (!cortexar_check_data_abort(target, status))
			return false;
	}
	return true;
}

static inline uint32_t cortexar_core_reg_read(target_s *const target, const uint8_t reg)
{
	/* If the register is a GPR and not the program counter, use a "simple" MCR to read */
	if (reg < 15U) {
		uint32_t value = 0;
		/* Build an issue a core to coprocessor transfer for the requested register and read back the result */
		(void)cortexar_run_read_insn(target, ARM_MCR_INSN | ENCODE_CP_ACCESS(14, 0, reg, 0, 5, 0), &value);
		/* Return whatever value was read as we don't care about DCSR.SDABORT here */
		return value;
	}
	/* If the register is the program counter, we first have to extract it to r0 */
	else if (reg == 15U) {
		cortexar_run_insn(target, ARM_MOV_R0_PC_INSN);
		return cortexar_core_reg_read(target, 0U);
	}
	return 0U;
}

static void cortexar_core_regs_save(target_s *const target)
{
	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	/* Save out r0-r15 in that order (r15, aka pc, clobbers r0) */
	for (size_t i = 0U; i < ARRAY_LENGTH(priv->core_regs.r); ++i)
		priv->core_regs.r[i] = cortexar_core_reg_read(target, i);
	/* Read CPSR to r0 and retrieve it */
	cortexar_run_insn(target, ARM_MRS_R0_CPSR_INSN);
	priv->core_regs.cpsr = cortexar_core_reg_read(target, 0U);
	/* Adjust the program counter according to the mode */
	priv->core_regs.r[CORTEX_REG_PC] -= (priv->core_regs.cpsr & CORTEXAR_CPSR_THUMB) ? 4U : 8U;
	/* Read the SPSRs into r0 and retrieve them */
	for (size_t i = 0; i < ARRAY_LENGTH(priv->core_regs.spsr); ++i) {
		/* Build and issue the banked MRS for the required SPSR */
		cortexar_run_insn(target, ARM_MRS_R0_SPSR_INSN | (cortexar_spsr_encodings[i] << 4U));
		priv->core_regs.spsr[i] = cortexar_core_reg_read(target, 0U);
	}
}

static void cortexar_float_regs_save(target_s *const target)
{
	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	/* Read FPCSR to r0 and retrieve it */
	cortexar_run_insn(target, ARM_VMRS_R0_FPCSR_INSN);
	priv->core_regs.fpcsr = cortexar_core_reg_read(target, 0U);
	/* Now step through each double-precision float register, reading it back to r0,r1 */
	for (size_t i = 0; i < ARRAY_LENGTH(priv->core_regs.d); ++i) {
		/* The float register to read slots into the bottom 4 bits of the instruction */
		cortexar_run_insn(target, ARM_VMOV_R0_R1_DN_INSN | i);
		/* Read back the data */
		const uint32_t d_low = cortexar_core_reg_read(target, 0U);
		const uint32_t d_high = cortexar_core_reg_read(target, 1U);
		/* Reassemble it as a full 64-bit value */
		priv->core_regs.d[i] = d_low | ((uint64_t)d_high << 32U);
	}
}

static void cortexar_regs_save(target_s *const target)
{
	cortexar_core_regs_save(target);
	if (target->target_options & TOPT_FLAVOUR_FLOAT)
		cortexar_float_regs_save(target);
}

static inline void cortexar_core_reg_write(target_s *const target, const uint8_t reg, const uint32_t value)
{
	/* If the register is a GPR and not the program counter, use a "simple" MCR to read */
	if (reg < 15U)
		/* Build and issue a coprocessor to core transfer for the requested register and send the new data */
		cortexar_run_write_insn(target, ARM_MRC_INSN | ENCODE_CP_ACCESS(14, 0, reg, 0, 5, 0), value);
	/* If the register is the program counter, we first have to write it to r0 */
	else if (reg == 15U) {
		cortexar_core_reg_write(target, 0U, value);
		cortexar_run_insn(target, ARM_MOV_PC_R0_INSN);
	}
}

static void cortexar_core_regs_restore(target_s *const target)
{
	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	/* Load the values for each of the SPSRs in turn into r0 and shove them back into place */
	for (size_t i = 0; i < ARRAY_LENGTH(priv->core_regs.spsr); ++i) {
		cortexar_core_reg_write(target, 0U, priv->core_regs.spsr[i]);
		/* Build and issue the banked MSR for the required SPSR */
		cortexar_run_insn(target, ARM_MSR_SPSR_R0_INSN | (cortexar_spsr_encodings[i] << 4U));
	}
	/* Load the value for CPSR to r0 and then shove it back into place */
	cortexar_core_reg_write(target, 0U, priv->core_regs.cpsr);
	cortexar_run_insn(target, ARM_MSR_CPSR_R0_INSN);
	/* Fix up the program counter for the mode */
	if (priv->core_regs.cpsr & CORTEXAR_CPSR_THUMB)
		priv->core_regs.r[CORTEX_REG_PC] |= 1U;
	/* Restore r1-15 in that order. Ignore r0 for the moment as it gets clobbered repeatedly */
	for (size_t i = 1U; i < ARRAY_LENGTH(priv->core_regs.r); ++i)
		cortexar_core_reg_write(target, i, priv->core_regs.r[i]);
	/* Now we're done with the rest of the registers, restore r0 */
	cortexar_core_reg_write(target, 0U, priv->core_regs.r[0U]);
}

static void cortexar_float_regs_restore(target_s *const target)
{
	const cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	/* Step through each double-precision float register, writing it back via r0,r1 */
	for (size_t i = 0; i < ARRAY_LENGTH(priv->core_regs.d); ++i) {
		/* Load the low 32 bits into r0, and the high into r1 */
		cortexar_core_reg_write(target, 0U, priv->core_regs.d[i] & UINT32_MAX);
		cortexar_core_reg_write(target, 1U, priv->core_regs.d[i] >> 32U);
		/* The float register to write slots into the bottom 4 bits of the instruction */
		cortexar_run_insn(target, ARM_VMOV_DN_R0_R1_INSN | i);
	}
	/* Load the value for FPCSR to r0 and then shove it back into place */
	cortexar_core_reg_write(target, 0U, priv->core_regs.fpcsr);
	cortexar_run_insn(target, ARM_VMSR_FPCSR_R0_INSN);
}

static void cortexar_regs_restore(target_s *const target)
{
	if (target->target_options & TOPT_FLAVOUR_FLOAT)
		cortexar_float_regs_restore(target);
	cortexar_core_regs_restore(target);
}

static uint32_t cortexar_coproc_read(target_s *const target, const uint8_t coproc, const uint16_t op)
{
	/*
	 * Perform a read of a coprocessor - which one (between 0 and 15) is given by the coproc parameter
	 * and which register of the coprocessor to read and the operands required is given by op.
	 * This follows the steps laid out in DDI0406C §C6.4.1 pg2109
	 *
	 * Encode the MCR (Move to ARM core register from Coprocessor) instruction in ARM ISA format
	 * using core reg r0 as the read target.
	 */
	cortexar_run_insn(target,
		ARM_MRC_INSN |
			ENCODE_CP_ACCESS(coproc & 0xfU, (op >> 8U) & 0x7U, 0U, (op >> 4U) & 0xfU, op & 0xfU, (op >> 12U) & 0x7U));
	const uint32_t result = cortexar_core_reg_read(target, 0U);
	DEBUG_PROTO("%s: coproc %u (%04x): %08" PRIx32 "\n", __func__, coproc, op, result);
	return result;
}

static void cortexar_coproc_write(target_s *const target, const uint8_t coproc, const uint16_t op, const uint32_t value)
{
	DEBUG_PROTO("%s: coproc %u (%04x): %08" PRIx32 "\n", __func__, coproc, op, value);
	/*
	 * Perform a write of a coprocessor - which one (between 0 and 15) is given by the coproc parameter
	 * and which register of the coprocessor to write and the operands required is given by op.
	 * This follows the steps laid out in DDI0406C §C6.4.1 pg2109
	 *
	 * Encode the MRC (Move to Coprocessor from ARM core register) instruction in ARM ISA format
	 * using core reg r0 as the write data source.
	 */
	cortexar_core_reg_write(target, 0U, value);
	cortexar_run_insn(target,
		ARM_MCR_INSN |
			ENCODE_CP_ACCESS(coproc & 0xfU, (op >> 8U) & 0x7U, 0U, (op >> 4U) & 0xfU, op & 0xfU, (op >> 12U) & 0x7U));
}

/*
 * Perform a virtual to physical address translation.
 * NB: This requires the core to be halted! Trashes r0.
 */
static target_addr_t cortexar_virt_to_phys(target_s *const target, const target_addr_t virt_addr)
{
	/* Check if the target is PMSA and return early if it is */
	if (!(target->target_options & TOPT_FLAVOUR_VIRT_MEM))
		return virt_addr;

	/*
	 * Now we know the target is VMSA and so has the address translation machinery,
	 * start by loading r0 with the VA to translate and request its translation
	 */
	cortexar_core_reg_write(target, 0U, virt_addr);
	cortexar_coproc_write(target, CORTEXAR_ATS1CPR, 0U);
	/*
	 * Ensure that's complete with a sync barrier, then read the result back
	 * from the physical address register into r0 so we can extract the result
	 */
	cortexar_run_insn(target, ARM_ISB_INSN);
	cortexar_coproc_read(target, CORTEXAR_PAR32);

	const uint32_t phys_addr = cortexar_core_reg_read(target, 0U);
	/* Check if the MMU indicated a translation failure, marking a fault if it did */
	if (phys_addr & CORTEXAR_PAR32_FAULT) {
		cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
		priv->core_status |= CORTEXAR_STATUS_MMU_FAULT;
	}

	/* Convert the physical address to a virtual one using the top 20 bits of PAR and the bottom 12 of the virtual. */
	const target_addr_t address = (phys_addr & 0xfffff000U) | (virt_addr & 0x00000fffU);
	return address;
}

static bool cortexar_oslock_unlock(target_s *const target)
{
	const uint32_t lock_status = cortex_dbg_read32(target, CORTEXAR_DBG_OSLSR);
	DEBUG_TARGET("%s: OS lock status: %08" PRIx32 "\n", __func__, lock_status);
	/* Check if the lock is implemented, then if it is, if it's set */
	if (((lock_status & CORTEXAR_DBG_OSLSR_OS_LOCK_MODEL) == CORTEXAR_DBG_OSLSR_OS_LOCK_MODEL_FULL ||
			(lock_status & CORTEXAR_DBG_OSLSR_OS_LOCK_MODEL) == CORTEXAR_DBG_OSLSR_OS_LOCK_MODEL_PARTIAL) &&
		(lock_status & CORTEXAR_DBG_OSLSR_LOCKED)) {
		/* Lock implemented, and set. Try to unlock */
		DEBUG_WARN("%s: OS lock set, unlocking\n", __func__);
		cortex_dbg_write32(target, CORTEXAR_DBG_OSLAR, 0U);

		/* Read back to check if we succeeded */
		const bool locked = cortex_dbg_read32(target, CORTEXAR_DBG_OSLSR) & CORTEXAR_DBG_OSLSR_LOCKED;
		if (locked)
			DEBUG_ERROR("%s: Lock sticky. Core not powered?\n", __func__);
		return !locked;
	}
	return true;
}

static bool cortexar_ensure_core_powered(target_s *const target)
{
	/* Read the power/reset status register and check if the core is up or down */
	uint8_t status = cortex_dbg_read32(target, CORTEXAR_DBG_PRSR) & 0xffU;
	if (!(status & CORTEXAR_DBG_PRSR_POWERED_UP)) {
		/* The core is powered down, so get it up. */
		cortex_dbg_write32(
			target, CORTEXAR_DBG_PRCR, CORTEXAR_DBG_PRCR_CORE_POWER_UP_REQ | CORTEXAR_DBG_PRCR_HOLD_CORE_WARM_RESET);
		/* Spin waiting for the core to come up */
		platform_timeout_s timeout;
		platform_timeout_set(&timeout, 250);
		while (!(cortex_dbg_read32(target, CORTEXAR_DBG_PRSR) & CORTEXAR_DBG_PRSR_POWERED_UP) &&
			!platform_timeout_is_expired(&timeout))
			continue;
		/*
		 * Assume it worked, because it's implementation-defined if we can even do a power-up this way.
		 * Clear the PRCR back to 0 so the hold and power-up requests don't interfere further.
		 */
		cortex_dbg_write32(target, CORTEXAR_DBG_PRCR, 0U);
	}
	/* Re-read the PRSR and check if the core actually powered on */
	status = cortex_dbg_read32(target, CORTEXAR_DBG_PRSR) & 0xffU;
	if (!(status & CORTEXAR_DBG_PRSR_POWERED_UP))
		return false;

	/* Check for the OS double lock */
	if (status & CORTEXAR_DBG_PRSR_DOUBLE_LOCK)
		return false;

	/*
	 * Finally, check for the normal OS Lock and clear it if it's set prior to halting the core.
	 * Trying to do this after target_halt_request() does not function over JTAG and triggers
	 * the lock sticky message.
	 */
	if (status & CORTEXAR_DBG_PRSR_OS_LOCK)
		return cortexar_oslock_unlock(target);
	return true;
}

static target_s *cortexar_probe(
	adiv5_access_port_s *const ap, const target_addr_t base_address, const char *const core_type)
{
	target_s *target = target_new();
	if (!target)
		return NULL;

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

	cortexar_priv_s *const priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return NULL;
	}

	target->driver = core_type;
	target->priv = priv;
	target->priv_free = cortex_priv_free;
	priv->base.ap = ap;
	priv->base.base_addr = base_address;

	target->reset = cortexar_reset;
	target->halt_request = cortexar_halt_request;
	target->halt_poll = cortexar_halt_poll;
	target->halt_resume = cortexar_halt_resume;

	/* Ensure the core is powered up and we can talk to it */
	cortexar_ensure_core_powered(target);

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
	const uint32_t debug_id = cortex_dbg_read32(target, CORTEXAR_DBG_IDR);
	/* Reserve the last available breakpoint for our use to implement single-stepping */
	priv->base.breakpoints_available =
		(debug_id >> CORTEXAR_DBG_IDR_BREAKPOINT_SHIFT) & CORTEXAR_DBG_IDR_BREAKPOINT_MASK;
	priv->base.watchpoints_available =
		((debug_id >> CORTEXAR_DBG_IDR_WATCHPOINT_SHIFT) & CORTEXAR_DBG_IDR_WATCHPOINT_MASK) + 1U;
	DEBUG_TARGET("%s %s core has %u breakpoint and %u watchpoint units available\n", target->driver, target->core,
		priv->base.breakpoints_available + 1U, priv->base.watchpoints_available);

	/* Read out processor feature register 1 and check for the security and virtualisation extensions */
	const uint32_t proc_features = cortex_dbg_read32(target, CORTEXAR_PFR1);
	if (proc_features & CORTEXAR_PFR1_SEC_EXT_MASK) {
		target->target_options |= TOPT_FLAVOUR_SEC_EXT;
		DEBUG_TARGET("%s: Core has security extensions\n", __func__);
	}
	if (proc_features & CORTEXAR_PFR1_VIRT_EXT_MASK) {
		target->target_options |= TOPT_FLAVOUR_VIRT_EXT;
		DEBUG_TARGET("%s: Core has virtualisation extensions\n", __func__);
	}

	/*
	 * Read out memory model feature register 0 and check for VMSA vs PMSA memory models to
	 * configure address translation and determine which cp15 registers we can poke.
	 */
	const uint32_t memory_model = cortex_dbg_read32(target, CORTEXAR_MMFR0);
	/* The manual says this cannot happen, if it does then assume VMSA */
	if ((memory_model & CORTEXAR_MMFR0_VMSA_MASK) && (memory_model & CORTEXAR_MMFR0_PMSA_MASK))
		DEBUG_ERROR("%s: Core claims to support both virtual and protected memory modes!\n", __func__);
	if (memory_model & CORTEXAR_MMFR0_VMSA_MASK)
		target->target_options |= TOPT_FLAVOUR_VIRT_MEM;
	DEBUG_TARGET(
		"%s: Core uses the %cMSA memory model\n", __func__, target->target_options & TOPT_FLAVOUR_VIRT_MEM ? 'V' : 'P');

	target->attach = cortexar_attach;
	target->detach = cortexar_detach;

	/* Probe for FP extension. */
	uint32_t cpacr = cortexar_coproc_read(target, CORTEXAR_CPACR);
	cpacr |= CORTEXAR_CPACR_CP10_FULL_ACCESS | CORTEXAR_CPACR_CP11_FULL_ACCESS;
	cortexar_coproc_write(target, CORTEXAR_CPACR, cpacr);
	const bool core_has_fpu = cortexar_coproc_read(target, CORTEXAR_CPACR) == cpacr;
	DEBUG_TARGET("%s: FPU present? %s\n", __func__, core_has_fpu ? "yes" : "no");

	target->regs_description = cortexar_target_description;
	target->regs_read = cortexar_regs_read;
	target->regs_write = cortexar_regs_write;
	target->reg_read = cortexar_reg_read;
	target->reg_write = cortexar_reg_write;
	target->regs_size = sizeof(uint32_t) * CORTEXAR_GENERAL_REG_COUNT;

	if (core_has_fpu) {
		target->target_options |= TOPT_FLAVOUR_FLOAT;
		target->regs_size += sizeof(uint32_t) * CORTEX_FLOAT_REG_COUNT;
		cortexar_float_regs_save(target);
	}

	target->check_error = cortexar_check_error;
	target->mem_read = cortexar_mem_read;
	target->mem_write = cortexar_mem_write;

	target->breakwatch_set = cortexar_breakwatch_set;
	target->breakwatch_clear = cortexar_breakwatch_clear;

	/* Check cache type */
	const uint32_t cache_type = cortex_dbg_read32(target, CORTEXAR_CTR);
	if (cache_type >> CORTEX_CTR_FORMAT_SHIFT == CORTEX_CTR_FORMAT_ARMv7) {
		/* If there is an ICache defined, decompress its length to a uint32_t count */
		if (cache_type & CORTEX_CTR_ICACHE_LINE_MASK)
			priv->base.icache_line_length = CORTEX_CTR_ICACHE_LINE(cache_type);
		/* If there is a DCache defined, decompress its length to a uint32_t count */
		if ((cache_type >> CORTEX_CTR_DCACHE_LINE_SHIFT) & CORTEX_CTR_DCACHE_LINE_MASK)
			priv->base.dcache_line_length = CORTEX_CTR_DCACHE_LINE(cache_type);

		DEBUG_TARGET("%s: ICache line length = %u, DCache line length = %u\n", __func__,
			priv->base.icache_line_length * 4U, priv->base.dcache_line_length * 4U);
	} else
		target_check_error(target);

	return target;
}

bool cortexa_probe(adiv5_access_port_s *const ap, const target_addr_t base_address)
{
	target_s *const target = cortexar_probe(ap, base_address, "ARM Cortex-A");
	if (!target)
		return false;

	switch (target->designer_code) {
	case JEP106_MANUFACTURER_STM:
		PROBE(stm32mp15_ca7_probe);
		break;
	case JEP106_MANUFACTURER_XILINX:
		PROBE(zynq7_probe);
		break;
	case JEP106_MANUFACTURER_RENESAS:
		PROBE(renesas_rz_probe);
		break;
	}

#if PC_HOSTED == 0
	gdb_outf("Please report unknown device with Designer 0x%x Part ID 0x%x\n", target->designer_code, target->part_id);
#else
	DEBUG_WARN(
		"Please report unknown device with Designer 0x%x Part ID 0x%x\n", target->designer_code, target->part_id);
#endif
	return true;
}

bool cortexr_probe(adiv5_access_port_s *const ap, const target_addr_t base_address)
{
	target_s *const target = cortexar_probe(ap, base_address, "ARM Cortex-R");
	if (!target)
		return false;

#if PC_HOSTED == 0
	gdb_outf("Please report unknown device with Designer 0x%x Part ID 0x%x\n", target->designer_code, target->part_id);
#else
	DEBUG_WARN(
		"Please report unknown device with Designer 0x%x Part ID 0x%x\n", target->designer_code, target->part_id);
#endif
	return true;
}

bool cortexar_attach(target_s *const target)
{
	adiv5_access_port_s *ap = cortex_ap(target);
	/* Mark the DP as being in fault so error recovery will switch to this core when in multi-drop mode */
	ap->dp->fault = 1;

	/* Clear any pending fault condition (and switch to this core) */
	target_check_error(target);

	/* Ensure the OS lock is unset just in case it was re-set between probe and attach */
	cortexar_oslock_unlock(target);
	/* Try to halt the core */
	target_halt_request(target);
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);
	target_halt_reason_e reason = TARGET_HALT_RUNNING;
	while (!platform_timeout_is_expired(&timeout) && reason == TARGET_HALT_RUNNING)
		reason = target_halt_poll(target, NULL);
	if (reason != TARGET_HALT_REQUEST) {
		DEBUG_ERROR("Failed to halt the core\n");
		return false;
	}

	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	/* Clear any stale breakpoints */
	priv->base.breakpoints_mask = 0U;
	for (size_t i = 0; i <= priv->base.breakpoints_available; ++i) {
		cortex_dbg_write32(target, CORTEXAR_DBG_BVR + (i << 2U), 0U);
		cortex_dbg_write32(target, CORTEXAR_DBG_BCR + (i << 2U), 0U);
	}

	/* Clear any stale watchpoints */
	priv->base.watchpoints_mask = 0U;
	for (size_t i = 0; i < priv->base.watchpoints_available; ++i) {
		cortex_dbg_write32(target, CORTEXAR_DBG_WVR + (i << 2U), 0U);
		cortex_dbg_write32(target, CORTEXAR_DBG_WCR + (i << 2U), 0U);
	}

	return true;
}

void cortexar_detach(target_s *const target)
{
	const cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;

	/* Clear any set breakpoints */
	for (size_t i = 0; i <= priv->base.breakpoints_available; ++i) {
		cortex_dbg_write32(target, CORTEXAR_DBG_BVR + (i << 2U), 0U);
		cortex_dbg_write32(target, CORTEXAR_DBG_BCR + (i << 2U), 0U);
	}

	/* Clear any set watchpoints */
	for (size_t i = 0; i < priv->base.watchpoints_available; ++i) {
		cortex_dbg_write32(target, CORTEXAR_DBG_WVR + (i << 2U), 0U);
		cortex_dbg_write32(target, CORTEXAR_DBG_WCR + (i << 2U), 0U);
	}

	target_halt_resume(target, false);
}

static bool cortexar_check_error(target_s *const target)
{
	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	const bool fault = priv->core_status & (CORTEXAR_STATUS_DATA_FAULT | CORTEXAR_STATUS_MMU_FAULT);
	priv->core_status = (uint8_t) ~(CORTEXAR_STATUS_DATA_FAULT | CORTEXAR_STATUS_MMU_FAULT);
	return fault || cortex_check_error(target);
}

/* Fast path for cortexar_mem_read(). Assumes the address to read data from is already loaded in r0. */
static inline bool cortexar_mem_read_fast(target_s *const target, uint32_t *const dest, const size_t count)
{
	/* If we need to read more than a couple of uint32_t's, DCC Fast mode makes more sense, so use it. */
	if (count > 2U) {
		cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
		/* Make sure we're banked mode */
		cortexar_banked_dcc_mode(target);
		/* Switch into DCC Fast mode */
		const uint32_t dbg_dcsr =
			adiv5_dp_read(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DCSR)) & ~CORTEXAR_DBG_DCSR_DCC_MASK;
		adiv5_dp_write(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DCSR), dbg_dcsr | CORTEXAR_DBG_DCSR_DCC_FAST);
		/* Set up continual load so we can hammer the DTR */
		adiv5_dp_write(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_ITR), ARM_LDC_R0_POSTINC4_DTRTX_INSN);
		/* Run the transfer, hammering the DTR */
		for (size_t offset = 0; offset < count; ++offset) {
			/* Read the next value, which is the value for the last instruction run */
			const uint32_t value = adiv5_dp_read(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DTRRX));
			/* If we've run the instruction at least once, store it */
			if (offset)
				dest[offset - 1U] = value;
		}
		/* Now read out the status from the DCSR in case anything went wrong */
		const uint32_t status = adiv5_dp_read(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DCSR));
		/* Go back into DCC Normal (Non-blocking) mode */
		adiv5_dp_write(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DCSR), dbg_dcsr | CORTEXAR_DBG_DCSR_DCC_NORMAL);
		/* Grab the value of the last instruction run now it won't run again */
		dest[count - 1U] = adiv5_dp_read(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DTRRX));
		/* Check if the instruction triggered a synchronous data abort */
		return cortexar_check_data_abort(target, status);
	}

	/* Read each of the uint32_t's checking for failure */
	for (size_t offset = 0; offset < count; ++offset) {
		if (!cortexar_run_read_insn(target, ARM_LDC_R0_POSTINC4_DTRTX_INSN, dest + offset))
			return false; /* Propagate failure if it happens */
	}
	return true; /* Signal success */
}

/* Slow path for cortexar_mem_read(). Trashes r0 and r1. */
static bool cortexar_mem_read_slow(target_s *const target, uint8_t *const data, target_addr_t addr, const size_t length)
{
	size_t offset = 0;
	/* If the address is odd, read a byte to get onto an even address */
	if (addr & 1U) {
		if (!cortexar_run_insn(target, ARM_LDRB_R0_R1_INSN))
			return false;
		data[offset++] = (uint8_t)cortexar_core_reg_read(target, 1U);
		++addr;
	}
	/* If the address is now even but only 16-bit aligned, read a uint16_t to get onto 32-bit alignment */
	if ((addr & 2U) && length - offset >= 2U) {
		if (!cortexar_run_insn(target, ARM_LDRH_R0_R1_INSN))
			return false;
		write_le2(data, offset, (uint16_t)cortexar_core_reg_read(target, 1U));
		offset += 2U;
	}
	/* Use the fast path to read as much as possible before doing a slow path fixup at the end */
	if (!cortexar_mem_read_fast(target, (uint32_t *)(data + offset), (length - offset) >> 2U))
		return false;
	const uint8_t remainder = (length - offset) & 3U;
	/* If the remainder needs at least 2 more bytes read, do this first */
	if (remainder & 2U) {
		if (!cortexar_run_insn(target, ARM_LDRH_R0_R1_INSN))
			return false;
		write_le2(data, offset, (uint16_t)cortexar_core_reg_read(target, 1U));
		offset += 2U;
	}
	/* Finally, fix things up if a final byte is required. */
	if (remainder & 1U) {
		if (!cortexar_run_insn(target, ARM_LDRB_R0_R1_INSN))
			return false;
		data[offset] = (uint8_t)cortexar_core_reg_read(target, 1U);
	}
	return true; /* Signal success */
}

static void cortexar_mem_handle_fault(target_s *const target, const char *const func)
{
	const cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	/* If we suffered a fault of some kind, grab the reason and restore DFSR/DFAR */
	if (priv->core_status & CORTEXAR_STATUS_DATA_FAULT) {
#ifndef DEBUG_WARN_IS_NOOP
		const uint32_t fault_status = cortexar_coproc_read(target, CORTEXAR_DFSR);
		const uint32_t fault_address = cortexar_coproc_read(target, CORTEXAR_DFAR);
		DEBUG_WARN("%s: Failed at 0x%08" PRIx32 " (%08" PRIx32 ")\n", func, fault_address, fault_status);
#else
		(void)func;
#endif
		cortexar_coproc_write(target, CORTEXAR_DFAR, priv->fault_address);
		cortexar_coproc_write(target, CORTEXAR_DFSR, priv->fault_status);
	}
}

/*
 * This reads memory by jumping from the debug unit bus to the system bus.
 * NB: This requires the core to be halted! Uses instruction launches on
 * the core and requires we're in debug mode to work. Trashes r0.
 */
static void cortexar_mem_read(target_s *const target, void *const dest, const target_addr64_t src, const size_t len)
{
	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	/* Cache DFSR and DFAR in case we wind up triggering a data fault */
	if (!(priv->core_status & CORTEXAR_STATUS_FAULT_CACHE_VALID)) {
		priv->fault_status = cortexar_coproc_read(target, CORTEXAR_DFSR);
		priv->fault_address = cortexar_coproc_read(target, CORTEXAR_DFAR);
		priv->core_status |= CORTEXAR_STATUS_FAULT_CACHE_VALID;
	}
	/* Clear any existing fault state */
	priv->core_status &= ~(CORTEXAR_STATUS_DATA_FAULT | CORTEXAR_STATUS_MMU_FAULT);

	/* Move the start address into the core's r0 */
	cortexar_core_reg_write(target, 0U, src);

	/* If the address is 32-bit aligned and we're reading 32 bits at a time, use the fast path */
	if ((src & 3U) == 0U && (len & 3U) == 0U)
		cortexar_mem_read_fast(target, (uint32_t *)dest, len >> 2U);
	else
		cortexar_mem_read_slow(target, (uint8_t *)dest, src, len);
	/* Deal with any data faults that occurred */
	cortexar_mem_handle_fault(target, __func__);

	DEBUG_PROTO("%s: Reading %zu bytes @0x%" PRIx64 ":", __func__, len, src);
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
}

/* Fast path for cortexar_mem_write(). Assumes the address to read data from is already loaded in r0. */
static inline bool cortexar_mem_write_fast(target_s *const target, const uint32_t *const src, const size_t count)
{
	/* If we need to write more than a couple of uint32_t's, DCC Fast mode makes more sense, so use it. */
	if (count > 2U) {
		cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
		/* Make sure we're banked mode */
		cortexar_banked_dcc_mode(target);
		/* Switch into DCC Fast mode */
		const uint32_t dbg_dcsr =
			adiv5_dp_read(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DCSR)) & ~CORTEXAR_DBG_DCSR_DCC_MASK;
		adiv5_dp_write(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DCSR), dbg_dcsr | CORTEXAR_DBG_DCSR_DCC_FAST);
		/* Set up continual store so we can hammer the DTR */
		adiv5_dp_write(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_ITR), ARM_STC_DTRRX_R0_POSTINC4_INSN);
		/* Run the transfer, hammering the DTR */
		for (size_t offset = 0; offset < count; ++offset)
			adiv5_dp_write(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DTRTX), src[offset]);
		/* Now read out the status from the DCSR in case anything went wrong */
		const uint32_t status = adiv5_dp_read(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DCSR));
		/* Go back into DCC Normal (Non-blocking) mode */
		adiv5_dp_write(priv->base.ap->dp, ADIV5_AP_DB(CORTEXAR_BANKED_DCSR), dbg_dcsr | CORTEXAR_DBG_DCSR_DCC_NORMAL);
		/* Check if the instruction triggered a synchronous data abort */
		return cortexar_check_data_abort(target, status);
	}

	/* Write each of the uint32_t's checking for failure */
	for (size_t offset = 0; offset < count; ++offset) {
		if (!cortexar_run_write_insn(target, ARM_STC_DTRRX_R0_POSTINC4_INSN, src[offset]))
			return false; /* Propagate failure if it happens */
	}
	return true; /* Signal success */
}

/* Slow path for cortexar_mem_write(). Trashes r0 and r1. */
static bool cortexar_mem_write_slow(
	target_s *const target, target_addr_t addr, const uint8_t *const data, const size_t length)
{
	size_t offset = 0;
	/* If the address is odd, write a byte to get onto an even address */
	if (addr & 1U) {
		cortexar_core_reg_write(target, 1U, data[offset++]);
		if (!cortexar_run_insn(target, ARM_STRB_R1_R0_INSN))
			return false;
		++addr;
	}
	/* If the address is now even but only 16-bit aligned, write a uint16_t to get onto 32-bit alignment */
	if ((addr & 2U) && length - offset >= 2U) {
		cortexar_core_reg_write(target, 1U, read_le2(data, offset));
		if (!cortexar_run_insn(target, ARM_STRH_R1_R0_INSN))
			return false;
		offset += 2U;
	}
	/* Use the fast path to write as much as possible before doing a slow path fixup at the end */
	if (!cortexar_mem_write_fast(target, (uint32_t *)(data + offset), (length - offset) >> 2U))
		return false;
	const uint8_t remainder = (length - offset) & 3U;
	/* If the remainder needs at least 2 more bytes write, do this first */
	if (remainder & 2U) {
		cortexar_core_reg_write(target, 1U, read_le2(data, offset));
		if (!cortexar_run_insn(target, ARM_STRH_R1_R0_INSN))
			return false;
		offset += 2U;
	}
	/* Finally, fix things up if a final byte is required. */
	if (remainder & 1U) {
		cortexar_core_reg_write(target, 1U, data[offset]);
		if (!cortexar_run_insn(target, ARM_STRB_R1_R0_INSN))
			return false;
	}
	return true; /* Signal success */
}

/*
 * This writes memory by jumping from the debug unit bus to the system bus.
 * NB: This requires the core to be halted! Uses instruction launches on
 * the core and requires we're in debug mode to work. Trashes r0.
 */
static void cortexar_mem_write(
	target_s *const target, const target_addr64_t dest, const void *const src, const size_t len)
{
	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	DEBUG_PROTO("%s: Writing %zu bytes @0x%" PRIx64 ":", __func__, len, dest);
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

	/* Cache DFSR and DFAR in case we wind up triggering a data fault */
	if (!(priv->core_status & CORTEXAR_STATUS_FAULT_CACHE_VALID)) {
		priv->fault_status = cortexar_coproc_read(target, CORTEXAR_DFSR);
		priv->fault_address = cortexar_coproc_read(target, CORTEXAR_DFAR);
		priv->core_status |= CORTEXAR_STATUS_FAULT_CACHE_VALID;
	}
	/* Clear any existing fault state */
	priv->core_status &= ~(CORTEXAR_STATUS_DATA_FAULT | CORTEXAR_STATUS_MMU_FAULT);

	/* Move the start address into the core's r0 */
	cortexar_core_reg_write(target, 0U, dest);

	/* If the address is 32-bit aligned and we're writing 32 bits at a time, use the fast path */
	if ((dest & 3U) == 0U && (len & 3U) == 0U)
		cortexar_mem_write_fast(target, (const uint32_t *)src, len >> 2U);
	else
		cortexar_mem_write_slow(target, dest, (const uint8_t *)src, len);
	/* Deal with any data faults that occurred */
	cortexar_mem_handle_fault(target, __func__);
}

static void cortexar_regs_read(target_s *const target, void *const data)
{
	const cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	uint32_t *const regs = (uint32_t *)data;
	/* Copy the register values out from our cache */
	memcpy(regs, priv->core_regs.r, sizeof(priv->core_regs.r));
	regs[CORTEX_REG_CPSR] = priv->core_regs.cpsr;
	if (target->target_options & TOPT_FLAVOUR_FLOAT) {
		memcpy(regs + CORTEXAR_GENERAL_REG_COUNT, priv->core_regs.d, sizeof(priv->core_regs.d));
		regs[CORTEX_REG_FPCSR] = priv->core_regs.fpcsr;
	}
}

static void cortexar_regs_write(target_s *const target, const void *const data)
{
	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	const uint32_t *const regs = (const uint32_t *)data;
	/* Copy the new register values into our cache */
	memcpy(priv->core_regs.r, regs, sizeof(priv->core_regs.r));
	priv->core_regs.cpsr = regs[CORTEX_REG_CPSR];
	if (target->target_options & TOPT_FLAVOUR_FLOAT) {
		memcpy(priv->core_regs.d, regs + CORTEXAR_GENERAL_REG_COUNT, sizeof(priv->core_regs.d));
		priv->core_regs.fpcsr = regs[CORTEX_REG_FPCSR];
	}
}

static void *cortexar_reg_ptr(target_s *const target, const size_t reg)
{
	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	/* r0-r15 */
	if (reg < 16U)
		return &priv->core_regs.r[reg];
	/* cpsr */
	if (reg == CORTEXAR_CPSR_GDB_REMAP_POS)
		return &priv->core_regs.cpsr;
	/* Check if the core has a FPU first */
	if (!(target->target_options & TOPT_FLAVOUR_FLOAT))
		return NULL;
	/* d0-d15 */
	if (reg < 33U)
		return &priv->core_regs.d[reg - CORTEXAR_GENERAL_REG_COUNT];
	/* fpcsr */
	if (reg == 33U)
		return &priv->core_regs.fpcsr;
	return NULL;
}

static size_t cortexar_reg_width(const size_t reg)
{
	/* r0-r15, cpsr, fpcsr */
	if (reg < CORTEXAR_GENERAL_REG_COUNT || reg == CORTEXAR_CPSR_GDB_REMAP_POS || reg == 33U)
		return 4U;
	/* d0-d15 */
	return 8U;
}

static size_t cortexar_reg_read(target_s *const target, const uint32_t reg, void *const data, const size_t max)
{
	/* Try to get a pointer to the storage for the requested register, and return -1 if that fails */
	const void *const reg_ptr = cortexar_reg_ptr(target, reg);
	if (!reg_ptr)
		return 0;
	/* Now we have a valid register, get its width in bytes, and check that against max */
	const size_t reg_width = cortexar_reg_width(reg);
	if (max < reg_width)
		return 0;
	/* Finally, copy the register data out and return the width */
	memcpy(data, reg_ptr, reg_width);
	return reg_width;
}

static size_t cortexar_reg_write(target_s *const target, const uint32_t reg, const void *const data, const size_t max)
{
	/* Try to get a pointer to the storage for the requested register, and return -1 if that fails */
	void *const reg_ptr = cortexar_reg_ptr(target, reg);
	if (!reg_ptr)
		return 0;
	/* Now we have a valid register, get its width in bytes, and check that against max */
	const size_t reg_width = cortexar_reg_width(reg);
	if (max < reg_width)
		return 0;
	/* Finally, copy the new register data in and return the width */
	memcpy(reg_ptr, data, reg_width);
	return reg_width;
}

static void cortexar_reset(target_s *const target)
{
	/* Read PRSR here to clear DBG_PRSR.SR before reset */
	cortex_dbg_read32(target, CORTEXAR_DBG_PRSR);
	/* If the physical reset pin is not inhibited, use it */
	if (!(target->target_options & TOPT_INHIBIT_NRST)) {
		platform_nrst_set_val(true);
		platform_nrst_set_val(false);
		/* Precautionary delay as with the Cortex-M code for targets that take a hot minute to come back */
		platform_delay(10);
	}

	/* Check if the reset succeeded */
	const uint32_t status = cortex_dbg_read32(target, CORTEXAR_DBG_PRSR);
	if (!(status & CORTEXAR_DBG_PRSR_STICKY_RESET))
		/* No reset seen yet, or nRST is inhibited, so let's do this via PRCR */
		cortex_dbg_write32(target, CORTEXAR_DBG_PRCR, CORTEXAR_DBG_PRCR_CORE_WARM_RESET_REQ);

	/* If the targets needs to do something extra, handle that here */
	if (target->extended_reset)
		target->extended_reset(target);

	/* Now wait for sticky reset to read high and reset low, indicating the reset has been completed */
	platform_timeout_s reset_timeout;
	platform_timeout_set(&reset_timeout, 1000);
	while ((cortex_dbg_read32(target, CORTEXAR_DBG_PRSR) &
			   (CORTEXAR_DBG_PRSR_STICKY_RESET | CORTEXAR_DBG_PRSR_RESET_ACTIVE)) &&
		!platform_timeout_is_expired(&reset_timeout))
		continue;

#if defined(PLATFORM_HAS_DEBUG)
	if (platform_timeout_is_expired(&reset_timeout))
		DEBUG_WARN("Reset seems to be stuck low!\n");
#endif

	/* 10ms delay to ensure bootroms have had time to run */
	platform_delay(10);
	/* Ignore any initial errors out of reset */
	target_check_error(target);
}

static void cortexar_halt_request(target_s *const target)
{
	TRY (EXCEPTION_TIMEOUT) {
		cortex_dbg_write32(target, CORTEXAR_DBG_DRCR, CORTEXAR_DBG_DRCR_HALT_REQ);
	}
	CATCH () {
	default:
		tc_printf(target, "Timeout sending interrupt, is target in WFI?\n");
	}
}

static target_halt_reason_e cortexar_halt_poll(target_s *const target, target_addr_t *const watch)
{
	volatile uint32_t dscr = 0;
	TRY (EXCEPTION_ALL) {
		/* If this times out because the target is in WFI then the target is still running. */
		dscr = cortex_dbg_read32(target, CORTEXAR_DBG_DSCR);
	}
	CATCH () {
	case EXCEPTION_ERROR:
		/* Things went seriously wrong and there is no recovery from this... */
		target_list_free();
		return TARGET_HALT_ERROR;
	case EXCEPTION_TIMEOUT:
		/* Timeout isn't actually a problem and probably means target is in WFI */
		return TARGET_HALT_RUNNING;
	}

	/* Check that the core actually halted */
	if (!(dscr & CORTEXAR_DBG_DSCR_HALTED))
		return TARGET_HALT_RUNNING;

	/* Ensure the OS lock is cleared as a precaution */
	cortexar_oslock_unlock(target);
	/* Make sure ITR is enabled and likewise halting debug (so breakpoints work) */
	cortex_dbg_write32(
		target, CORTEXAR_DBG_DSCR, dscr | CORTEXAR_DBG_DSCR_ITR_ENABLE | CORTEXAR_DBG_DSCR_HALTING_DBG_ENABLE);

	/* Save the target core's registers as debugging operations clobber them */
	cortexar_regs_save(target);

	target_halt_reason_e reason = TARGET_HALT_FAULT;
	/* Determine why we halted exactly from the Method Of Entry bits */
	switch (dscr & CORTEXAR_DBG_DSCR_MOE_MASK) {
	case CORTEXAR_DBG_DSCR_MOE_HALT_REQUEST:
		reason = TARGET_HALT_REQUEST;
		break;
	case CORTEXAR_DBG_DSCR_MOE_EXTERNAL_DBG:
	case CORTEXAR_DBG_DSCR_MOE_BREAKPOINT:
	case CORTEXAR_DBG_DSCR_MOE_BKPT_INSN:
	case CORTEXAR_DBG_DSCR_MOE_VEC_CATCH:
		reason = TARGET_HALT_BREAKPOINT;
		break;
	case CORTEXAR_DBG_DSCR_MOE_SYNC_WATCH:
	case CORTEXAR_DBG_DSCR_MOE_ASYNC_WATCH: {
		const cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
		if (priv->base.watchpoints_mask == 1U) {
			for (const breakwatch_s *breakwatch = target->bw_list; breakwatch; breakwatch = breakwatch->next) {
				if (breakwatch->type != TARGET_WATCH_READ && breakwatch->type != TARGET_WATCH_WRITE &&
					breakwatch->type != TARGET_WATCH_ACCESS)
					continue;
				*watch = breakwatch->addr;
				break;
			}
			reason = TARGET_HALT_WATCHPOINT;
		} else
			reason = TARGET_HALT_BREAKPOINT;
		break;
	}
	}
	/* Check if we halted because we were actually single-stepping */
	return reason;
}

static void cortexar_halt_resume(target_s *const target, const bool step)
{
	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;
	priv->base.ap->dp->quirks &= ~ADIV5_AP_ACCESS_BANKED;
	/* Restore the core's registers so the running program doesn't know we've been in there */
	cortexar_regs_restore(target);

	uint32_t dscr = cortex_dbg_read32(target, CORTEXAR_DBG_DSCR);
	/*
	 * If we're setting up to single-step the core, configure the final breakpoint slot appropriately.
	 * We always keep the final supported breakpoint reserved for this purpose so
	 * `priv->base.breakpoints_available` represents this slot index.
	 * Additionally, adjust DSCR to disable interrupts as necessary.
	 */
	if (step) {
		cortexar_config_breakpoint(target, priv->base.breakpoints_available,
			CORTEXAR_DBG_BCR_TYPE_UNLINKED_INSN_MISMATCH | ((priv->core_regs.cpsr & CORTEXAR_CPSR_THUMB) ? 2 : 4),
			priv->core_regs.r[CORTEX_REG_PC]);
		dscr |= CORTEXAR_DBG_DSCR_INTERRUPT_DISABLE;
	} else {
		cortex_dbg_write32(target, CORTEXAR_DBG_BCR + (priv->base.breakpoints_available << 2U), 0U);
		dscr &= ~CORTEXAR_DBG_DSCR_INTERRUPT_DISABLE;
	}

	/* Invalidate all the instruction caches if we're on a VMSA model device */
	if (target->target_options & TOPT_FLAVOUR_VIRT_MEM)
		cortexar_coproc_write(target, CORTEXAR_ICIALLU, 0U);
	/* Mark the fault status and address cache invalid */
	priv->core_status &= ~CORTEXAR_STATUS_FAULT_CACHE_VALID;

	cortex_dbg_write32(target, CORTEXAR_DBG_DSCR, dscr & ~CORTEXAR_DBG_DSCR_ITR_ENABLE);
	/* Ask to resume the core */
	cortex_dbg_write32(target, CORTEXAR_DBG_DRCR, CORTEXAR_DBG_DRCR_CLR_STICKY_EXC | CORTEXAR_DBG_DRCR_RESTART_REQ);

	/* Then poll for when the core actually resumes */
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);
	uint32_t status = CORTEXAR_DBG_DSCR_HALTED;
	while (!(status & CORTEXAR_DBG_DSCR_RESTARTED) && !platform_timeout_is_expired(&timeout))
		status = cortex_dbg_read32(target, CORTEXAR_DBG_DSCR);
}

void cortexar_invalidate_all_caches(target_s *const target)
{
	/* Extract the cache geometry */
	const uint32_t cache_geometry = cortexar_coproc_read(target, CORTEXAR_CLIDR);
	/* Extract the LoC bits which determines the cache level where coherence is reached */
	const uint8_t coherence_level =
		(cache_geometry & CORTEXAR_CLIDR_LEVEL_OF_COHERENCE_MASK) >> CORTEXAR_CLIDR_LEVEL_OF_COHERENCE_SHIFT;

	/* For each cache level to invalidate */
	for (uint8_t cache_level = 0; cache_level < coherence_level; ++cache_level) {
		/* Extract what kind of cache is at this level */
		const uint8_t cache_type = (cache_geometry >> (cache_level * 3U)) & CORTEXAR_CACHE_MASK;
		/* If there's no D-cache at this level, skip */
		if ((cache_type & CORTEXAR_DCACHE_MASK) != CORTEXAR_HAS_DCACHE)
			continue;
		/* Next, select the cache to read out the size for */
		cortexar_coproc_write(target, CORTEXAR_CSSELR, cache_level << 1U);
		const uint32_t cache_size = cortexar_coproc_read(target, CORTEXAR_CCSIDR);
		/* Extract the size of a cache line in uint32_t's ulog2()-2 and adjust to get to a size in uint8_t's ulog2() */
		const uint8_t cache_set_shift = (cache_size & 7U) + 4U;
		/* Extract the cache associativity (number of ways) */
		const uint16_t cache_ways = ((cache_size >> 3U) & 0x3ffU) + 1U;
		/* Extract the number of cache sets */
		const uint16_t cache_sets = ((cache_size >> 13U) & 0x7fffU) + 1U;
		/* Calculate how much to shift the cache way number by */
		const uint8_t cache_ways_shift = 32U - ulog2(cache_ways - 1U);
		/* For each set in the cache */
		for (uint16_t cache_set = 0U; cache_set < cache_sets; ++cache_set) {
			/* For each way in the cache */
			for (uint16_t cache_way = 0U; cache_way < cache_ways; ++cache_way) {
				/*
				 * Invalidate and clean the cache set + way for the current cache level
				 *
				 * The register involved here has the following form:
				 * 31  31-A        B     L … 4   3 2 1   0
				 * ├─────┼─────────┼─────┼─────┬───────┬───╮
				 * │ Way │    0    │ Set │  0  │ Level │ 0 │
				 * ╰─────┴─────────┴─────┴─────┴───────┴───╯
				 * Where:
				 *  A is log2(cache_ways)
				 *  L is log2(cache_line_length)
				 *  S is log2(cache_sets)
				 *  B is L + S
				 *
				 * log2(cache_line_length) is (cache_size & 7U) + 4U
				 */
				cortexar_coproc_write(target, CORTEXAR_DCCISW,
					(cache_way << cache_ways_shift) | (cache_set << cache_set_shift) | (cache_level << 1U));
			}
		}
	}

	cortexar_coproc_write(target, CORTEXAR_ICIALLU, 0U);
}

static void cortexar_config_breakpoint(
	target_s *const target, const size_t slot, uint32_t mode, const target_addr_t addr)
{
	/*
	 * Figure out if the breakpoint is for an ARM or Thumb instruction and which
	 * part of the lowest 2 bits of the address to match + how
	 */
	const bool thumb_breakpoint = (mode & 7U) == 2U;
	if (thumb_breakpoint)
		mode |= (addr & 2U) ? CORTEXAR_DBG_BCR_BYTE_SELECT_HIGH_HALF : CORTEXAR_DBG_BCR_BYTE_SELECT_LOW_HALF;
	else
		mode |= CORTEXAR_DBG_BCR_BYTE_SELECT_ALL;

	/* Configure the breakpoint slot */
	cortex_dbg_write32(target, CORTEXAR_DBG_BVR + (slot << 2U), cortexar_virt_to_phys(target, addr & ~3U));
	cortex_dbg_write32(
		target, CORTEXAR_DBG_BCR + (slot << 2U), CORTEXAR_DBG_BCR_ENABLE | CORTEXAR_DBG_BCR_ALL_MODES | (mode & ~7U));
}

static uint32_t cortexar_watchpoint_mode(const target_breakwatch_e type)
{
	switch (type) {
	case TARGET_WATCH_READ:
		return CORTEXAR_DBG_WCR_MATCH_ON_LOAD;
	case TARGET_WATCH_WRITE:
		return CORTEXAR_DBG_WCR_MATCH_ON_STORE;
	case TARGET_WATCH_ACCESS:
		return CORTEXAR_DBG_WCR_MATCH_ANY_ACCESS;
	default:
		return 0U;
	}
}

static void cortexar_config_watchpoint(target_s *const target, const size_t slot, const breakwatch_s *const breakwatch)
{
	/*
	 * Construct the access and bytes masks - starting with the bytes mask which uses the fact
	 * that any `(1 << N) - 1` will result in all the bits less than the Nth being set.
	 * The DBG_WCR BAS field is a bit-per-byte bitfield, so to match on two bytes, one has to program
	 * it with 0b11 somewhere in its length, and for four bytes that's 0b1111.
	 * Which set of bits need to be 1's depends on the address low bits.
	 */
	const uint32_t byte_mask = ((1U << breakwatch->size) - 1U) << (breakwatch->addr & 3U);
	const uint32_t mode = cortexar_watchpoint_mode(breakwatch->type) | CORTEXAR_DBG_WCR_BYTE_SELECT(byte_mask);

	/* Configure the watchpoint slot */
	cortex_dbg_write32(target, CORTEXAR_DBG_WVR + (slot << 2U), cortexar_virt_to_phys(target, breakwatch->addr & ~3U));
	cortex_dbg_write32(
		target, CORTEXAR_DBG_WCR + (slot << 2U), CORTEXAR_DBG_WCR_ENABLE | CORTEXAR_DBG_WCR_ALL_MODES | mode);
}

static int cortexar_breakwatch_set(target_s *const target, breakwatch_s *const breakwatch)
{
	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;

	switch (breakwatch->type) {
	case TARGET_BREAK_HARD: {
		/* First try and find a unused breakpoint slot */
		size_t breakpoint = 0;
		for (; breakpoint < priv->base.breakpoints_available; ++breakpoint) {
			/* Check if the slot is presently in use, breaking if it is not */
			if (!(priv->base.breakpoints_mask & (1U << breakpoint)))
				break;
		}
		/* If none was available, return an error */
		if (breakpoint == priv->base.breakpoints_available)
			return -1;

		/* Set the breakpoint slot up and mark it used */
		cortexar_config_breakpoint(
			target, breakpoint, CORTEXAR_DBG_BCR_TYPE_UNLINKED_INSN_MATCH | (breakwatch->size & 7U), breakwatch->addr);
		priv->base.breakpoints_mask |= 1U << breakpoint;
		breakwatch->reserved[0] = breakpoint;
		/* Tell the debugger that it was successfully able to set the breakpoint */
		return 0;
	}
	case TARGET_WATCH_READ:
	case TARGET_WATCH_WRITE:
	case TARGET_WATCH_ACCESS: {
		/* First try and find an unused watchpoint slot */
		size_t watchpoint = 0;
		for (; watchpoint < priv->base.watchpoints_available; ++watchpoint) {
			/* Check if the slot is presently in use, breaking if it is not */
			if (!(priv->base.watchpoints_mask & (1U << watchpoint)))
				break;
		}
		/* If none was available, return an error */
		if (watchpoint == priv->base.watchpoints_available)
			return -1;

		/* Set the watchpoint slot up and mark it used */
		cortexar_config_watchpoint(target, watchpoint, breakwatch);
		priv->base.watchpoints_mask |= 1U << watchpoint;
		breakwatch->reserved[0] = watchpoint;
		/* Tell the debugger that it was successfully able to set the watchpoint */
		return 0;
	}
	default:
		/* If the breakwatch type is not one of the above, tell the debugger we don't support it */
		return 1;
	}
}

static int cortexar_breakwatch_clear(target_s *const target, breakwatch_s *const breakwatch)
{
	cortexar_priv_s *const priv = (cortexar_priv_s *)target->priv;

	switch (breakwatch->type) {
	case TARGET_BREAK_HARD: {
		/* Clear the breakpoint slot this used */
		const size_t breakpoint = breakwatch->reserved[0];
		cortex_dbg_write32(target, CORTEXAR_DBG_BCR + (breakpoint << 2U), 0);
		priv->base.breakpoints_mask &= ~(1U << breakpoint);
		/* Tell the debugger that it was successfully able to clear the breakpoint */
		return 0;
	}
	case TARGET_WATCH_READ:
	case TARGET_WATCH_WRITE:
	case TARGET_WATCH_ACCESS: {
		/* Clear the watchpoint slot this used */
		const size_t watchpoint = breakwatch->reserved[0];
		cortex_dbg_write32(target, CORTEXAR_DBG_WCR + (watchpoint << 2U), 0);
		priv->base.watchpoints_mask &= ~(1U << watchpoint);
		/* Tell the debugger that it was successfully able to clear the watchpoint */
		return 0;
	}
	default:
		/* If the breakwatch type is not one of the above, tell the debugger wed on't support it */
		return 1;
	}
}

/*
 * This function creates the target description XML substring for the FPU (VFPv2) on
 * a Cortex-A/R part. This has the same rationale as the function below.
 *
 * The string it creates is conceptually the following:
 * <feature name="org.gnu.gdb.arm.vfp">
 *   <reg name="d0" bitsize="64" type="ieee_double"/>
 *   <reg name="d1" bitsize="64" type="ieee_double"/>
 *   <reg name="d2" bitsize="64" type="ieee_double"/>
 *   <reg name="d3" bitsize="64" type="ieee_double"/>
 *   <reg name="d4" bitsize="64" type="ieee_double"/>
 *   <reg name="d5" bitsize="64" type="ieee_double"/>
 *   <reg name="d6" bitsize="64" type="ieee_double"/>
 *   <reg name="d7" bitsize="64" type="ieee_double"/>
 *   <reg name="d8" bitsize="64" type="ieee_double"/>
 *   <reg name="d9" bitsize="64" type="ieee_double"/>
 *   <reg name="d10" bitsize="64" type="ieee_double"/>
 *   <reg name="d11" bitsize="64" type="ieee_double"/>
 *   <reg name="d12" bitsize="64" type="ieee_double"/>
 *   <reg name="d13" bitsize="64" type="ieee_double"/>
 *   <reg name="d14" bitsize="64" type="ieee_double"/>
 *   <reg name="d15" bitsize="64" type="ieee_double"/>
 *   <reg name="fpscr" bitsize="32"/>
 * </feature>
 */
static size_t cortexar_build_target_fpu_description(char *const buffer, size_t max_length)
{
	size_t print_size = max_length;
	/* Terminate the previous feature block and start the new */
	int offset = snprintf(buffer, print_size, "</feature><feature name=\"org.gnu.gdb.arm.vfp\">");

	/* Build the FPU general purpose register descriptions for d0-d15 */
	for (uint8_t i = 0; i < 16U; ++i) {
		if (max_length != 0)
			print_size = max_length - (size_t)offset;

		offset += snprintf(buffer + offset, print_size, "<reg name=\"d%u\" bitsize=\"64\" type=\"ieee_double\"/>", i);
	}

	/* Build the FPU status/control register (fpscr) description */
	if (max_length != 0)
		print_size = max_length - (size_t)offset;

	offset += snprintf(buffer + offset, print_size, "<reg name=\"fpscr\" bitsize=\"32\"/>");

	/* offset is now the total length of the string created, discard the sign and return it. */
	return (size_t)offset;
}

/*
 * This function creates the target description XML string for a Cortex-A/R part.
 * This is done this way to decrease string duplications and thus code size,
 * making it unfortunately much less readable than the string literal it is
 * equivalent to.
 *
 * The string it creates is approximately the following:
 * <?xml version="1.0"?>
 * <!DOCTYPE target SYSTEM "gdb-target.dtd">
 * <target>
 *   <architecture>arm</architecture>
 *   <feature name="org.gnu.gdb.arm.core">
 *     <reg name="r0" bitsize="32"/>
 *     <reg name="r1" bitsize="32"/>
 *     <reg name="r2" bitsize="32"/>
 *     <reg name="r3" bitsize="32"/>
 *     <reg name="r4" bitsize="32"/>
 *     <reg name="r5" bitsize="32"/>
 *     <reg name="r6" bitsize="32"/>
 *     <reg name="r7" bitsize="32"/>
 *     <reg name="r8" bitsize="32"/>
 *     <reg name="r9" bitsize="32"/>
 *     <reg name="r10" bitsize="32"/>
 *     <reg name="r11" bitsize="32"/>
 *     <reg name="r12" bitsize="32"/>
 *     <reg name="sp" bitsize="32" type="data_ptr"/>
 *     <reg name="lr" bitsize="32" type="code_ptr"/>
 *     <reg name="pc" bitsize="32" type="code_ptr"/>
 *     <reg name="cpsr" bitsize="32" regnum="25"/>
 *   </feature>
 * </target>
 */
static size_t cortexar_build_target_description(char *const buffer, size_t max_length, const bool has_fpu)
{
	size_t print_size = max_length;
	/* Start with the "preamble" chunks which are mostly common across targets save for 2 words. */
	int offset = snprintf(buffer, print_size, "%s target %sarm%s <feature name=\"org.gnu.gdb.arm.core\">",
		gdb_xml_preamble_first, gdb_xml_preamble_second, gdb_xml_preamble_third);

	/* Then build the general purpose register descriptions for r0-r12 */
	for (uint8_t i = 0; i <= 12U; ++i) {
		if (max_length != 0)
			print_size = max_length - (size_t)offset;

		offset += snprintf(buffer + offset, print_size, "<reg name=\"r%u\" bitsize=\"32\"/>", i);
	}

	/* Now build the special-purpose register descriptions using the arrays at the top of file */
	for (uint8_t i = 0; i < ARRAY_LENGTH(cortexr_spr_names); ++i) {
		if (max_length != 0)
			print_size = max_length - (size_t)offset;

		const char *const name = cortexr_spr_names[i];
		const gdb_reg_type_e type = cortexr_spr_types[i];

		/*
		 * Create tag for each register In the case of CPSR, remap it to 25 so it aligns
		 * with the target description XML string above. CORTEXAR_CPSR_GDB_REMAP_POS is
		 * used for this mapping elsewhere in the logic.
		 */
		offset += snprintf(buffer + offset, print_size, "<reg name=\"%s\" bitsize=\"32\"%s%s/>", name,
			gdb_reg_type_strings[type], i == 3U ? " regnum=\"25\"" : "");
	}

	/* Handle when the core has a FPU (VFP) */
	if (has_fpu) {
		if (max_length != 0)
			print_size = max_length - (size_t)offset;

		offset += cortexar_build_target_fpu_description(buffer + offset, print_size);
	}

	/* Build the XML blob's termination */
	if (max_length != 0)
		print_size = max_length - (size_t)offset;

	offset += snprintf(buffer + offset, print_size, "</feature></target>");
	/* offset is now the total length of the string created, discard the sign and return it. */
	return (size_t)offset;
}

static const char *cortexar_target_description(target_s *const target)
{
	const size_t description_length =
		cortexar_build_target_description(NULL, 0, target->target_options & TOPT_FLAVOUR_FLOAT) + 1U;
	char *const description = malloc(description_length);
	if (description)
		(void)cortexar_build_target_description(
			description, description_length, target->target_options & TOPT_FLAVOUR_FLOAT);
	return description;
}
