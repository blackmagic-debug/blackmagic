/*
 * This file is part of the Black Magic Debug project.
 *
 * Based on work that is Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Copyright (C) 2024 Mary Guillemard <mary@mary.zone>
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
 * This file implements support ARMv8-A based processors.
 *
 * References:
 * DDI0487 - Arm Architecture Reference Manual for A-profile architecture
 *   https://documentation-service.arm.com/static/65fdad3c1bc22b03bca90781
 * 100442  - Arm Cortex-A55 Core Technical Reference Manual
 *   https://documentation-service.arm.com/static/649ac6d4df6cd61d528c2bf1
 */
#include "general.h"
#include "exception.h"
#include "adi.h"
#include "adiv5.h"
#include "target.h"
#include "target_internal.h"
#include "target_probe.h"
#include "jep106.h"
#include "cortex.h"
#include "cortex_internal.h"
#include "arm_coresight_cti.h"
#include "gdb_reg.h"
#include "gdb_packet.h"
#include "maths_utils.h"
#include "buffer_utils.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct cortexa_armv8_priv_fault_state {
	uint64_t status;
	uint64_t address;
} cortexa_armv8_priv_fault_state_s;

typedef struct cortexa_armv8_priv {
	/* Base core information */
	cortex_priv_s base;
	arm_coresight_cti_s cti;

	/* Core registers cache */
	struct {
		uint64_t x[CORTEXA_ARMV8_GENERAL_REG_COUNT];
		uint64_t sp;
		uint64_t pc;

		uint64_t spsr;
	} core_regs;

	/* Fault status/address cache */
	cortexa_armv8_priv_fault_state_s fault_state;

	/* Cached value of EDSCR */
	uint32_t edscr;

	/* Control and status information */
	uint8_t core_status;

	/* Indicate if the DC was init properly */
	bool dc_is_valid;
} cortexa_armv8_priv_s;

#define CORTEXA_ARMV8_TARGET_NAME ("ARM Cortex-A (ARMv8-A)")

#define CORTEXA_DBG_EDECR      0x024U                 /* Debug Execution Control Register */
#define CORTEXA_DBG_DTRRX_EL0  0x080U                 /* Debug Data Transfer Register, Receive */
#define CORTEXA_DBG_EDITR      0x084U                 /* Debug Instruction Transfer Register */
#define CORTEXA_DBG_EDSCR      0x088U                 /* Debug Status and Control Register */
#define CORTEXA_DBG_DTRTX_EL0  0x08cU                 /* Debug Data Transfer Register, Transmit */
#define CORTEXA_DBG_EDRCR      0x090U                 /* Debug Reserve Control Register */
#define CORTEXA_DBG_OSLAR_EL1  0x300U                 /* OS Lock Access Register */
#define CORTEXA_DBG_EDPRSR     0x314U                 /* Debug Processor Status Register */
#define CORTEXA_DBG_BVR_EL1(n) (0x400U + ((n)*0x10U)) /* Debug Breakpoint Value Register */
#define CORTEXA_DBG_BCR_EL1(n) (0x408U + ((n)*0x10U)) /* Debug Breakpoint Control Register */
#define CORTEXA_DBG_WVR_EL1(n) (0x800U + ((n)*0x10U)) /* Debug Watchpoint Value Register */
#define CORTEXA_DBG_WCR_EL1(n) (0x808U + ((n)*0x10U)) /* Debug Watchpoint Control Register */
#define CORTEXA_DBG_EDDFR      0xd28U                 /* Debug Feature Register */

#define CORTEXA_DBG_EDECR_SINGLE_STEP (1 << 2U)

#define CORTEXA_DBG_EDSCR_RX_FULL                    (1 << 30U)
#define CORTEXA_DBG_EDSCR_TX_FULL                    (1 << 29U)
#define CORTEXA_DBG_EDSCR_ITO                        (1 << 28U)
#define CORTEXA_DBG_EDSCR_RXO                        (1 << 27U)
#define CORTEXA_DBG_EDSCR_TXU                        (1 << 26U)
#define CORTEXA_DBG_EDSCR_PIPE_ADV                   (1 << 25U)
#define CORTEXA_DBG_EDSCR_ITE                        (1 << 24U)
#define CORTEXA_DBG_EDSCR_INTERRUPT_DISABLE          (1 << 22U)
#define CORTEXA_DBG_EDSCR_INTERRUPT_DISABLE_MASK     (0xff3fffffU)
#define CORTEXA_DBG_EDSCR_TDA                        (1 << 21U)
#define CORTEXA_DBG_EDSCR_MA                         (1 << 20U)
#define CORTEXA_DBG_EDSCR_HALTING_DBG_ENABLE         (1 << 14U)
#define CORTEXA_DBG_EDSCR_ERR                        (1 << 6U)
#define CORTEXA_DBG_EDSCR_STATUS_MASK                0x0000003fU
#define CORTEXA_DBG_EDSCR_STATUS_PE_EXIT_DBG         0x00000001U
#define CORTEXA_DBG_EDSCR_STATUS_PE_DGB              0x00000002U
#define CORTEXA_DBG_EDSCR_STATUS_BREAKPOINT          0x00000007U
#define CORTEXA_DBG_EDSCR_STATUS_EXT_DBG_REQ         0x00000013U
#define CORTEXA_DBG_EDSCR_STATUS_HALT_STEP_NORMAL    0x0000001bU
#define CORTEXA_DBG_EDSCR_STATUS_HALT_STEP_EXCLUSIVE 0x0000001fU
#define CORTEXA_DBG_EDSCR_STATUS_OS_UNLOCK_CATCH     0x00000023U
#define CORTEXA_DBG_EDSCR_STATUS_RESET_CATCH         0x00000027U
#define CORTEXA_DBG_EDSCR_STATUS_WATCHPOINT          0x0000002bU
#define CORTEXA_DBG_EDSCR_STATUS_HLT_INSTRUCTION     0x0000002fU
#define CORTEXA_DBG_EDSCR_STATUS_SW_ACCESS_DBG_REG   0x00000033U
#define CORTEXA_DBG_EDSCR_STATUS_EXCEPTION_CATCH     0x00000037U
#define CORTEXA_DBG_EDSCR_STATUS_HALT_STEP_NO_SYN    0x0000003bU

#define CORTEXA_DBG_EDRCR_CLR_STICKY_ERR (1U << 2U)

#define CORTEXA_DBG_EDPRSR_POWERED_UP           (1U << 0U)
#define CORTEXA_DBG_EDPRSR_STICKY_PD            (1U << 1U)
#define CORTEXA_DBG_EDPRSR_RESET_STATUS         (1U << 2U)
#define CORTEXA_DBG_EDPRSR_STICKY_CORE_RESET    (1U << 3U)
#define CORTEXA_DBG_EDPRSR_HALTED               (1U << 4U)
#define CORTEXA_DBG_EDPRSR_OS_LOCK              (1U << 5U)
#define CORTEXA_DBG_EDPRSR_DOUBLE_LOCK          (1U << 6U)
#define CORTEXA_DBG_EDPRSR_STICKY_DEBUG_RESTART (1U << 11U)

#define CORTEXA_DBG_BCR_EL1_TYPE_UNLINKED_INSN_MATCH (0 << 20U)
#define CORTEXA_DBG_BCR_EL1_HIGH_MODE_CONTROL        (1 << 13U)
#define CORTEXA_DBG_BCR_EL1_PRIV_MODE_CONTROL(x)     ((x) << 1U)
#define CORTEXA_DBG_BCR_EL1_BYTE_SELECT_LOW_HALF     (0x3 << 5U)
#define CORTEXA_DBG_BCR_EL1_BYTE_SELECT_ALL          (0xf << 5U)
#define CORTEXA_DBG_BCR_EL1_BYTE_SELECT_HIGH_HALF    (0xc << 5U)
#define CORTEXA_DBG_BCR_EL1_ENABLE                   (1 << 0U)

#define CORTEXA_DBG_WCR_EL1_ENABLE               (1 << 0U)
#define CORTEXA_DBG_WCR_EL1_PRIV_MODE_CONTROL(x) ((x) << 1U)
#define CORTEXA_DBG_WCR_EL1_MATCH_ON_LOAD        (1 << 3U)
#define CORTEXA_DBG_WCR_EL1_MATCH_ON_STORE       (1 << 4U)
#define CORTEXA_DBG_WCR_EL1_MATCH_ANY_ACCESS     (CORTEXA_DBG_WCR_EL1_MATCH_ON_LOAD | CORTEXA_DBG_WCR_EL1_MATCH_ON_STORE)
#define CORTEXA_DBG_WCR_EL1_HIGH_MODE_CONTROL    (1 << 13U)
#define CORTEXA_DBG_WCR_EL1_BYTE_SELECT_OFFSET   5U
#define CORTEXA_DBG_WCR_EL1_BYTE_SELECT_MASK     0x00001fe0U
#define CORTEXA_DBG_WCR_EL1_BYTE_SELECT(x) \
	(((x) << CORTEXA_DBG_WCR_EL1_BYTE_SELECT_OFFSET) & CORTEXA_DBG_WCR_EL1_BYTE_SELECT_MASK)

