/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2016  Black Sphere Technologies Ltd.
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

/* This file implements debugging functionality specific to ARM
 * the Cortex-A9 core.  This should be generic to ARMv7-A as it is
 * implemented according to the "ARMv7-A Architecture Reference Manual",
 * ARM doc DDI0406C.
 *
 * Cache line length is from Cortex-A9 TRM, may differ for others.
 * Janky reset code is for Zynq-7000 which disconnects the DP from the JTAG
 * scan chain during reset.
 */
#include "general.h"
#include "exception.h"
#include "adiv5.h"
#include "target.h"
#include "target_internal.h"
#include "target_probe.h"
#include "cortex.h"
#include "cortex_internal.h"
#include "gdb_reg.h"

#include <stdlib.h>
#include <assert.h>

static bool cortexa_attach(target_s *t);
static void cortexa_detach(target_s *t);
static void cortexa_halt_resume(target_s *t, bool step);

static const char *cortexa_regs_description(target_s *t);
static void cortexa_regs_read(target_s *t, void *data);
static void cortexa_regs_write(target_s *t, const void *data);
static void cortexa_regs_read_internal(target_s *t);
static void cortexa_regs_write_internal(target_s *t);
static ssize_t cortexa_reg_read(target_s *t, uint32_t reg, void *data, size_t max);
static ssize_t cortexa_reg_write(target_s *t, uint32_t reg, const void *data, size_t max);

static void cortexa_reset(target_s *t);
static target_halt_reason_e cortexa_halt_poll(target_s *t, target_addr_t *watch);
static void cortexa_halt_request(target_s *t);

static int cortexa_breakwatch_set(target_s *t, breakwatch_s *);
static int cortexa_breakwatch_clear(target_s *t, breakwatch_s *);
static uint32_t bp_bas(uint32_t addr, uint8_t len);

static void write_gpreg(target_s *t, uint8_t regno, uint32_t val);
static uint32_t read_gpreg(target_s *t, uint8_t regno);

typedef struct cortexa_priv {
	/* Base core information */
	cortex_priv_s base;

	struct {
		uint32_t r[16];
		uint32_t cpsr;
		uint32_t fpscr;
		uint64_t d[16];
	} reg_cache;

	uint32_t bcr0;
	uint32_t bvr0;
	bool mmu_fault;
} cortexa_priv_s;

#define CORTEXAR_DBG_IDR   0x000U
#define CORTEXAR_DBG_DTRTX 0x080U /* DBGDTRRXext */
#define CORTEXAR_DBG_ITR   0x084U
#define CORTEXAR_DBG_DSCR  0x088U
#define CORTEXAR_DBG_DTRRX 0x08cU /* DBGDTRTXext */
#define CORTEXAR_DBG_DRCR  0x090U
#define CORTEXAR_DBG_BVR   0x100U
#define CORTEXAR_DBG_BCR   0x140U
#define CORTEXAR_DBG_WVR   0x180U
#define CORTEXAR_DBG_WCR   0x1c0U
#define CORTEXAR_CTR       0xd04U

#define CORTEXAR_DBG_DSCCR 0x028U
#define CORTEXAR_DBG_DSMCR 0x02cU
#define CORTEXAR_DBG_OSLAR 0x300U
#define CORTEXAR_DBG_OSLSR 0x304U
#define CORTEXAR_DBG_LAR   0xfb0U /* Lock Access */
#define CORTEXAR_DBG_LSR   0xfb4U /* Lock Status */

#define CORTEXAR_DBG_OSLSR_OSLM0 (1U << 0U)
#define CORTEXAR_DBG_OSLSR_OSLK  (1U << 1U)
#define CORTEXAR_DBG_OSLSR_NTT   (1U << 2U)
#define CORTEXAR_DBG_OSLSR_OSLM1 (1U << 3U)
#define CORTEXAR_DBG_OSLSR_OSLM  (CORTEXAR_DBG_OSLSR_OSLM0 | CORTEXAR_DBG_OSLSR_OSLM1)

#define CORTEXAR_DBG_IDR_BREAKPOINT_MASK  0xfU
#define CORTEXAR_DBG_IDR_BREAKPOINT_SHIFT 24U
#define CORTEXAR_DBG_IDR_WATCHPOINT_MASK  0xfU
#define CORTEXAR_DBG_IDR_WATCHPOINT_SHIFT 28U

#define CORTEXAR_DBG_DSCR_HALTED           (1U << 0U)
#define CORTEXAR_DBG_DSCR_RESTARTED        (1U << 1U)
#define CORTEXAR_DBG_DSCR_MOE_MASK         0x0000003cU
#define CORTEXAR_DBG_DSCR_MOE_HALT_REQUEST 0x00000000U
#define CORTEXAR_DBG_DSCR_MOE_BREAKPOINT   0x00000004U
#define CORTEXAR_DBG_DSCR_MOE_ASYNC_WATCH  0x00000008U
#define CORTEXAR_DBG_DSCR_MOE_BKPT_INSN    0x0000000cU
#define CORTEXAR_DBG_DSCR_MOE_EXTERNAL_DBG 0x00000010U
#define CORTEXAR_DBG_DSCR_MOE_VEC_CATCH    0x00000014U
#define CORTEXAR_DBG_DSCR_MOE_SYNC_WATCH   0x00000028U
#define CORTEXAR_DBG_DSCR_ITR_ENABLE       (1U << 13U)
#define CORTEXAR_DBG_DSCR_HALT_DBG_ENABLE  (1U << 14U)
#define CORTEXAR_DBG_DSCR_INSN_COMPLETE    (1U << 24U)
#define CORTEXAR_DBG_DSCR_DTR_READ_READY   (1U << 29U)
#define CORTEXAR_DBG_DSCR_DTR_WRITE_DONE   (1U << 30U)

#define DBGDSCR_EXTDCCMODE_STALL (1U << 20U)
#define DBGDSCR_EXTDCCMODE_FAST  (2U << 20U)
#define DBGDSCR_EXTDCCMODE_MASK  (3U << 20U)
#define DBGDSCR_INTDIS           (1U << 11U)
#define DBGDSCR_UND_I            (1U << 8U)
#define DBGDSCR_SDABORT_L        (1U << 6U)

