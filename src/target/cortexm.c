/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012-2020  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>,
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Modified by Rachel Mant <git@dragonmux.network>
 * Koen De Vleeschauwer and Uwe Bonnes
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

/*
 * This file implements debugging functionality specific ARM Cortex-M cores.
 * This is be generic to both the ARMv6-M and ARMv7-M profiles as defined in
 * the architecture TRMs with ARM document IDs DDI0419E and DDI0403C.
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
#include "cortexm.h"
#include "gdb_reg.h"
#include "command.h"
#include "gdb_packet.h"
#include "semihosting.h"
#include "platform.h"

#include <string.h>
#include <assert.h>
#if defined(_WIN32) || defined(__CYGWIN__)
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#if PC_HOSTED == 1

/*
 * pc-hosted semihosting does keyboard, file and screen i/o on the system
 * where blackmagic_hosted runs, using linux system calls.
 * semihosting in the probe does keyboard, file and screen i/o on the system
 * where gdb runs, using gdb file i/o calls.
 */

#define TARGET_NULL ((target_addr_t)0)
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

static bool cortexm_vector_catch(target_s *t, int argc, const char **argv);
#if PC_HOSTED == 0
static bool cortexm_redirect_stdout(target_s *t, int argc, const char **argv);
#endif

const command_s cortexm_cmd_list[] = {
	{"vector_catch", cortexm_vector_catch, "Catch exception vectors"},
#if PC_HOSTED == 0
	{"redirect_stdout", cortexm_redirect_stdout, "Redirect semihosting stdout to USB UART"},
#endif
	{NULL, NULL, NULL},
};

/* target options recognised by the Cortex-M target */
#define TOPT_FLAVOUR_V6M  (1U << 0U) /* if not set, target is assumed to be v7m */
#define TOPT_FLAVOUR_V7MF (1U << 1U) /* if set, floating-point enabled. */

static const char *cortexm_regs_description(target_s *t);
static void cortexm_regs_read(target_s *t, void *data);
static void cortexm_regs_write(target_s *t, const void *data);
static uint32_t cortexm_pc_read(target_s *t);
static ssize_t cortexm_reg_read(target_s *t, uint32_t reg, void *data, size_t max);
static ssize_t cortexm_reg_write(target_s *t, uint32_t reg, const void *data, size_t max);

static void cortexm_reset(target_s *t);
static target_halt_reason_e cortexm_halt_poll(target_s *t, target_addr_t *watch);
static void cortexm_halt_request(target_s *t);
static int cortexm_fault_unwind(target_s *t);

static int cortexm_breakwatch_set(target_s *t, breakwatch_s *bw);
static int cortexm_breakwatch_clear(target_s *t, breakwatch_s *bw);
static target_addr_t cortexm_check_watch(target_s *t);

static int cortexm_hostio_request(target_s *t);

static uint32_t time0_sec = UINT32_MAX; /* sys_clock time origin */

typedef struct cortexm_priv {
	cortex_priv_s base;
	bool stepping;
	bool on_bkpt;
	/* Flash Patch controller configuration */
	uint32_t flash_patch_revision;
	/* Copy of DEMCR for vector-catch */
	uint32_t demcr;
} cortexm_priv_s;

/* Register number tables */
static const uint32_t regnum_cortex_m[CORTEXM_GENERAL_REG_COUNT] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, /* standard r0-r15 */
	0x10,                                                 /* xpsr */
	0x11,                                                 /* msp */
	0x12,                                                 /* psp */
	0x14,                                                 /* special */
};

static const uint32_t regnum_cortex_mf[CORTEX_FLOAT_REG_COUNT] = {
	0x21,                                           /* fpscr */
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, /* s0-s7 */
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, /* s8-s15 */
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, /* s16-s23 */
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, /* s24-s31 */
};

/*
 * Fields for Cortex-M special purpose registers, used in the generation of GDB's target description XML.
 * The general purpose registers r0-r12 and the vector floating point registers d0-d15 all follow a very
 * regular format, so we only need to store fields for the special purpose registers.
 * The arrays for each SPR field have the same order as each other, making each of them a pseudo
 * 'associative array'.
 */

// Strings for the names of the Cortex-M's special purpose registers.
static const char *cortex_m_spr_names[] = {
	"sp",
	"lr",
	"pc",
	"xpsr",
	"msp",
	"psp",
	"primask",
	"basepri",
	"faultmask",
	"control",
};

// The "type" field for each Cortex-M special purpose register.
static const gdb_reg_type_e cortex_m_spr_types[] = {
	GDB_TYPE_DATA_PTR,    // sp
	GDB_TYPE_CODE_PTR,    // lr
	GDB_TYPE_CODE_PTR,    // pc
	GDB_TYPE_UNSPECIFIED, // xpsr
	GDB_TYPE_DATA_PTR,    // msp
	GDB_TYPE_DATA_PTR,    // psp
	GDB_TYPE_UNSPECIFIED, // primask
	GDB_TYPE_UNSPECIFIED, // basepri
	GDB_TYPE_UNSPECIFIED, // faultmask
	GDB_TYPE_UNSPECIFIED, // control
};

// clang-format off
static_assert(ARRAY_LENGTH(cortex_m_spr_types) == ARRAY_LENGTH(cortex_m_spr_names),
	"SPR array length mismatch! SPR type array should have the same length as SPR name array."
);
// clang-format on

// The "save-restore" field of each SPR.
static const gdb_reg_save_restore_e cortex_m_spr_save_restores[] = {
	GDB_SAVE_RESTORE_UNSPECIFIED, // sp
	GDB_SAVE_RESTORE_UNSPECIFIED, // lr
	GDB_SAVE_RESTORE_UNSPECIFIED, // pc
	GDB_SAVE_RESTORE_UNSPECIFIED, // xpsr
	GDB_SAVE_RESTORE_NO,          // msp
	GDB_SAVE_RESTORE_NO,          // psp
	GDB_SAVE_RESTORE_NO,          // primask
	GDB_SAVE_RESTORE_NO,          // basepri
	GDB_SAVE_RESTORE_NO,          // faultmask
	GDB_SAVE_RESTORE_NO,          // control
};

// clang-format off
static_assert(ARRAY_LENGTH(cortex_m_spr_save_restores) == ARRAY_LENGTH(cortex_m_spr_names),
	"SPR array length mismatch! SPR save-restore array should have the same length as SPR name array."
);
// clang-format on

// The "bitsize" field of each SPR.
static const uint8_t cortex_m_spr_bitsizes[] = {
	32, // sp
	32, // lr
	32, // pc
	32, // xpsr
	32, // msp
	32, // psp
	8,  // primask
	8,  // basepri
	8,  // faultmask
	8,  // control
};

// clang-format off
static_assert(ARRAY_LENGTH(cortex_m_spr_bitsizes) == ARRAY_LENGTH(cortex_m_spr_names),
	"SPR array length mismatch! SPR bitsize array should have the same length as SPR name array."
);

// clang-format on