#define CORTEXA_DBG_EDDFR_BREAKPOINT_MASK  0xfUL
#define CORTEXA_DBG_EDDFR_BREAKPOINT_SHIFT 12UL
#define CORTEXA_DBG_EDDFR_WATCHPOINT_MASK  0xfUL
#define CORTEXA_DBG_EDDFR_WATCHPOINT_SHIFT 20UL

#define CORTEXA_CTI_CHANNEL_HALT_SINGLE      0U
#define CORTEXA_CTI_CHANNEL_RESTART          1U
#define CORTEXA_CTI_EVENT_HALT_PE_SINGLE_IDX 0U
#define CORTEXA_CTI_EVENT_RESTART_PE_IDX     1U

#define CORTEXA_CORE_STATUS_ITR_ERR           (1U << 0U)
#define CORTEXA_CORE_STATUS_FAULT_CACHE_VALID (1U << 1U)

/*
 * Instruction encodings for the system registers
 * MRS -> Move System Register to general-purpose register (DDI0487K §C6.2.247, pg2208)
 * MSR -> Move general-purpose register to System register (DDI0487K §C6.2.250, pg2214)
 * ADD -> Add immediate, used for the alias MOV (to/from SP) (DDI0487K §C6.2.5, pg1652)
 */
#define A64_MRS_INSN                       0xd5300000U
#define A64_MSR_INSN                       0xd5100000U
#define A64_ADD_IMM_INSN                   0x11000000U
#define A64_MRS(Xt, systemreg)             (A64_MRS_INSN | ((systemreg) << 5U) | (Xt))
#define A64_MSR(systemreg, Xt)             (A64_MSR_INSN | ((systemreg) << 5U) | (Xt))
#define A64_ADD_IMM(sp, Rd, Rn, imm12, sh) (A64_ADD_IMM_INSN | ((sh) << 22U) | ((imm12) << 10U) | ((Rn) << 5U) | ((Rd)))
#define A64_READ_SP(sp, Rd)                A64_ADD_IMM(sp, Rd, 0x1fU, 0, 0)
#define A64_WRITE_SP(sp, Rn)               A64_ADD_IMM(sp, 0x1fU, Rn, 0, 0)

/*
 * Instruction encodings for indirect loads and stores of data via the CPU
 * LDRB -> Load Register Byte (immediate) (DDI0487K §C6.2.188, pg2085)
 * LDRH -> Load Register Halfword (immediate) (DDI0487K §C6.2.190, pg2090)
 * STRB -> Store Register Byte (immediate) (DDI0487K §C6.2.367, pg2447)
 * STRH -> Store Register Halfword (immediate) (DDI0487K §C6.2.369, pg2452)
 *
 * The first is `LDRB W1, [X0], #1` to load a uint8_t from [X0] into W1 and increment the
 * address in X0 by 1, writing the new address back to X0.
 * The second is `LDRH W1, [X0], #2` to load a uint16_t from [X0] into W1 and increment
 * the address in X0 by 2, writing the new address back to X0.
 * The third is `STRB W1, [X0], #1` to store a uint8_t to [X0] from X1 and increment the
 * address in X0 by 1, writing the new address back to X0.
 * The fourth is `STRH W1, [X0], #2` to store a uint16_t to [X0] from X1 and increment
 * the address in X0 by 2, writing the new address back to X0.
 */
#define ARM_LDRB_X0_X1_INSN 0x38401401U
#define ARM_LDRH_X0_X1_INSN 0x78402401U
#define ARM_STRB_X1_X0_INSN 0x38001401U
#define ARM_STRH_X1_X0_INSN 0x78002401U

#define A64_ENCODE_SYSREG(op0, op1, crn, crm, op2) \
	(((op0) << 14U) | ((op1) << 11U) | ((crn) << 7U) | ((crm) << 3U) | ((op2) << 0U))

#define A64_DBGDTR_EL0   A64_ENCODE_SYSREG(2, 3, 0, 4, 0) /* Debug Data Transfer Register, half-duplex */
#define A64_DBGDTRTX_EL0 A64_ENCODE_SYSREG(2, 3, 0, 5, 0) /* Debug Data Transfer Register, Transmit */
#define A64_DBGDTRRX_EL0 A64_ENCODE_SYSREG(2, 3, 0, 5, 0) /* Debug Data Transfer Register, Receive */
#define A64_DSPSR_EL0    A64_ENCODE_SYSREG(3, 3, 4, 5, 0) /* Debug Saved Program Status Register */
#define A64_DLR_EL0      A64_ENCODE_SYSREG(3, 3, 4, 5, 1) /* Debug Link Register */
#define A64_ESR_EL3      A64_ENCODE_SYSREG(3, 6, 5, 2, 0) /* Exception Syndrome Register (EL3) */
#define A64_FAR_EL3      A64_ENCODE_SYSREG(3, 6, 6, 0, 0) /* Fault Address Register (EL3) */
#define A64_ESR_EL2      A64_ENCODE_SYSREG(3, 4, 5, 2, 0) /* Exception Syndrome Register (EL2) */
#define A64_FAR_EL2      A64_ENCODE_SYSREG(3, 4, 6, 0, 0) /* Fault Address Register (EL2) */
#define A64_ESR_EL1      A64_ENCODE_SYSREG(3, 0, 5, 2, 0) /* Exception Syndrome Register (EL1) */
#define A64_FAR_EL1      A64_ENCODE_SYSREG(3, 0, 6, 0, 0) /* Fault Address Register (EL1) */

#define A64_DSPSR_EL0_M_EL_OFFSET   (2U)
#define A64_DSPSR_EL0_M_EL_MASK     (0xcU)
#define A64_DSPSR_EL0_EXTRACT_EL(x) (((x)&A64_DSPSR_EL0_M_EL_MASK) >> A64_DSPSR_EL0_M_EL_OFFSET)

/*
 * Fields for Cortex-A special-purpose registers, used in the generation of GDB's target description XML.
 */

struct cortexa_armv8_gdb_field_def {
	const char *name;
	const uint8_t start;
	const uint8_t end;
};

struct cortexa_armv8_gdb_flags_def {
	const char *id;
	const struct cortexa_armv8_gdb_field_def *fields;
	uint8_t fields_count;
	uint8_t size;
};

struct cortexa_armv8_gdb_reg_def {
	const char *name;
	uint8_t bit_size;
	const char *type_name;
};

/* Cortex-A custom flags */
static const struct cortexa_armv8_gdb_field_def cortexa_armv8_cpsr_flags_fields[] = {
	{.name = "SP", .start = 0, .end = 0},
	{.name = "EL", .start = 2, .end = 3},
	{.name = "nRW", .start = 4, .end = 4},
	{.name = "F", .start = 6, .end = 6},
	{.name = "I", .start = 7, .end = 7},
	{.name = "A", .start = 8, .end = 8},
	{.name = "D", .start = 9, .end = 9},
	{.name = "BTYPE", .start = 10, .end = 11},
	{.name = "SSBS", .start = 12, .end = 12},
	{.name = "IL", .start = 20, .end = 20},
	{.name = "SS", .start = 21, .end = 21},
	{.name = "PAN", .start = 22, .end = 22},
	{.name = "UAO", .start = 23, .end = 23},
	{.name = "DIT", .start = 24, .end = 24},
	{.name = "TCO", .start = 25, .end = 25},
	{.name = "V", .start = 28, .end = 28},
	{.name = "C", .start = 29, .end = 29},
	{.name = "Z", .start = 30, .end = 30},
	{.name = "N", .start = 31, .end = 31},
};

static const struct cortexa_armv8_gdb_flags_def cortexa_armv8_flags[] = {{.id = "cpsr_flags",
	.size = 4,
	.fields = cortexa_armv8_cpsr_flags_fields,
	.fields_count = ARRAY_LENGTH(cortexa_armv8_cpsr_flags_fields)}};

/* Cortex-A special-purpose registers */
static const struct cortexa_armv8_gdb_reg_def cortexa_armv8_sprs[] = {
	{.name = "sp", .bit_size = 64, .type_name = "data_ptr"},
	{.name = "pc", .bit_size = 64, .type_name = "code_ptr"},
	{.name = "cpsr", .bit_size = 32, .type_name = "cpsr_flags"},
};

static void cortexa_armv8_halt_request(target_s *target);
static target_halt_reason_e cortexa_armv8_halt_poll(target_s *target, target_addr64_t *watch);
static void cortexa_armv8_halt_resume(target_s *target, bool step);
static bool cortexa_armv8_attach(target_s *target);
static void cortexa_armv8_detach(target_s *target);