#define DBGDRCR_CSE (1U << 2U)
#define DBGDRCR_RRQ (1U << 1U)
#define DBGDRCR_HRQ (1U << 0U)

#define DBGBCR_INST_MISMATCH (4U << 20U)
#define DBGBCR_BAS_ANY       (0xfU << 5U)
#define DBGBCR_BAS_LOW_HW    (0x3U << 5U)
#define DBGBCR_BAS_HIGH_HW   (0xcU << 5U)
#define DBGBCR_EN            (1U << 0U)
#define DBGBCR_PMC_ANY       (0x3U << 1U) /* 0b11 */

#define DBGWCR_LSC_LOAD     (0x1U << 3U) /* 0b01 */
#define DBGWCR_LSC_STORE    (0x2U << 3U) /* 0b10 */
#define DBGWCR_LSC_ANY      (0x3U << 3U) /* 0b11U */
#define DBGWCR_BAS_BYTE     (0x1U << 5U) /* 0b0001U */
#define DBGWCR_BAS_HALFWORD (0x3U << 5U) /* 0b0011U */
#define DBGWCR_BAS_WORD     (0xfU << 5U) /* 0b1111U */
#define DBGWCR_PAC_ANY      (0x3U << 1U) /* 0b11U */
#define DBGWCR_EN           (1U << 0U)

/* Instruction encodings for accessing the coprocessor interface */
#define MCR 0xee000010U
#define MRC 0xee100010U
#define CPREG(coproc, opc1, rt, crn, crm, opc2) \
	(((opc1) << 21U) | ((crn) << 16U) | ((rt) << 12U) | ((coproc) << 8U) | ((opc2) << 5U) | (crm))

/* Debug registers CP14 */
#define DBGDTRRXint CPREG(14U, 0U, 0U, 0U, 5U, 0U)
#define DBGDTRTXint CPREG(14U, 0U, 0U, 0U, 5U, 0U)

/* Address translation registers CP15 */
#define PAR     CPREG(15U, 0U, 0U, 7U, 4U, 0U)
#define ATS1CPR CPREG(15U, 0U, 0U, 7U, 8U, 0U)

/* Cache management registers CP15 */
#define ICIALLU  CPREG(15U, 0U, 0U, 7U, 5U, 0U)
#define DCCIMVAC CPREG(15U, 0U, 0U, 7U, 14U, 1U)
#define DCCMVAC  CPREG(15U, 0U, 0U, 7U, 10U, 1U)

/* Thumb mode bit in CPSR */
#define CPSR_THUMB (1U << 5U)

/**
 * Fields for Cortex-A special purpose registers, used in the generation of GDB's target description XML.
 * The general purpose registers r0-r12 and the vector floating point registers d0-d15 all follow a very
 * regular format, so we only need to store fields for the special purpose registers.
 * The arrays for each SPR field have the same order as each other, making each of them as pseduo
 * 'associative array'.
 */

// Strings for the names of the Cortex-A's special purpose registers.
static const char *cortex_a_spr_names[] = {"sp", "lr", "pc", "cpsr"};

// The "type" field for each Cortex-A special purpose register.
static const gdb_reg_type_e cortex_a_spr_types[] = {
	GDB_TYPE_DATA_PTR,   // sp
	GDB_TYPE_CODE_PTR,   // lr
	GDB_TYPE_CODE_PTR,   // pc
	GDB_TYPE_UNSPECIFIED // cpsr
};

// clang-format off
static_assert(ARRAY_LENGTH(cortex_a_spr_types) == ARRAY_LENGTH(cortex_a_spr_names),
	"SPR array length mixmatch! SPR type array should have the same length as SPR name array."
);

// clang-format on

