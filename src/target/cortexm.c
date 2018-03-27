/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
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
 * the Cortex-M3 core.  This should be generic to ARMv7-M as it is
 * implemented according to the "ARMv7-M Architectue Reference Manual",
 * ARM doc DDI0403C.
 *
 * Also supports Cortex-M0 / ARMv6-M
 */
#include "general.h"
#include "exception.h"
#include "adiv5.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#include <unistd.h>

static const char cortexm_driver_str[] = "ARM Cortex-M";

static bool cortexm_vector_catch(target *t, int argc, char *argv[]);

const struct command_s cortexm_cmd_list[] = {
	{"vector_catch", (cmd_handler)cortexm_vector_catch, "Catch exception vectors"},
	{NULL, NULL, NULL}
};

/* target options recognised by the Cortex-M target */
#define	TOPT_FLAVOUR_V6M	(1<<0)	/* if not set, target is assumed to be v7m */
#define	TOPT_FLAVOUR_V7MF	(1<<1)	/* if set, floating-point enabled. */

static void cortexm_regs_read(target *t, void *data);
static void cortexm_regs_write(target *t, const void *data);
static uint32_t cortexm_pc_read(target *t);

static void cortexm_reset(target *t);
static enum target_halt_reason cortexm_halt_poll(target *t, target_addr *watch);
static void cortexm_halt_request(target *t);
static int cortexm_fault_unwind(target *t);

static int cortexm_breakwatch_set(target *t, struct breakwatch *);
static int cortexm_breakwatch_clear(target *t, struct breakwatch *);
static target_addr cortexm_check_watch(target *t);

#define CORTEXM_MAX_WATCHPOINTS	4	/* architecture says up to 15, no implementation has > 4 */
#define CORTEXM_MAX_BREAKPOINTS	6	/* architecture says up to 127, no implementation has > 6 */

static int cortexm_hostio_request(target *t);

struct cortexm_priv {
	ADIv5_AP_t *ap;
	bool stepping;
	bool on_bkpt;
	/* Watchpoint unit status */
	bool hw_watchpoint[CORTEXM_MAX_WATCHPOINTS];
	unsigned flash_patch_revision;
	unsigned hw_watchpoint_max;
	/* Breakpoint unit status */
	bool hw_breakpoint[CORTEXM_MAX_BREAKPOINTS];
	unsigned hw_breakpoint_max;
	/* Copy of DEMCR for vector-catch */
	uint32_t demcr;
	/* Cache parameters */
	bool has_cache;
	uint32_t dcache_minline;
};

/* Register number tables */
static const uint32_t regnum_cortex_m[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,	/* standard r0-r15 */
	0x10,	/* xpsr */
	0x11,	/* msp */
	0x12,	/* psp */
	0x14	/* special */
};

static const uint32_t regnum_cortex_mf[] = {
	0x21,	/* fpscr */
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,	/* s0-s7 */
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,	/* s8-s15 */
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,	/* s16-s23 */
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,	/* s24-s31 */
};

static const char tdesc_cortex_m[] =
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
	"</target>";

static const char tdesc_cortex_mf[] =
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
	"</target>";

ADIv5_AP_t *cortexm_ap(target *t)
{
	return ((struct cortexm_priv *)t->priv)->ap;
}

static void cortexm_cache_clean(target *t, target_addr addr, size_t len, bool invalidate)
{
	struct cortexm_priv *priv = t->priv;
	if (!priv->has_cache || (priv->dcache_minline == 0))
		return;
	uint32_t cache_reg = invalidate ? CORTEXM_DCCIMVAC : CORTEXM_DCCMVAC;
	size_t minline = priv->dcache_minline;

	/* flush data cache for RAM regions that intersect requested region */
	target_addr mem_end = addr + len; /* following code is NOP if wraparound */
	/* requested region is [src, src_end) */
	for (struct target_ram *r = t->ram; r; r = r->next) {
		target_addr ram = r->start;
		target_addr ram_end = r->start + r->length;
		/* RAM region is [ram, ram_end) */
		if (addr > ram)
			ram = addr;
		if (mem_end < ram_end)
			ram_end = mem_end;
		/* intersection is [ram, ram_end) */
		for (ram &= ~(minline-1); ram < ram_end; ram += minline)
			adiv5_mem_write(cortexm_ap(t), cache_reg, &ram, 4);
	}
}