static bool cortexa_armv8_core_reg_read64(target_s *target, uint8_t reg, uint64_t *result);
static bool cortexa_armv8_core_reg_write64(target_s *target, uint8_t reg, uint64_t value);
static void cortexa_armv8_regs_save(target_s *target);
static void cortexa_armv8_regs_restore(target_s *target);
static void cortexa_armv8_regs_read(target_s *target, void *data);
static void cortexa_armv8_regs_write(target_s *target, const void *data);
static size_t cortexa_armv8_reg_read(target_s *target, uint32_t reg, void *data, size_t max);
static size_t cortexa_armv8_reg_write(target_s *target, uint32_t reg, const void *data, size_t max);
static const char *cortexa_armv8_target_description(target_s *target);

static bool cortexa_armv8_check_error(target_s *target);

static void cortexa_armv8_mem_read(target_s *target, void *dest, target_addr64_t src, size_t len);
static void cortexa_armv8_mem_write(target_s *target, target_addr64_t dest, const void *src, size_t len);

static int cortexa_armv8_breakwatch_set(target_s *target, breakwatch_s *breakwatch);
static int cortexa_armv8_breakwatch_clear(target_s *target, breakwatch_s *breakwatch);

static void cortexa_armv8_priv_free(void *const priv)
{
	arm_coresight_cti_fini(&((cortexa_armv8_priv_s *)priv)->cti);
	cortex_priv_free(priv);
}

static bool cortexa_armv8_oslock_unlock(target_s *const target)
{
	if ((cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR) & CORTEXA_DBG_EDPRSR_OS_LOCK)) {
		/* Lock set. Try to unlock */
		DEBUG_WARN("%s: OS lock set, unlocking\n", __func__);
		cortex_dbg_write32(target, CORTEXA_DBG_OSLAR_EL1, 0U);

		/* Read back to check if we succeeded */
		const bool locked = cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR) & CORTEXA_DBG_EDPRSR_OS_LOCK;
		if (locked)
			DEBUG_ERROR("%s: Lock sticky. Core not powered?\n", __func__);
		return !locked;
	}

	return true;
}

static bool cortexa_armv8_ensure_core_powered(target_s *const target)
{
	uint32_t edprsr = cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR);

	/* XXX: We don't have any way of powering it up, check if we are missing something from the docs */
	if (!(edprsr & CORTEXA_DBG_EDPRSR_POWERED_UP))
		return false;

	/* Check for the OS double lock */
	if (edprsr & CORTEXA_DBG_EDPRSR_DOUBLE_LOCK)
		return false;

	/*
	 * Finally, check for the normal OS Lock and clear it if it's set prior to halting the core.
	 * Trying to do this after target_halt_request() does not function over JTAG and triggers
	 * the lock sticky message.
	 */
	if (edprsr & CORTEXA_DBG_EDPRSR_OS_LOCK)
		return cortexa_armv8_oslock_unlock(target);

	return true;
}

bool cortexa_armv8_dc_probe(adiv5_access_port_s *const ap, const target_addr_t base_address)
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

	cortexa_armv8_priv_s *const priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
				 /* XXX: Free target? */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return NULL;
	}

	target->driver = CORTEXA_ARMV8_TARGET_NAME;
	target->priv = priv;
	target->priv_free = cortexa_armv8_priv_free;
	priv->base.ap = ap;
	priv->base.base_addr = base_address;

	/* Ensure the core is powered up and we can talk to it */
	if (!cortexa_armv8_ensure_core_powered(target))
		return false;

	priv->dc_is_valid = true;

	return true;
}

bool cortexa_armv8_configure_cti(arm_coresight_cti_s *const cti)
{
	/* Ensure CTI is unlocked */
	if (!arm_coresight_cti_ensure_unlock(cti))
		return false;

	/* Ensure CTI is disabled */
	arm_coresight_cti_enable(cti, false);

	/* Do not allow any propagation of events to CTM by default */
	arm_coresight_cti_set_gate(cti, 0);

	/* Configure identity mapping for events (Following H5-1 and H5-2 example) */
	arm_coresight_cti_set_output_channel(cti, CORTEXA_CTI_EVENT_HALT_PE_SINGLE_IDX, CORTEXA_CTI_CHANNEL_HALT_SINGLE);
	arm_coresight_cti_set_output_channel(cti, CORTEXA_CTI_EVENT_RESTART_PE_IDX, CORTEXA_CTI_CHANNEL_RESTART);

	/* Now we enable CTI */
	arm_coresight_cti_enable(cti, true);

	return true;
}

bool cortexa_armv8_cti_probe(adiv5_access_port_s *const ap, const target_addr_t base_address)
{
	target_s *target = target_list_get_last();

	if (!target)
		return false;

	/* Ensure that the previous target is actually from the same driver */
	if (strcmp(target->driver, CORTEXA_ARMV8_TARGET_NAME) != 0)
		return false;

	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Init CTI component */
	arm_coresight_cti_init(&priv->cti, ap, base_address);

	/* In case DC init failed, we should try to do anything here */
	if (!priv->dc_is_valid)
		return false;

	/* Configure CTI component */
	if (!cortexa_armv8_configure_cti(&priv->cti))
		return false;

	target->halt_request = cortexa_armv8_halt_request;
	target->halt_poll = cortexa_armv8_halt_poll;
	target->halt_resume = cortexa_armv8_halt_resume;

	/* Try to halt the PE */
	target_halt_request(target);
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);
	target_halt_reason_e reason = TARGET_HALT_RUNNING;
	while (!platform_timeout_is_expired(&timeout) && reason == TARGET_HALT_RUNNING)
		reason = target_halt_poll(target, NULL);
	if (reason != TARGET_HALT_REQUEST) {
		DEBUG_ERROR("Failed to halt the core, reason: %d\n", reason);
		return false;
	}

	cortex_read_cpuid(target);

	/* Detect debug features */
	const uint32_t eddfr_low = cortex_dbg_read32(target, CORTEXA_DBG_EDDFR);
	const uint32_t eddfr_high = cortex_dbg_read32(target, CORTEXA_DBG_EDDFR + 4U);
	const uint64_t eddfr = eddfr_low | ((uint64_t)eddfr_high << 32U);
	priv->base.breakpoints_available =
		((eddfr >> CORTEXA_DBG_EDDFR_BREAKPOINT_SHIFT) & CORTEXA_DBG_EDDFR_BREAKPOINT_MASK) + 1U;
	priv->base.watchpoints_available =
		((eddfr >> CORTEXA_DBG_EDDFR_WATCHPOINT_SHIFT) & CORTEXA_DBG_EDDFR_WATCHPOINT_MASK) + 1U;

	DEBUG_TARGET("%s %s core has %u breakpoint and %u watchpoint units available\n", target->driver, target->core,
		priv->base.breakpoints_available, priv->base.watchpoints_available);

	/* XXX: Detect optional features */

	target->attach = cortexa_armv8_attach;
	target->detach = cortexa_armv8_detach;
	target->check_error = cortexa_armv8_check_error;

	target->regs_description = cortexa_armv8_target_description;
	target->regs_read = cortexa_armv8_regs_read;
	target->regs_write = cortexa_armv8_regs_write;
	target->reg_read = cortexa_armv8_reg_read;
	target->reg_write = cortexa_armv8_reg_write;
	target->regs_size = sizeof(uint64_t) * CORTEXA_ARMV8_GENERAL_REG_COUNT;

	target->mem_read = cortexa_armv8_mem_read;
	target->mem_write = cortexa_armv8_mem_write;

	target->breakwatch_set = cortexa_armv8_breakwatch_set;
	target->breakwatch_clear = cortexa_armv8_breakwatch_clear;

	target_check_error(target);

	return true;
}

static void cortexa_armv8_halt_request(target_s *const target)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* First ensure that halting events are enabled */
	TRY (EXCEPTION_TIMEOUT) {
		priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
		priv->edscr |= CORTEXA_DBG_EDSCR_HALTING_DBG_ENABLE;
		cortex_dbg_write32(target, CORTEXA_DBG_EDSCR, priv->edscr);
	}
	CATCH () {
	default:
		tc_printf(target, "Timeout sending interrupt, is target in WFI?\n");
	}

	/* We assume that halting channel do not pass events to the CTM */
	/* XXX: SMP handling */

	/* Send CTI request */
	arm_coresight_cti_pulse_channel(&priv->cti, CORTEXA_CTI_CHANNEL_HALT_SINGLE);
}