// Creates the target description XML string for a Cortex-A. Like snprintf(), this function
// will write no more than max_len and returns the amount of bytes written. Or, if max_len is 0,
// then this function will return the amount of bytes that _would_ be necessary to create this
// string.
//
// This function is hand-optimized to decrease string duplication and thus code size, making it
// Unfortunately much less readable than the string literal it is equivalent to.
//
// The string it creates is XML-equivalent to the following:
/*
	"<?xml version=\"1.0\"?>"
	"<!DOCTYPE feature SYSTEM \"gdb-target.dtd\">"
	"<target>"
	"  <architecture>arm</architecture>"
	"  <feature name=\"org.gnu.gdb.arm.core\">"
	"    <reg name=\"r0\" bitsize=\"32\"/>"
	"    <reg name=\"r1\" bitsize=\"32\"/>"
	"    <reg name=\"r2\" bitsize=\"32\"/>"
	"    <reg name=\"r3\" bitsize=\"32\"/>"
	"    <reg name=\"r4\" bitsize=\"32\"/>"
	"    <reg name=\"r5\" bitsize=\"32\"/>"
	"    <reg name=\"r6\" bitsize=\"32\"/>"
	"    <reg name=\"r7\" bitsize=\"32\"/>"
	"    <reg name=\"r8\" bitsize=\"32\"/>"
	"    <reg name=\"r9\" bitsize=\"32\"/>"
	"    <reg name=\"r10\" bitsize=\"32\"/>"
	"    <reg name=\"r11\" bitsize=\"32\"/>"
	"    <reg name=\"r12\" bitsize=\"32\"/>"
	"    <reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\"/>"
	"    <reg name=\"lr\" bitsize=\"32\" type=\"code_ptr\"/>"
	"    <reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>"
	"    <reg name=\"cpsr\" bitsize=\"32\"/>"
	"  </feature>"
	"  <feature name=\"org.gnu.gdb.arm.vfp\">"
	"    <reg name=\"fpscr\" bitsize=\"32\"/>"
	"    <reg name=\"d0\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d1\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d2\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d3\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d4\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d5\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d6\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d7\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d8\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d9\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d10\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d11\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d12\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d13\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d14\" bitsize=\"64\" type=\"float\"/>"
	"    <reg name=\"d15\" bitsize=\"64\" type=\"float\"/>"
	"  </feature>"
	"</target>";
*/
// Returns the amount of characters written to the buffer.
static size_t create_tdesc_cortex_a(char *buffer, size_t max_len)
{
	// Minor hack: technically snprintf returns an int for possibility of error, but in this case
	// these functions are given static input that should not be able to fail -- and if it does,
	// then there's nothing we can do about it, so we'll repatedly cast this variable to a size_t
	// when calculating printsz (see below).
	int total = 0;

	// We can't just repeatedly pass max_len to snprintf, because we keep changing the start
	// of buffer (effectively changing its size), so we have to repeatedly compute the size
	// passed to snprintf by subtracting the current total from max_len.
	// ...Unless max_len is 0, in which case that subtraction will result in an (underflowed)
	// negative number. So we also have to repeatedly check if max_len is 0 before performing
	// that subtraction.
	size_t printsz = max_len;

	// Start with the "preamble", which is generic across ARM targets,
	// ...save for one word, so we'll have to do the preamble in halves, and then we'll
	// follow it with the GDB ARM Core feature tag.
	total += snprintf(buffer, printsz, "%s feature %sarm%s <feature name=\"org.gnu.gdb.arm.core\">",
		gdb_xml_preamble_first, gdb_xml_preamble_second, gdb_xml_preamble_third);

	// Then the general purpose registers, which have names of r0 to r12.
	for (uint8_t i = 0; i <= 12; ++i) {
		if (max_len != 0)
			printsz = max_len - (size_t)total;

		total += snprintf(buffer + total, printsz, "<reg name=\"r%u\" bitsize=\"32\"/>", i);
	}

	// The special purpose registers are a slightly more complicated.
	// Some of them have different types specified, however unlike the Cortex-M SPRs,
	// all of the Cortex-A target description SPRs have the same bitsize, and none of them
	// have a specified save-restore value. So we only need one "associative array" here.
	// NOTE: unlike the other loops, this loop uses a size_t for its counter, as it's used to index into arrays.
	for (size_t i = 0; i < ARRAY_LENGTH(cortex_a_spr_names); ++i) {
		gdb_reg_type_e type = cortex_a_spr_types[i];

		if (max_len != 0)
			printsz = max_len - (size_t)total;

		total += snprintf(buffer + total, printsz, "<reg name=\"%s\" bitsize=\"32\"%s/>", cortex_a_spr_names[i],
			gdb_reg_type_strings[type]);
	}

	if (max_len != 0)
		printsz = max_len - (size_t)total;

	// Now onto the floating point registers.
	// The first register is unique; the rest all follow the same format.
	total += snprintf(buffer + total, printsz,
		"</feature>"
		"<feature name=\"org.gnu.gdb.arm.vfp\">"
		"<reg name=\"fpscr\" bitsize=\"32\"/>");

	// Now onto the simple ones.
	for (uint8_t i = 0; i <= 15; ++i) {
		if (max_len != 0)
			printsz = max_len - (size_t)total;

		total += snprintf(buffer + total, printsz, "<reg name=\"d%u\" bitsize=\"64\" type=\"float\"/>", i);
	}

	if (max_len != 0)
		printsz = max_len - (size_t)total;

	total += snprintf(buffer + total, printsz, "</feature></target>");

	// Minor hack: technically snprintf returns an int for possibility of error, but in this case
	// these functions are given static input that should not ever be able to fail -- and if it
	// does, then there's nothing we can do about it, so we'll just discard the signedness
	// of total when we return it.
	return (size_t)total;
}

#ifdef ENABLE_DEBUG
typedef struct bitfield_entry {
	char *desc;
	uint8_t bitnum;
} bitfields_lut_s;

static const bitfields_lut_s cortexa_dbg_dscr_lut[] = {
	{"HALTED", 0U},
	{"RESTARTED", 1U},
	{"SDABORT_l", 6U},
	{"ADABORT_l", 7U},
	{"UND_l", 8U},
	{"FS", 9U},
	{"ITRen", 13U},
	{"HDBGen", 14U},
	{"MDBGen", 15U},
	{"InstrCompl_l", 24U},
	{"PipeAdv", 25U},
	{"TXfull_l", 26U},
	{"RXfull_l", 27U},
	{"TXfull", 29U},
	{"RXfull", 30U},
};

static void helper_print_bitfields(const uint32_t val, const bitfields_lut_s *lut, const size_t array_length)
{
	for (size_t i = 0; i < array_length; i++) {
		if (val & (1U << lut[i].bitnum))
			DEBUG_TARGET("%s ", lut[i].desc);
	}
}

static void cortexa_decode_bitfields(const uint32_t reg, const uint32_t val)
{
	DEBUG_TARGET("Bits set in reg ");
	switch (reg) {
	case CORTEXAR_DBG_DSCR:
		DEBUG_TARGET("DBGDSCR: ");
		helper_print_bitfields(val, cortexa_dbg_dscr_lut, ARRAY_LENGTH(cortexa_dbg_dscr_lut));
		break;
	default:
		DEBUG_TARGET("unknown reg");
		break;
	}

	DEBUG_TARGET("\n");
}
#else
static void cortexa_decode_bitfields(const uint32_t reg, const uint32_t val)
{
	(void)reg;
	(void)val;
}
#endif

static void cortexar_run_insn(target_s *const target, const uint32_t insn)
{
	/* Issue the requested instruction to the core */
	cortex_dbg_write32(target, CORTEXAR_DBG_ITR, insn);
	/* Poll for the instruction to complete */
	while (!(cortex_dbg_read32(target, CORTEXAR_DBG_DSCR) & CORTEXAR_DBG_DSCR_INSN_COMPLETE))
		continue;
}