static void cortexm_mem_read(target *t, void *dest, target_addr src, size_t len)
{
	cortexm_cache_clean(t, src, len, false);
	adiv5_mem_read(cortexm_ap(t), dest, src, len);
}

static void cortexm_mem_write(target *t, target_addr dest, const void *src, size_t len)
{
	cortexm_cache_clean(t, dest, len, true);
	adiv5_mem_write(cortexm_ap(t), dest, src, len);
}

static bool cortexm_check_error(target *t)
{
	ADIv5_AP_t *ap = cortexm_ap(t);
	return adiv5_dp_error(ap->dp) != 0;
}

static void cortexm_priv_free(void *priv)
{
	adiv5_ap_unref(((struct cortexm_priv *)priv)->ap);
	free(priv);
}

bool cortexm_probe(ADIv5_AP_t *ap)
{
	target *t;

	t = target_new();
	adiv5_ap_ref(ap);
	struct cortexm_priv *priv = calloc(1, sizeof(*priv));
	t->priv = priv;
	t->priv_free = cortexm_priv_free;
	priv->ap = ap;

	t->check_error = cortexm_check_error;
	t->mem_read = cortexm_mem_read;
	t->mem_write = cortexm_mem_write;

	t->driver = cortexm_driver_str;

	t->attach = cortexm_attach;
	t->detach = cortexm_detach;

	/* Should probe here to make sure it's Cortex-M3 */
	t->tdesc = tdesc_cortex_m;
	t->regs_read = cortexm_regs_read;
	t->regs_write = cortexm_regs_write;

	t->reset = cortexm_reset;
	t->halt_request = cortexm_halt_request;
	t->halt_poll = cortexm_halt_poll;
	t->halt_resume = cortexm_halt_resume;
	t->regs_size = sizeof(regnum_cortex_m);

	t->breakwatch_set = cortexm_breakwatch_set;
	t->breakwatch_clear = cortexm_breakwatch_clear;

	target_add_commands(t, cortexm_cmd_list, cortexm_driver_str);

	/* Probe for FP extension */
	uint32_t cpacr = target_mem_read32(t, CORTEXM_CPACR);
	cpacr |= 0x00F00000; /* CP10 = 0b11, CP11 = 0b11 */
	target_mem_write32(t, CORTEXM_CPACR, cpacr);
	if (target_mem_read32(t, CORTEXM_CPACR) == cpacr) {
		t->target_options |= TOPT_FLAVOUR_V7MF;
		t->regs_size += sizeof(regnum_cortex_mf);
		t->tdesc = tdesc_cortex_mf;
	}

	/* Default vectors to catch */
	priv->demcr = CORTEXM_DEMCR_TRCENA | CORTEXM_DEMCR_VC_HARDERR |
			CORTEXM_DEMCR_VC_CORERESET;

	/* Check cache type */
	uint32_t ctr = target_mem_read32(t, CORTEXM_CTR);
	if ((ctr >> 29) == 4) {
		priv->has_cache = true;
		priv->dcache_minline = 4 << (ctr & 0xf);
	} else {
		target_check_error(t);
	}

#define PROBE(x) \
	do { if ((x)(t)) return true; else target_check_error(t); } while (0)

	PROBE(stm32f1_probe);
	PROBE(stm32f4_probe);
	PROBE(stm32l0_probe);   /* STM32L0xx & STM32L1xx */
	PROBE(stm32l4_probe);
	PROBE(lpc11xx_probe);
	PROBE(lpc15xx_probe);
	PROBE(lpc17xx_probe);
	PROBE(lpc43xx_probe);
	PROBE(sam3x_probe);
	PROBE(sam4l_probe);
	PROBE(nrf51_probe);
	PROBE(samd_probe);
	PROBE(lmi_probe);
	PROBE(kinetis_probe);
	PROBE(efm32_probe);
#undef PROBE

	return true;
}