static target_halt_reason_e cortexa_armv8_halt_poll(target_s *const target, target_addr64_t *const watch)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	volatile uint32_t edprsr = 0;
	TRY (EXCEPTION_ALL) {
		/* If this times out because the target is in WFI then the target is still running. */
		edprsr = cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR);
	}
	CATCH () {
	case EXCEPTION_ERROR:
		/* Things went seriously wrong and there is no recovery from this... */
		target_list_free();
		return TARGET_HALT_ERROR;
	case EXCEPTION_TIMEOUT:
		/* XXX: Is that also valid for our target? */
		/* Timeout isn't actually a problem and probably means target is in WFI */
		return TARGET_HALT_RUNNING;
	}

	/* Check that the core is powered up */
	/* XXX: Should we add a new status in that case? */
	if (!(edprsr & CORTEXA_DBG_EDPRSR_POWERED_UP))
		return TARGET_HALT_ERROR;

	/* Check that the core actually halted */
	if (!(edprsr & CORTEXA_DBG_EDPRSR_HALTED))
		return TARGET_HALT_RUNNING;

	priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);

	/* Ensure the OS lock is cleared as a precaution */
	cortexa_armv8_oslock_unlock(target);

	/* Make sure halting debug is enabled (so breakpoints work) */
	priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
	priv->edscr |= CORTEXA_DBG_EDSCR_HALTING_DBG_ENABLE;
	cortex_dbg_write32(target, CORTEXA_DBG_EDSCR, priv->edscr);

	/* Save the target core's registers as debugging operations clobber them */
	cortexa_armv8_regs_save(target);

	target_halt_reason_e reason = TARGET_HALT_FAULT;
	/* Determine why we halted exactly from the Method Of Entry bits */
	switch (priv->edscr & CORTEXA_DBG_EDSCR_STATUS_MASK) {
	case CORTEXA_DBG_EDSCR_STATUS_PE_EXIT_DBG:
		reason = TARGET_HALT_RUNNING;
		break;
	case CORTEXA_DBG_EDSCR_STATUS_PE_DGB:
	case CORTEXA_DBG_EDSCR_STATUS_EXT_DBG_REQ:
		reason = TARGET_HALT_REQUEST;
		break;
	case CORTEXA_DBG_EDSCR_STATUS_BREAKPOINT:
	case CORTEXA_DBG_EDSCR_STATUS_HLT_INSTRUCTION:
	case CORTEXA_DBG_EDSCR_STATUS_EXCEPTION_CATCH:
		reason = TARGET_HALT_BREAKPOINT;
		break;
	case CORTEXA_DBG_EDSCR_STATUS_WATCHPOINT: {
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
	case CORTEXA_DBG_EDSCR_STATUS_HALT_STEP_NORMAL:
	case CORTEXA_DBG_EDSCR_STATUS_HALT_STEP_EXCLUSIVE:
	case CORTEXA_DBG_EDSCR_STATUS_HALT_STEP_NO_SYN:
		reason = TARGET_HALT_BREAKPOINT;
		break;

	case CORTEXA_DBG_EDSCR_STATUS_OS_UNLOCK_CATCH:
	case CORTEXA_DBG_EDSCR_STATUS_RESET_CATCH:
	case CORTEXA_DBG_EDSCR_STATUS_SW_ACCESS_DBG_REG:
		/* XXX What do we do for those cases? */
		break;
	}
	/* Check if we halted because we were actually single-stepping */
	return reason;
}

static void cortexa_armv8_halt_resume(target_s *const target, const bool step)
{
	uint32_t edprsr = cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR);

	/* Check that the core is powered up */
	if (!(edprsr & CORTEXA_DBG_EDPRSR_POWERED_UP))
		return;

	if (!(edprsr & CORTEXA_DBG_EDPRSR_HALTED))
		return;

	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Ensure consistent single step state */
	cortex_dbg_write32(
		target, CORTEXA_DBG_EDECR, cortex_dbg_read32(target, CORTEXA_DBG_EDECR) & ~CORTEXA_DBG_EDECR_SINGLE_STEP);

	/* Restore the core's registers so the running program doesn't know we've been in there */
	cortexa_armv8_regs_restore(target);

	/* First ensure that halting events are enabled */
	priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
	priv->edscr |= CORTEXA_DBG_EDSCR_HALTING_DBG_ENABLE;

	/* Handle single step */
	if (step) {
		cortex_dbg_write32(
			target, CORTEXA_DBG_EDECR, cortex_dbg_read32(target, CORTEXA_DBG_EDECR) | CORTEXA_DBG_EDECR_SINGLE_STEP);
		priv->edscr |= CORTEXA_DBG_EDSCR_INTERRUPT_DISABLE;
	} else {
		priv->edscr &= ~CORTEXA_DBG_EDSCR_INTERRUPT_DISABLE;
	}
	cortex_dbg_write32(target, CORTEXA_DBG_EDSCR, priv->edscr);

	/* Clear any possible error that might have happened */
	cortex_dbg_write32(target, CORTEXA_DBG_EDRCR, CORTEXA_DBG_EDRCR_CLR_STICKY_ERR);

	/* Mark the fault status and address cache invalid */
	priv->core_status &= ~CORTEXA_CORE_STATUS_FAULT_CACHE_VALID;

	/* We assume that halting channel do not pass events to the CTM */

	/* Acknowledge pending halt PE event */
	arm_coresight_cti_acknowledge_interrupt(&priv->cti, CORTEXA_CTI_EVENT_HALT_PE_SINGLE_IDX);

	/* Wait for it to be deasserted */
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);

	bool halt_pe_event_high = true;
	while (!platform_timeout_is_expired(&timeout) && halt_pe_event_high)
		halt_pe_event_high = arm_coresight_cti_read_output_channel_status(&priv->cti, CORTEXA_CTI_CHANNEL_HALT_SINGLE);

	if (halt_pe_event_high) {
		DEBUG_ERROR("Failed to acknowledge pending halt PE event!\n");
		return;
	}

	/* Send CTI request */
	arm_coresight_cti_pulse_channel(&priv->cti, CORTEXA_CTI_CHANNEL_RESTART);

	/* Then poll for when the core actually resumes */
	platform_timeout_set(&timeout, 250);
	edprsr = 0;
	while ((edprsr & CORTEXA_DBG_EDPRSR_STICKY_DEBUG_RESTART) && !platform_timeout_is_expired(&timeout))
		edprsr = cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR);

	if (edprsr & CORTEXA_DBG_EDPRSR_STICKY_DEBUG_RESTART)
		DEBUG_ERROR("Failed to resume PE!\n");
}

static bool cortexa_armv8_attach(target_s *target)
{
	adiv5_access_port_s *ap = cortex_ap(target);
	/* Mark the DP as being in fault so error recovery will switch to this core when in multi-drop mode */
	ap->dp->fault = 1;

	/* Clear any pending fault condition (and switch to this core) */
	target_check_error(target);

	/* Ensure the OS lock is unset just in case it was re-set between probe and attach */
	cortexa_armv8_oslock_unlock(target);
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

	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Clear any stale breakpoints */
	priv->base.breakpoints_mask = 0U;
	for (size_t i = 0; i <= priv->base.breakpoints_available; ++i) {
		cortex_dbg_write32(target, CORTEXA_DBG_BVR_EL1(i), 0U);
		cortex_dbg_write32(target, CORTEXA_DBG_BVR_EL1(i) + 4U, 0U);
		cortex_dbg_write32(target, CORTEXA_DBG_BCR_EL1(i), 0U);
	}

	/* Clear any stale watchpoints */
	priv->base.watchpoints_mask = 0U;
	for (size_t i = 0; i < priv->base.watchpoints_available; ++i) {
		cortex_dbg_write32(target, CORTEXA_DBG_WVR_EL1(i), 0U);
		cortex_dbg_write32(target, CORTEXA_DBG_WVR_EL1(i) + 4U, 0U);
		cortex_dbg_write32(target, CORTEXA_DBG_WCR_EL1(i), 0U);
	}

	return true;
}

static void cortexa_armv8_detach(target_s *target)
{
	const cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Clear any set breakpoints */
	for (size_t i = 0; i <= priv->base.breakpoints_available; ++i) {
		cortex_dbg_write32(target, CORTEXA_DBG_BVR_EL1(i), 0U);
		cortex_dbg_write32(target, CORTEXA_DBG_BVR_EL1(i) + 4U, 0U);
		cortex_dbg_write32(target, CORTEXA_DBG_BCR_EL1(i), 0U);
	}

	/* Clear any set watchpoints */
	for (size_t i = 0; i < priv->base.watchpoints_available; ++i) {
		cortex_dbg_write32(target, CORTEXA_DBG_WVR_EL1(i), 0U);
		cortex_dbg_write32(target, CORTEXA_DBG_WVR_EL1(i) + 4U, 0U);
		cortex_dbg_write32(target, CORTEXA_DBG_WCR_EL1(i), 0U);
	}

	target_halt_resume(target, false);
}

static bool cortexa_armv8_check_error(target_s *const target)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;
	const bool fault = priv->core_status & CORTEXA_CORE_STATUS_ITR_ERR;
	priv->core_status &= (uint8_t)~CORTEXA_CORE_STATUS_ITR_ERR;
	return fault || cortex_check_error(target);
}

static bool cortexa_armv8_check_itr_err(target_s *const target)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* If the instruction triggered an error, signal failure having cleared it */
	if (priv->edscr & CORTEXA_DBG_EDSCR_ERR) {
		priv->core_status |= CORTEXA_CORE_STATUS_ITR_ERR;
		cortex_dbg_write32(target, CORTEXA_DBG_EDRCR, CORTEXA_DBG_EDRCR_CLR_STICKY_ERR);
	}

	return !(priv->edscr & CORTEXA_DBG_EDSCR_ERR);
}