static uint32_t cortexar_run_read_insn(target_s *const target, const uint32_t insn)
{
	/* Issue the requested instruction to the core */
	cortex_dbg_write32(target, CORTEXAR_DBG_ITR, insn);
	/* Poll for the instruction to complete and the data to become ready in the DTR */
	while ((cortex_dbg_read32(target, CORTEXAR_DBG_DSCR) &
			   (CORTEXAR_DBG_DSCR_INSN_COMPLETE | CORTEXAR_DBG_DSCR_DTR_READ_READY)) !=
		(CORTEXAR_DBG_DSCR_INSN_COMPLETE | CORTEXAR_DBG_DSCR_DTR_READ_READY))
		continue;
	/* Read back the DTR to complete the read */
	return cortex_dbg_read32(target, CORTEXAR_DBG_DTRRX);
}

static void cortexar_run_write_insn(target_s *const target, const uint32_t insn, const uint32_t data)
{
	/* Set up the data in the DTR for the transaction */
	cortex_dbg_write32(target, CORTEXAR_DBG_DTRTX, data);
	/* Poll for the data to become ready in the DTR */
	while (!(cortex_dbg_read32(target, CORTEXAR_DBG_DSCR) & CORTEXAR_DBG_DSCR_DTR_WRITE_DONE))
		continue;
	/* Issue the requested instruction to the core */
	cortex_dbg_write32(target, CORTEXAR_DBG_ITR, insn);
	/* Poll for the instruction to complete and the data to be consumed from the DTR */
	while ((cortex_dbg_read32(target, CORTEXAR_DBG_DSCR) &
			   (CORTEXAR_DBG_DSCR_INSN_COMPLETE | CORTEXAR_DBG_DSCR_DTR_WRITE_DONE)) != CORTEXAR_DBG_DSCR_INSN_COMPLETE)
		continue;
}

static uint32_t va_to_pa(target_s *t, uint32_t va)
{
	cortexa_priv_s *priv = t->priv;
	write_gpreg(t, 0, va);
	cortexar_run_insn(t, MCR | ATS1CPR);
	cortexar_run_insn(t, MRC | PAR);
	uint32_t par = read_gpreg(t, 0);
	if (par & 1U)
		priv->mmu_fault = true;
	uint32_t pa = (par & ~0xfffU) | (va & 0xfffU);
	DEBUG_INFO("%s: VA = 0x%08" PRIx32 ", PAR = 0x%08" PRIx32 ", PA = 0x%08" PRIX32 "\n", __func__, va, par, pa);
	return pa;
}

static void cortexa_slow_mem_read(target_s *t, void *dest, target_addr_t src, size_t len)
{
	cortexa_priv_s *priv = t->priv;
	unsigned words = (len + (src & 3U) + 3U) / 4U;
	uint32_t dest32[words];

	/* Set r0 to aligned src address */
	write_gpreg(t, 0, src & ~3);

	/* Switch to fast DCC mode */
	uint32_t dbgdscr = cortex_dbg_read32(t, CORTEXAR_DBG_DSCR);
	dbgdscr = (dbgdscr & ~DBGDSCR_EXTDCCMODE_MASK) | DBGDSCR_EXTDCCMODE_FAST;
	cortex_dbg_write32(t, CORTEXAR_DBG_DSCR, dbgdscr);

	cortex_dbg_write32(t, CORTEXAR_DBG_ITR, 0xecb05e01); /* ldc 14, cr5, [r0], #4 */
	/*
	 * According to the ARMv7-AR ARM, in fast mode, the first read from
	 * DBGDTRTXext (CORTEXAR_DBG_DTRRX) is supposed to block until the instruction
	 * is complete, but we see the first read returns junk, so it's read here and ignored.
	 */
	cortex_dbg_read32(t, CORTEXAR_DBG_DTRRX);

	for (unsigned i = 0; i < words; i++)
		dest32[i] = cortex_dbg_read32(t, CORTEXAR_DBG_DTRRX);

	memcpy(dest, (uint8_t *)dest32 + (src & 3U), len);

	/* Switch back to non-blocking DCC mode */
	dbgdscr = (dbgdscr & ~DBGDSCR_EXTDCCMODE_MASK);
	cortex_dbg_write32(t, CORTEXAR_DBG_DSCR, dbgdscr);

	if (cortex_dbg_read32(t, CORTEXAR_DBG_DSCR) & DBGDSCR_SDABORT_L) {
		/* Memory access aborted, flag a fault */
		cortex_dbg_write32(t, CORTEXAR_DBG_DRCR, DBGDRCR_CSE);
		priv->mmu_fault = true;
	} else {
		cortex_dbg_read32(t, CORTEXAR_DBG_DTRRX);
	}
}

static void cortexa_slow_mem_write_bytes(target_s *t, target_addr_t dest, const uint8_t *src, size_t len)
{
	cortexa_priv_s *priv = t->priv;

	/* Set r13 to dest address */
	write_gpreg(t, 13, dest);

	while (len--) {
		write_gpreg(t, 0, *src++);
		cortex_dbg_write32(t, CORTEXAR_DBG_ITR, 0xe4cd0001); /* strb r0, [sp], #1 */
		if (cortex_dbg_read32(t, CORTEXAR_DBG_DSCR) & DBGDSCR_SDABORT_L) {
			/* Memory access aborted, flag a fault */
			cortex_dbg_write32(t, CORTEXAR_DBG_DRCR, DBGDRCR_CSE);
			priv->mmu_fault = true;
			return;
		}
	}
}

static void cortexa_slow_mem_write(target_s *t, target_addr_t dest, const void *src, size_t len)
{
	cortexa_priv_s *priv = t->priv;
	if (len == 0)
		return;

	if ((dest & 3U) || (len & 3U)) {
		cortexa_slow_mem_write_bytes(t, dest, src, len);
		return;
	}

	write_gpreg(t, 0, dest);
	const uint32_t *src32 = src;

	/* Switch to fast DCC mode */
	uint32_t dbgdscr = cortex_dbg_read32(t, CORTEXAR_DBG_DSCR);
	dbgdscr = (dbgdscr & ~DBGDSCR_EXTDCCMODE_MASK) | DBGDSCR_EXTDCCMODE_FAST;
	cortex_dbg_write32(t, CORTEXAR_DBG_DSCR, dbgdscr);

	cortex_dbg_write32(t, CORTEXAR_DBG_ITR, 0xeca05e01); /* stc 14, cr5, [r0], #4 */

	for (; len; len -= 4U)
		cortex_dbg_write32(t, CORTEXAR_DBG_DTRTX, *src32++);

	/* Switch back to non-blocking DCC mode */
	dbgdscr &= ~DBGDSCR_EXTDCCMODE_MASK;
	cortex_dbg_write32(t, CORTEXAR_DBG_DSCR, dbgdscr);

	if (cortex_dbg_read32(t, CORTEXAR_DBG_DSCR) & DBGDSCR_SDABORT_L) {
		/* Memory access aborted, flag a fault */
		cortex_dbg_write32(t, CORTEXAR_DBG_DRCR, DBGDRCR_CSE);
		priv->mmu_fault = true;
	}
}