// Creates the target description XML string for a Cortex-M. Like snprintf(), this function
// will write no more than max_len and returns the amount of bytes written. Or, if max_len is 0,
// then this function will return the amount of bytes that _would_ be necessary to create this
// string.
//
// This function is hand-optimized to decrease string duplication and thus code size, making it
// unfortunately much less readable than the string literal it is equivalent to.
//
// The string it creates is XML-equivalent to the following:
/*
	"<?xml version=\"1.0\"?>"
	"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
	"<target>"
	"  <architecture>arm</architecture>"
	"  <feature name=\"org.gnu.gdb.arm.m-profile\">"
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
	"    <reg name=\"xpsr\" bitsize=\"32\"/>"
	"    <reg name=\"msp\" bitsize=\"32\" save-restore=\"no\" type=\"data_ptr\"/>"
	"    <reg name=\"psp\" bitsize=\"32\" save-restore=\"no\" type=\"data_ptr\"/>"
	"    <reg name=\"primask\" bitsize=\"8\" save-restore=\"no\"/>"
	"    <reg name=\"basepri\" bitsize=\"8\" save-restore=\"no\"/>"
	"    <reg name=\"faultmask\" bitsize=\"8\" save-restore=\"no\"/>"
	"    <reg name=\"control\" bitsize=\"8\" save-restore=\"no\"/>"
	"  </feature>"
	"</target>"
*/
static size_t create_tdesc_cortex_m(char *buffer, size_t max_len)
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
	// negative number. So we also have to repatedly check if max_len is 0 before performing
	// that subtraction.
	size_t printsz = max_len;

	// Start with the "preamble", which is generic across ARM targets,
	// ...save for one word, so we'll have to do the preamble in halves.
	total += snprintf(buffer, printsz, "%s target %sarm%s <feature name=\"org.gnu.gdb.arm.m-profile\">",
		gdb_xml_preamble_first, gdb_xml_preamble_second, gdb_xml_preamble_third);

	// Then the general purpose registers, which have names of r0 to r12,
	// and all the same bitsize.
	for (uint8_t i = 0; i <= 12; ++i) {
		if (max_len != 0)
			printsz = max_len - (size_t)total;

		total += snprintf(buffer + total, printsz, "<reg name=\"r%u\" bitsize=\"32\"/>", i);
	}

	// Now for sp, lr, pc, xpsr, msp, psp, primask, basepri, faultmask, and control.
	// These special purpose registers are a little more complicated.
	// Some of them have different bitsizes, specified types, or specified save-restore values.
	// We'll use the 'associative arrays' defined for those values.
	// NOTE: unlike the other loops, this loop uses a size_t for its counter, as it's used to index into arrays.
	for (size_t i = 0; i < ARRAY_LENGTH(cortex_m_spr_names); ++i) {
		if (max_len != 0)
			printsz = max_len - (size_t)total;

		gdb_reg_type_e type = cortex_m_spr_types[i];
		gdb_reg_save_restore_e save_restore = cortex_m_spr_save_restores[i];

		total += snprintf(buffer + total, printsz, "<reg name=\"%s\" bitsize=\"%u\"%s%s/>", cortex_m_spr_names[i],
			cortex_m_spr_bitsizes[i], gdb_reg_save_restore_strings[save_restore], gdb_reg_type_strings[type]);
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

// Creates the target description XML string for a Cortex-MF. Like snprintf(), this function
// will write no more than max_len and returns the amount of bytes written. Or, if max_len is 0,
// then this function will return the amount of bytes that _would_ be necessary to create this
// string.
//
// This function is hand-optimized to decrease string duplication and thus code size, making it
// unfortunately much less readable than the string literal it is equivalent to.
//
// The string it creates is XML-equivalent to the following:
/*
	"<?xml version=\"1.0\"?>"
	"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
	"<target>"
	"  <architecture>arm</architecture>"
	"  <feature name=\"org.gnu.gdb.arm.m-profile\">"
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
	"    <reg name=\"xpsr\" bitsize=\"32\"/>"
	"    <reg name=\"msp\" bitsize=\"32\" save-restore=\"no\" type=\"data_ptr\"/>"
	"    <reg name=\"psp\" bitsize=\"32\" save-restore=\"no\" type=\"data_ptr\"/>"
	"    <reg name=\"primask\" bitsize=\"8\" save-restore=\"no\"/>"
	"    <reg name=\"basepri\" bitsize=\"8\" save-restore=\"no\"/>"
	"    <reg name=\"faultmask\" bitsize=\"8\" save-restore=\"no\"/>"
	"    <reg name=\"control\" bitsize=\"8\" save-restore=\"no\"/>"
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
	"</target>"
*/
static size_t create_tdesc_cortex_mf(char *buffer, size_t max_len)
{
	// Minor hack: technically snprintf returns an int for possibility of error, but in this case
	// these functions are given static input that should not be able to fail -- and if it does,
	// then there's not really anything we can do about it, so we repatedly cast this variable
	// to a size_t when calculating printsz (see below). Likewise, create_tdesc_cortex_m()
	// has static inputs and shouldn't ever return a value large enough for casting it to a
	// signed int to change its value, and if it does, then again there's something wrong that
	// we can't really do anything about.

	// The first part of the target description for the Cortex-MF is identical to the Cortex-M
	// target description.
	int total = (int)create_tdesc_cortex_m(buffer, max_len);

	// We can't just repeatedly pass max_len to snprintf, because we keep changing the start
	// of buffer (effectively changing its size), so we have to repeatedly compute the size
	// passed to snprintf by subtracting the current total from max_len.
	// ...Unless max_len is 0, in which case that subtraction will result in an (underflowed)
	// negative number. So we also have to repatedly check if max_len is 0 before perofmring
	// that subtraction.
	size_t printsz = max_len;

	if (max_len != 0) {
		// Minor hack: subtract the target closing tag, since we have a bit more to add.
		total -= strlen("</target>");

		printsz = max_len - (size_t)total;
	}

	total += snprintf(buffer + total, printsz,
		"<feature name=\"org.gnu.gdb.arm.vfp\">"
		"<reg name=\"fpscr\" bitsize=\"32\"/>");

	// After fpscr, the rest of the vfp registers follow a regular format: d0-d15, bitsize 64, type float.
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

static void cortexm_cache_clean(target_s *t, target_addr_t addr, size_t len, bool invalidate)
{
	cortexm_priv_s *priv = t->priv;
	if (!priv->base.dcache_line_length)
		return;
	uint32_t cache_reg = invalidate ? CORTEXM_DCCIMVAC : CORTEXM_DCCMVAC;
	size_t minline = priv->base.dcache_line_length << 2U;

	/* flush data cache for RAM regions that intersect requested region */
	target_addr_t mem_end = addr + len; /* following code is NOP if wraparound */
	/* requested region is [src, src_end) */
	for (target_ram_s *r = t->ram; r; r = r->next) {
		target_addr_t ram = r->start;
		target_addr_t ram_end = r->start + r->length;
		/* RAM region is [ram, ram_end) */
		if (addr > ram)
			ram = addr;
		if (mem_end < ram_end)
			ram_end = mem_end;
		/* intersection is [ram, ram_end) */
		for (ram &= ~(minline - 1U); ram < ram_end; ram += minline)
			adiv5_mem_write(cortex_ap(t), cache_reg, &ram, 4);
	}
}

static void cortexm_mem_read(target_s *t, void *dest, target_addr_t src, size_t len)
{
	cortexm_cache_clean(t, src, len, false);
	adiv5_mem_read(cortex_ap(t), dest, src, len);
}

static void cortexm_mem_write(target_s *t, target_addr_t dest, const void *src, size_t len)
{
	cortexm_cache_clean(t, dest, len, true);
	adiv5_mem_write(cortex_ap(t), dest, src, len);
}

const char *cortexm_regs_description(target_s *t)
{
	const bool is_cortexmf = t->target_options & TOPT_FLAVOUR_V7MF;
	const size_t description_length =
		(is_cortexmf ? create_tdesc_cortex_mf(NULL, 0) : create_tdesc_cortex_m(NULL, 0)) + 1U;
	char *const description = malloc(description_length);
	if (description) {
		if (is_cortexmf)
			create_tdesc_cortex_mf(description, description_length);
		else
			create_tdesc_cortex_m(description, description_length);
	}
	return description;
}

bool cortexm_probe(adiv5_access_port_s *ap)
{
	target_s *t = target_new();
	if (!t)
		return false;

	adiv5_ap_ref(ap);
	if (ap->dp->version >= 2 && ap->dp->target_designer_code != 0) {
		/* Use TARGETID register to identify target */
		t->designer_code = ap->dp->target_designer_code;
		t->part_id = ap->dp->target_partno;
	} else {
		/* Use AP DESIGNER and AP PARTNO to identify target */
		t->designer_code = ap->designer_code;
		t->part_id = ap->partno;
	}

	/* MM32F5xxx: part designer code is Arm China, target designer code uses forbidden continuation code */
	if (t->designer_code == JEP106_MANUFACTURER_ERRATA_ARM_CHINA &&
		ap->dp->designer_code == JEP106_MANUFACTURER_ARM_CHINA)
		t->designer_code = JEP106_MANUFACTURER_ARM_CHINA;

	cortexm_priv_s *priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}

	t->priv = priv;
	t->priv_free = cortex_priv_free;
	priv->base.ap = ap;
	priv->base.base_addr = CORTEXM_SCS_BASE;

	t->check_error = cortex_check_error;
	t->mem_read = cortexm_mem_read;
	t->mem_write = cortexm_mem_write;

	t->driver = "ARM Cortex-M";

	cortex_read_cpuid(t);

	t->attach = cortexm_attach;
	t->detach = cortexm_detach;

	/* Probe for FP extension. */
	uint32_t cpacr = target_mem_read32(t, CORTEXM_CPACR);
	cpacr |= 0x00f00000U; /* CP10 = 0b11, CP11 = 0b11 */
	target_mem_write32(t, CORTEXM_CPACR, cpacr);
	bool is_cortexmf = target_mem_read32(t, CORTEXM_CPACR) == cpacr;

	t->regs_description = cortexm_regs_description;
	t->regs_read = cortexm_regs_read;
	t->regs_write = cortexm_regs_write;
	t->reg_read = cortexm_reg_read;
	t->reg_write = cortexm_reg_write;

	t->reset = cortexm_reset;
	t->halt_request = cortexm_halt_request;
	t->halt_poll = cortexm_halt_poll;
	t->halt_resume = cortexm_halt_resume;
	t->regs_size = sizeof(uint32_t) * CORTEXM_GENERAL_REG_COUNT;

	t->breakwatch_set = cortexm_breakwatch_set;
	t->breakwatch_clear = cortexm_breakwatch_clear;

	target_add_commands(t, cortexm_cmd_list, t->driver);

	if (is_cortexmf) {
		t->target_options |= TOPT_FLAVOUR_V7MF;
		t->regs_size += sizeof(uint32_t) * CORTEX_FLOAT_REG_COUNT;
	}

	/* Default vectors to catch */
	priv->demcr = CORTEXM_DEMCR_TRCENA | CORTEXM_DEMCR_VC_HARDERR | CORTEXM_DEMCR_VC_CORERESET;

	/*
	 * Some devices, such as the STM32F0, will not correctly respond to probes under reset.
	 * Therefore, if we're attempting to connect under reset, we should first write to
	 * the debug register to catch the reset vector so that we immediately halt when reset
	 * is released, then request a halt and release reset. This will prevent any user code
	 * from running on the target.
	 */
	bool conn_reset = false;
	if (platform_nrst_get_val()) {
		conn_reset = true;

		/* Request halt when reset is de-asseted */
		target_mem_write32(t, CORTEXM_DEMCR, priv->demcr);
		/* Force a halt */
		cortexm_halt_request(t);
		/* Release reset */
		platform_nrst_set_val(false);
		/* Poll for release from reset */
		platform_timeout_s timeout;
		platform_timeout_set(&timeout, 1000);
		while (target_mem_read32(t, CORTEXM_DHCSR) & CORTEXM_DHCSR_S_RESET_ST) {
			if (platform_timeout_is_expired(&timeout)) {
				DEBUG_ERROR("Error releasing from reset\n");
				/* Go on and try to detect the target anyways */
				break;
			}
			continue;
		}
	}

	/* Check cache type */
	const uint32_t cache_type = target_mem_read32(t, CORTEXM_CTR);
	if (cache_type >> CORTEX_CTR_FORMAT_SHIFT == CORTEX_CTR_FORMAT_ARMv7) {
		priv->base.icache_line_length = CORTEX_CTR_ICACHE_LINE(cache_type);
		priv->base.dcache_line_length = CORTEX_CTR_DCACHE_LINE(cache_type);
	} else
		target_check_error(t);

	/* If we set the interrupt catch vector earlier, clear it. */
	if (conn_reset)
		target_mem_write32(t, CORTEXM_DEMCR, 0);

	switch (t->designer_code) {
	case JEP106_MANUFACTURER_FREESCALE:
		PROBE(imxrt_probe);
		PROBE(kinetis_probe);
		break;
	case JEP106_MANUFACTURER_GIGADEVICE:
		PROBE(gd32f1_probe);
		PROBE(gd32f4_probe);
		break;
	case JEP106_MANUFACTURER_STM:
		PROBE(stm32f1_probe);
		PROBE(stm32f4_probe);
		PROBE(stm32h5_probe);
		PROBE(stm32h7_probe);
		PROBE(stm32mp15_cm4_probe);
		PROBE(stm32l0_probe);
		PROBE(stm32l4_probe);
		PROBE(stm32g0_probe);
		break;
	case JEP106_MANUFACTURER_CYPRESS:
		DEBUG_WARN("Unhandled Cypress device\n");
		break;
	case JEP106_MANUFACTURER_INFINEON:
		DEBUG_WARN("Unhandled Infineon device\n");
		break;
	case JEP106_MANUFACTURER_NORDIC:
		PROBE(nrf51_probe);
		PROBE(nrf91_probe);
		break;
	case JEP106_MANUFACTURER_ATMEL:
		PROBE(samx7x_probe);
		PROBE(sam4l_probe);
		PROBE(samd_probe);
		PROBE(samx5x_probe);
		break;
	case JEP106_MANUFACTURER_ENERGY_MICRO:
		PROBE(efm32_probe);
		break;
	case JEP106_MANUFACTURER_TEXAS:
		PROBE(msp432p4_probe);
		break;
	case JEP106_MANUFACTURER_SPECULAR:
		PROBE(lpc11xx_probe); /* LPC845 */
		break;
	case JEP106_MANUFACTURER_RASPBERRY:
		PROBE(rp_probe);
		break;
	case JEP106_MANUFACTURER_RENESAS:
		PROBE(renesas_probe);
		break;
	case JEP106_MANUFACTURER_NXP:
		if ((t->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M33)
			PROBE(lpc55xx_probe);
		else
			DEBUG_WARN("Unhandled NXP device\n");
		break;
	case JEP106_MANUFACTURER_ARM_CHINA:
		PROBE(mm32f3xx_probe); /* MindMotion Star-MC1 */
		break;
	case JEP106_MANUFACTURER_ARM:
		/*
		 * All of these have braces as a brake from the standard so they're completely
		 * consistent and easier to add new probe calls to.
		 */
		if (t->part_id == 0x4c0U) {        /* Cortex-M0+ ROM */
			PROBE(lpc11xx_probe);          /* LPC8 */
			PROBE(hc32l110_probe);         /* HDSC HC32L110 */
		} else if (t->part_id == 0x4c1U) { /* NXP Cortex-M0+ ROM */
			PROBE(lpc11xx_probe);          /* newer LPC11U6x */
		} else if (t->part_id == 0x4c3U) { /* Cortex-M3 ROM */
			PROBE(lmi_probe);
			PROBE(ch32f1_probe);
			PROBE(stm32f1_probe);          /* Care for other STM32F1 clones (?) */
			PROBE(lpc15xx_probe);          /* Thanks to JojoS for testing */
			PROBE(mm32f3xx_probe);         /* MindMotion MM32 */
		} else if (t->part_id == 0x471U) { /* Cortex-M0 ROM */
			PROBE(lpc11xx_probe);          /* LPC24C11 */
			PROBE(lpc43xx_probe);
			PROBE(mm32l0xx_probe);         /* MindMotion MM32 */
		} else if (t->part_id == 0x4c4U) { /* Cortex-M4 ROM */
			PROBE(sam3x_probe);
			PROBE(lmi_probe);
			/*
			 * The LPC546xx and LPC43xx parts present with the same AP ROM part number,
			 * so we need to probe both. Unfortunately, when probing for the LPC43xx
			 * when the target is actually an LPC546xx, the memory location checked
			 * is illegal for the LPC546xx and puts the chip into lockup, requiring a
			 * reset pulse to recover. Instead, make sure to probe for the LPC546xx first,
			 * which experimentally doesn't harm LPC43xx detection.
			 */
			PROBE(lpc546xx_probe);
			PROBE(lpc43xx_probe);
			PROBE(lpc40xx_probe);
			PROBE(kinetis_probe); /* Older K-series */
			PROBE(at32fxx_probe);
			PROBE(msp432e4_probe);
		} else if (t->part_id == 0x4cbU) { /* Cortex-M23 ROM */
			PROBE(gd32f1_probe);           /* GD32E23x uses GD32F1 peripherals */
		}
		break;
	case ASCII_CODE_FLAG:
		/*
		 * these devices enumerate an AP with an empty ascii code,
		 * and have no available designer code elsewhere
		 */
		PROBE(sam3x_probe);
		PROBE(ke04_probe);
		PROBE(lpc17xx_probe);
		PROBE(lpc11xx_probe); /* LPC1343 */
		break;
	}
#if PC_HOSTED == 0
	gdb_outf("Please report unknown device with Designer 0x%x Part ID 0x%x\n", t->designer_code, t->part_id);
#else
	DEBUG_WARN("Please report unknown device with Designer 0x%x Part ID 0x%x\n", t->designer_code, t->part_id);
#endif
	return true;
}

bool cortexm_attach(target_s *t)
{
	adiv5_access_port_s *ap = cortex_ap(t);
	/* Mark the DP as being in fault so error recovery will switch to this core when in multi-drop mode */
	ap->dp->fault = 1;
	cortexm_priv_s *priv = t->priv;

	/* Clear any pending fault condition (and switch to this core) */
	target_check_error(t);

	target_halt_request(t);
	/* Request halt on reset */
	target_mem_write32(t, CORTEXM_DEMCR, priv->demcr);

	/* Reset DFSR flags */
	target_mem_write32(t, CORTEXM_DFSR, CORTEXM_DFSR_RESETALL);

	/* size the break/watchpoint units */
	priv->base.breakpoints_available = CORTEX_MAX_BREAKPOINTS;
	const uint32_t flash_break_cfg = target_mem_read32(t, CORTEXM_FPB_CTRL);
	const uint32_t breakpoints = ((flash_break_cfg >> 4U) & 0xfU);
	if (breakpoints < priv->base.breakpoints_available) /* only look at NUM_COMP1 */
		priv->base.breakpoints_available = breakpoints;
	priv->flash_patch_revision = flash_break_cfg >> 28U;

	priv->base.watchpoints_available = CORTEX_MAX_WATCHPOINTS;
	const uint32_t watchpoints = target_mem_read32(t, CORTEXM_DWT_CTRL);
	if ((watchpoints >> 28U) < priv->base.watchpoints_available)
		priv->base.watchpoints_available = watchpoints >> 28U;

	/* Clear any stale breakpoints */
	priv->base.breakpoints_mask = 0;
	for (size_t i = 0; i < priv->base.breakpoints_available; i++)
		target_mem_write32(t, CORTEXM_FPB_COMP(i), 0);

	/* Clear any stale watchpoints */
	priv->base.watchpoints_mask = 0;
	for (size_t i = 0; i < priv->base.watchpoints_available; i++)
		target_mem_write32(t, CORTEXM_DWT_FUNC(i), 0);

	/* Flash Patch Control Register: set ENABLE */
	target_mem_write32(t, CORTEXM_FPB_CTRL, CORTEXM_FPB_CTRL_KEY | CORTEXM_FPB_CTRL_ENABLE);

	(void)target_mem_read32(t, CORTEXM_DHCSR);
	if (target_mem_read32(t, CORTEXM_DHCSR) & CORTEXM_DHCSR_S_RESET_ST) {
		platform_nrst_set_val(false);
		platform_timeout_s timeout;
		platform_timeout_set(&timeout, 1000);
		while (1) {
			const uint32_t reset_status = target_mem_read32(t, CORTEXM_DHCSR);
			if (!(reset_status & CORTEXM_DHCSR_S_RESET_ST))
				break;
			if (platform_timeout_is_expired(&timeout)) {
				DEBUG_ERROR("Error releasing from reset\n");
				return false;
			}
		}
	}
	return true;
}

void cortexm_detach(target_s *t)
{
	cortexm_priv_s *priv = t->priv;

	/* Clear any stale breakpoints */
	for (size_t i = 0; i < priv->base.breakpoints_available; i++)
		target_mem_write32(t, CORTEXM_FPB_COMP(i), 0);

	/* Clear any stale watchpoints */
	for (size_t i = 0; i < priv->base.watchpoints_available; i++)
		target_mem_write32(t, CORTEXM_DWT_FUNC(i), 0);

	/* Restore DEMCR */
	adiv5_access_port_s *ap = cortex_ap(t);
	target_mem_write32(t, CORTEXM_DEMCR, ap->ap_cortexm_demcr);
	/* Resume target and disable debug, re-enabling interrupts in the process */
	target_mem_write32(t, CORTEXM_DHCSR, CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_DEBUGEN | CORTEXM_DHCSR_C_HALT);
	target_mem_write32(t, CORTEXM_DHCSR, CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_DEBUGEN);
	target_mem_write32(t, CORTEXM_DHCSR, CORTEXM_DHCSR_DBGKEY);
}

enum {
	DB_DHCSR,
	DB_DCRSR,
	DB_DCRDR,
	DB_DEMCR
};

static void cortexm_regs_read(target_s *const target, void *const data)
{
	uint32_t *const regs = data;
	adiv5_access_port_s *const ap = cortex_ap(target);
#if PC_HOSTED == 1
	if (ap->dp->ap_regs_read && ap->dp->ap_reg_read) {
		uint32_t core_regs[21U];
		ap->dp->ap_regs_read(ap, core_regs);
		for (size_t i = 0; i < CORTEXM_GENERAL_REG_COUNT; ++i)
			regs[i] = core_regs[regnum_cortex_m[i]];

		if (target->target_options & TOPT_FLAVOUR_V7MF) {
			const size_t offset = CORTEXM_GENERAL_REG_COUNT;
			for (size_t i = 0; i < CORTEX_FLOAT_REG_COUNT; ++i)
				regs[offset + i] = ap->dp->ap_reg_read(ap, regnum_cortex_mf[i]);
		}
	} else {
#endif
		/* Set up CSW for 32-bit access to allow us to read the target's registers */
		adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw | ADIV5_AP_CSW_SIZE_WORD);
		/*
		 * Map the AP's banked data registers (0x10-0x1c) to the
		 * debug registers DHCSR, DCRSR, DCRDR and DEMCR respectively
		 */
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_TAR, CORTEXM_DHCSR);
		/* Configure the bank selection to the appropriate AP register bank */
		adiv5_dp_write(ap->dp, ADIV5_DP_SELECT, ((uint32_t)ap->apsel << 24U) | 0x10U);

		/* Walk the regnum_cortex_m array, reading the registers it specifies */
		for (size_t i = 0; i < CORTEXM_GENERAL_REG_COUNT; ++i) {
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DB(DB_DCRSR), regnum_cortex_m[i]);
			regs[i] = adiv5_dp_read(ap->dp, ADIV5_AP_DB(DB_DCRDR));
		}
		/* If the device has a FPU, also walk the regnum_cortex_mf array */
		if (target->target_options & TOPT_FLAVOUR_V7MF) {
			const size_t offset = CORTEXM_GENERAL_REG_COUNT;
			for (size_t i = 0; i < CORTEX_FLOAT_REG_COUNT; ++i) {
				adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DB(DB_DCRSR), regnum_cortex_mf[i]);
				regs[offset + i] = adiv5_dp_read(ap->dp, ADIV5_AP_DB(DB_DCRDR));
			}
		}