static void cortexa_armv8_dcc_write64(target_s *const target, const uint64_t value)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Poll for empty data */
	priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
	while (priv->edscr & CORTEXA_DBG_EDSCR_RX_FULL)
		priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);

	/* In case of 64-bit, we need to read RX and then TX (yes, that's not a typo here) */
	cortex_dbg_write32(target, CORTEXA_DBG_DTRRX_EL0, value & UINT32_MAX);
	cortex_dbg_write32(target, CORTEXA_DBG_DTRTX_EL0, value >> 32U);

	/* Poll for the data to become ready in the DCC */
	while (!(priv->edscr & CORTEXA_DBG_EDSCR_RX_FULL))
		priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
}

static void cortexa_armv8_dcc_write32(target_s *const target, const uint32_t value)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Poll for empty data */
	priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
	while (priv->edscr & CORTEXA_DBG_EDSCR_RX_FULL)
		priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);

	cortex_dbg_write32(target, CORTEXA_DBG_DTRRX_EL0, value);

	/* Poll for the data to become ready in the DCC */
	while (!(priv->edscr & CORTEXA_DBG_EDSCR_RX_FULL))
		priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
}

static void cortexa_armv8_dcc_read64(target_s *target, uint64_t *const value)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* In case of no data, we wait */
	while (!(priv->edscr & CORTEXA_DBG_EDSCR_TX_FULL))
		priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);

	/* In case of 64-bit, we need to read TX and then RX (yes, that's not a typo here) */
	const uint32_t low = cortex_dbg_read32(target, CORTEXA_DBG_DTRTX_EL0);
	const uint32_t high = cortex_dbg_read32(target, CORTEXA_DBG_DTRRX_EL0);

	*value = low | ((uint64_t)high << 32U);
}

static void cortexa_armv8_dcc_read32(target_s *const target, uint32_t *const value)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* In case of no data, we wait */
	priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
	while (!(priv->edscr & CORTEXA_DBG_EDSCR_TX_FULL))
		priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);

	*value = cortex_dbg_read32(target, CORTEXA_DBG_DTRTX_EL0);
}

static bool cortexa_armv8_run_insn(target_s *const target, const uint32_t insn)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;
	cortex_dbg_write32(target, CORTEXA_DBG_EDITR, insn);

	while (!(priv->edscr & CORTEXA_DBG_EDSCR_ITE))
		priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);

	/* Poll for the operation to be complete */
	priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
	while (!(priv->edscr & CORTEXA_DBG_EDSCR_ITE))
		priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);

	/* Check possible execution failures */
	return cortexa_armv8_check_itr_err(target);
}

static inline bool cortexa_armv8_system_reg_read(
	target_s *const target, const uint16_t system_reg, uint64_t *const result)
{
	if (!cortexa_armv8_run_insn(target, A64_MRS(0, system_reg)))
		return false;

	return cortexa_armv8_core_reg_read64(target, 0, result);
}

static bool cortexa_armv8_core_reg_read64(target_s *const target, const uint8_t reg, uint64_t *const result)
{
	if (reg < 31U) {
		if (!cortexa_armv8_run_insn(target, A64_MSR(A64_DBGDTR_EL0, reg)))
			return false;

		cortexa_armv8_dcc_read64(target, result);
		return true;
	}
	/* If the register is the stack pointer, we first have to extract it to x0 */
	else if (reg == 31U) {
		if (!cortexa_armv8_run_insn(target, A64_READ_SP(1, 0)))
			return false;

		return cortexa_armv8_core_reg_read64(target, 0, result);
	}
	/* If the register is the program counter, we first have to extract it to x0 */
	else if (reg == 32U) {
		return cortexa_armv8_system_reg_read(target, A64_DLR_EL0, result);
	}
	/* If the register is the SPSR, we first have to extract it to x0 */
	else if (reg == 33U) {
		return cortexa_armv8_system_reg_read(target, A64_DSPSR_EL0, result);
	}

	DEBUG_ERROR("%s: Unknown register %d", __func__, reg);
	return false;
}

static bool cortexa_armv8_core_reg_read32(target_s *const target, const uint8_t reg, uint32_t *const result)
{
	if (reg < 31U) {
		if (!cortexa_armv8_run_insn(target, A64_MSR(A64_DBGDTRTX_EL0, reg)))
			return false;

		cortexa_armv8_dcc_read32(target, result);
		return true;
	}

	/* We do not allow read on special registers */
	DEBUG_ERROR("%s: Unknown register %d", __func__, reg);
	return false;
}

static inline bool cortexa_armv8_system_reg_write(
	target_s *const target, const uint16_t system_reg, const uint64_t value)
{
	if (!cortexa_armv8_core_reg_write64(target, 0, value))
		return false;

	return cortexa_armv8_run_insn(target, A64_MSR(system_reg, 0));
}

static bool cortexa_armv8_core_reg_write64(target_s *const target, const uint8_t reg, const uint64_t value)
{
	if (reg < 31U) {
		cortexa_armv8_dcc_write64(target, value);
		return cortexa_armv8_run_insn(target, A64_MRS(reg, A64_DBGDTR_EL0));
	}
	/* If the register is the stack pointer, we first have to write it to x0 */
	else if (reg == 31U) {
		if (!cortexa_armv8_core_reg_write64(target, 0, value))
			return false;

		return cortexa_armv8_run_insn(target, A64_WRITE_SP(1, 0));
	}
	/* If the register is the program counter, we first have to write it to x0 */
	else if (reg == 32U) {
		return cortexa_armv8_system_reg_write(target, A64_DLR_EL0, value);
	}
	/* If the register is the SPSR, we first have to write it to x0 */
	else if (reg == 33U) {
		return cortexa_armv8_system_reg_write(target, A64_DSPSR_EL0, value);
	}

	DEBUG_ERROR("%s: Unknown register %d", __func__, reg);
	return false;
}

static bool cortexa_armv8_core_reg_write32(target_s *const target, const uint8_t reg, const uint32_t value)
{
	if (reg < 31U) {
		cortexa_armv8_dcc_write32(target, value);
		return cortexa_armv8_run_insn(target, A64_MRS(reg, A64_DBGDTRTX_EL0));
	}

	/* We do not allow write on special registers */
	DEBUG_ERROR("%s: Unknown register %d", __func__, reg);
	return false;
}

static void cortexa_armv8_core_regs_save(target_s *const target)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Save out x0-x30 in that order (clobbers x0 and x1) */
	for (size_t i = 0U; i < ARRAY_LENGTH(priv->core_regs.x); ++i) {
		bool success = cortexa_armv8_core_reg_read64(target, i, &priv->core_regs.x[i]);

		if (!success)
			DEBUG_ERROR("%s: Failed to read register x%lu\n", __func__, i);
	}

	/* Save SP/PC/SPSR registers */
	cortexa_armv8_core_reg_read64(target, 31U, &priv->core_regs.sp);
	cortexa_armv8_core_reg_read64(target, 32U, &priv->core_regs.pc);
	cortexa_armv8_core_reg_read64(target, 33U, &priv->core_regs.spsr);

	/* Adjust PC as it is given by the DLR register */
	priv->core_regs.pc -= 0x4;
}

static void cortexa_armv8_regs_save(target_s *const target)
{
	cortexa_armv8_core_regs_save(target);
	/* XXX: Save float registers */
}

static void cortexa_armv8_core_regs_restore(target_s *const target)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Restore SP/PC/SPSR registers */
	cortexa_armv8_core_reg_write64(target, 31U, priv->core_regs.sp);
	cortexa_armv8_core_reg_write64(target, 32U, priv->core_regs.pc);
	cortexa_armv8_core_reg_write64(target, 33U, priv->core_regs.spsr);

	/* Restore x1-31 in that order. Ignore r0 for the moment as it gets clobbered repeatedly */
	for (size_t i = 1U; i < ARRAY_LENGTH(priv->core_regs.x); ++i)
		cortexa_armv8_core_reg_write64(target, i, priv->core_regs.x[i]);

	/* Now we're done with the rest of the registers, restore x0 */
	cortexa_armv8_core_reg_write64(target, 0U, priv->core_regs.x[0U]);
}

static void cortexa_armv8_regs_restore(target_s *const target)
{
	/* XXX: Restore float registers */
	cortexa_armv8_core_regs_restore(target);
}

static void cortexa_armv8_regs_read(target_s *target, void *data)
{
	const cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;
	uint64_t *const regs = (uint64_t *)data;

	/* Copy the register values out from our cache */
	memcpy(regs, priv->core_regs.x, sizeof(priv->core_regs.x));
	regs[31U] = priv->core_regs.sp;
	regs[32U] = priv->core_regs.pc;

	/* GDB expects CPSR to be 32-bit, only copy the lower bits */
	memcpy(regs + 33U, &priv->core_regs.spsr, sizeof(uint32_t));

	/* XXX: float registers */
}