static bool cortexa_check_error(target_s *target)
{
	cortexa_priv_s *priv = target->priv;
	bool err = priv->mmu_fault;
	priv->mmu_fault = false;
	return err || cortex_check_error(target);
}

const char *cortexa_regs_description(target_s *t)
{
	(void)t;
	const size_t description_length = create_tdesc_cortex_a(NULL, 0) + 1U;
	char *const description = malloc(description_length);
	if (description)
		create_tdesc_cortex_a(description, description_length);
	return description;
}

static void cortexa_oslock_unlock(target_s *target)
{
	uint32_t dbg_osreg = cortex_dbg_read32(target, CORTEXAR_DBG_OSLSR);
	DEBUG_INFO("%s: DBGOSLSR = 0x%08" PRIx32 "\n", __func__, dbg_osreg);
	/* Is OS Lock implemented? */
	if ((dbg_osreg & CORTEXAR_DBG_OSLSR_OSLM) == CORTEXAR_DBG_OSLSR_OSLM0 ||
		(dbg_osreg & CORTEXAR_DBG_OSLSR_OSLM) == CORTEXAR_DBG_OSLSR_OSLM1) {
		/* Is OS Lock set? */
		if (dbg_osreg & CORTEXAR_DBG_OSLSR_OSLK) {
			DEBUG_WARN("%s: OSLock set! Trying to unlock\n", __func__);
			cortex_dbg_write32(target, CORTEXAR_DBG_OSLAR, 0U);
			dbg_osreg = cortex_dbg_read32(target, CORTEXAR_DBG_OSLSR);

			if ((dbg_osreg & CORTEXAR_DBG_OSLSR_OSLK) != 0)
				DEBUG_ERROR("%s: OSLock sticky, core not powered?\n", __func__);
		}
	}
}

bool cortexa_probe(adiv5_access_port_s *ap, target_addr_t base_address)
{
	target_s *const target = target_new();
	if (!target)
		return false;

	adiv5_ap_ref(ap);
	cortexa_priv_s *const priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}

	target->priv = priv;
	target->priv_free = cortex_priv_free;
	priv->base.ap = ap;
	priv->base.base_addr = base_address;

	target->mem_read = cortexa_slow_mem_read;
	target->mem_write = cortexa_slow_mem_write;
	target->check_error = cortexa_check_error;

	target->driver = "ARM Cortex-A";

	target->halt_request = cortexa_halt_request;
	target->halt_poll = cortexa_halt_poll;
	target->halt_resume = cortexa_halt_resume;

#if 0
	/* Reset 0xc5acce55 lock access to deter software */
	cortex_dbg_write32(target, CORTEXAR_DBG_LAR, 0U);
	/* Cache write-through */
	cortex_dbg_write32(target, CORTEXAR_DBG_DSCCR, 0U);
	/* Disable TLB lookup and refill/eviction */
	cortex_dbg_write32(target, CORTEXAR_DBG_DSMCR, 0U);
#endif

	/*
	 * Clear the OSLock if set prior to halting the core - trying to do this after target_halt_request()
	 * does not function over JTAG and triggers the lock sticky message.
	 */
	cortexa_oslock_unlock(target);

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

	target->attach = cortexa_attach;
	target->detach = cortexa_detach;

	target->regs_description = cortexa_regs_description;
	target->regs_read = cortexa_regs_read;
	target->regs_write = cortexa_regs_write;
	target->reg_read = cortexa_reg_read;
	target->reg_write = cortexa_reg_write;

	target->reset = cortexa_reset;
	target->regs_size = sizeof(uint32_t) * (CORTEXAR_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT);
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
			priv->base.icache_line_length << 2U, priv->base.dcache_line_length << 2U);
	} else
		target_check_error(target);

	target->breakwatch_set = cortexa_breakwatch_set;
	target->breakwatch_clear = cortexa_breakwatch_clear;

	return true;
}

bool cortexa_attach(target_s *target)
{
	cortexa_priv_s *priv = target->priv;

	/* Clear any pending fault condition */
	target_check_error(target);

	/* Make sure the OSLock is cleared prior to halting the core in case it got re-set between probe and attach. */
	cortexa_oslock_unlock(target);
	target_halt_request(target);
	size_t tries = 10;
	while (!platform_nrst_get_val() && !target_halt_poll(target, NULL) && --tries)
		platform_delay(200);
	if (!tries)
		return false;

	/* Clear any stale breakpoints */
	priv->base.breakpoints_mask = 0U;
	for (size_t i = 0; i <= priv->base.breakpoints_available; ++i) {
		cortex_dbg_write32(target, CORTEXAR_DBG_BVR + (i << 2U), 0U);
		cortex_dbg_write32(target, CORTEXAR_DBG_BCR + (i << 2U), 0U);
	}
	priv->bcr0 = 0;

	platform_nrst_set_val(false);

	return true;
}