bool cortexm_attach(target *t)
{
	struct cortexm_priv *priv = t->priv;
	unsigned i;
	uint32_t r;
	int tries;

	/* Clear any pending fault condition */
	target_check_error(t);

	target_halt_request(t);
	tries = 10;
	while(!platform_srst_get_val() && !target_halt_poll(t, NULL) && --tries)
		platform_delay(200);
	if(!tries)
		return false;

	/* Request halt on reset */
	target_mem_write32(t, CORTEXM_DEMCR, priv->demcr);

	/* Reset DFSR flags */
	target_mem_write32(t, CORTEXM_DFSR, CORTEXM_DFSR_RESETALL);

	/* size the break/watchpoint units */
	priv->hw_breakpoint_max = CORTEXM_MAX_BREAKPOINTS;
	r = target_mem_read32(t, CORTEXM_FPB_CTRL);
	if (((r >> 4) & 0xf) < priv->hw_breakpoint_max)	/* only look at NUM_COMP1 */
		priv->hw_breakpoint_max = (r >> 4) & 0xf;
	priv->flash_patch_revision = (r >> 28);
	priv->hw_watchpoint_max = CORTEXM_MAX_WATCHPOINTS;
	r = target_mem_read32(t, CORTEXM_DWT_CTRL);
	if ((r >> 28) > priv->hw_watchpoint_max)
		priv->hw_watchpoint_max = r >> 28;

	/* Clear any stale breakpoints */
	for(i = 0; i < priv->hw_breakpoint_max; i++) {
		target_mem_write32(t, CORTEXM_FPB_COMP(i), 0);
		priv->hw_breakpoint[i] = 0;
	}

	/* Clear any stale watchpoints */
	for(i = 0; i < priv->hw_watchpoint_max; i++) {
		target_mem_write32(t, CORTEXM_DWT_FUNC(i), 0);
		priv->hw_watchpoint[i] = 0;
	}

	/* Flash Patch Control Register: set ENABLE */
	target_mem_write32(t, CORTEXM_FPB_CTRL,
			CORTEXM_FPB_CTRL_KEY | CORTEXM_FPB_CTRL_ENABLE);

	platform_srst_set_val(false);

	return true;
}

void cortexm_detach(target *t)
{
	struct cortexm_priv *priv = t->priv;
	unsigned i;

	/* Clear any stale breakpoints */
	for(i = 0; i < priv->hw_breakpoint_max; i++)
		target_mem_write32(t, CORTEXM_FPB_COMP(i), 0);

	/* Clear any stale watchpoints */
	for(i = 0; i < priv->hw_watchpoint_max; i++)
		target_mem_write32(t, CORTEXM_DWT_FUNC(i), 0);

	/* Disable debug */
	target_mem_write32(t, CORTEXM_DHCSR, CORTEXM_DHCSR_DBGKEY);
}

enum { DB_DHCSR, DB_DCRSR, DB_DCRDR, DB_DEMCR };

static void cortexm_regs_read(target *t, void *data)
{
	ADIv5_AP_t *ap = cortexm_ap(t);
	uint32_t *regs = data;
	unsigned i;

	/* FIXME: Describe what's really going on here */
	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw | ADIV5_AP_CSW_SIZE_WORD);

	/* Map the banked data registers (0x10-0x1c) to the
	 * debug registers DHCSR, DCRSR, DCRDR and DEMCR respectively */
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_TAR, CORTEXM_DHCSR);

	/* Walk the regnum_cortex_m array, reading the registers it
	 * calls out. */
	adiv5_ap_write(ap, ADIV5_AP_DB(DB_DCRSR), regnum_cortex_m[0]); /* Required to switch banks */
	*regs++ = adiv5_dp_read(ap->dp, ADIV5_AP_DB(DB_DCRDR));
	for(i = 1; i < sizeof(regnum_cortex_m) / 4; i++) {
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DB(DB_DCRSR),
		                    regnum_cortex_m[i]);
		*regs++ = adiv5_dp_read(ap->dp, ADIV5_AP_DB(DB_DCRDR));
	}
	if (t->target_options & TOPT_FLAVOUR_V7MF)
		for(i = 0; i < sizeof(regnum_cortex_mf) / 4; i++) {
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE,
			                    ADIV5_AP_DB(DB_DCRSR),
			                    regnum_cortex_mf[i]);
			*regs++ = adiv5_dp_read(ap->dp, ADIV5_AP_DB(DB_DCRDR));
		}
}

