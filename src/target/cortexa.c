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
#include "gdb_reg.h"
#include "target_internal.h"

#include <stdlib.h>
#include <assert.h>

static const char cortexa_driver_str[] = "ARM Cortex-A";

static bool cortexa_attach(target_s *t);
static void cortexa_detach(target_s *t);
static void cortexa_halt_resume(target_s *t, bool step);

static const char *cortexa_regs_description(target_s *t);
static void cortexa_regs_read(target_s *t, void *data);
static void cortexa_regs_write(target_s *t, const void *data);
static void cortexa_regs_read_internal(target_s *t);
static void cortexa_regs_write_internal(target_s *t);
static ssize_t cortexa_reg_read(target_s *t, int reg, void *data, size_t max);
static ssize_t cortexa_reg_write(target_s *t, int reg, const void *data, size_t max);

static void cortexa_reset(target_s *t);
static target_halt_reason_e cortexa_halt_poll(target_s *t, target_addr_t *watch);
static void cortexa_halt_request(target_s *t);

static int cortexa_breakwatch_set(target_s *t, breakwatch_s *);
static int cortexa_breakwatch_clear(target_s *t, breakwatch_s *);
static uint32_t bp_bas(uint32_t addr, uint8_t len);

static void apb_write(target_s *t, uint16_t reg, uint32_t val);
static uint32_t apb_read(target_s *t, uint16_t reg);
static void write_gpreg(target_s *t, uint8_t regno, uint32_t val);
static uint32_t read_gpreg(target_s *t, uint8_t regno);

typedef struct cortexa_priv {
	uint32_t base;
	adiv5_access_port_s *apb;

	struct {
		uint32_t r[16];
		uint32_t cpsr;
		uint32_t fpscr;
		uint64_t d[16];
	} reg_cache;

	unsigned hw_breakpoint_max;
	uint16_t hw_breakpoint_mask;
	uint32_t bcr0;
	uint32_t bvr0;
	unsigned hw_watchpoint_max;
	uint16_t hw_watchpoint_mask;
	bool mmu_fault;
} cortexa_priv_s;

/* This may be specific to Cortex-A9 */
#define CACHE_LINE_LENGTH (8U * 4U)

/* Debug APB registers */
#define DBGDIDR 0U

#define DBGDTRRX 32U /* DCC: Host to target */
#define DBGITR   33U

#define DBGDSCR                  34U
#define DBGDSCR_TXFULL           (1U << 29U)
#define DBGDSCR_INSTRCOMPL       (1U << 24U)
#define DBGDSCR_EXTDCCMODE_STALL (1U << 20U)
#define DBGDSCR_EXTDCCMODE_FAST  (2U << 20U)
#define DBGDSCR_EXTDCCMODE_MASK  (3U << 20U)
#define DBGDSCR_HDBGEN           (1U << 14U)
#define DBGDSCR_ITREN            (1U << 13U)
#define DBGDSCR_INTDIS           (1U << 11U)
#define DBGDSCR_UND_I            (1U << 8U)
#define DBGDSCR_SDABORT_L        (1U << 6U)
#define DBGDSCR_MOE_MASK         (0xfU << 2U)
#define DBGDSCR_MOE_HALT_REQ     (0x0U << 2U)
#define DBGDSCR_MOE_WATCH_ASYNC  (0x2U << 2U)
#define DBGDSCR_MOE_WATCH_SYNC   (0xaU << 2U)
#define DBGDSCR_RESTARTED        (1U << 1U)
#define DBGDSCR_HALTED           (1U << 0U)

#define DBGDTRTX 35U /* DCC: Target to host */

#define DBGDRCR     36U
#define DBGDRCR_CSE (1U << 2U)
#define DBGDRCR_RRQ (1U << 1U)
#define DBGDRCR_HRQ (1U << 0U)