void cortexa_detach(target_s *target)
{
	cortexa_priv_s *priv = target->priv;

	/* Clear any stale breakpoints */
	for (size_t i = 0; i <= priv->base.breakpoints_available; ++i) {
		cortex_dbg_write32(target, CORTEXAR_DBG_BVR + (i << 2U), 0U);
		cortex_dbg_write32(target, CORTEXAR_DBG_BCR + (i << 2U), 0U);
	}

	/* Restore any clobbered registers */
	cortexa_regs_write_internal(target);
	/* Invalidate cache */
	cortexar_run_insn(target, MCR | ICIALLU);

	/* Disable halting debug mode */
	uint32_t dbgdscr = cortex_dbg_read32(target, CORTEXAR_DBG_DSCR);
	dbgdscr &= ~(CORTEXAR_DBG_DSCR_HALT_DBG_ENABLE | CORTEXAR_DBG_DSCR_ITR_ENABLE);
	cortex_dbg_write32(target, CORTEXAR_DBG_DSCR, dbgdscr);
	/* Clear sticky error and resume */
	cortex_dbg_write32(target, CORTEXAR_DBG_DRCR, DBGDRCR_CSE | DBGDRCR_RRQ);
}

static inline uint32_t read_gpreg(target_s *t, uint8_t regno)
{
	return cortexar_run_read_insn(t, MCR | DBGDTRTXint | ((regno & 0xfU) << 12U));
}

static inline void write_gpreg(target_s *t, uint8_t regno, uint32_t val)
{
	cortexar_run_write_insn(t, MRC | DBGDTRRXint | ((regno & 0xfU) << 12U), val);
}

static void cortexa_regs_read(target_s *t, void *data)
{
	cortexa_priv_s *priv = (cortexa_priv_s *)t->priv;
	memcpy(data, &priv->reg_cache, t->regs_size);
}

static void cortexa_regs_write(target_s *t, const void *data)
{
	cortexa_priv_s *priv = (cortexa_priv_s *)t->priv;
	memcpy(&priv->reg_cache, data, t->regs_size);
}

static ssize_t ptr_for_reg(target_s *t, uint32_t reg, void **r)
{
	cortexa_priv_s *priv = (cortexa_priv_s *)t->priv;
	if (reg <= 15U) { /* 0 .. 15 */
		*r = &priv->reg_cache.r[reg];
		return 4U;
	} else if (reg == 16U) { /* 16 */
		*r = &priv->reg_cache.cpsr;
		return 4U;
	} else if (reg == 17U) { /* 17 */
		*r = &priv->reg_cache.fpscr;
		return 4U;
	} else if (reg <= 33U) { /* 18 .. 33 */
		*r = &priv->reg_cache.d[reg - 18U];
		return 8U;
	}
	return -1;
}

static ssize_t cortexa_reg_read(target_s *t, uint32_t reg, void *data, size_t max)
{
	void *r = NULL;
	size_t s = ptr_for_reg(t, reg, &r);
	if (s > max)
		return -1;
	memcpy(data, r, s);
	return s;
}

static ssize_t cortexa_reg_write(target_s *t, uint32_t reg, const void *data, size_t max)
{
	void *r = NULL;
	size_t s = ptr_for_reg(t, reg, &r);
	if (s > max)
		return -1;
	memcpy(r, data, s);
	return s;
}

static void cortexa_regs_read_internal(target_s *t)
{
	cortexa_priv_s *priv = (cortexa_priv_s *)t->priv;
	/* Read general purpose registers */
	for (size_t i = 0; i < 15U; i++)
		priv->reg_cache.r[i] = read_gpreg(t, i);

	/* Read PC, via r0.  MCR is UNPREDICTABLE for Rt = r15. */
	cortexar_run_insn(t, 0xe1a0000f); /* mov r0, pc */
	priv->reg_cache.r[15] = read_gpreg(t, 0);
	/* Read CPSR */
	cortexar_run_insn(t, 0xe10f0000); /* mrs r0, CPSR */
	priv->reg_cache.cpsr = read_gpreg(t, 0);
	/* Read FPSCR */
	cortexar_run_insn(t, 0xeef10a10); /* vmrs r0, fpscr */
	priv->reg_cache.fpscr = read_gpreg(t, 0);
	/* Read out VFP registers */
	for (size_t i = 0; i < 16U; i++) {
		/* Read D[i] to R0/R1 */
		cortexar_run_insn(t, 0xec510b10 | i); /* vmov r0, r1, d0 */
		priv->reg_cache.d[i] = ((uint64_t)read_gpreg(t, 1) << 32U) | read_gpreg(t, 0);
	}
	priv->reg_cache.r[15] -= (priv->reg_cache.cpsr & CPSR_THUMB) ? 4 : 8;
}

static void cortexa_regs_write_internal(target_s *t)
{
	cortexa_priv_s *priv = (cortexa_priv_s *)t->priv;
	/* First write back floats */
	for (size_t i = 0; i < 16U; i++) {
		write_gpreg(t, 1, priv->reg_cache.d[i] >> 32U);
		write_gpreg(t, 0, priv->reg_cache.d[i]);
		cortexar_run_insn(t, 0xec410b10U | i); /* vmov d[i], r0, r1 */
	}
	/* Write back FPSCR */
	write_gpreg(t, 0, priv->reg_cache.fpscr);
	cortexar_run_insn(t, 0xeee10a10); /* vmsr fpscr, r0 */
	/* Write back the CPSR */
	write_gpreg(t, 0, priv->reg_cache.cpsr);
	cortexar_run_insn(t, 0xe12ff000); /* msr CPSR_fsxc, r0 */
	/* Write back PC, via r0.  MRC clobbers CPSR instead */
	write_gpreg(t, 0, priv->reg_cache.r[15] | ((priv->reg_cache.cpsr & CPSR_THUMB) ? 1 : 0));
	cortexar_run_insn(t, 0xe1a0f000); /* mov pc, r0 */
	/* Finally the GP registers now that we're done using them */
	for (size_t i = 0; i < 15U; i++)
		write_gpreg(t, i, priv->reg_cache.r[i]);
}