static void cortexm_regs_write(target *t, const void *data)
{
	ADIv5_AP_t *ap = cortexm_ap(t);
	const uint32_t *regs = data;
	unsigned i;

	/* FIXME: Describe what's really going on here */
	adiv5_ap_write(ap, ADIV5_AP_CSW, ap->csw | ADIV5_AP_CSW_SIZE_WORD);

	/* Map the banked data registers (0x10-0x1c) to the
	 * debug registers DHCSR, DCRSR, DCRDR and DEMCR respectively */
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_TAR, CORTEXM_DHCSR);

	/* Walk the regnum_cortex_m array, writing the registers it
	 * calls out. */
	adiv5_ap_write(ap, ADIV5_AP_DB(DB_DCRDR), *regs++); /* Required to switch banks */
	adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DB(DB_DCRSR),
	                    0x10000 | regnum_cortex_m[0]);
	for(i = 1; i < sizeof(regnum_cortex_m) / 4; i++) {
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE,
		                    ADIV5_AP_DB(DB_DCRDR), *regs++);
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DB(DB_DCRSR),
		                    0x10000 | regnum_cortex_m[i]);
	}
	if (t->target_options & TOPT_FLAVOUR_V7MF)
		for(i = 0; i < sizeof(regnum_cortex_mf) / 4; i++) {
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE,
			                    ADIV5_AP_DB(DB_DCRDR), *regs++);
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE,
			                    ADIV5_AP_DB(DB_DCRSR),
			                    0x10000 | regnum_cortex_mf[i]);
		}
}

static uint32_t cortexm_pc_read(target *t)
{
	target_mem_write32(t, CORTEXM_DCRSR, 0x0F);
	return target_mem_read32(t, CORTEXM_DCRDR);
}

static void cortexm_pc_write(target *t, const uint32_t val)
{
	target_mem_write32(t, CORTEXM_DCRDR, val);
	target_mem_write32(t, CORTEXM_DCRSR, CORTEXM_DCRSR_REGWnR | 0x0F);
}

/* The following three routines implement target halt/resume
 * using the core debug registers in the NVIC. */
static void cortexm_reset(target *t)
{
	if ((t->target_options & CORTEXM_TOPT_INHIBIT_SRST) == 0) {
		platform_srst_set_val(true);
		platform_srst_set_val(false);
	}

	/* Read DHCSR here to clear S_RESET_ST bit before reset */
	target_mem_read32(t, CORTEXM_DHCSR);

	/* Request system reset from NVIC: SRST doesn't work correctly */
	/* This could be VECTRESET: 0x05FA0001 (reset only core)
	 *          or SYSRESETREQ: 0x05FA0004 (system reset)
	 */
	target_mem_write32(t, CORTEXM_AIRCR,
	                   CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_SYSRESETREQ);

	/* If target needs to do something extra (see Atmel SAM4L for example) */
	if (t->extended_reset != NULL) {
		t->extended_reset(t);
	}

	/* Poll for release from reset */
	while (target_mem_read32(t, CORTEXM_DHCSR) & CORTEXM_DHCSR_S_RESET_ST);

	/* Reset DFSR flags */
	target_mem_write32(t, CORTEXM_DFSR, CORTEXM_DFSR_RESETALL);
}