#define DBGBVR(i)            (64U + (i))
#define DBGBCR(i)            (80U + (i))
#define DBGBCR_INST_MISMATCH (4U << 20U)
#define DBGBCR_BAS_ANY       (0xfU << 5U)
#define DBGBCR_BAS_LOW_HW    (0x3U << 5U)
#define DBGBCR_BAS_HIGH_HW   (0xcU << 5U)
#define DBGBCR_EN            (1U << 0U)
#define DBGBCR_PMC_ANY       (0b11U << 1U)

#define DBGWVR(i)           (96U + (i))
#define DBGWCR(i)           (112U + (i))
#define DBGWCR_LSC_LOAD     (0b01U << 3U)
#define DBGWCR_LSC_STORE    (0b10U << 3U)
#define DBGWCR_LSC_ANY      (0b11U << 3U)
#define DBGWCR_BAS_BYTE     (0b0001U << 5U)
#define DBGWCR_BAS_HALFWORD (0b0011U << 5U)
#define DBGWCR_BAS_WORD     (0b1111U << 5U)
#define DBGWCR_PAC_ANY      (0b11U << 1U)
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

static void apb_write(target_s *t, uint16_t reg, uint32_t val)
{
	cortexa_priv_s *priv = t->priv;
	adiv5_access_port_s *ap = priv->apb;
	uint32_t addr = priv->base + 4U * reg;
	adiv5_ap_write(ap, ADIV5_AP_TAR, addr);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DRW, val);
}

static uint32_t apb_read(target_s *t, uint16_t reg)
{
	cortexa_priv_s *priv = t->priv;
	adiv5_access_port_s *ap = priv->apb;
	uint32_t addr = priv->base + 4U * reg;
	adiv5_ap_write(ap, ADIV5_AP_TAR, addr);
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_AP_DRW, 0);
	return adiv5_dp_low_access(ap->dp, ADIV5_LOW_READ, ADIV5_DP_RDBUFF, 0);
}

static uint32_t va_to_pa(target_s *t, uint32_t va)
{
	cortexa_priv_s *priv = t->priv;
	write_gpreg(t, 0, va);
	apb_write(t, DBGITR, MCR | ATS1CPR);
	apb_write(t, DBGITR, MRC | PAR);
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
	uint32_t dbgdscr = apb_read(t, DBGDSCR);
	dbgdscr = (dbgdscr & ~DBGDSCR_EXTDCCMODE_MASK) | DBGDSCR_EXTDCCMODE_FAST;
	apb_write(t, DBGDSCR, dbgdscr);

	apb_write(t, DBGITR, 0xecb05e01); /* ldc 14, cr5, [r0], #4 */
	/* According to the ARMv7-A ARM, in fast mode, the first read from
	 * DBGDTRTX is  supposed to block until the instruction is complete,
	 * but we see the first read returns junk, so it's read here and
	 * ignored. */
	apb_read(t, DBGDTRTX);

	for (unsigned i = 0; i < words; i++)
		dest32[i] = apb_read(t, DBGDTRTX);

	memcpy(dest, (uint8_t *)dest32 + (src & 3U), len);

	/* Switch back to stalling DCC mode */
	dbgdscr = (dbgdscr & ~DBGDSCR_EXTDCCMODE_MASK) | DBGDSCR_EXTDCCMODE_STALL;
	apb_write(t, DBGDSCR, dbgdscr);

	if (apb_read(t, DBGDSCR) & DBGDSCR_SDABORT_L) {
		/* Memory access aborted, flag a fault */
		apb_write(t, DBGDRCR, DBGDRCR_CSE);
		priv->mmu_fault = true;
	} else {
		apb_read(t, DBGDTRTX);
	}
}