static void cortexa_reset(target_s *target)
{
	/* This mess is Xilinx Zynq specific
	 * See Zynq-7000 TRM, Xilinx doc UG585
	 */
#define ZYNQ_SLCR_UNLOCK       0xf8000008U
#define ZYNQ_SLCR_UNLOCK_KEY   0xdf0dU
#define ZYNQ_SLCR_PSS_RST_CTRL 0xf8000200U
	target_mem_write32(target, ZYNQ_SLCR_UNLOCK, ZYNQ_SLCR_UNLOCK_KEY);
	target_mem_write32(target, ZYNQ_SLCR_PSS_RST_CTRL, 1);

	/* Try hard reset too */
	platform_nrst_set_val(true);
	platform_nrst_set_val(false);

	/* Spin until Xilinx reconnects us */
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 1000);
	volatile exception_s e;
	do {
		TRY_CATCH (e, EXCEPTION_ALL) {
			cortex_dbg_read32(target, CORTEXAR_DBG_IDR);
		}
	} while (!platform_timeout_is_expired(&timeout) && e.type == EXCEPTION_ERROR);
	if (e.type == EXCEPTION_ERROR)
		raise_exception(e.type, e.msg);

	platform_delay(100);

	cortexa_attach(target);
}

static void cortexa_halt_request(target_s *t)
{
	volatile exception_s e;
	TRY_CATCH (e, EXCEPTION_TIMEOUT) {
		cortex_dbg_write32(t, CORTEXAR_DBG_DRCR, DBGDRCR_HRQ);
	}
	if (e.type) {
		tc_printf(t, "Timeout sending interrupt, is target in WFI?\n");
	}
}

static target_halt_reason_e cortexa_halt_poll(target_s *t, target_addr_t *watch)
{
	volatile uint32_t dbgdscr = 0;
	volatile exception_s e;
	TRY_CATCH (e, EXCEPTION_ALL) {
		/* If this times out because the target is in WFI then
		 * the target is still running. */
		dbgdscr = cortex_dbg_read32(t, CORTEXAR_DBG_DSCR);
	}
	switch (e.type) {
	case EXCEPTION_ERROR:
		/* Oh crap, there's no recovery from this... */
		target_list_free();
		return TARGET_HALT_ERROR;
	case EXCEPTION_TIMEOUT:
		/* Timeout isn't a problem, target could be in WFI */
		return TARGET_HALT_RUNNING;
	}

	if (!(dbgdscr & CORTEXAR_DBG_DSCR_HALTED)) /* Not halted */
		return TARGET_HALT_RUNNING;

	cortexa_oslock_unlock(t);

	DEBUG_INFO("%s: DBGDSCR = 0x%08" PRIx32 "\n", __func__, dbgdscr);
	cortexa_decode_bitfields(CORTEXAR_DBG_DSCR, dbgdscr);

	/* Enable halting debug mode */
	dbgdscr |= CORTEXAR_DBG_DSCR_HALT_DBG_ENABLE | CORTEXAR_DBG_DSCR_ITR_ENABLE;
	dbgdscr &= ~DBGDSCR_EXTDCCMODE_MASK;
	cortex_dbg_write32(t, CORTEXAR_DBG_DSCR, dbgdscr);

	dbgdscr = cortex_dbg_read32(t, CORTEXAR_DBG_DSCR);
	DEBUG_INFO("%s: DBGDSCR = 0x%08" PRIx32 "\n", __func__, dbgdscr);
	cortexa_decode_bitfields(CORTEXAR_DBG_DSCR, dbgdscr);

	/* Find out why we halted */
	target_halt_reason_e reason = TARGET_HALT_BREAKPOINT;
	switch (dbgdscr & CORTEXAR_DBG_DSCR_MOE_MASK) {
	case CORTEXAR_DBG_DSCR_MOE_HALT_REQUEST:
		reason = TARGET_HALT_REQUEST;
		break;
	case CORTEXAR_DBG_DSCR_MOE_ASYNC_WATCH:
	case CORTEXAR_DBG_DSCR_MOE_SYNC_WATCH:
		/* How do we know which watchpoint was hit? */
		/* If there is only one set, it's that */
		for (breakwatch_s *bw = t->bw_list; bw; bw = bw->next) {
			if ((bw->type != TARGET_WATCH_READ) && (bw->type != TARGET_WATCH_WRITE) &&
				(bw->type != TARGET_WATCH_ACCESS))
				continue;
			if (reason == TARGET_HALT_WATCHPOINT) {
				/* More than one watchpoint set,
				 * we can't tell which triggered. */
				reason = TARGET_HALT_BREAKPOINT;
				break;
			}
			*watch = bw->addr;
			reason = TARGET_HALT_WATCHPOINT;
		}
		break;
	default:
		reason = TARGET_HALT_BREAKPOINT;
	}

	cortexa_regs_read_internal(t);

	return reason;
}

void cortexa_halt_resume(target_s *t, bool step)
{
	cortexa_priv_s *priv = t->priv;
	/* Set breakpoint comparator for single stepping if needed */
	if (step) {
		uint32_t addr = priv->reg_cache.r[15];
		uint32_t bas = bp_bas(addr, (priv->reg_cache.cpsr & CPSR_THUMB) ? 2 : 4);
		DEBUG_INFO("step 0x%08" PRIx32 "  %" PRIx32 "\n", addr, bas);
		/* Set match any breakpoint */
		cortex_dbg_write32(t, CORTEXAR_DBG_BVR + 0, priv->reg_cache.r[15] & ~3);
		cortex_dbg_write32(t, CORTEXAR_DBG_BCR + 0, DBGBCR_INST_MISMATCH | bas | DBGBCR_PMC_ANY | DBGBCR_EN);
	} else {
		cortex_dbg_write32(t, CORTEXAR_DBG_BVR + 0, priv->bvr0);
		cortex_dbg_write32(t, CORTEXAR_DBG_BCR + 0, priv->bcr0);
	}

	/* Write back register cache */
	cortexa_regs_write_internal(t);

	cortexar_run_insn(t, MCR | ICIALLU); /* invalidate cache */

	/* Disable DBGITR.  Not sure why, but RRQ is ignored otherwise. */
	uint32_t dbgdscr = cortex_dbg_read32(t, CORTEXAR_DBG_DSCR);
	if (step)
		dbgdscr |= DBGDSCR_INTDIS;
	else
		dbgdscr &= ~(DBGDSCR_INTDIS | CORTEXAR_DBG_DSCR_HALT_DBG_ENABLE);
	dbgdscr &= ~CORTEXAR_DBG_DSCR_ITR_ENABLE;
	cortex_dbg_write32(t, CORTEXAR_DBG_DSCR, dbgdscr);

	platform_timeout_s to;
	platform_timeout_set(&to, 200);
	do {
		cortex_dbg_write32(t, CORTEXAR_DBG_DRCR, DBGDRCR_CSE | DBGDRCR_RRQ);
		dbgdscr = cortex_dbg_read32(t, CORTEXAR_DBG_DSCR);
		DEBUG_INFO("%s: DBGDSCR = 0x%08" PRIx32 "\n", __func__, dbgdscr);
	} while (!(dbgdscr & CORTEXAR_DBG_DSCR_RESTARTED) && !platform_timeout_is_expired(&to));
}