static void cortexa_armv8_regs_write(target_s *target, const void *data)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;
	const uint64_t *const regs = (const uint64_t *)data;

	/* Copy the new register values into our cache */
	memcpy(priv->core_regs.x, regs, sizeof(priv->core_regs.x));

	priv->core_regs.sp = regs[31U];
	priv->core_regs.pc = regs[32U];

	/* GDB expects CPSR to be 32-bit, only copy the lower bits */
	memcpy(&priv->core_regs.spsr, &regs[33U], sizeof(uint32_t));

	/* XXX: float registers */
}

static void *cortexa_armv8_reg_ptr(target_s *const target, const size_t reg)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* x0-x30 */
	if (reg < CORTEXA_ARMV8_GENERAL_REG_COUNT)
		return &priv->core_regs.x[reg];

	/* sp */
	if (reg == 31U)
		return &priv->core_regs.sp;

	/* pc */
	if (reg == 32U)
		return &priv->core_regs.pc;

	/* spsr */
	if (reg == 33U)
		return &priv->core_regs.spsr;

	return NULL;
}

static size_t cortexa_armv8_reg_width(const size_t reg)
{
	/* GDB map SPSR to CPSR, we ignore the top bits in that case */
	if (reg == 33U)
		return 4U;

	return 8U;
}

static size_t cortexa_armv8_reg_read(target_s *const target, const uint32_t reg, void *const data, const size_t max)
{
	/* Try to get a pointer to the storage for the requested register, and return -1 if that fails */
	const void *const reg_ptr = cortexa_armv8_reg_ptr(target, reg);
	if (!reg_ptr)
		return 0;
	/* Now we have a valid register, get its width in bytes, and check that against max */
	const size_t reg_width = cortexa_armv8_reg_width(reg);
	if (max < reg_width)
		return 0;
	/* Finally, copy the register data out and return the width */
	memcpy(data, reg_ptr, reg_width);
	return reg_width;
}

static size_t cortexa_armv8_reg_write(
	target_s *const target, const uint32_t reg, const void *const data, const size_t max)
{
	/* Try to get a pointer to the storage for the requested register, and return -1 if that fails */
	void *const reg_ptr = cortexa_armv8_reg_ptr(target, reg);
	if (!reg_ptr)
		return 0;
	/* Now we have a valid register, get its width in bytes, and check that against max */
	const size_t reg_width = cortexa_armv8_reg_width(reg);
	if (max < reg_width)
		return 0;
	/* Finally, copy the new register data in and return the width */
	memcpy(reg_ptr, data, reg_width);
	return reg_width;
}

/*
 * This function creates the target description XML string for a Cortex-A/R part.
 * This is done this way to decrease string duplications and thus code size,
 * making it unfortunately much less readable than the string literal it is
 * equivalent to.
 *
 * The string it creates is approximately the following:
 * <?xml version="1.0"?>
 * <!DOCTYPE feature SYSTEM "gdb-target.dtd">
 * <target>
 *   <architecture>aarch64</architecture>
 *   <feature name="org.gnu.gdb.aarch64.core">
 *     <reg name="x0" bitsize="64"/>
 *     <reg name="x1" bitsize="64"/>
 *     <reg name="x2" bitsize="64"/>
 *     <reg name="x3" bitsize="64"/>
 *     <reg name="x4" bitsize="64"/>
 *     <reg name="x5" bitsize="64"/>
 *     <reg name="x6" bitsize="64"/>
 *     <reg name="x7" bitsize="64"/>
 *     <reg name="x8" bitsize="64"/>
 *     <reg name="x9" bitsize="64"/>
 *     <reg name="x10" bitsize="64"/>
 *     <reg name="x11" bitsize="64"/>
 *     <reg name="x12" bitsize="64"/>
 *     <reg name="x13" bitsize="64"/>
 *     <reg name="x14" bitsize="64"/>
 *     <reg name="x15" bitsize="64"/>
 *     <reg name="x16" bitsize="64"/>
 *     <reg name="x17" bitsize="64"/>
 *     <reg name="x18" bitsize="64"/>
 *     <reg name="x19" bitsize="64"/>
 *     <reg name="x20" bitsize="64"/>
 *     <reg name="x21" bitsize="64"/>
 *     <reg name="x22" bitsize="64"/>
 *     <reg name="x23" bitsize="64"/>
 *     <reg name="x24" bitsize="64"/>
 *     <reg name="x25" bitsize="64"/>
 *     <reg name="x26" bitsize="64"/>
 *     <reg name="x27" bitsize="64"/>
 *     <reg name="x28" bitsize="64"/>
 *     <reg name="x29" bitsize="64"/>
 *     <reg name="x30" bitsize="64"/>
 *     <reg name="sp" bitsize="64" type="data_ptr"/>
 *     <reg name="pc" bitsize="64" type="code_ptr"/>
 *     <flags id="cpsr_flags" size="4">
 *       <field name="SP" start="0" end="0"/>
 *       <field name="EL" start="2" end="3"/>
 *       <field name="nRW" start="4" end="4"/>
 *       <field name="F" start="6" end="6"/>
 *       <field name="I" start="7" end="7"/>
 *       <field name="A" start="8" end="8"/>
 *       <field name="D" start="9" end="9"/>
 *       <field name="BTYPE" start="10" end="11"/>
 *       <field name="SSBS" start="12" end="12"/>
 *       <field name="IL" start="20" end="20"/>
 *       <field name="SS" start="21" end="21"/>
 *       <field name="PAN" start="22" end="22"/>
 *       <field name="UAO" start="23" end="23"/>
 *       <field name="DIT" start="24" end="24"/>
 *       <field name="TCO" start="25" end="25"/>
 *       <field name="V" start="28" end="28"/>
 *       <field name="C" start="29" end="29"/>
 *       <field name="Z" start="30" end="30"/>
 *       <field name="N" start="31" end="31"/>
 *     </flags>
 *     <reg name="cpsr" bitsize="32" type="cpsr_flags"/>
 *   </feature>
 * </target>
 */
static size_t cortexa_armv8_build_target_description(char *const buffer, size_t max_length)
{
	size_t print_size = max_length;
	/* Start with the "preamble" chunks which are mostly common across targets save for 2 words. */
	int offset = snprintf(buffer, print_size, "%s target %saarch64%s <feature name=\"org.gnu.gdb.aarch64.core\">",
		gdb_xml_preamble_first, gdb_xml_preamble_second, gdb_xml_preamble_third);

	/* Then build the general purpose register descriptions for x0-x30 */
	for (uint8_t i = 0; i < CORTEXA_ARMV8_GENERAL_REG_COUNT; ++i) {
		if (max_length != 0)
			print_size = max_length - (size_t)offset;

		offset += snprintf(buffer + offset, print_size, "<reg name=\"x%u\" bitsize=\"64\"/>", i);
	}

	/* Then we build the flags that we have defined */
	for (uint8_t i = 0; i < ARRAY_LENGTH(cortexa_armv8_flags); ++i) {
		if (max_length != 0)
			print_size = max_length - (size_t)offset;

		const struct cortexa_armv8_gdb_flags_def *def = &cortexa_armv8_flags[i];

		offset += snprintf(buffer + offset, print_size, "<flags id=\"%s\" size=\"%d\">", def->id, def->size);

		if (max_length != 0)
			print_size = max_length - (size_t)offset;

		for (uint8_t y = 0; y < def->fields_count; ++y) {
			if (max_length != 0)
				print_size = max_length - (size_t)offset;

			const struct cortexa_armv8_gdb_field_def *field = &def->fields[y];
			offset += snprintf(buffer + offset, print_size, "<field name=\"%s\" start=\"%d\" end=\"%d\"/>", field->name,
				field->start, field->end);
		}

		if (max_length != 0)
			print_size = max_length - (size_t)offset;

		offset += snprintf(buffer + offset, print_size, "</flags>");
	}

	/* Now build the special-purpose register descriptions using the arrays at the top of file */
	for (uint8_t i = 0; i < ARRAY_LENGTH(cortexa_armv8_sprs); ++i) {
		if (max_length != 0)
			print_size = max_length - (size_t)offset;

		const struct cortexa_armv8_gdb_reg_def *def = &cortexa_armv8_sprs[i];

		/*
		 * Create tag for each register
		 */
		offset += snprintf(buffer + offset, print_size, "<reg name=\"%s\" bitsize=\"%d\" type=\"%s\"/>", def->name,
			def->bit_size, def->type_name);
	}

	/* Build the XML blob's termination */
	if (max_length != 0)
		print_size = max_length - (size_t)offset;

	offset += snprintf(buffer + offset, print_size, "</feature></target>");
	/* offset is now the total length of the string created, discard the sign and return it. */
	return (size_t)offset;
}

static const char *cortexa_armv8_target_description(target_s *const target)
{
	(void)target;

	const size_t description_length = cortexa_armv8_build_target_description(NULL, 0) + 1U;
	char *const description = malloc(description_length);
	if (description)
		(void)cortexa_armv8_build_target_description(description, description_length);

	return description;
}

/*
 * Halt the core and await halted status. This function should only return true when
 * it is, itself, responsible for having halted the target. This allows storing of the
 * returned value to later determine whether the target should be resumed.
 */