#if PC_HOSTED == 1
	}
#endif
}

static void cortexm_regs_write(target_s *const target, const void *const data)
{
	const uint32_t *const regs = data;
	adiv5_access_port_s *const ap = cortex_ap(target);
#if PC_HOSTED == 1
	if (ap->dp->ap_reg_write) {
		for (size_t i = 0; i < CORTEXM_GENERAL_REG_COUNT; ++i)
			ap->dp->ap_reg_write(ap, regnum_cortex_m[i], regs[i]);

		if (target->target_options & TOPT_FLAVOUR_V7MF) {
			const size_t offset = CORTEXM_GENERAL_REG_COUNT;
			for (size_t i = 0; i < CORTEX_FLOAT_REG_COUNT; ++i)
				ap->dp->ap_reg_write(ap, regnum_cortex_mf[i], regs[offset + i]);
		}
	} else {
#endif
		/* Set up CSW for 32-bit access to allow us to write the target's registers */
		adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw | ADIV5_AP_CSW_SIZE_WORD);
		/*
		 * Map the banked data registers (0x10-0x1c) to the
		 * debug registers DHCSR, DCRSR, DCRDR and DEMCR respectively
		 */
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_TAR, CORTEXM_DHCSR);
		/* Configure the bank selection to the appropriate AP register bank */
		adiv5_dp_write(ap->dp, ADIV5_DP_SELECT, ((uint32_t)ap->apsel << 24U) | 0x10U);

		/* Walk the regnum_cortex_m array, writing the registers it specifies */
		for (size_t i = 0; i < CORTEXM_GENERAL_REG_COUNT; ++i) {
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DB(DB_DCRDR), regs[i]);
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DB(DB_DCRSR), 0x10000 | regnum_cortex_m[i]);
		}
		if (target->target_options & TOPT_FLAVOUR_V7MF) {
			size_t offset = CORTEXM_GENERAL_REG_COUNT;
			for (size_t i = 0; i < CORTEX_FLOAT_REG_COUNT; ++i) {
				adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DB(DB_DCRDR), regs[offset + i]);
				adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DB(DB_DCRSR), 0x10000 | regnum_cortex_mf[i]);
			}
		}