/* Breakpoints */
static uint32_t bp_bas(uint32_t addr, uint8_t len)
{
	if (len == 4U)
		return DBGBCR_BAS_ANY;
	if (addr & 2U)
		return DBGBCR_BAS_HIGH_HW;
	return DBGBCR_BAS_LOW_HW;
}

static int cortexa_breakwatch_set(target_s *t, breakwatch_s *bw)
{
	cortexa_priv_s *priv = t->priv;
	uint32_t i;

	switch (bw->type) {
	case TARGET_BREAK_SOFT:
		switch (bw->size) {
		case 2:
			bw->reserved[0] = target_mem_read16(t, bw->addr);
			target_mem_write16(t, bw->addr, 0xbe00);
			return target_check_error(t);
		case 4:
			bw->reserved[0] = target_mem_read32(t, bw->addr);
			target_mem_write32(t, bw->addr, 0xe1200070);
			return target_check_error(t);
		default:
			return -1;
		}
	case TARGET_BREAK_HARD:
		if ((bw->size != 4) && (bw->size != 2))
			return -1;

		/* Find the first available breakpoint slot */
		for (i = 0; i < priv->base.breakpoints_available; i++) {
			if (!(priv->base.breakpoints_mask & (1U << i)))
				break;
		}

		if (i == priv->base.breakpoints_available)
			return -1;

		bw->reserved[0] = i;
		priv->base.breakpoints_mask |= 1U << i;

		uint32_t addr = va_to_pa(t, bw->addr);
		uint32_t bcr = bp_bas(addr, bw->size) | DBGBCR_PMC_ANY | DBGBCR_EN;
		cortex_dbg_write32(t, CORTEXAR_DBG_BVR + (i << 2U), addr & ~3);
		cortex_dbg_write32(t, CORTEXAR_DBG_BCR + (i << 2U), bcr);
		if (i == 0) {
			priv->bcr0 = bcr;
			priv->bvr0 = addr & ~3;
		}

		return 0;

	case TARGET_WATCH_WRITE:
	case TARGET_WATCH_READ:
	case TARGET_WATCH_ACCESS:
		/* Find the first available watchpoint slot */
		for (i = 0; i < priv->base.watchpoints_available; i++) {
			if (!(priv->base.watchpoints_mask & (1U << i)))
				break;
		}

		if (i == priv->base.watchpoints_available)
			return -1;

		bw->reserved[0] = i;
		priv->base.watchpoints_mask |= 1U << i;

		{
			uint32_t wcr = DBGWCR_PAC_ANY | DBGWCR_EN;
			uint32_t bas = 0;
			switch (bw->size) { /* Convert bytes size to BAS bits */
			case 1U:
				bas = DBGWCR_BAS_BYTE;
				break;
			case 2U:
				bas = DBGWCR_BAS_HALFWORD;
				break;
			case 4U:
				bas = DBGWCR_BAS_WORD;
				break;
			default:
				return -1;
			}
			/* Apply shift based on address LSBs */
			wcr |= bas << (bw->addr & 3U);

			switch (bw->type) { /* Convert gdb type */
			case TARGET_WATCH_WRITE:
				wcr |= DBGWCR_LSC_STORE;
				break;
			case TARGET_WATCH_READ:
				wcr |= DBGWCR_LSC_LOAD;
				break;
			case TARGET_WATCH_ACCESS:
				wcr |= DBGWCR_LSC_ANY;
				break;
			default:
				return -1;
			}

			cortex_dbg_write32(t, CORTEXAR_DBG_WVR + (i << 2U), wcr);
			cortex_dbg_write32(t, CORTEXAR_DBG_WCR + (i << 2U), bw->addr & ~3U);
			DEBUG_INFO("Watchpoint set WCR = 0x%08" PRIx32 ", WVR = %08" PRIx32 "\n",
				cortex_dbg_read32(t, CORTEXAR_DBG_WVR + (i << 2U)), cortex_dbg_read32(t, CORTEXAR_DBG_WCR + (i << 2U)));
		}
		return 0;

	default:
		return 1;
	}
}

static int cortexa_breakwatch_clear(target_s *t, breakwatch_s *bw)
{
	cortexa_priv_s *priv = t->priv;
	uint32_t i = bw->reserved[0];
	switch (bw->type) {
	case TARGET_BREAK_SOFT:
		switch (bw->size) {
		case 2:
			target_mem_write16(t, bw->addr, i);
			return target_check_error(t);
		case 4:
			target_mem_write32(t, bw->addr, i);
			return target_check_error(t);
		default:
			return -1;
		}
	case TARGET_BREAK_HARD:
		priv->base.breakpoints_mask &= ~(1U << i);
		cortex_dbg_write32(t, CORTEXAR_DBG_BCR + (i << 2U), 0);
		if (i == 0)
			priv->bcr0 = 0;
		return 0;
	case TARGET_WATCH_WRITE:
	case TARGET_WATCH_READ:
	case TARGET_WATCH_ACCESS:
		priv->base.watchpoints_mask &= ~(1U << i);
		cortex_dbg_write32(t, CORTEXAR_DBG_WCR + (i << 2U), 0);
		return 0;
	default:
		return 1;
	}
}