static void cortexm_halt_request(target *t)
{
	volatile struct exception e;
	TRY_CATCH (e, EXCEPTION_TIMEOUT) {
		target_mem_write32(t, CORTEXM_DHCSR, CORTEXM_DHCSR_DBGKEY |
		                                          CORTEXM_DHCSR_C_HALT |
		                                          CORTEXM_DHCSR_C_DEBUGEN);
	}
	if (e.type) {
		tc_printf(t, "Timeout sending interrupt, is target in WFI?\n");
	}
}

static enum target_halt_reason cortexm_halt_poll(target *t, target_addr *watch)
{
	struct cortexm_priv *priv = t->priv;

	volatile uint32_t dhcsr = 0;
	volatile struct exception e;
	TRY_CATCH (e, EXCEPTION_ALL) {
		/* If this times out because the target is in WFI then
		 * the target is still running. */
		dhcsr = target_mem_read32(t, CORTEXM_DHCSR);
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

	if (!(dhcsr & CORTEXM_DHCSR_S_HALT))
		return TARGET_HALT_RUNNING;

	/* We've halted.  Let's find out why. */
	uint32_t dfsr = target_mem_read32(t, CORTEXM_DFSR);
	target_mem_write32(t, CORTEXM_DFSR, dfsr); /* write back to reset */

	if ((dfsr & CORTEXM_DFSR_VCATCH) && cortexm_fault_unwind(t))
		return TARGET_HALT_FAULT;

	/* Remember if we stopped on a breakpoint */
	priv->on_bkpt = dfsr & (CORTEXM_DFSR_BKPT);
	if (priv->on_bkpt) {
		/* If we've hit a programmed breakpoint, check for semihosting
		 * call. */
		uint32_t pc = cortexm_pc_read(t);
		uint16_t bkpt_instr;
		bkpt_instr = target_mem_read16(t, pc);
		if (bkpt_instr == 0xBEAB) {
			if (cortexm_hostio_request(t)) {
				return TARGET_HALT_REQUEST;
			} else {
				target_halt_resume(t, priv->stepping);
				return 0;
			}
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

void cortexm_halt_resume(target *t, bool step)
{
	struct cortexm_priv *priv = t->priv;
	uint32_t dhcsr = CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_DEBUGEN;

	if (step)
		dhcsr |= CORTEXM_DHCSR_C_STEP | CORTEXM_DHCSR_C_MASKINTS;

	/* Disable interrupts while single stepping... */
	if(step != priv->stepping) {
		target_mem_write32(t, CORTEXM_DHCSR, dhcsr | CORTEXM_DHCSR_C_HALT);
		priv->stepping = step;
	}

	if (priv->on_bkpt) {
		uint32_t pc = cortexm_pc_read(t);
		if ((target_mem_read16(t, pc) & 0xFF00) == 0xBE00)
			cortexm_pc_write(t, pc + 2);
	}

	if (priv->has_cache)
		target_mem_write32(t, CORTEXM_ICIALLU, 0);

	target_mem_write32(t, CORTEXM_DHCSR, dhcsr);
}

static int cortexm_fault_unwind(target *t)
{
	uint32_t hfsr = target_mem_read32(t, CORTEXM_HFSR);
	uint32_t cfsr = target_mem_read32(t, CORTEXM_CFSR);
	target_mem_write32(t, CORTEXM_HFSR, hfsr);/* write back to reset */
	target_mem_write32(t, CORTEXM_CFSR, cfsr);/* write back to reset */
	/* We check for FORCED in the HardFault Status Register or
	 * for a configurable fault to avoid catching core resets */
	if((hfsr & CORTEXM_HFSR_FORCED) || cfsr) {
		/* Unwind exception */
		uint32_t regs[t->regs_size / 4];
		uint32_t stack[8];
		uint32_t retcode, framesize;
		/* Read registers for post-exception stack pointer */
		target_regs_read(t, regs);
		/* save retcode currently in lr */
		retcode = regs[REG_LR];
		bool spsel = retcode & (1<<2);
		bool fpca = !(retcode & (1<<4));
		/* Read stack for pre-exception registers */
		uint32_t sp = spsel ? regs[REG_PSP] : regs[REG_MSP];
		target_mem_read(t, stack, sp, sizeof(stack));
		if (target_check_error(t))
			return 0;
		regs[REG_LR] = stack[5];	/* restore LR to pre-exception state */
		regs[REG_PC] = stack[6];	/* restore PC to pre-exception state */

		/* adjust stack to pop exception state */
		framesize = fpca ? 0x68 : 0x20;	/* check for basic vs. extended frame */
		if (stack[7] & (1<<9))				/* check for stack alignment fixup */
			framesize += 4;

		if (spsel) {
			regs[REG_SPECIAL] |= 0x4000000;
			regs[REG_SP] = regs[REG_PSP] += framesize;
		} else {
			regs[REG_SP] = regs[REG_MSP] += framesize;
		}

		if (fpca)
			regs[REG_SPECIAL] |= 0x2000000;

		/* FIXME: stack[7] contains xPSR when this is supported */
		/* although, if we caught the exception it will be unchanged */

		/* Reset exception state to allow resuming from restored
		 * state.
		 */
		target_mem_write32(t, CORTEXM_AIRCR,
				CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_VECTCLRACTIVE);

		/* Write pre-exception registers back to core */
		target_regs_write(t, regs);

		return 1;
	}
	return 0;
}

int cortexm_run_stub(target *t, uint32_t loadaddr,
                     uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3)
{
	uint32_t regs[t->regs_size / 4];

	memset(regs, 0, sizeof(regs));
	regs[0] = r0;
	regs[1] = r1;
	regs[2] = r2;
	regs[3] = r3;
	regs[15] = loadaddr;
	regs[16] = 0x1000000;
	regs[19] = 0;

	cortexm_regs_write(t, regs);

	if (target_check_error(t))
		return -1;

	/* Execute the stub */
	enum target_halt_reason reason;
	cortexm_halt_resume(t, 0);
	while ((reason = cortexm_halt_poll(t, NULL)) == TARGET_HALT_RUNNING)
		;

	if (reason == TARGET_HALT_ERROR)
		raise_exception(EXCEPTION_ERROR, "Target lost in stub");

	if (reason != TARGET_HALT_BREAKPOINT)
		return -2;

	uint32_t pc = cortexm_pc_read(t);
	uint16_t bkpt_instr = target_mem_read16(t, pc);
	if (bkpt_instr >> 8 != 0xbe)
		return -2;

	return bkpt_instr & 0xff;
}

/* The following routines implement hardware breakpoints and watchpoints.
 * The Flash Patch and Breakpoint (FPB) and Data Watch and Trace (DWT)
 * systems are used. */

static uint32_t dwt_mask(size_t len)
{
	switch (len) {
	case 1:
		return CORTEXM_DWT_MASK_BYTE;
	case 2:
		return CORTEXM_DWT_MASK_HALFWORD;
	case 4:
		return CORTEXM_DWT_MASK_WORD;
	default:
		return -1;
	}
}

static uint32_t dwt_func(target *t, enum target_breakwatch type)
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

static int cortexm_breakwatch_set(target *t, struct breakwatch *bw)
{
	struct cortexm_priv *priv = t->priv;
	unsigned i;
	uint32_t val = bw->addr;

	switch (bw->type) {
	case TARGET_BREAK_HARD:
		if (priv->flash_patch_revision == 0) {
			val &= 0x1FFFFFFC;
			val |= (bw->addr & 2)?0x80000000:0x40000000;
		}
		val |= 1;

		for(i = 0; i < priv->hw_breakpoint_max; i++)
			if (!priv->hw_breakpoint[i])
				break;

		if (i == priv->hw_breakpoint_max)
			return -1;

		priv->hw_breakpoint[i] = true;
		target_mem_write32(t, CORTEXM_FPB_COMP(i), val);
		bw->reserved[0] = i;
		return 0;

	case TARGET_WATCH_WRITE:
	case TARGET_WATCH_READ:
	case TARGET_WATCH_ACCESS:
		for(i = 0; i < priv->hw_watchpoint_max; i++)
			if (!priv->hw_watchpoint[i])
				break;

		if (i == priv->hw_watchpoint_max)
			return -1;

		priv->hw_watchpoint[i] = true;

		target_mem_write32(t, CORTEXM_DWT_COMP(i), val);
		target_mem_write32(t, CORTEXM_DWT_MASK(i), dwt_mask(bw->size));
		target_mem_write32(t, CORTEXM_DWT_FUNC(i), dwt_func(t, bw->type));

		bw->reserved[0] = i;
		return 0;
	default:
		return 1;
	}
}

static int cortexm_breakwatch_clear(target *t, struct breakwatch *bw)
{
	struct cortexm_priv *priv = t->priv;
	unsigned i = bw->reserved[0];
	switch (bw->type) {
	case TARGET_BREAK_HARD:
		priv->hw_breakpoint[i] = false;
		target_mem_write32(t, CORTEXM_FPB_COMP(i), 0);
		return 0;
	case TARGET_WATCH_WRITE:
	case TARGET_WATCH_READ:
	case TARGET_WATCH_ACCESS:
		priv->hw_watchpoint[i] = false;
		target_mem_write32(t, CORTEXM_DWT_FUNC(i), 0);
		return 0;
	default:
		return 1;
	}
}

static target_addr cortexm_check_watch(target *t)
{
	struct cortexm_priv *priv = t->priv;
	unsigned i;

	for(i = 0; i < priv->hw_watchpoint_max; i++)
		/* if SET and MATCHED then break */
		if(priv->hw_watchpoint[i] &&
		   (target_mem_read32(t, CORTEXM_DWT_FUNC(i)) &
					CORTEXM_DWT_FUNC_MATCHED))
			break;

	if (i == priv->hw_watchpoint_max)
		return 0;

	return target_mem_read32(t, CORTEXM_DWT_COMP(i));
}

static bool cortexm_vector_catch(target *t, int argc, char *argv[])
{
	struct cortexm_priv *priv = t->priv;
	const char *vectors[] = {"reset", NULL, NULL, NULL, "mm", "nocp",
				"chk", "stat", "bus", "int", "hard"};
	uint32_t tmp = 0;
	unsigned i;

	if ((argc < 3) || ((argv[1][0] != 'e') && (argv[1][0] != 'd'))) {
		tc_printf(t, "usage: monitor vector_catch (enable|disable) "
			     "(hard|int|bus|stat|chk|nocp|mm|reset)\n");
	} else {
		for (int j = 0; j < argc; j++)
			for (i = 0; i < sizeof(vectors) / sizeof(char*); i++) {
				if (vectors[i] && !strcmp(vectors[i], argv[j]))
					tmp |= 1 << i;
			}

		if (argv[1][0] == 'e')
			priv->demcr |= tmp;
		else
			priv->demcr &= ~tmp;

		target_mem_write32(t, CORTEXM_DEMCR, priv->demcr);
	}

	tc_printf(t, "Catching vectors: ");
	for (i = 0; i < sizeof(vectors) / sizeof(char*); i++) {
		if (!vectors[i])
			continue;
		if (priv->demcr & (1 << i))
			tc_printf(t, "%s ", vectors[i]);
	}
	tc_printf(t, "\n");
	return true;
}

/* Windows defines this with some other meaning... */
#ifdef SYS_OPEN
#	undef SYS_OPEN
#endif

/* Semihosting support */
/* ARM Semihosting syscall numbers, from ARM doc DUI0471C, Chapter 8 */
#define SYS_CLOSE	0x02
#define SYS_CLOCK	0x10
#define SYS_ELAPSED	0x30
#define SYS_ERRNO	0x13
#define SYS_FLEN	0x0C
#define SYS_GET_CMDLINE	0x15
#define SYS_HEAPINFO	0x16
#define SYS_ISERROR	0x08
#define SYS_ISTTY	0x09
#define SYS_OPEN	0x01
#define SYS_READ	0x06
#define SYS_READC	0x07
#define SYS_REMOVE	0x0E
#define SYS_RENAME	0x0F
#define SYS_SEEK	0x0A
#define SYS_SYSTEM	0x12
#define SYS_TICKFREQ	0x31
#define SYS_TIME	0x11
#define SYS_TMPNAM	0x0D
#define SYS_WRITE	0x05
#define SYS_WRITEC	0x03
#define SYS_WRITE0	0x04

static int cortexm_hostio_request(target *t)
{
	uint32_t arm_regs[t->regs_size];
	uint32_t params[4];

	t->tc->interrupted = false;
	target_regs_read(t, arm_regs);
	target_mem_read(t, params, arm_regs[1], sizeof(params));
	uint32_t syscall = arm_regs[0];
	int32_t ret = 0;

	DEBUG("syscall 0"PRIx32"%"PRIx32" (%"PRIx32" %"PRIx32" %"PRIx32" %"PRIx32")\n",
              syscall, params[0], params[1], params[2], params[3]);
	switch (syscall) {
	case SYS_OPEN:{	/* open */
		/* Translate stupid fopen modes to open flags.
		 * See DUI0471C, Table 8-3 */
		const uint32_t flags[] = {
			TARGET_O_RDONLY,	/* r, rb */
			TARGET_O_RDWR,		/* r+, r+b */
			TARGET_O_WRONLY | TARGET_O_CREAT | TARGET_O_TRUNC,/*w*/
			TARGET_O_RDWR | TARGET_O_CREAT | TARGET_O_TRUNC,/*w+*/
			TARGET_O_WRONLY | TARGET_O_CREAT | TARGET_O_APPEND,/*a*/
			TARGET_O_RDWR | TARGET_O_CREAT | TARGET_O_APPEND,/*a+*/
		};
		uint32_t pflag = flags[params[1] >> 1];
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

		ret = tc_open(t, params[0], params[2] + 1, pflag, 0644);
		if (ret != -1)
			ret++;
		break;
		}
	case SYS_CLOSE:	/* close */
		ret = tc_close(t, params[0] - 1);
		break;
	case SYS_READ:	/* read */
		ret = tc_read(t, params[0] - 1, params[1], params[2]);
		if (ret > 0)
			ret = params[2] - ret;
		break;
	case SYS_WRITE:	/* write */
		ret = tc_write(t, params[0] - 1, params[1], params[2]);
		if (ret > 0)
			ret = params[2] - ret;
		break;
	case SYS_WRITEC: /* writec */
		ret = tc_write(t, 2, arm_regs[1], 1);
		break;
	case SYS_ISTTY:	/* isatty */
		ret = tc_isatty(t, params[0] - 1);
		break;
	case SYS_SEEK:	/* lseek */
		ret = tc_lseek(t, params[0] - 1, params[1], TARGET_SEEK_SET);
		break;
	case SYS_RENAME:/* rename */
		ret = tc_rename(t, params[0] - 1, params[1] + 1,
				params[2], params[3] + 1);
		break;
	case SYS_REMOVE:/* unlink */
		ret = tc_unlink(t, params[0] - 1, params[1] + 1);
		break;
	case SYS_SYSTEM:/* system */
		ret = tc_system(t, params[0] - 1, params[1] + 1);
		break;

	case SYS_FLEN:	/* Not supported, fake success */
		t->tc->errno_ = 0;
		break;

	case SYS_ERRNO: /* Return last errno from GDB */
		ret = t->tc->errno_;
		break;

	case SYS_TIME:	/* gettimeofday */
		/* FIXME How do we use gdb's gettimeofday? */
		break;
	}

	arm_regs[0] = ret;
	target_regs_write(t, arm_regs);

	return t->tc->interrupted;
}