#if PC_HOSTED == 1
	}
#endif
}

int cortexm_mem_write_sized(target_s *t, target_addr_t dest, const void *src, size_t len, align_e align)
{
	cortexm_cache_clean(t, dest, len, true);
	adiv5_mem_write_sized(cortex_ap(t), dest, src, len, align);
	return target_check_error(t);
}

static int dcrsr_regnum(target_s *t, uint32_t reg)
{
	if (reg < CORTEXM_GENERAL_REG_COUNT)
		return regnum_cortex_m[reg];
	if ((t->target_options & TOPT_FLAVOUR_V7MF) && reg < CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT)
		return regnum_cortex_mf[reg - CORTEXM_GENERAL_REG_COUNT];
	return -1;
}

static ssize_t cortexm_reg_read(target_s *t, uint32_t reg, void *data, size_t max)
{
	if (max < 4U)
		return -1;
	uint32_t *r = data;
	target_mem_write32(t, CORTEXM_DCRSR, dcrsr_regnum(t, reg));
	*r = target_mem_read32(t, CORTEXM_DCRDR);
	return 4U;
}

static ssize_t cortexm_reg_write(target_s *t, uint32_t reg, const void *data, size_t max)
{
	if (max < 4U)
		return -1;
	const uint32_t *r = data;
	target_mem_write32(t, CORTEXM_DCRDR, *r);
	target_mem_write32(t, CORTEXM_DCRSR, CORTEXM_DCRSR_REGWnR | dcrsr_regnum(t, reg));
	return 4U;
}

static uint32_t cortexm_pc_read(target_s *t)
{
	target_mem_write32(t, CORTEXM_DCRSR, 0x0f);
	return target_mem_read32(t, CORTEXM_DCRDR);
}

static void cortexm_pc_write(target_s *t, const uint32_t val)
{
	target_mem_write32(t, CORTEXM_DCRDR, val);
	target_mem_write32(t, CORTEXM_DCRSR, CORTEXM_DCRSR_REGWnR | 0x0fU);
}

/*
 * The following three routines implement target halt/resume
 * using the core debug registers in the NVIC.
 */