static void cortexa_slow_mem_write_bytes(target_s *t, target_addr_t dest, const uint8_t *src, size_t len)
{
	cortexa_priv_s *priv = t->priv;

	/* Set r13 to dest address */
	write_gpreg(t, 13, dest);

	while (len--) {
		write_gpreg(t, 0, *src++);
		apb_write(t, DBGITR, 0xe4cd0001); /* strb r0, [sp], #1 */
		if (apb_read(t, DBGDSCR) & DBGDSCR_SDABORT_L) {
			/* Memory access aborted, flag a fault */
			apb_write(t, DBGDRCR, DBGDRCR_CSE);
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
	uint32_t dbgdscr = apb_read(t, DBGDSCR);
	dbgdscr = (dbgdscr & ~DBGDSCR_EXTDCCMODE_MASK) | DBGDSCR_EXTDCCMODE_FAST;
	apb_write(t, DBGDSCR, dbgdscr);

	apb_write(t, DBGITR, 0xeca05e01); /* stc 14, cr5, [r0], #4 */

	for (; len; len -= 4U)
		apb_write(t, DBGDTRRX, *src32++);

	/* Switch back to stalling DCC mode */
	dbgdscr = (dbgdscr & ~DBGDSCR_EXTDCCMODE_MASK) | DBGDSCR_EXTDCCMODE_STALL;
	apb_write(t, DBGDSCR, dbgdscr);

	if (apb_read(t, DBGDSCR) & DBGDSCR_SDABORT_L) {
		/* Memory access aborted, flag a fault */
		apb_write(t, DBGDRCR, DBGDRCR_CSE);
		priv->mmu_fault = true;
	}
}

static bool cortexa_check_error(target_s *t)
{
	cortexa_priv_s *priv = t->priv;
	bool err = priv->mmu_fault;
	priv->mmu_fault = false;
	return err;
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

bool cortexa_probe(adiv5_access_port_s *apb, uint32_t debug_base)
{
	target_s *t = target_new();
	if (!t) {
		return false;
	}

	adiv5_ap_ref(apb);
	cortexa_priv_s *priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}

	t->priv = priv;
	t->priv_free = free;
	priv->apb = apb;
	t->mem_read = cortexa_slow_mem_read;
	t->mem_write = cortexa_slow_mem_write;

	priv->base = debug_base;
	/* Set up APB CSW, we won't touch this again */
	uint32_t csw = apb->csw | ADIV5_AP_CSW_SIZE_WORD;
	adiv5_ap_write(apb, ADIV5_AP_CSW, csw);
	uint32_t dbgdidr = apb_read(t, DBGDIDR);
	priv->hw_breakpoint_max = ((dbgdidr >> 24U) & 15U) + 1U;
	priv->hw_watchpoint_max = ((dbgdidr >> 28U) & 15U) + 1U;

	t->check_error = cortexa_check_error;

	t->driver = cortexa_driver_str;

	t->attach = cortexa_attach;
	t->detach = cortexa_detach;

	t->regs_description = cortexa_regs_description;
	t->regs_read = cortexa_regs_read;
	t->regs_write = cortexa_regs_write;
	t->reg_read = cortexa_reg_read;
	t->reg_write = cortexa_reg_write;

	t->reset = cortexa_reset;
	t->halt_request = cortexa_halt_request;
	t->halt_poll = cortexa_halt_poll;
	t->halt_resume = cortexa_halt_resume;
	t->regs_size = sizeof(priv->reg_cache);

	t->breakwatch_set = cortexa_breakwatch_set;
	t->breakwatch_clear = cortexa_breakwatch_clear;

	return true;
}

bool cortexa_attach(target_s *t)
{
	cortexa_priv_s *priv = t->priv;

	/* Clear any pending fault condition */
	target_check_error(t);

	/* Enable halting debug mode */
	uint32_t dbgdscr = apb_read(t, DBGDSCR);
	dbgdscr |= DBGDSCR_HDBGEN | DBGDSCR_ITREN;
	dbgdscr = (dbgdscr & ~DBGDSCR_EXTDCCMODE_MASK) | DBGDSCR_EXTDCCMODE_STALL;
	apb_write(t, DBGDSCR, dbgdscr);
	DEBUG_INFO("DBGDSCR = 0x%08" PRIx32 "\n", dbgdscr);

	target_halt_request(t);
	size_t tries = 10;
	while (!platform_nrst_get_val() && !target_halt_poll(t, NULL) && --tries)
		platform_delay(200);
	if (!tries)
		return false;

	/* Clear any stale breakpoints */
	for (unsigned i = 0; i < priv->hw_breakpoint_max; i++) {
		apb_write(t, DBGBCR(i), 0);
	}
	priv->hw_breakpoint_mask = 0;
	priv->bcr0 = 0;

	platform_nrst_set_val(false);

	return true;
}

void cortexa_detach(target_s *t)
{
	cortexa_priv_s *priv = t->priv;

	/* Clear any stale breakpoints */
	for (size_t i = 0; i < priv->hw_breakpoint_max; i++)
		apb_write(t, DBGBCR(i), 0);

	/* Restore any clobbered registers */
	cortexa_regs_write_internal(t);
	/* Invalidate cache */
	apb_write(t, DBGITR, MCR | ICIALLU);

	platform_timeout_s to;
	platform_timeout_set(&to, 200);

	/* Wait for instruction to complete */
	uint32_t dbgdscr;
	do {
		dbgdscr = apb_read(t, DBGDSCR);
	} while (!(dbgdscr & DBGDSCR_INSTRCOMPL) && !platform_timeout_is_expired(&to));

	/* Disable halting debug mode */
	dbgdscr &= ~(DBGDSCR_HDBGEN | DBGDSCR_ITREN);
	apb_write(t, DBGDSCR, dbgdscr);
	/* Clear sticky error and resume */
	apb_write(t, DBGDRCR, DBGDRCR_CSE | DBGDRCR_RRQ);
}

static uint32_t read_gpreg(target_s *t, uint8_t regno)
{
	/* To read a register we use DBGITR to load an MCR instruction
	 * that sends the value via DCC DBGDTRTX using the CP14 interface.
	 */
	uint32_t instr = MCR | DBGDTRTXint | ((regno & 0xfU) << 12U);
	apb_write(t, DBGITR, instr);
	/* Return value read from DCC channel */
	return apb_read(t, DBGDTRTX);
}

static void write_gpreg(target_s *t, uint8_t regno, uint32_t val)
{
	/* Write value to DCC channel */
	apb_write(t, DBGDTRRX, val);
	/* Run instruction to load register */
	uint32_t instr = MRC | DBGDTRRXint | ((regno & 0xfU) << 12U);
	apb_write(t, DBGITR, instr);
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

static ssize_t ptr_for_reg(target_s *t, int reg, void **r)
{
	cortexa_priv_s *priv = (cortexa_priv_s *)t->priv;
	switch (reg) {
	case 0 ... 15:
		*r = &priv->reg_cache.r[reg];
		return 4;
	case 16:
		*r = &priv->reg_cache.cpsr;
		return 4;
	case 17:
		*r = &priv->reg_cache.fpscr;
		return 4;
	case 18 ... 33:
		*r = &priv->reg_cache.d[reg - 18];
		return 8;
	default:
		return -1;
	}
}

static ssize_t cortexa_reg_read(target_s *t, int reg, void *data, size_t max)
{
	void *r = NULL;
	size_t s = ptr_for_reg(t, reg, &r);
	if (s > max)
		return -1;
	memcpy(data, r, s);
	return s;
}

static ssize_t cortexa_reg_write(target_s *t, int reg, const void *data, size_t max)
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
	apb_write(t, DBGITR, 0xe1a0000f); /* mov r0, pc */
	priv->reg_cache.r[15] = read_gpreg(t, 0);
	/* Read CPSR */
	apb_write(t, DBGITR, 0xe10f0000); /* mrs r0, CPSR */
	priv->reg_cache.cpsr = read_gpreg(t, 0);
	/* Read FPSCR */
	apb_write(t, DBGITR, 0xeef10a10); /* vmrs r0, fpscr */
	priv->reg_cache.fpscr = read_gpreg(t, 0);
	/* Read out VFP registers */
	for (size_t i = 0; i < 16U; i++) {
		/* Read D[i] to R0/R1 */
		apb_write(t, DBGITR, 0xec510b10 | i); /* vmov r0, r1, d0 */
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
		apb_write(t, DBGITR, 0xec410b10U | i); /* vmov d[i], r0, r1 */
	}
	/* Write back FPSCR */
	write_gpreg(t, 0, priv->reg_cache.fpscr);
	apb_write(t, DBGITR, 0xeee10a10); /* vmsr fpscr, r0 */
	/* Write back the CPSR */
	write_gpreg(t, 0, priv->reg_cache.cpsr);
	apb_write(t, DBGITR, 0xe12ff000); /* msr CPSR_fsxc, r0 */
	/* Write back PC, via r0.  MRC clobbers CPSR instead */
	write_gpreg(t, 0, priv->reg_cache.r[15] | ((priv->reg_cache.cpsr & CPSR_THUMB) ? 1 : 0));
	apb_write(t, DBGITR, 0xe1a0f000); /* mov pc, r0 */
	/* Finally the GP registers now that we're done using them */
	for (size_t i = 0; i < 15U; i++)
		write_gpreg(t, i, priv->reg_cache.r[i]);
}

static void cortexa_reset(target_s *t)
{
	/* This mess is Xilinx Zynq specific
	 * See Zynq-7000 TRM, Xilinx doc UG585
	 */
#define ZYNQ_SLCR_UNLOCK       0xf8000008U
#define ZYNQ_SLCR_UNLOCK_KEY   0xdf0dU
#define ZYNQ_SLCR_PSS_RST_CTRL 0xf8000200U
	target_mem_write32(t, ZYNQ_SLCR_UNLOCK, ZYNQ_SLCR_UNLOCK_KEY);
	target_mem_write32(t, ZYNQ_SLCR_PSS_RST_CTRL, 1);

	/* Try hard reset too */
	platform_nrst_set_val(true);
	platform_nrst_set_val(false);

	/* Spin until Xilinx reconnects us */
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 1000);
	volatile exception_s e;
	do {
		TRY_CATCH (e, EXCEPTION_ALL) {
			apb_read(t, DBGDIDR);
		}
	} while (!platform_timeout_is_expired(&timeout) && e.type == EXCEPTION_ERROR);
	if (e.type == EXCEPTION_ERROR)
		raise_exception(e.type, e.msg);

	platform_delay(100);

	cortexa_attach(t);
}

static void cortexa_halt_request(target_s *t)
{
	volatile exception_s e;
	TRY_CATCH (e, EXCEPTION_TIMEOUT) {
		apb_write(t, DBGDRCR, DBGDRCR_HRQ);
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
		dbgdscr = apb_read(t, DBGDSCR);
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

	if (!(dbgdscr & DBGDSCR_HALTED)) /* Not halted */
		return TARGET_HALT_RUNNING;

	DEBUG_INFO("%s: DBGDSCR = 0x%08" PRIx32 "\n", __func__, dbgdscr);
	/* Reenable DBGITR */
	dbgdscr |= DBGDSCR_ITREN;
	apb_write(t, DBGDSCR, dbgdscr);

	/* Find out why we halted */
	target_halt_reason_e reason = TARGET_HALT_BREAKPOINT;
	switch (dbgdscr & DBGDSCR_MOE_MASK) {
	case DBGDSCR_MOE_HALT_REQ:
		reason = TARGET_HALT_REQUEST;
		break;
	case DBGDSCR_MOE_WATCH_ASYNC:
	case DBGDSCR_MOE_WATCH_SYNC:
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
	/* Set breakpoint comarator for single stepping if needed */
	if (step) {
		uint32_t addr = priv->reg_cache.r[15];
		uint32_t bas = bp_bas(addr, (priv->reg_cache.cpsr & CPSR_THUMB) ? 2 : 4);
		DEBUG_INFO("step 0x%08" PRIx32 "  %" PRIx32 "\n", addr, bas);
		/* Set match any breakpoint */
		apb_write(t, DBGBVR(0), priv->reg_cache.r[15] & ~3);
		apb_write(t, DBGBCR(0), DBGBCR_INST_MISMATCH | bas | DBGBCR_PMC_ANY | DBGBCR_EN);
	} else {
		apb_write(t, DBGBVR(0), priv->bvr0);
		apb_write(t, DBGBCR(0), priv->bcr0);
	}

	/* Write back register cache */
	cortexa_regs_write_internal(t);

	apb_write(t, DBGITR, MCR | ICIALLU); /* invalidate cache */

	platform_timeout_s to;
	platform_timeout_set(&to, 200);

	/* Wait for instruction to complete */
	uint32_t dbgdscr;
	do {
		dbgdscr = apb_read(t, DBGDSCR);
	} while (!(dbgdscr & DBGDSCR_INSTRCOMPL) && !platform_timeout_is_expired(&to));

	/* Disable DBGITR.  Not sure why, but RRQ is ignored otherwise. */
	if (step)
		dbgdscr |= DBGDSCR_INTDIS;
	else
		dbgdscr &= ~DBGDSCR_INTDIS;
	dbgdscr &= ~DBGDSCR_ITREN;
	apb_write(t, DBGDSCR, dbgdscr);

	do {
		apb_write(t, DBGDRCR, DBGDRCR_CSE | DBGDRCR_RRQ);
		dbgdscr = apb_read(t, DBGDSCR);
		DEBUG_INFO("%s: DBGDSCR = 0x%08" PRIx32 "\n", __func__, dbgdscr);
	} while (!(dbgdscr & DBGDSCR_RESTARTED) && !platform_timeout_is_expired(&to));
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
	unsigned i;

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

		for (i = 0; i < priv->hw_breakpoint_max; i++)
			if ((priv->hw_breakpoint_mask & (1 << i)) == 0)
				break;

		if (i == priv->hw_breakpoint_max)
			return -1;

		bw->reserved[0] = i;
		priv->hw_breakpoint_mask |= (1 << i);

		uint32_t addr = va_to_pa(t, bw->addr);
		uint32_t bcr = bp_bas(addr, bw->size) | DBGBCR_PMC_ANY | DBGBCR_EN;
		apb_write(t, DBGBVR(i), addr & ~3);
		apb_write(t, DBGBCR(i), bcr);
		if (i == 0) {
			priv->bcr0 = bcr;
			priv->bvr0 = addr & ~3;
		}

		return 0;

	case TARGET_WATCH_WRITE:
	case TARGET_WATCH_READ:
	case TARGET_WATCH_ACCESS:
		for (i = 0; i < priv->hw_watchpoint_max; i++)
			if ((priv->hw_watchpoint_mask & (1 << i)) == 0)
				break;

		if (i == priv->hw_watchpoint_max)
			return -1;

		bw->reserved[0] = i;
		priv->hw_watchpoint_mask |= (1 << i);

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

			apb_write(t, DBGWCR(i), wcr);
			apb_write(t, DBGWVR(i), bw->addr & ~3U);
			DEBUG_INFO("Watchpoint set WCR = 0x%08" PRIx32 ", WVR = %08" PRIx32 "\n", apb_read(t, DBGWCR(i)),
				apb_read(t, DBGWVR(i)));
		}
		return 0;

	default:
		return 1;
	}
}

static int cortexa_breakwatch_clear(target_s *t, breakwatch_s *bw)
{
	cortexa_priv_s *priv = t->priv;
	unsigned i = bw->reserved[0];
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
		priv->hw_breakpoint_mask &= ~(1 << i);
		apb_write(t, DBGBCR(i), 0);
		if (i == 0)
			priv->bcr0 = 0;
		return 0;
	case TARGET_WATCH_WRITE:
	case TARGET_WATCH_READ:
	case TARGET_WATCH_ACCESS:
		priv->hw_watchpoint_mask &= ~(1 << i);
		apb_write(t, DBGWCR(i), 0);
		return 0;
	default:
		return 1;
	}
}