static bool cortexa_armv8_halt_and_wait(target_s *const target)
{
	/*
	 * Check the target is already halted; return false as this function was not
	 * responsible for halting the target.
	 */
	if (cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR) & CORTEXA_DBG_EDPRSR_HALTED)
		return false;

	platform_timeout_s timeout;
	target_halt_request(target);
	platform_timeout_set(&timeout, 250);
	target_halt_reason_e reason = TARGET_HALT_RUNNING;
	while (!platform_timeout_is_expired(&timeout) && reason == TARGET_HALT_RUNNING)
		reason = target_halt_poll(target, NULL);
	if (reason != TARGET_HALT_REQUEST) {
		DEBUG_ERROR("Failed to halt the core\n");
		/* This function tried to halt the target but ultimately was not successful. */
		return false;
	}

	/* Return true as this function successfully halted the target. */
	return true;
}

static inline void cortexa_armv8_mem_read_fault_state(
	target_s *const target, cortexa_armv8_priv_fault_state_s *const state)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	switch (A64_DSPSR_EL0_EXTRACT_EL(priv->core_regs.spsr)) {
	case 3:
		cortexa_armv8_system_reg_read(target, A64_ESR_EL3, &state->status);
		cortexa_armv8_system_reg_read(target, A64_FAR_EL3, &state->address);
		break;
	case 2:
		cortexa_armv8_system_reg_read(target, A64_ESR_EL2, &state->status);
		cortexa_armv8_system_reg_read(target, A64_FAR_EL2, &state->address);
		break;
	default:
		cortexa_armv8_system_reg_read(target, A64_ESR_EL1, &state->status);
		cortexa_armv8_system_reg_read(target, A64_FAR_EL1, &state->address);
		break;
	}
}

static inline void cortexa_armv8_mem_restore_regs(target_s *const target, const char *const func)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;
	cortexa_armv8_priv_fault_state_s *const fault_state = &priv->fault_state;

	/* In case of fault and if the data saved are valid, we restore appropriate system values */
	if (!cortexa_armv8_check_itr_err(target) && priv->core_status & CORTEXA_CORE_STATUS_FAULT_CACHE_VALID) {
		cortexa_armv8_priv_fault_state_s tmp;
#ifndef DEBUG_WARN_IS_NOOP
		cortexa_armv8_mem_read_fault_state(target, &tmp);
		DEBUG_WARN("%s: Failed at 0x%08" PRIx64 " (%08" PRIx64 ")\n", func, tmp.address, tmp.status);
#else
		(void)func;
#endif

		switch (A64_DSPSR_EL0_EXTRACT_EL(priv->core_regs.spsr)) {
		case 3:
			cortexa_armv8_system_reg_write(target, A64_ESR_EL3, fault_state->status);
			cortexa_armv8_system_reg_write(target, A64_FAR_EL3, fault_state->address);
			break;
		case 2:
			cortexa_armv8_system_reg_write(target, A64_ESR_EL2, fault_state->status);
			cortexa_armv8_system_reg_write(target, A64_FAR_EL2, fault_state->address);
			break;
		default:
			cortexa_armv8_system_reg_write(target, A64_ESR_EL1, fault_state->status);
			cortexa_armv8_system_reg_write(target, A64_FAR_EL1, fault_state->address);
			break;
		}
	}
}

/* Fast path for cortexa_armv8_mem_read(). */
static inline void cortexa_armv8_core_mem_read32(
	target_s *const target, uint32_t *const dst, const target_addr64_t src, const size_t n)
{
	if (n == 0)
		return;

	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Clear memory access mode (sanity) */
	priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
	priv->edscr &= ~CORTEXA_DBG_EDSCR_MA;
	cortex_dbg_write32(target, CORTEXA_DBG_EDSCR, priv->edscr);

	/* Follows "Using memory access mode in AArch64" state (DDI0487K §K12.2, pg14564) */

	/* 1.abc Setup address in X0 */
	cortexa_armv8_core_reg_write64(target, 0U, src);

	/* 1.d Do dummy operation */
	cortexa_armv8_run_insn(target, A64_MSR(A64_DBGDTR_EL0, 0U));

	/* 1.e Set memory access mode */
	priv->edscr |= CORTEXA_DBG_EDSCR_MA;
	cortex_dbg_write32(target, CORTEXA_DBG_EDSCR, priv->edscr);

	/* 1.f Read DBGDTRTX_EL0 and discard */
	cortex_dbg_read32(target, CORTEXA_DBG_DTRTX_EL0);

	/* 2. Loop n-1 times and read all values except last one */
	for (size_t i = 0; i < n - 1; i++)
		dst[i] = cortex_dbg_read32(target, CORTEXA_DBG_DTRTX_EL0);

	/* 3.a Clear memory access mode */
	priv->edscr &= ~CORTEXA_DBG_EDSCR_MA;
	cortex_dbg_write32(target, CORTEXA_DBG_EDSCR, priv->edscr);

	/* 3.b Read the last value */
	dst[n - 1] = cortex_dbg_read32(target, CORTEXA_DBG_DTRTX_EL0);

	/* We defer 3.c to the caller of this function */
}

static inline void cortexa_armv8_core_mem_read(
	target_s *const target, uint8_t *const dst, target_addr64_t src, const size_t dst_size)
{
	if (dst_size == 0)
		return;

	size_t offset = 0;
	uint32_t tmp;

	cortexa_armv8_core_reg_write64(target, 0U, src);

	/* If the address is odd, read a byte to get onto an even address */
	if (src & 1U) {
		if (!cortexa_armv8_run_insn(target, ARM_LDRB_X0_X1_INSN))
			return;

		if (!cortexa_armv8_core_reg_read32(target, 1U, &tmp))
			return;

		dst[offset++] = (uint8_t)tmp;
		++src;
	}
	/* If the address is now even but only 16-bit aligned, read a uint16_t to get onto 32-bit alignment */
	if ((src & 2U) && dst_size - offset >= 2U) {
		if (!cortexa_armv8_run_insn(target, ARM_LDRH_X0_X1_INSN))
			return;

		if (!cortexa_armv8_core_reg_read32(target, 1U, &tmp))
			return;

		write_le2(dst, offset, (uint16_t)tmp);
		offset += 2U;
		src += 2U;
	}

	/* Use the fast path to read as much as possible before doing a slow path fixup at the end */
	cortexa_armv8_core_mem_read32(target, (uint32_t *)(dst + offset), src, (dst_size - offset) >> 2U);

	const uint8_t remainder = (dst_size - offset) & 3U;
	/* If the remainder needs at least 2 more bytes read, do this first */
	if (remainder & 2U) {
		if (!cortexa_armv8_run_insn(target, ARM_LDRH_X0_X1_INSN))
			return;

		if (!cortexa_armv8_core_reg_read32(target, 1U, &tmp))
			return;

		write_le2(dst, offset, tmp);
		offset += 2U;
	}
	/* Finally, fix things up if a final byte is required. */
	if (remainder & 1U) {
		if (!cortexa_armv8_run_insn(target, ARM_LDRB_X0_X1_INSN))
			return;

		if (!cortexa_armv8_core_reg_read32(target, 1U, &tmp))
			return;

		dst[offset++] = (uint8_t)tmp;
	}
}

/*
 * This reads memory by jumping from the debug unit bus to the system bus.
 * NB: This requires the core to be halted! Uses instruction launches on
 * the core and requires we're in debug mode to work. Trashes x0.
 * If core is not halted, temporarily halts target and resumes at the end
 * of the function.
 */
static void cortexa_armv8_mem_read(
	target_s *const target, void *const dest, const target_addr64_t src, const size_t len)
{
	/* If system is not halted, halt temporarily within this function. */
	const bool halted_in_function = cortexa_armv8_halt_and_wait(target);

	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Cache current fault info */
	if (!(priv->core_status & CORTEXA_CORE_STATUS_FAULT_CACHE_VALID)) {
		cortexa_armv8_mem_read_fault_state(target, &priv->fault_state);
		priv->core_status |= CORTEXA_CORE_STATUS_FAULT_CACHE_VALID;
	}

	/* Clear any existing fault state */
	priv->core_status &= ~CORTEXA_CORE_STATUS_ITR_ERR;

	cortexa_armv8_core_mem_read(target, (uint8_t *)dest, src, len);

	/* Deal with any data faults that occurred */
	cortexa_armv8_mem_restore_regs(target, __func__);

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

	if (halted_in_function)
		cortexa_armv8_halt_resume(target, false);
}