static void cortexm_reset(target_s *t)
{
	/* Read DHCSR here to clear S_RESET_ST bit before reset */
	target_mem_read32(t, CORTEXM_DHCSR);
	platform_timeout_s reset_timeout;
	if ((t->target_options & CORTEXM_TOPT_INHIBIT_NRST) == 0) {
		platform_nrst_set_val(true);
		platform_nrst_set_val(false);
		/* Some NRF52840 users saw invalid SWD transaction with  native/firmware without this delay.*/
		platform_delay(10);
	}
	uint32_t dhcsr = target_mem_read32(t, CORTEXM_DHCSR);
	if ((dhcsr & CORTEXM_DHCSR_S_RESET_ST) == 0) {
		/*
		 * No reset seen yet, maybe as nRST is not connected, or device has CORTEXM_TOPT_INHIBIT_NRST set.
		 * Trigger reset by AIRCR.
		 */
		target_mem_write32(t, CORTEXM_AIRCR, CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_SYSRESETREQ);
	}
	/* If target needs to do something extra (see Atmel SAM4L for example) */
	if (t->extended_reset != NULL)
		t->extended_reset(t);
	/* Wait for CORTEXM_DHCSR_S_RESET_ST to read 0, meaning reset released.*/
	platform_timeout_set(&reset_timeout, 1000);
	while ((target_mem_read32(t, CORTEXM_DHCSR) & CORTEXM_DHCSR_S_RESET_ST) &&
		!platform_timeout_is_expired(&reset_timeout))
		continue;
#if defined(PLATFORM_HAS_DEBUG)
	if (platform_timeout_is_expired(&reset_timeout))
		DEBUG_WARN("Reset seem to be stuck low!\n");
#endif
	/* 10 ms delay to ensure that things such as the STM32 HSI clock have started up fully. */
	platform_delay(10);
	/* Reset DFSR flags */
	target_mem_write32(t, CORTEXM_DFSR, CORTEXM_DFSR_RESETALL);
	/* Make sure we ignore any initial DAP error */
	target_check_error(t);
}

static void cortexm_halt_request(target_s *t)
{
	volatile exception_s e;
	TRY_CATCH (e, EXCEPTION_TIMEOUT) {
		target_mem_write32(t, CORTEXM_DHCSR, CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_HALT | CORTEXM_DHCSR_C_DEBUGEN);
	}
	if (e.type)
		tc_printf(t, "Timeout sending interrupt, is target in WFI?\n");
}

static target_halt_reason_e cortexm_halt_poll(target_s *t, target_addr_t *watch)
{
	cortexm_priv_s *priv = t->priv;

	volatile uint32_t dhcsr = 0;
	volatile exception_s e;
	TRY_CATCH (e, EXCEPTION_ALL) {
		/* If this times out because the target is in WFI then the target is still running. */
		dhcsr = target_mem_read32(t, CORTEXM_DHCSR);
	}
	switch (e.type) {
	case EXCEPTION_ERROR:
		/* Things went seriously wrong and there is no recovery from this... */
		target_list_free();
		return TARGET_HALT_ERROR;
	case EXCEPTION_TIMEOUT:
		/* Timeout isn't actually a problem and probably means target is in WFI */
		return TARGET_HALT_RUNNING;
	}

	/* Check that the core actually halted */
	if (!(dhcsr & CORTEXM_DHCSR_S_HALT))
		return TARGET_HALT_RUNNING;

	/* Read out the status register to determine why */
	uint32_t dfsr = target_mem_read32(t, CORTEXM_DFSR);
	target_mem_write32(t, CORTEXM_DFSR, dfsr); /* write back to reset */

	if ((dfsr & CORTEXM_DFSR_VCATCH) && cortexm_fault_unwind(t))
		return TARGET_HALT_FAULT;

	/* Remember if we stopped on a breakpoint */
	priv->on_bkpt = dfsr & CORTEXM_DFSR_BKPT;
	if (priv->on_bkpt) {
		/* If we've hit a programmed breakpoint, check for semihosting call. */
		const uint32_t program_counter = cortexm_pc_read(t);
		const uint16_t instruction = target_mem_read16(t, program_counter);
		/* 0xbeab encodes the breakpoint instruction used to indicate a semihosting call */
		if (instruction == 0xbeabU) {
			if (cortexm_hostio_request(t))
				return TARGET_HALT_REQUEST;

			target_halt_resume(t, priv->stepping);
			return TARGET_HALT_RUNNING;
		}
	}

	if (dfsr & CORTEXM_DFSR_DWTTRAP) {
		if (watch != NULL)
			*watch = cortexm_check_watch(t);
		return TARGET_HALT_WATCHPOINT;
	}
	if (dfsr & CORTEXM_DFSR_BKPT)
		return TARGET_HALT_BREAKPOINT;

	if (dfsr & CORTEXM_DFSR_HALTED)
		return priv->stepping ? TARGET_HALT_STEPPING : TARGET_HALT_REQUEST;

	return TARGET_HALT_BREAKPOINT;
}

void cortexm_halt_resume(target_s *const target, const bool step)
{
	cortexm_priv_s *priv = target->priv;
	/* Begin building the new DHCSR value to resume the core with */
	uint32_t dhcsr = CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_DEBUGEN;

	/* Disable interrupts while single stepping */
	if (step)
		dhcsr |= CORTEXM_DHCSR_C_STEP | CORTEXM_DHCSR_C_MASKINTS;

	/*
	 * If we're switching between single-stepped and run modes, update C_MASKINTS
	 * (which requires C_HALT to be set or the write is unpredictable)
	 */
	if (step != priv->stepping) {
		target_mem_write32(target, CORTEXM_DHCSR, dhcsr | CORTEXM_DHCSR_C_HALT);
		priv->stepping = step;
	}

	if (priv->on_bkpt) {
		/* Read the instruction to resume on */
		uint32_t pc = cortexm_pc_read(target);
		/* If it actually is a breakpoint instruction, update the program counter one past it. */
		if ((target_mem_read16(target, pc) & 0xff00U) == 0xbe00U)
			cortexm_pc_write(target, pc + 2U);
	}

	if (priv->base.icache_line_length)
		target_mem_write32(target, CORTEXM_ICIALLU, 0);

	/* Release C_HALT to resume the core in whichever mode is selected */
	target_mem_write32(target, CORTEXM_DHCSR, dhcsr);
}

static int cortexm_fault_unwind(target_s *t)
{
	/* Read the fault status registers */
	uint32_t hfsr = target_mem_read32(t, CORTEXM_HFSR);
	uint32_t cfsr = target_mem_read32(t, CORTEXM_CFSR);
	/* Write them back to reset them */
	target_mem_write32(t, CORTEXM_HFSR, hfsr);
	target_mem_write32(t, CORTEXM_CFSR, cfsr);
	/*
	 * We check for FORCED in the HardFault Status Register or
	 * for a configurable fault to avoid catching core resets
	 */
	if ((hfsr & CORTEXM_HFSR_FORCED) || cfsr) {
		/* Unwind exception */
		uint32_t regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];
		uint32_t stack[8];
		/* Read registers for post-exception stack pointer */
		target_regs_read(t, regs);
		/* save retcode currently in lr */
		const uint32_t retcode = regs[CORTEX_REG_LR];
		bool spsel = retcode & (1U << 2U);
		bool fpca = !(retcode & (1U << 4U));
		/* Read stack for pre-exception registers */
		uint32_t sp = spsel ? regs[CORTEX_REG_PSP] : regs[CORTEX_REG_MSP];
		target_mem_read(t, stack, sp, sizeof(stack));
		if (target_check_error(t))
			return 0;
		/* Restore LR and PC to their pre-exception states */
		regs[CORTEX_REG_LR] = stack[5];
		regs[CORTEX_REG_PC] = stack[6];

		/* Adjust stack to pop exception statem checking for basic vs extended exception frames */
		uint32_t framesize = fpca ? 0x68U : 0x20U;
		/* Check for stack alignment fixup */
		if (stack[7] & (1U << 9U))
			framesize += 4U;

		if (spsel) {
			regs[CORTEX_REG_SPECIAL] |= 0x4000000U;
			regs[CORTEX_REG_SP] = regs[CORTEX_REG_PSP] += framesize;
		} else
			regs[CORTEX_REG_SP] = regs[CORTEX_REG_MSP] += framesize;

		if (fpca)
			regs[CORTEX_REG_SPECIAL] |= 0x2000000U;

		/*
		 * FIXME: stack[7] contains xPSR when this is supported
		 * although, if we caught the exception it will be unchanged
		 */

		/* Reset exception state to allow resuming from restored state. */
		target_mem_write32(t, CORTEXM_AIRCR, CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_VECTCLRACTIVE);

		/* Write pre-exception registers back to core */
		target_regs_write(t, regs);

		return 1;
	}
	return 0;
}

bool cortexm_run_stub(target_s *t, uint32_t loadaddr, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3)
{
	uint32_t regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT] = {0};

	regs[0] = r0;
	regs[1] = r1;
	regs[2] = r2;
	regs[3] = r3;
	regs[15] = loadaddr;
	regs[CORTEX_REG_XPSR] = CORTEXM_XPSR_THUMB;
	regs[19] = 0;

	cortexm_regs_write(t, regs);

	if (target_check_error(t))
		return false;

	/* Execute the stub */
	target_halt_reason_e reason = TARGET_HALT_RUNNING;
#if defined(PLATFORM_HAS_DEBUG)
	uint32_t arm_regs_start[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];
	target_regs_read(t, arm_regs_start);
#endif
	cortexm_halt_resume(t, 0);
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 5000);
	while (reason == TARGET_HALT_RUNNING) {
		if (platform_timeout_is_expired(&timeout)) {
			cortexm_halt_request(t);
#if defined(PLATFORM_HAS_DEBUG)
			DEBUG_WARN("Stub hung\n");
			uint32_t arm_regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];
			target_regs_read(t, arm_regs);
			for (uint32_t i = 0; i < 20U; ++i)
				DEBUG_WARN("%2" PRIu32 ": %08" PRIx32 ", %08" PRIx32 "\n", i, arm_regs_start[i], arm_regs[i]);
#endif
			return false;
		}
		reason = cortexm_halt_poll(t, NULL);
	}

	if (reason == TARGET_HALT_ERROR)
		raise_exception(EXCEPTION_ERROR, "Target lost in stub");

	if (reason != TARGET_HALT_BREAKPOINT) {
		DEBUG_WARN(" Reason %d\n", reason);
		return false;
	}

	uint32_t pc = cortexm_pc_read(t);
	uint16_t bkpt_instr = target_mem_read16(t, pc);
	if (bkpt_instr >> 8U != 0xbeU)
		return false;

	return bkpt_instr & 0xffU;
}

/*
 * The following routines implement hardware breakpoints and watchpoints.
 * The Flash Patch and Breakpoint (FPB) and Data Watch and Trace (DWT)
 * systems are used.
 */

/*
 * DWT only supports powers of two as size. Convert length in bytes to
 * number of least-significant bits of the address to ignore during
 * match (maximum 31).
 */
static uint32_t dwt_mask(size_t len)
{
	if (len < 2)
		return 0;
	return MIN(ulog2(len - 1), 31);
}

static uint32_t dwt_func(target_s *t, target_breakwatch_e type)
{
	uint32_t x = 0;

	if ((t->target_options & TOPT_FLAVOUR_V6M) == 0)
		x = CORTEXM_DWT_FUNC_DATAVSIZE_WORD;

	switch (type) {
	case TARGET_WATCH_WRITE:
		return CORTEXM_DWT_FUNC_FUNC_WRITE | x;
	case TARGET_WATCH_READ:
		return CORTEXM_DWT_FUNC_FUNC_READ | x;
	case TARGET_WATCH_ACCESS:
		return CORTEXM_DWT_FUNC_FUNC_ACCESS | x;
	default:
		return -1;
	}
}

static int cortexm_breakwatch_set(target_s *t, breakwatch_s *bw)
{
	cortexm_priv_s *priv = t->priv;
	size_t i;
	uint32_t val = bw->addr;

	switch (bw->type) {
	case TARGET_BREAK_HARD:
		if (priv->flash_patch_revision == 0) {
			val &= 0x1ffffffcU;
			val |= (bw->addr & 2U) ? 0x80000000U : 0x40000000U;
		}
		val |= 1U;

		/* Find the first available breakpoint slot */
		for (i = 0; i < priv->base.breakpoints_available; i++) {
			if (!(priv->base.breakpoints_mask & (1U << i)))
				break;
		}

		if (i == priv->base.breakpoints_available)
			return -1;

		priv->base.breakpoints_mask |= 1U << i;
		target_mem_write32(t, CORTEXM_FPB_COMP(i), val);
		bw->reserved[0] = i;
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

		priv->base.watchpoints_mask |= 1U << i;

		target_mem_write32(t, CORTEXM_DWT_COMP(i), val);
		target_mem_write32(t, CORTEXM_DWT_MASK(i), dwt_mask(bw->size));
		target_mem_write32(t, CORTEXM_DWT_FUNC(i), dwt_func(t, bw->type));

		bw->reserved[0] = i;
		return 0;
	default:
		return 1;
	}
}

static int cortexm_breakwatch_clear(target_s *t, breakwatch_s *bw)
{
	cortexm_priv_s *priv = t->priv;
	unsigned i = bw->reserved[0];
	switch (bw->type) {
	case TARGET_BREAK_HARD:
		priv->base.breakpoints_mask &= ~(1U << i);
		target_mem_write32(t, CORTEXM_FPB_COMP(i), 0);
		return 0;
	case TARGET_WATCH_WRITE:
	case TARGET_WATCH_READ:
	case TARGET_WATCH_ACCESS:
		priv->base.watchpoints_mask &= ~(1U << i);
		target_mem_write32(t, CORTEXM_DWT_FUNC(i), 0);
		return 0;
	default:
		return 1;
	}
}

static target_addr_t cortexm_check_watch(target_s *t)
{
	cortexm_priv_s *priv = t->priv;
	unsigned i;

	for (i = 0; i < priv->base.watchpoints_available; i++) {
		/* if SET and MATCHED then break */
		if ((priv->base.watchpoints_mask & (1U << i)) &&
			(target_mem_read32(t, CORTEXM_DWT_FUNC(i)) & CORTEXM_DWT_FUNC_MATCHED))
			break;
	}

	if (i == priv->base.watchpoints_available)
		return 0;

	return target_mem_read32(t, CORTEXM_DWT_COMP(i));
}

static bool cortexm_vector_catch(target_s *t, int argc, const char **argv)
{
	cortexm_priv_s *priv = t->priv;
	static const char *const vectors[] = {"reset", NULL, NULL, NULL, "mm", "nocp", "chk", "stat", "bus", "int", "hard"};
	uint32_t tmp = 0;

	if (argc < 3)
		tc_printf(t, "usage: monitor vector_catch (enable|disable) (hard|int|bus|stat|chk|nocp|mm|reset)\n");
	else {
		for (int j = 0; j < argc; j++) {
			for (size_t i = 0; i < ARRAY_LENGTH(vectors); i++) {
				if (vectors[i] && !strcmp(vectors[i], argv[j]))
					tmp |= 1U << i;
			}
		}

		bool enable;
		if (parse_enable_or_disable(argv[1], &enable)) {
			if (enable)
				priv->demcr |= tmp;
			else
				priv->demcr &= ~tmp;

			target_mem_write32(t, CORTEXM_DEMCR, priv->demcr);
		}
	}

	tc_printf(t, "Catching vectors: ");
	for (size_t i = 0; i < ARRAY_LENGTH(vectors); i++) {
		if (!vectors[i])
			continue;
		if (priv->demcr & (1U << i))
			tc_printf(t, "%s ", vectors[i]);
	}
	tc_printf(t, "\n");
	return true;
}

#if PC_HOSTED == 0
static bool cortexm_redirect_stdout(target_s *t, int argc, const char **argv)
{
	if (argc == 1)
		gdb_outf("Semihosting stdout redirection: %s\n", t->stdout_redirected ? "enabled" : "disabled");
	else
		t->stdout_redirected = !strncmp(argv[1], "enable", strlen(argv[1]));
	return true;
}
#endif

#if PC_HOSTED == 0
/* probe memory access functions */
static void probe_mem_read(target_s *t __attribute__((unused)), void *probe_dest, target_addr_t target_src, size_t len)
{
	uint8_t *dst = (uint8_t *)probe_dest;
	uint8_t *src = (uint8_t *)target_src;

	DEBUG_INFO("probe_mem_read\n");

	memcpy(dst, src, len);
}

static void probe_mem_write(
	target_s *t __attribute__((unused)), target_addr_t target_dest, const void *probe_src, size_t len)
{
	uint8_t *dst = (uint8_t *)target_dest;
	uint8_t *src = (uint8_t *)probe_src;

	DEBUG_INFO("probe_mem_write\n");
	memcpy(dst, src, len);
}
#endif