/* Fast path for cortexa_armv8_mem_write(). */
static inline void cortexa_armv8_core_mem_write32(
	target_s *const target, const target_addr64_t dst, const uint32_t *const src, const size_t n)
{
	if (n == 0)
		return;

	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Clear memory access mode (sanity) */
	priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
	priv->edscr &= ~CORTEXA_DBG_EDSCR_MA;
	cortex_dbg_write32(target, CORTEXA_DBG_EDSCR, priv->edscr);

	/* Follows "Using memory access mode in AArch64" state (DDI0487K §K12.1, pg14563) */

	/* 1.abc Setup address in X0 */
	cortexa_armv8_core_reg_write64(target, 0U, dst);

	/* 1.d Do dummy operation */
	cortexa_armv8_run_insn(target, A64_MSR(A64_DBGDTR_EL0, 0U));

	/* 1.e Set memory access mode */
	priv->edscr |= CORTEXA_DBG_EDSCR_MA;
	cortex_dbg_write32(target, CORTEXA_DBG_EDSCR, priv->edscr);

	/* 2. Loop n times and write all values */
	for (size_t i = 0; i < n; i++)
		cortex_dbg_write32(target, CORTEXA_DBG_DTRRX_EL0, src[i]);

	/* 3.a Clear memory access mode */
	priv->edscr &= ~CORTEXA_DBG_EDSCR_MA;
	cortex_dbg_write32(target, CORTEXA_DBG_EDSCR, priv->edscr);

	/* We defer 3.b to the caller of this function */
}

static void cortexa_armv8_core_mem_write(
	target_s *const target, target_addr_t dst, const uint8_t *const src, const size_t src_size)
{
	if (src_size == 0)
		return;

	size_t offset = 0;

	cortexa_armv8_core_reg_write64(target, 0U, dst);

	/* If the address is odd, write a byte to get onto an even address */
	if (dst & 1U) {
		cortexa_armv8_core_reg_write32(target, 1U, src[offset++]);
		if (!cortexa_armv8_run_insn(target, ARM_STRB_X1_X0_INSN))
			return;
		++dst;
	}
	/* If the address is now even but only 16-bit aligned, write a uint16_t to get onto 32-bit alignment */
	if ((dst & 2U) && src_size - offset >= 2U) {
		cortexa_armv8_core_reg_write32(target, 1U, read_le2(src, offset));
		if (!cortexa_armv8_run_insn(target, ARM_STRH_X1_X0_INSN))
			return;
		offset += 2U;
		dst += 2U;
	}

	/* Use the fast path to write as much as possible before doing a slow path fixup at the end */
	cortexa_armv8_core_mem_write32(target, dst, (const uint32_t *)(src + offset), (src_size - offset) >> 2U);

	const uint8_t remainder = (src_size - offset) & 3U;
	/* If the remainder needs at least 2 more bytes write, do this first */
	if (remainder & 2U) {
		cortexa_armv8_core_reg_write32(target, 1U, read_le2(src, offset));
		if (!cortexa_armv8_run_insn(target, ARM_STRH_X1_X0_INSN))
			return;
		offset += 2U;
	}
	/* Finally, fix things up if a final byte is required. */
	if (remainder & 1U) {
		cortexa_armv8_core_reg_write32(target, 1U, src[offset++]);
		if (!cortexa_armv8_run_insn(target, ARM_STRB_X1_X0_INSN))
			return;
	}
}

/*
 * This writes memory by jumping from the debug unit bus to the system bus.
 * NB: This requires the core to be halted! Uses instruction launches on
 * the core and requires we're in debug mode to work. Trashes x0.
 */
static void cortexa_armv8_mem_write(
	target_s *const target, const target_addr64_t dest, const void *const src, const size_t len)
{
	/* If system is not halted, halt temporarily within this function. */
	const bool halted_in_function = cortexa_armv8_halt_and_wait(target);

	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;
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

	/* Cache current fault info */
	if (!(priv->core_status & CORTEXA_CORE_STATUS_FAULT_CACHE_VALID)) {
		cortexa_armv8_mem_read_fault_state(target, &priv->fault_state);
		priv->core_status |= CORTEXA_CORE_STATUS_FAULT_CACHE_VALID;
	}

	/* Clear any existing fault state */
	priv->core_status &= ~CORTEXA_CORE_STATUS_ITR_ERR;

	cortexa_armv8_core_mem_write(target, dest, (const uint8_t *)src, len);

	/* Deal with any data faults that occurred */
	cortexa_armv8_mem_restore_regs(target, __func__);

	if (halted_in_function)
		cortexa_armv8_halt_resume(target, false);
}

static void cortexa_armv8_config_breakpoint(
	target_s *const target, const size_t slot, const breakwatch_s *const breakwatch)
{
	/* Enable breakpoint on all exception levels (DDI0487K §D2.8.4, pg6165) */
	uint32_t control = CORTEXA_DBG_BCR_EL1_ENABLE | CORTEXA_DBG_BCR_EL1_TYPE_UNLINKED_INSN_MATCH |
		CORTEXA_DBG_BCR_EL1_HIGH_MODE_CONTROL | CORTEXA_DBG_BCR_EL1_PRIV_MODE_CONTROL(2);

	if (breakwatch->size == 4)
		control |= CORTEXA_DBG_BCR_EL1_BYTE_SELECT_ALL;
	else if ((breakwatch->addr & 2) && breakwatch->size == 2)
		control |= CORTEXA_DBG_BCR_EL1_BYTE_SELECT_HIGH_HALF;
	else if (breakwatch->size == 2)
		control |= CORTEXA_DBG_BCR_EL1_BYTE_SELECT_LOW_HALF;
	else {
		DEBUG_ERROR("Invalid breakpoint size %ld\n", breakwatch->size);
		return;
	}

	/* Configure the breakpoint slot */
	cortex_dbg_write32(target, CORTEXA_DBG_BVR_EL1(slot), breakwatch->addr & ~3U);
	cortex_dbg_write32(target, CORTEXA_DBG_BVR_EL1(slot) + 4U, breakwatch->addr >> 32);
	cortex_dbg_write32(target, CORTEXA_DBG_BCR_EL1(slot), control);
}

static uint32_t cortexa_armv8_watchpoint_mode(const target_breakwatch_e type)
{
	switch (type) {
	case TARGET_WATCH_READ:
		return CORTEXA_DBG_WCR_EL1_MATCH_ON_LOAD;
	case TARGET_WATCH_WRITE:
		return CORTEXA_DBG_WCR_EL1_MATCH_ON_STORE;
	case TARGET_WATCH_ACCESS:
		return CORTEXA_DBG_WCR_EL1_MATCH_ANY_ACCESS;
	default:
		return 0U;
	}
}

static void cortexa_armv8_config_watchpoint(
	target_s *const target, const size_t slot, const breakwatch_s *const breakwatch)
{
	const uint32_t byte_mask = ((1U << breakwatch->size) - 1U) << (breakwatch->addr & 3U);

	/* Enable watchpoint on all exception levels (DDI0487K §D2.8.4, pg6165) */
	const uint32_t control = CORTEXA_DBG_WCR_EL1_ENABLE | CORTEXA_DBG_WCR_EL1_PRIV_MODE_CONTROL(2) |
		CORTEXA_DBG_WCR_EL1_HIGH_MODE_CONTROL | cortexa_armv8_watchpoint_mode(breakwatch->type) |
		CORTEXA_DBG_WCR_EL1_BYTE_SELECT(byte_mask);

	/* Configure the watchpoint slot */
	cortex_dbg_write32(target, CORTEXA_DBG_WVR_EL1(slot), breakwatch->addr & ~3U);
	cortex_dbg_write32(target, CORTEXA_DBG_WVR_EL1(slot) + 4U, breakwatch->addr >> 32);
	cortex_dbg_write32(target, CORTEXA_DBG_WCR_EL1(slot), control);
}

static int cortexa_armv8_breakwatch_set(target_s *const target, breakwatch_s *const breakwatch)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

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
		cortexa_armv8_config_breakpoint(target, breakpoint, breakwatch);
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
		cortexa_armv8_config_watchpoint(target, watchpoint, breakwatch);
		priv->base.watchpoints_mask |= 1U << watchpoint;
		breakwatch->reserved[0] = watchpoint;
		/* Tell the debugger that it was successfully able to set the watchpoint */
		return 0;
	}
	default:
		/* If the breakwatch type is not one of the above, tell the debugger we don't support it */
		return 1;
	}

	return 1;
}

static int cortexa_armv8_breakwatch_clear(target_s *const target, breakwatch_s *const breakwatch)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	switch (breakwatch->type) {
	case TARGET_BREAK_HARD: {
		/* Clear the breakpoint slot this used */
		const size_t breakpoint = breakwatch->reserved[0];
		cortex_dbg_write32(target, CORTEXA_DBG_BCR_EL1(breakpoint), 0);
		priv->base.breakpoints_mask &= ~(1U << breakpoint);
		/* Tell the debugger that it was successfully able to clear the breakpoint */
		return 0;
	}
	case TARGET_WATCH_READ:
	case TARGET_WATCH_WRITE:
	case TARGET_WATCH_ACCESS: {
		/* Clear the watchpoint slot this used */
		const size_t watchpoint = breakwatch->reserved[0];
		cortex_dbg_write32(target, CORTEXA_DBG_WCR_EL1(watchpoint), 0);
		priv->base.watchpoints_mask &= ~(1U << watchpoint);
		/* Tell the debugger that it was successfully able to clear the watchpoint */
		return 0;
	}
	default:
		/* If the breakwatch type is not one of the above, tell the debugger wed on't support it */
		return 1;
	}
}