static int cortexm_hostio_request(target_s *t)
{
	uint32_t arm_regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];
	uint32_t params[4] = {0};

	t->tc->interrupted = false;
	target_regs_read(t, arm_regs);
	uint32_t syscall = arm_regs[0];
	if (syscall != SEMIHOSTING_SYS_EXIT)
		target_mem_read(t, params, arm_regs[1], sizeof(params));
	int32_t ret = 0;

	DEBUG_INFO("syscall 0" PRIx32 "%" PRIx32 " (%" PRIx32 " %" PRIx32 " %" PRIx32 " %" PRIx32 ")\n", syscall, params[0],
		params[1], params[2], params[3]);
	switch (syscall) {
#if PC_HOSTED == 1

		/* code that runs in pc-hosted process. use linux system calls. */

	case SEMIHOSTING_SYS_OPEN: { /* open */
		target_addr_t fnam_taddr = params[0];
		uint32_t fnam_len = params[2];
		ret = -1;
		if (fnam_taddr == TARGET_NULL || fnam_len == 0)
			break;

		/* Translate stupid fopen modes to open flags.
		 * See DUI0471C, Table 8-3 */
		static const uint32_t flags[] = {
			O_RDONLY,                      /* r, rb */
			O_RDWR,                        /* r+, r+b */
			O_WRONLY | O_CREAT | O_TRUNC,  /*w*/
			O_RDWR | O_CREAT | O_TRUNC,    /*w+*/
			O_WRONLY | O_CREAT | O_APPEND, /*a*/
			O_RDWR | O_CREAT | O_APPEND,   /*a+*/
		};
		uint32_t pflag = flags[params[1] >> 1U];
		char filename[4];

		target_mem_read(t, filename, fnam_taddr, sizeof(filename));
		/* handle requests for console i/o */
		if (!strcmp(filename, ":tt")) {
			if (pflag == TARGET_O_RDONLY)
				ret = STDIN_FILENO;
			else if (pflag & TARGET_O_TRUNC)
				ret = STDOUT_FILENO;
			else
				ret = STDERR_FILENO;
			ret++;
			break;
		}

		char *fnam = malloc(fnam_len + 1U);
		if (fnam == NULL)
			break;
		target_mem_read(t, fnam, fnam_taddr, fnam_len + 1U);
		if (target_check_error(t)) {
			free(fnam);
			break;
		}
		fnam[fnam_len] = '\0';
		ret = open(fnam, pflag, 0644);
		free(fnam);
		if (ret != -1)
			ret++;
		break;
	}

	case SEMIHOSTING_SYS_CLOSE: /* close */
		ret = close(params[0] - 1);
		break;

	case SEMIHOSTING_SYS_READ: { /* read */
		ret = -1;
		target_addr_t buf_taddr = params[1];
		uint32_t buf_len = params[2];
		if (buf_taddr == TARGET_NULL)
			break;
		if (buf_len == 0) {
			ret = 0;
			break;
		}
		uint8_t *buf = malloc(buf_len);
		if (buf == NULL)
			break;
		ssize_t rc = read(params[0] - 1, buf, buf_len);
		if (rc >= 0)
			rc = buf_len - rc;
		target_mem_write(t, buf_taddr, buf, buf_len);
		free(buf);
		if (target_check_error(t))
			break;
		ret = rc;
		break;
	}

	case SEMIHOSTING_SYS_WRITE: { /* write */
		ret = -1;
		target_addr_t buf_taddr = params[1];
		uint32_t buf_len = params[2];
		if (buf_taddr == TARGET_NULL)
			break;
		if (buf_len == 0) {
			ret = 0;
			break;
		}
		uint8_t *buf = malloc(buf_len);
		if (buf == NULL)
			break;
		target_mem_read(t, buf, buf_taddr, buf_len);
		if (target_check_error(t)) {
			free(buf);
			break;
		}
		ret = write(params[0] - 1, buf, buf_len);
		free(buf);
		if (ret >= 0)
			ret = buf_len - ret;
		break;
	}

	case SEMIHOSTING_SYS_WRITEC: { /* writec */
		ret = -1;
		uint8_t ch;
		target_addr_t ch_taddr = arm_regs[1];
		if (ch_taddr == TARGET_NULL)
			break;
		ch = target_mem_read8(t, ch_taddr);
		if (target_check_error(t))
			break;
		fputc(ch, stderr);
		ret = 0;
		break;
	}

	case SEMIHOSTING_SYS_WRITE0: { /* write0 */
		ret = -1;
		target_addr_t str_addr = arm_regs[1];
		if (str_addr == TARGET_NULL)
			break;
		while (true) {
			const uint8_t str_c = target_mem_read8(t, str_addr++);
			if (target_check_error(t) || str_c == 0x00)
				break;
			fputc(str_c, stderr);
		}
		ret = 0;
		break;
	}

	case SEMIHOSTING_SYS_ISTTY: /* isatty */
		ret = isatty(params[0] - 1);
		break;

	case SEMIHOSTING_SYS_SEEK: { /* lseek */
		off_t pos = params[1];
		if (lseek(params[0] - 1, pos, SEEK_SET) == (off_t)pos)
			ret = 0;
		else
			ret = -1;
		break;
	}

	case SEMIHOSTING_SYS_RENAME: { /* rename */
		ret = -1;
		target_addr_t fnam1_taddr = params[0];
		uint32_t fnam1_len = params[1];
		if (fnam1_taddr == TARGET_NULL)
			break;
		if (fnam1_len == 0)
			break;
		target_addr_t fnam2_taddr = params[2];
		uint32_t fnam2_len = params[3];
		if (fnam2_taddr == TARGET_NULL)
			break;
		if (fnam2_len == 0)
			break;
		char *fnam1 = malloc(fnam1_len + 1U);
		if (fnam1 == NULL)
			break;
		target_mem_read(t, fnam1, fnam1_taddr, fnam1_len + 1U);
		if (target_check_error(t)) {
			free(fnam1);
			break;
		}
		fnam1[fnam1_len] = '\0';
		char *fnam2 = malloc(fnam2_len + 1U);
		if (fnam2 == NULL) {
			free(fnam1);
			break;
		}
		target_mem_read(t, fnam2, fnam2_taddr, fnam2_len + 1U);
		if (target_check_error(t)) {
			free(fnam1);
			free(fnam2);
			break;
		}
		fnam2[fnam2_len] = '\0';
		ret = rename(fnam1, fnam2);
		free(fnam1);
		free(fnam2);
		break;
	}

	case SEMIHOSTING_SYS_REMOVE: { /* unlink */
		ret = -1;
		target_addr_t fnam_taddr = params[0];
		if (fnam_taddr == TARGET_NULL)
			break;
		uint32_t fnam_len = params[1];
		if (fnam_len == 0)
			break;
		char *fnam = malloc(fnam_len + 1U);
		if (fnam == NULL)
			break;
		target_mem_read(t, fnam, fnam_taddr, fnam_len + 1U);
		if (target_check_error(t)) {
			free(fnam);
			break;
		}
		fnam[fnam_len] = '\0';
		ret = remove(fnam);
		free(fnam);
		break;
	}

	case SEMIHOSTING_SYS_SYSTEM: { /* system */
		ret = -1;
		target_addr_t cmd_taddr = params[0];
		if (cmd_taddr == TARGET_NULL)
			break;
		uint32_t cmd_len = params[1];
		if (cmd_len == 0)
			break;
		char *cmd = malloc(cmd_len + 1U);
		if (cmd == NULL)
			break;
		target_mem_read(t, cmd, cmd_taddr, cmd_len + 1U);
		if (target_check_error(t)) {
			free(cmd);
			break;
		}
		cmd[cmd_len] = '\0';
		ret = system(cmd);
		free(cmd);
		break;
	}

	case SEMIHOSTING_SYS_FLEN: { /* file length */
		ret = -1;
		struct stat stat_buf;
		if (fstat(params[0] - 1, &stat_buf) != 0)
			break;
		if (stat_buf.st_size > INT32_MAX)
			break;
		ret = stat_buf.st_size;
		break;
	}

	case SEMIHOSTING_SYS_CLOCK: { /* clock */
		/* can't use clock() because that would give cpu time of pc-hosted process */
		ret = -1;
		struct timeval timeval_buf;
		if (gettimeofday(&timeval_buf, NULL) != 0)
			break;
		uint32_t sec = timeval_buf.tv_sec;
		uint64_t usec = timeval_buf.tv_usec;
		if (time0_sec > sec)
			time0_sec = sec;
		sec -= time0_sec;
		uint64_t csec64 = (sec * UINT64_C(1000000) + usec) / UINT64_C(10000);
		uint32_t csec = csec64 & 0x7fffffffU;
		ret = csec;
		break;
	}

	case SEMIHOSTING_SYS_TIME: /* time */
		ret = time(NULL);
		break;

	case SEMIHOSTING_SYS_READC: /* readc */
		ret = getchar();
		break;

	case SEMIHOSTING_SYS_ERRNO: /* errno */
		ret = errno;
		break;

#else

		/* code that runs in probe. use gdb fileio calls. */

	case SEMIHOSTING_SYS_OPEN: { /* open */
		/* Translate stupid fopen modes to open flags.
		 * See DUI0471C, Table 8-3 */
		static const uint32_t flags[] = {
			TARGET_O_RDONLY,                                    /* r, rb */
			TARGET_O_RDWR,                                      /* r+, r+b */
			TARGET_O_WRONLY | TARGET_O_CREAT | TARGET_O_TRUNC,  /*w*/
			TARGET_O_RDWR | TARGET_O_CREAT | TARGET_O_TRUNC,    /*w+*/
			TARGET_O_WRONLY | TARGET_O_CREAT | TARGET_O_APPEND, /*a*/
			TARGET_O_RDWR | TARGET_O_CREAT | TARGET_O_APPEND,   /*a+*/
		};
		uint32_t pflag = flags[params[1] >> 1U];
		char filename[4];

		target_mem_read(t, filename, params[0], sizeof(filename));
		/* handle requests for console i/o */
		if (!strcmp(filename, ":tt")) {
			if (pflag == TARGET_O_RDONLY)
				ret = STDIN_FILENO;
			else if (pflag & TARGET_O_TRUNC)
				ret = STDOUT_FILENO;
			else
				ret = STDERR_FILENO;
			ret++;
			break;
		}

		ret = tc_open(t, params[0], params[2] + 1U, pflag, 0644);
		if (ret != -1)
			ret++;
		break;
	}

	case SEMIHOSTING_SYS_CLOSE: /* close */
		ret = tc_close(t, params[0] - 1);
		break;
	case SEMIHOSTING_SYS_READ: /* read */
		ret = tc_read(t, params[0] - 1, params[1], params[2]);
		if (ret >= 0)
			ret = params[2] - ret;
		break;
	case SEMIHOSTING_SYS_WRITE: /* write */
		ret = tc_write(t, params[0] - 1, params[1], params[2]);
		if (ret >= 0)
			ret = params[2] - ret;
		break;
	case SEMIHOSTING_SYS_WRITEC: /* writec */
		ret = tc_write(t, STDERR_FILENO, arm_regs[1], 1);
		break;
	case SEMIHOSTING_SYS_WRITE0: { /* write0 */
		ret = -1;
		target_addr_t str_begin = arm_regs[1];
		target_addr_t str_end = str_begin;
		while (target_mem_read8(t, str_end) != 0) {
			if (target_check_error(t))
				break;
			str_end++;
		}
		int len = str_end - str_begin;
		if (len != 0) {
			int rc = tc_write(t, STDERR_FILENO, str_begin, len);
			if (rc != len)
				break;
		}
		ret = 0;
		break;
	}
	case SEMIHOSTING_SYS_ISTTY: /* isatty */
		ret = tc_isatty(t, params[0] - 1);
		break;
	case SEMIHOSTING_SYS_SEEK: /* lseek */
		if (tc_lseek(t, params[0] - 1, params[1], TARGET_SEEK_SET) == (long)params[1])
			ret = 0;
		else
			ret = -1;
		break;
	case SEMIHOSTING_SYS_RENAME: /* rename */
		ret = tc_rename(t, params[0], params[1] + 1U, params[2], params[3] + 1U);
		break;
	case SEMIHOSTING_SYS_REMOVE: /* unlink */
		ret = tc_unlink(t, params[0], params[1] + 1U);
		break;
	case SEMIHOSTING_SYS_SYSTEM: /* system */
		/* before use first enable system calls with the following gdb command: 'set remote system-call-allowed 1' */
		ret = tc_system(t, params[0], params[1] + 1U);
		break;

	case SEMIHOSTING_SYS_FLEN: { /* file length */
		ret = -1;
		uint32_t fio_stat[16]; /* same size as fio_stat in gdb/include/gdb/fileio.h */
		//DEBUG("SYS_FLEN fio_stat addr %p\n", fio_stat);
		void (*saved_mem_read)(target_s *t, void *dest, target_addr_t src, size_t len);
		void (*saved_mem_write)(target_s *t, target_addr_t dest, const void *src, size_t len);
		saved_mem_read = t->mem_read;
		saved_mem_write = t->mem_write;
		t->mem_read = probe_mem_read;
		t->mem_write = probe_mem_write;
		int rc = tc_fstat(t, params[0] - 1, (target_addr_t)fio_stat); /* write fstat() result in fio_stat[] */
		t->mem_read = saved_mem_read;
		t->mem_write = saved_mem_write;
		if (rc)
			break;                           /* tc_fstat() failed */
		uint32_t fst_size_msw = fio_stat[7]; /* most significant 32 bits of fst_size in fio_stat */
		uint32_t fst_size_lsw = fio_stat[8]; /* least significant 32 bits of fst_size in fio_stat */
		if (fst_size_msw != 0)
			break;                             /* file size too large for int32_t return type */
		ret = __builtin_bswap32(fst_size_lsw); /* convert from bigendian to target order */
		if (ret < 0)
			ret = -1; /* file size too large for int32_t return type */
		break;
	}

	case SEMIHOSTING_SYS_CLOCK:  /* clock */
	case SEMIHOSTING_SYS_TIME: { /* time */
		/* use same code for SYS_CLOCK and SYS_TIME, more compact */
		ret = -1;

		struct __attribute__((packed, aligned(4))) {
			uint32_t ftv_sec;
			uint64_t ftv_usec;
		} fio_timeval;

		//DEBUG("SYS_TIME fio_timeval addr %p\n", &fio_timeval);
		void (*saved_mem_read)(target_s *t, void *dest, target_addr_t src, size_t len);
		void (*saved_mem_write)(target_s *t, target_addr_t dest, const void *src, size_t len);
		saved_mem_read = t->mem_read;
		saved_mem_write = t->mem_write;
		t->mem_read = probe_mem_read;
		t->mem_write = probe_mem_write;
		/* write gettimeofday() result in fio_timeval[] */
		int rc = tc_gettimeofday(t, (target_addr_t)&fio_timeval, (target_addr_t)NULL);
		t->mem_read = saved_mem_read;
		t->mem_write = saved_mem_write;
		if (rc) /* tc_gettimeofday() failed */
			break;
		/* convert from bigendian to target order */
		/* XXX: Replace this madness with endian-aware IO */
		uint32_t sec = __builtin_bswap32(fio_timeval.ftv_sec);
		uint64_t usec = __builtin_bswap64(fio_timeval.ftv_usec);
		if (syscall == SEMIHOSTING_SYS_TIME) /* SYS_TIME: time in seconds */
			ret = sec;
		else { /* SYS_CLOCK: time in hundredths of seconds */
			if (time0_sec > sec)
				time0_sec = sec; /* set sys_clock time origin */
			sec -= time0_sec;
			/* Cast down microseconds to avoid u64 division */
			uint32_t csec32 = ((uint32_t)usec / 10000U) + (sec * 100U);
			int32_t csec = csec32 & 0x7fffffffU;
			ret = csec;
		}
		break;
	}

	case SEMIHOSTING_SYS_READC: { /* readc */
		uint8_t ch = '?';
		//DEBUG("SYS_READC ch addr %p\n", &ch);
		void (*saved_mem_read)(target_s *t, void *dest, target_addr_t src, size_t len);
		void (*saved_mem_write)(target_s *t, target_addr_t dest, const void *src, size_t len);
		saved_mem_read = t->mem_read;
		saved_mem_write = t->mem_write;
		t->mem_read = probe_mem_read;
		t->mem_write = probe_mem_write;
		int rc = tc_read(t, STDIN_FILENO, (target_addr_t)&ch, 1); /* read a character in ch */
		t->mem_read = saved_mem_read;
		t->mem_write = saved_mem_write;
		if (rc == 1)
			ret = ch;
		else
			ret = -1;
		break;
	}

	case SEMIHOSTING_SYS_ERRNO: /* Return last errno from GDB */
		ret = t->tc->errno_;
		break;
#endif

	case SEMIHOSTING_SYS_EXIT: /* _exit() */
		tc_printf(t, "_exit(0x%x)\n", arm_regs[1]);
		target_halt_resume(t, 1);
		break;

	case SEMIHOSTING_SYS_EXIT_EXTENDED:                          /* _exit() */
		tc_printf(t, "_exit(0x%x%08x)\n", params[1], params[0]); /* exit() with 64bit exit value */
		target_halt_resume(t, 1);
		break;

	case SEMIHOSTING_SYS_GET_CMDLINE: { /* get_cmdline */
		uint32_t retval[2];
		ret = -1;
		target_addr_t buf_ptr = params[0];
		target_addr_t buf_len = params[1];
		if (strlen(t->cmdline) + 1U > buf_len)
			break;
		if (target_mem_write(t, buf_ptr, t->cmdline, strlen(t->cmdline) + 1U))
			break;
		retval[0] = buf_ptr;
		retval[1] = strlen(t->cmdline) + 1U;
		if (target_mem_write(t, arm_regs[1], retval, sizeof(retval)))
			break;
		ret = 0;
		break;
	}

	case SEMIHOSTING_SYS_ISERROR: { /* iserror */
		int error = params[0];
		ret = error == TARGET_EPERM || error == TARGET_ENOENT || error == TARGET_EINTR || error == TARGET_EIO ||
			error == TARGET_EBADF || error == TARGET_EACCES || error == TARGET_EFAULT || error == TARGET_EBUSY ||
			error == TARGET_EEXIST || error == TARGET_ENODEV || error == TARGET_ENOTDIR || error == TARGET_EISDIR ||
			error == TARGET_EINVAL || error == TARGET_ENFILE || error == TARGET_EMFILE || error == TARGET_EFBIG ||
			error == TARGET_ENOSPC || error == TARGET_ESPIPE || error == TARGET_EROFS || error == TARGET_ENOSYS ||
			error == TARGET_ENAMETOOLONG || error == TARGET_EUNKNOWN;
		break;
	}

	case SEMIHOSTING_SYS_HEAPINFO:                                           /* heapinfo */
		target_mem_write(t, arm_regs[1], &t->heapinfo, sizeof(t->heapinfo)); /* See newlib/libc/sys/arm/crt0.S */
		break;

	case SEMIHOSTING_SYS_TMPNAM: { /* tmpnam */
		/* Given a target identifier between 0 and 255, returns a temporary name */
		target_addr_t buf_ptr = params[0];
		int target_id = params[1];
		int buf_size = params[2];
		char fnam[] = "tempXX.tmp";
		ret = -1;
		if (buf_ptr == 0)
			break;
		if (buf_size <= 0)
			break;
		if ((target_id < 0) || (target_id > 255))
			break;                         /* target id out of range */
		fnam[5] = 'A' + (target_id & 0xf); /* create filename */
		fnam[4] = 'A' + (target_id >> 4 & 0xf);
		if (strlen(fnam) + 1U > (uint32_t)buf_size)
			break; /* target buffer too small */
		if (target_mem_write(t, buf_ptr, fnam, strlen(fnam) + 1U))
			break; /* copy filename to target */
		ret = 0;
		break;
	}

	// not implemented yet:
	case SEMIHOSTING_SYS_ELAPSED:  /* elapsed */
	case SEMIHOSTING_SYS_TICKFREQ: /* tickfreq */
		ret = -1;
		break;
	}

	arm_regs[0] = ret;
	target_regs_write(t, arm_regs);

	return t->tc->interrupted;
}
