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
#include "adi.h"
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
#include "maths_utils.h"

#include <assert.h>

static bool cortexm_vector_catch(target_s *target, int argc, const char **argv);

const command_s cortexm_cmd_list[] = {
	{"vector_catch", cortexm_vector_catch, "Catch exception vectors"},
	{NULL, NULL, NULL},
};

#define CORTEXM_DCRSR_REG_WRITE (1U << 16U)

static const char *cortexm_target_description(target_s *target);
static void cortexm_regs_read(target_s *target, void *data);
static void cortexm_regs_write(target_s *target, const void *data);
static uint32_t cortexm_pc_read(target_s *target);
static size_t cortexm_reg_read(target_s *target, uint32_t reg, void *data, size_t max);
static size_t cortexm_reg_write(target_s *target, uint32_t reg, const void *data, size_t max);

static void cortexm_reset(target_s *target);
static target_halt_reason_e cortexm_halt_poll(target_s *target, target_addr64_t *watch);
static void cortexm_halt_request(target_s *target);
static int cortexm_fault_unwind(target_s *target);

static int cortexm_breakwatch_set(target_s *target, breakwatch_s *breakwatch);
static int cortexm_breakwatch_clear(target_s *target, breakwatch_s *breakwatch);
static target_addr_t cortexm_check_watch(target_s *target);

static bool cortexm_hostio_request(target_s *target);

typedef struct cortexm_priv {
	cortex_priv_s base;
	bool stepping;
	bool on_bkpt;
	bool icache_enabled;
	bool dcache_enabled;
	/* Flash Patch controller configuration */
	uint8_t flash_patch_revision;
	/* Copy of DEMCR for vector-catch */
	uint32_t demcr;
} cortexm_priv_s;

/* Register number tables */
static const uint8_t regnum_cortex_m[CORTEXM_GENERAL_REG_COUNT] = {
	0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, /* r0-r15 */
	0x10U,                                                                /* xpsr */
	0x11U,                                                                /* msp */
	0x12U,                                                                /* psp */
	0x14U,                                                                /* special */
};

static const uint8_t regnum_cortex_m_trustzone[CORTEXM_TRUSTZONE_REG_COUNT] = {
	0x18U, 0x19U, /* Non-secure msp + psp */
	0x1aU, 0x1bU, /* Secure msp + psp */
};

static const uint8_t regnum_cortex_mf[CORTEX_FLOAT_REG_COUNT] = {
	0x21U,                                                  /* fpscr */
	0x40U, 0x41U, 0x42U, 0x43U, 0x44U, 0x45U, 0x46U, 0x47U, /* s0-s7 */
	0x48U, 0x49U, 0x4aU, 0x4bU, 0x4cU, 0x4dU, 0x4eU, 0x4fU, /* s8-s15 */
	0x50U, 0x51U, 0x52U, 0x53U, 0x54U, 0x55U, 0x56U, 0x57U, /* s16-s23 */
	0x58U, 0x59U, 0x5aU, 0x5bU, 0x5cU, 0x5dU, 0x5eU, 0x5fU, /* s24-s31 */
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

static void cortexm_cache_clean(
	target_s *const target, const target_addr32_t addr, const size_t len, const bool invalidate)
{
	const cortexm_priv_s *const priv = (const cortexm_priv_s *)target->priv;
	/* Check if there is a data cache and if it's even actually enabled */
	if (!priv->base.dcache_line_length || !priv->dcache_enabled)
		return;
	const target_addr32_t cache_reg = invalidate ? CORTEXM_DCCIMVAC : CORTEXM_DCCMVAC;
	const size_t minline = priv->base.dcache_line_length << 2U;

	/* flush data cache for RAM regions that intersect requested region */
	const target_addr32_t mem_end = addr + len; /* following code is NOP if wraparound */
	/* requested region is [src, src_end) */
	for (target_ram_s *ram = target->ram; ram; ram = ram->next) {
		target_addr32_t region_start = ram->start;
		target_addr32_t region_end = ram->start + ram->length;
		/* RAM region is [region_start, region_end) */
		if (addr > region_start)
			region_start = addr;
		if (mem_end < region_end)
			region_end = mem_end;
		/* intersection is [region_start, region_end) */
		for (region_start &= ~(minline - 1U); region_start < region_end; region_start += minline)
			target_mem32_write32(target, cache_reg, region_start);
	}
}

static void cortexm_mem_read(target_s *target, void *dest, target_addr64_t src, size_t len)
{
	cortexm_cache_clean(target, src, len, false);
	adiv5_mem_read(cortex_ap(target), dest, src, len);
}

static void cortexm_mem_write(target_s *target, target_addr64_t dest, const void *src, size_t len)
{
	cortexm_cache_clean(target, dest, len, true);
	adiv5_mem_write(cortex_ap(target), dest, src, len);
}

bool target_is_cortexm(const target_s *target)
{
	return target != NULL && target->regs_description == cortexm_target_description;
}

uint32_t cortexm_demcr_read(const target_s *target)
{
	const cortexm_priv_s *priv = (const cortexm_priv_s *)target->priv;
	return priv->demcr;
}

void cortexm_demcr_write(target_s *target, uint32_t demcr)
{
	cortexm_priv_s *priv = (cortexm_priv_s *)target->priv;
	priv->demcr = demcr;
	target_mem32_write32(target, CORTEXM_DEMCR, demcr);
}

bool cortexm_probe(adiv5_access_port_s *ap)
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

	/* MM32F5xxx: part designer code is Arm China, target designer code uses forbidden continuation code */
	if (target->designer_code == JEP106_MANUFACTURER_ERRATA_ARM_CHINA &&
		ap->dp->designer_code == JEP106_MANUFACTURER_ARM_CHINA)
		target->designer_code = JEP106_MANUFACTURER_ARM_CHINA;

	cortexm_priv_s *priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}

	target->priv = priv;
	target->priv_free = cortex_priv_free;
	priv->base.ap = ap;
	priv->base.base_addr = CORTEXM_SCS_BASE;

	target->check_error = cortex_check_error;
	target->mem_read = cortexm_mem_read;
	target->mem_write = cortexm_mem_write;

	target->driver = "ARM Cortex-M";

	cortex_read_cpuid(target);

	target->attach = cortexm_attach;
	target->detach = cortexm_detach;

	/* Probe for the security extension if the core is not an ARMv6-M core */
	if (!(target->target_options & CORTEXM_TOPT_FLAVOUR_V6M) &&
		target_mem32_read32(target, CORTEXM_ID_PFR1) & CORTEXM_ID_PFR1_SECEXT_IMPL)
		target->target_options |= CORTEXM_TOPT_TRUSTZONE;

	/* Probe for FP extension. */
	uint32_t cpacr = target_mem32_read32(target, CORTEXM_CPACR);
	cpacr |= 0x00f00000U; /* CP10 = 0b11, CP11 = 0b11 */
	target_mem32_write32(target, CORTEXM_CPACR, cpacr);
	bool is_cortexmf = target_mem32_read32(target, CORTEXM_CPACR) == cpacr;

	target->regs_description = cortexm_target_description;
	target->regs_read = cortexm_regs_read;
	target->regs_write = cortexm_regs_write;
	target->reg_read = cortexm_reg_read;
	target->reg_write = cortexm_reg_write;

	target->reset = cortexm_reset;
	target->halt_request = cortexm_halt_request;
	target->halt_poll = cortexm_halt_poll;
	target->halt_resume = cortexm_halt_resume;
	target->regs_size = sizeof(uint32_t) * CORTEXM_GENERAL_REG_COUNT;

	/* Adjust the regs_size value for TrustZone */
	if (target->target_options & CORTEXM_TOPT_TRUSTZONE)
		target->regs_size += sizeof(uint32_t) * CORTEXM_TRUSTZONE_REG_COUNT;

	/* Adjust the regs_size value for having a FPU */
	if (is_cortexmf) {
		target->target_options |= CORTEXM_TOPT_FLAVOUR_FLOAT;
		target->regs_size += sizeof(uint32_t) * CORTEX_FLOAT_REG_COUNT;
	}

	target->breakwatch_set = cortexm_breakwatch_set;
	target->breakwatch_clear = cortexm_breakwatch_clear;

	target_add_commands(target, cortexm_cmd_list, target->driver);

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
		target_mem32_write32(target, CORTEXM_DEMCR, priv->demcr);
		/* Force a halt */
		cortexm_halt_request(target);
		/* Release reset */
		platform_nrst_set_val(false);
		/* Poll for release from reset */
		platform_timeout_s timeout;
		platform_timeout_set(&timeout, 1000);
		while (target_mem32_read32(target, CORTEXM_DHCSR) & CORTEXM_DHCSR_S_RESET_ST) {
			if (platform_timeout_is_expired(&timeout)) {
				DEBUG_ERROR("Error releasing from reset\n");
				/* Go on and try to detect the target anyways */
				break;
			}
		}
	}

	/* Check cache type */
	const uint32_t cache_type = target_mem32_read32(target, CORTEXM_CTR);
	if (cache_type >> CORTEX_CTR_FORMAT_SHIFT == CORTEX_CTR_FORMAT_ARMv7) {
		priv->base.icache_line_length = CORTEX_CTR_ICACHE_LINE(cache_type);
		priv->base.dcache_line_length = CORTEX_CTR_DCACHE_LINE(cache_type);
	} else
		target_check_error(target);

	/* If we set the interrupt catch vector earlier, clear it. */
	if (conn_reset)
		target_mem32_write32(target, CORTEXM_DEMCR, 0);

	DEBUG_TARGET("%s: Examining Part ID 0x%04x, AP Part ID: 0x%04x\n", __func__, target->part_id, ap->partno);

	switch (target->designer_code) {
	case JEP106_MANUFACTURER_FREESCALE:
		PROBE(imxrt_probe);
		PROBE(kinetis_probe);
		PROBE(s32k3xx_probe);
		PROBE(ke04_probe);
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
		PROBE(stm32l1_probe);
		PROBE(stm32l4_probe);
		PROBE(stm32g0_probe);
		PROBE(stm32wb0_probe);
		break;
	case JEP106_MANUFACTURER_CYPRESS:
		DEBUG_WARN("Unhandled Cypress device\n");
		break;
	case JEP106_MANUFACTURER_INFINEON:
		DEBUG_WARN("Unhandled Infineon device\n");
		break;
	case JEP106_MANUFACTURER_NORDIC:
		PROBE(nrf51_probe);
		PROBE(nrf54l_probe);
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
		PROBE(mspm0_probe);
		break;
	case JEP106_MANUFACTURER_SPECULAR:
		PROBE(lpc11xx_probe); /* LPC845 */
		break;
	case JEP106_MANUFACTURER_RASPBERRY:
		PROBE(rp2040_probe);
		PROBE(rp2350_probe);
		break;
	case JEP106_MANUFACTURER_RENESAS:
		PROBE(renesas_ra_probe);
		break;
	case JEP106_MANUFACTURER_WCH:
		PROBE(ch579_probe);
		break;
	case JEP106_MANUFACTURER_NXP:
		if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M33)
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
		if (target->part_id == 0x4c0U) {        /* Cortex-M0+ ROM */
			PROBE(lpc11xx_probe);               /* LPC8 */
			PROBE(hc32l110_probe);              /* HDSC HC32L110 */
			PROBE(puya_probe);                  /* Puya PY32 */
		} else if (target->part_id == 0x4c1U) { /* NXP Cortex-M0+ ROM */
			PROBE(lpc11xx_probe);               /* newer LPC11U6x */
		} else if (target->part_id == 0x4c3U) { /* Cortex-M3 ROM */
			PROBE(lmi_probe);
			PROBE(ch32f1_probe);
			PROBE(stm32f1_probe);               /* Care for other STM32F1 clones (?) */
			PROBE(lpc15xx_probe);               /* Thanks to JojoS for testing */
			PROBE(mm32f3xx_probe);              /* MindMotion MM32 */
		} else if (target->part_id == 0x471U) { /* Cortex-M0 ROM */
			PROBE(lpc11xx_probe);               /* LPC24C11 */
			PROBE(lpc43xx_probe);
			PROBE(mm32l0xx_probe);              /* MindMotion MM32 */
		} else if (target->part_id == 0x4c4U) { /* Cortex-M4 ROM */
			PROBE(sam3x_probe);
			PROBE(lmi_probe);
			PROBE(apollo_3_probe);
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
			PROBE(at32f40x_probe);
			PROBE(at32f43x_probe); /* AT32F435 doesn't survive LPC40xx IAP */
			PROBE(lpc40xx_probe);
			PROBE(kinetis_probe); /* Older K-series */
			PROBE(msp432e4_probe);
		} else if (target->part_id == 0x4cbU) { /* Cortex-M23 ROM */
			PROBE(gd32f1_probe);                /* GD32E23x uses GD32F1 peripherals */
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
		PROBE(am335x_cm3_probe);
		break;
	}
#if CONFIG_BMDA == 0
	gdb_outf("Please report unknown device with Designer 0x%x Part ID 0x%x\n", target->designer_code, target->part_id);
#else
	DEBUG_WARN(
		"Please report unknown device with Designer 0x%x Part ID 0x%x\n", target->designer_code, target->part_id);
#endif
	return true;
}

bool cortexm_attach(target_s *target)
{
	adiv5_access_port_s *ap = cortex_ap(target);
	/* Mark the DP as being in fault so error recovery will switch to this core when in multi-drop mode */
	ap->dp->fault = 1;
	cortexm_priv_s *priv = target->priv;

	/* Clear any pending fault condition (and switch to this core) */
	target_check_error(target);

	/* Try to halt the core, and then check that it worked (which also resets the halt reason) */
	target_halt_request(target);
	const target_halt_reason_e halt_result = target_halt_poll(target, NULL);
	/* If we failed to halt the target somehow, bail */
	if (halt_result == TARGET_HALT_ERROR || halt_result == TARGET_HALT_RUNNING)
		return false;

	/* Request halt on reset */
	target_mem32_write32(target, CORTEXM_DEMCR, priv->demcr);

	/* Find out how many breakpoint slots there are */
	priv->base.breakpoints_available = CORTEX_MAX_BREAKPOINTS;
	const uint32_t flash_break_cfg = target_mem32_read32(target, CORTEXM_FPB_CTRL);
	/*
	 * Extract and reconstruct the full NUM_CODE 7-bit value so we don't break and do bad things on
	 * parts with higher slot counts
	 */
	const uint8_t breakpoints = (uint8_t)(((flash_break_cfg & CORTEXM_FPB_CTRL_NUM_CODE_H) >> 8U) |
		((flash_break_cfg & CORTEXM_FPB_CTRL_NUM_CODE_L) >> 4U));
	/* If there are less breakpoints than we have slots for, then truncate the number of slots we use */
	if (breakpoints < priv->base.breakpoints_available)
		priv->base.breakpoints_available = breakpoints;
	/* Extract the revision number of the unit so we can alter our behavior on use as necessary */
	priv->flash_patch_revision = (flash_break_cfg >> CORTEXM_FPB_CTRL_REV_SHIFT) & CORTEXM_FPB_CTRL_REV_MASK;

	/* Now find out how many watchpoint slots there are */
	priv->base.watchpoints_available = CORTEX_MAX_WATCHPOINTS;
	const uint32_t watchpoints = target_mem32_read32(target, CORTEXM_DWT_CTRL);
	/* If the number of watchpoints available is fewer than we can handle, truncate to that number */
	if ((watchpoints >> 28U) < priv->base.watchpoints_available)
		priv->base.watchpoints_available = watchpoints >> 28U;

	/* Clear any stale breakpoints */
	priv->base.breakpoints_mask = 0;
	for (size_t i = 0; i < priv->base.breakpoints_available; i++)
		target_mem32_write32(target, CORTEXM_FPB_COMP(i), 0);

	/* Clear any stale watchpoints */
	priv->base.watchpoints_mask = 0;
	for (size_t i = 0; i < priv->base.watchpoints_available; i++)
		target_mem32_write32(target, CORTEXM_DWT_FUNC(i), 0);

	/* Flash Patch Control Register: set ENABLE */
	target_mem32_write32(target, CORTEXM_FPB_CTRL, CORTEXM_FPB_CTRL_KEY | CORTEXM_FPB_CTRL_ENABLE);

	(void)target_mem32_read32(target, CORTEXM_DHCSR);
	if (target_mem32_read32(target, CORTEXM_DHCSR) & CORTEXM_DHCSR_S_RESET_ST) {
		platform_nrst_set_val(false);
		platform_timeout_s timeout;
		platform_timeout_set(&timeout, 1000);
		while (true) {
			const uint32_t reset_status = target_mem32_read32(target, CORTEXM_DHCSR);
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

void cortexm_detach(target_s *target)
{
	cortexm_priv_s *priv = target->priv;

	/* Clear any stale breakpoints */
	for (size_t i = 0; i < priv->base.breakpoints_available; i++)
		target_mem32_write32(target, CORTEXM_FPB_COMP(i), 0);

	/* Clear any stale watchpoints */
	for (size_t i = 0; i < priv->base.watchpoints_available; i++)
		target_mem32_write32(target, CORTEXM_DWT_FUNC(i), 0);

	/* Restore DEMCR */
	adiv5_access_port_s *ap = cortex_ap(target);
	target_mem32_write32(target, CORTEXM_DEMCR, ap->ap_cortexm_demcr);
	/* Resume target and disable debug, re-enabling interrupts in the process */
	target_mem32_write32(target, CORTEXM_DHCSR, CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_DEBUGEN | CORTEXM_DHCSR_C_HALT);
	target_mem32_write32(target, CORTEXM_DHCSR, CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_DEBUGEN);
	target_mem32_write32(target, CORTEXM_DHCSR, CORTEXM_DHCSR_DBGKEY);
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
#if CONFIG_BMDA == 1
	if (ap->dp->ap_regs_read && ap->dp->ap_reg_read) {
		uint32_t core_regs[21U];
		ap->dp->ap_regs_read(ap, core_regs);
		for (size_t i = 0; i < CORTEXM_GENERAL_REG_COUNT; ++i)
			regs[i] = core_regs[regnum_cortex_m[i]];

		if (target->target_options & CORTEXM_TOPT_FLAVOUR_FLOAT) {
			const size_t offset = CORTEXM_GENERAL_REG_COUNT;
			for (size_t i = 0; i < CORTEX_FLOAT_REG_COUNT; ++i)
				regs[offset + i] = ap->dp->ap_reg_read(ap, regnum_cortex_mf[i]);
		}
	} else {
#endif
		/*
		 * Map the AP's banked data registers (0x10-0x1c) to the
		 * debug registers DHCSR, DCRSR, DCRDR and DEMCR respectively
		 * and do so for 32-bit access
		 */
		adi_ap_mem_access_setup(ap, CORTEXM_DHCSR, ALIGN_32BIT);
		adi_ap_banked_access_setup(ap);

		/* Walk the regnum_cortex_m array, reading the registers it specifies */
		for (size_t i = 0U; i < CORTEXM_GENERAL_REG_COUNT; ++i) {
			adiv5_dp_write(ap->dp, ADIV5_AP_DB(DB_DCRSR), regnum_cortex_m[i]);
			regs[i] = adiv5_dp_read(ap->dp, ADIV5_AP_DB(DB_DCRDR));
		}
		size_t offset = CORTEXM_GENERAL_REG_COUNT;
		/* If the core implements TrustZone, pull out the extra stack pointers */
		if (target->target_options & CORTEXM_TOPT_TRUSTZONE) {
			for (size_t i = 0U; i < CORTEXM_TRUSTZONE_REG_COUNT; ++i) {
				adiv5_dp_write(ap->dp, ADIV5_AP_DB(DB_DCRSR), regnum_cortex_m_trustzone[i]);
				regs[offset + i] = adiv5_dp_read(ap->dp, ADIV5_AP_DB(DB_DCRDR));
			}
			offset += CORTEXM_TRUSTZONE_REG_COUNT;
		}
		/* If the core has a FPU, also walk the regnum_cortex_mf array */
		if (target->target_options & CORTEXM_TOPT_FLAVOUR_FLOAT) {
			for (size_t i = 0U; i < CORTEX_FLOAT_REG_COUNT; ++i) {
				adiv5_dp_write(ap->dp, ADIV5_AP_DB(DB_DCRSR), regnum_cortex_mf[i]);
				regs[offset + i] = adiv5_dp_read(ap->dp, ADIV5_AP_DB(DB_DCRDR));
			}
		}
#if CONFIG_BMDA == 1
	}
#endif
}

static void cortexm_regs_write(target_s *const target, const void *const data)
{
	const uint32_t *const regs = data;
	adiv5_access_port_s *const ap = cortex_ap(target);
#if CONFIG_BMDA == 1
	if (ap->dp->ap_reg_write) {
		for (size_t i = 0; i < CORTEXM_GENERAL_REG_COUNT; ++i)
			ap->dp->ap_reg_write(ap, regnum_cortex_m[i], regs[i]);

		if (target->target_options & CORTEXM_TOPT_FLAVOUR_FLOAT) {
			const size_t offset = CORTEXM_GENERAL_REG_COUNT;
			for (size_t i = 0; i < CORTEX_FLOAT_REG_COUNT; ++i)
				ap->dp->ap_reg_write(ap, regnum_cortex_mf[i], regs[offset + i]);
		}
	} else {
#endif
		/*
		 * Map the banked data registers (0x10-0x1c) to the
		 * debug registers DHCSR, DCRSR, DCRDR and DEMCR respectively
		 * and do so for 32-bit access
		 */
		adi_ap_mem_access_setup(ap, CORTEXM_DHCSR, ALIGN_32BIT);
		adi_ap_banked_access_setup(ap);

		/* Walk the regnum_cortex_m array, writing the registers it specifies */
		for (size_t i = 0U; i < CORTEXM_GENERAL_REG_COUNT; ++i) {
			adiv5_dp_write(ap->dp, ADIV5_AP_DB(DB_DCRDR), regs[i]);
			adiv5_dp_write(ap->dp, ADIV5_AP_DB(DB_DCRSR), CORTEXM_DCRSR_REG_WRITE | regnum_cortex_m[i]);
		}
		size_t offset = CORTEXM_GENERAL_REG_COUNT;
		/* If the core implements TrustZone, write in the extra stack pointers */
		if (target->target_options & CORTEXM_TOPT_TRUSTZONE) {
			for (size_t i = 0U; i < CORTEXM_TRUSTZONE_REG_COUNT; ++i) {
				adiv5_dp_write(ap->dp, ADIV5_AP_DB(DB_DCRDR), regs[offset + i]);
				adiv5_dp_write(ap->dp, ADIV5_AP_DB(DB_DCRSR), CORTEXM_DCRSR_REG_WRITE | regnum_cortex_m_trustzone[i]);
			}
			offset += CORTEXM_TRUSTZONE_REG_COUNT;
		}
		/* If the core has a FPU, also walk the regnum_cortex_mf array */
		if (target->target_options & CORTEXM_TOPT_FLAVOUR_FLOAT) {
			for (size_t i = 0U; i < CORTEX_FLOAT_REG_COUNT; ++i) {
				adiv5_dp_write(ap->dp, ADIV5_AP_DB(DB_DCRDR), regs[offset + i]);
				adiv5_dp_write(ap->dp, ADIV5_AP_DB(DB_DCRSR), CORTEXM_DCRSR_REG_WRITE | regnum_cortex_mf[i]);
			}
		}
#if CONFIG_BMDA == 1
	}
#endif
}

int cortexm_mem_write_aligned(target_s *target, target_addr_t dest, const void *src, size_t len, align_e align)
{
	cortexm_cache_clean(target, dest, len, true);
	adiv5_mem_write_aligned(cortex_ap(target), dest, src, len, align);
	return target_check_error(target);
}

static int dcrsr_regnum(target_s *target, uint32_t reg)
{
	if (reg < CORTEXM_GENERAL_REG_COUNT)
		return (int)regnum_cortex_m[reg];
	size_t offset = CORTEXM_GENERAL_REG_COUNT;
	if (target->target_options & CORTEXM_TOPT_TRUSTZONE) {
		if (reg < offset + CORTEXM_TRUSTZONE_REG_COUNT)
			return (int)regnum_cortex_m_trustzone[reg - offset];
		offset += CORTEXM_TRUSTZONE_REG_COUNT;
	}
	if (target->target_options & CORTEXM_TOPT_FLAVOUR_FLOAT) {
		if (reg < offset + CORTEX_FLOAT_REG_COUNT)
			return (int)regnum_cortex_mf[reg - offset];
	}
	return -1;
}

static size_t cortexm_reg_read(target_s *target, uint32_t reg, void *data, size_t max)
{
	if (max < 4U)
		return 0;
	uint32_t *reg_value = data;
	target_mem32_write32(target, CORTEXM_DCRSR, dcrsr_regnum(target, reg));
	*reg_value = target_mem32_read32(target, CORTEXM_DCRDR);
	return 4U;
}

static size_t cortexm_reg_write(target_s *target, uint32_t reg, const void *data, size_t max)
{
	if (max < 4U)
		return 0;
	const uint32_t *reg_value = data;
	target_mem32_write32(target, CORTEXM_DCRDR, *reg_value);
	target_mem32_write32(target, CORTEXM_DCRSR, CORTEXM_DCRSR_REGWnR | dcrsr_regnum(target, reg));
	return 4U;
}

static uint32_t cortexm_pc_read(target_s *target)
{
	target_mem32_write32(target, CORTEXM_DCRSR, 0x0f);
	return target_mem32_read32(target, CORTEXM_DCRDR);
}

static void cortexm_pc_write(target_s *target, const uint32_t val)
{
	target_mem32_write32(target, CORTEXM_DCRDR, val);
	target_mem32_write32(target, CORTEXM_DCRSR, CORTEXM_DCRSR_REGWnR | 0x0fU);
}

/*
 * The following three routines implement target halt/resume
 * using the core debug registers in the NVIC.
 */
static void cortexm_reset(target_s *const target)
{
	/* Read DHCSR here to clear S_RESET_ST bit before reset */
	target_mem32_read32(target, CORTEXM_DHCSR);
	/* If the physical reset pin is not inhibited, use it */
	if (!(target->target_options & TOPT_INHIBIT_NRST)) {
		platform_nrst_set_val(true);
		platform_nrst_set_val(false);
		/* Some NRF52840 users saw invalid SWD transaction with native/firmware without this delay.*/
		platform_delay(10);
	}

	/* Check if the reset succeeded */
	const uint32_t status = target_mem32_read32(target, CORTEXM_DHCSR);
	if (!(status & CORTEXM_DHCSR_S_RESET_ST)) {
		/*
		 * No reset seen yet, maybe as nRST is not connected, or device has TOPT_INHIBIT_NRST set.
		 * Trigger reset by AIRCR.
		 */
		target_mem32_write32(target, CORTEXM_AIRCR, CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_SYSRESETREQ);
	}

	/* If target needs to do something extra (see Atmel SAM4L for example) */
	if (target->extended_reset != NULL)
		target->extended_reset(target);

	/* Wait for CORTEXM_DHCSR_S_RESET_ST to read 0, meaning reset released.*/
	platform_timeout_s reset_timeout;
	platform_timeout_set(&reset_timeout, 1000);
	while ((target_mem32_read32(target, CORTEXM_DHCSR) & CORTEXM_DHCSR_S_RESET_ST) &&
		!platform_timeout_is_expired(&reset_timeout))
		continue;

#if defined(PLATFORM_HAS_DEBUG)
	if (platform_timeout_is_expired(&reset_timeout))
		DEBUG_WARN("Reset seem to be stuck low!\n");
#endif

	/* 10 ms delay to ensure that things such as the STM32 HSI clock have started up fully. */
	platform_delay(10);
	/* Reset DFSR flags and ignore any initial DAP error */
	target_mem32_write32(target, CORTEXM_DFSR, CORTEXM_DFSR_RESETALL);
}

static void cortexm_halt_request(target_s *target)
{
	TRY (EXCEPTION_TIMEOUT) {
		target_mem32_write32(
			target, CORTEXM_DHCSR, CORTEXM_DHCSR_DBGKEY | CORTEXM_DHCSR_C_HALT | CORTEXM_DHCSR_C_DEBUGEN);
	}
	CATCH () {
	default:
		tc_printf(target, "Timeout sending interrupt, is target in WFI?\n");
	}
}

static target_halt_reason_e cortexm_halt_poll(target_s *target, target_addr64_t *watch)
{
	cortexm_priv_s *priv = target->priv;

	volatile uint32_t dhcsr = 0;
	TRY (EXCEPTION_ALL) {
		/* If this times out because the target is in WFI then the target is still running. */
		dhcsr = target_mem32_read32(target, CORTEXM_DHCSR);
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
	if (!(dhcsr & CORTEXM_DHCSR_S_HALT))
		return TARGET_HALT_RUNNING;

	/* Read out the status register to determine why */
	const uint32_t dfsr = target_mem32_read32(target, CORTEXM_DFSR);
	/* Write the same value back to clear the register */
	target_mem32_write32(target, CORTEXM_DFSR, dfsr);

	/* Check what caches are currently enabled */
	const uint32_t ccr = target_mem32_read32(target, CORTEXM_CCR);
	priv->dcache_enabled = ccr & CORTEXM_CCR_DCACHE_ENABLE;
	priv->icache_enabled = ccr & CORTEXM_CCR_ICACHE_ENABLE;

	if ((dfsr & CORTEXM_DFSR_VCATCH) && cortexm_fault_unwind(target))
		return TARGET_HALT_FAULT;

	/* Remember if we stopped on a breakpoint */
	priv->on_bkpt = dfsr & CORTEXM_DFSR_BKPT;
	if (priv->on_bkpt) {
		/* If we've hit a programmed breakpoint, check for semihosting call. */
		const uint32_t program_counter = cortexm_pc_read(target);
		const uint16_t instruction = target_mem32_read16(target, program_counter);
		/* 0xbeab encodes the breakpoint instruction used to indicate a semihosting call */
		if (instruction == 0xbeabU) {
			if (cortexm_hostio_request(target))
				return TARGET_HALT_REQUEST;

			target_halt_resume(target, priv->stepping);
			return TARGET_HALT_RUNNING;
		}
	}

	if (dfsr & CORTEXM_DFSR_DWTTRAP) {
		if (watch != NULL)
			*watch = cortexm_check_watch(target);
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
		target_mem32_write32(target, CORTEXM_DHCSR, dhcsr | CORTEXM_DHCSR_C_HALT);
		priv->stepping = step;
	}

	if (priv->on_bkpt) {
		/* Read the instruction to resume on */
		uint32_t pc = cortexm_pc_read(target);
		/* If it actually is a breakpoint instruction, update the program counter one past it. */
		if ((target_mem32_read16(target, pc) & 0xff00U) == 0xbe00U)
			cortexm_pc_write(target, pc + 2U);
	}

	if (priv->base.icache_line_length)
		target_mem32_write32(target, CORTEXM_ICIALLU, 0U);

	/* Release C_HALT to resume the core in whichever mode is selected */
	target_mem32_write32(target, CORTEXM_DHCSR, dhcsr);
}

static int cortexm_fault_unwind(target_s *target)
{
	/* Read the fault status registers */
	uint32_t hfsr = target_mem32_read32(target, CORTEXM_HFSR);
	uint32_t cfsr = target_mem32_read32(target, CORTEXM_CFSR);
	/* Write them back to reset them */
	target_mem32_write32(target, CORTEXM_HFSR, hfsr);
	target_mem32_write32(target, CORTEXM_CFSR, cfsr);
	/*
	 * We check for FORCED in the HardFault Status Register or
	 * for a configurable fault to avoid catching core resets
	 */
	if ((hfsr & CORTEXM_HFSR_FORCED) || cfsr) {
		/* Unwind exception */
		uint32_t regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];
		uint32_t stack[8];
		/* Read registers for post-exception stack pointer */
		target_regs_read(target, regs);
		/* save retcode currently in lr */
		const uint32_t retcode = regs[CORTEX_REG_LR];
		bool spsel = retcode & (1U << 2U);
		bool fpca = !(retcode & (1U << 4U));
		/* Read stack for pre-exception registers */
		uint32_t sp = spsel ? regs[CORTEX_REG_PSP] : regs[CORTEX_REG_MSP];
		target_mem32_read(target, stack, sp, sizeof(stack));
		if (target_check_error(target))
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
		target_mem32_write32(target, CORTEXM_AIRCR, CORTEXM_AIRCR_VECTKEY | CORTEXM_AIRCR_VECTCLRACTIVE);

		/* Write pre-exception registers back to core */
		target_regs_write(target, regs);

		return 1;
	}
	return 0;
}

bool cortexm_run_stub(target_s *target, uint32_t loadaddr, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3)
{
	uint32_t regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT] = {0};

	regs[0] = r0;
	regs[1] = r1;
	regs[2] = r2;
	regs[3] = r3;
	regs[15] = loadaddr;
	regs[CORTEX_REG_XPSR] = CORTEXM_XPSR_THUMB;
	regs[19] = 0;

	cortexm_regs_write(target, regs);

	if (target_check_error(target))
		return false;

	/* Execute the stub */
	target_halt_reason_e reason = TARGET_HALT_RUNNING;
#if defined(PLATFORM_HAS_DEBUG)
	uint32_t arm_regs_start[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];
	target_regs_read(target, arm_regs_start);
#endif
	cortexm_halt_resume(target, 0);
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 5000);
	while (reason == TARGET_HALT_RUNNING) {
		if (platform_timeout_is_expired(&timeout)) {
			cortexm_halt_request(target);
#if defined(PLATFORM_HAS_DEBUG)
			DEBUG_WARN("Stub hung\n");
			uint32_t arm_regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];
			target_regs_read(target, arm_regs);
			for (uint32_t i = 0; i < 20U; ++i)
				DEBUG_WARN("%2" PRIu32 ": %08" PRIx32 ", %08" PRIx32 "\n", i, arm_regs_start[i], arm_regs[i]);
#endif
			return false;
		}
		reason = cortexm_halt_poll(target, NULL);
	}

	if (reason == TARGET_HALT_ERROR)
		raise_exception(EXCEPTION_ERROR, "Target lost in stub");

	if (reason != TARGET_HALT_BREAKPOINT) {
		DEBUG_WARN(" Reason %d\n", reason);
		return false;
	}

	uint32_t pc = cortexm_pc_read(target);
	uint16_t bkpt_instr = target_mem32_read16(target, pc);
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
static uint32_t cortexm_dwt_mask(size_t len)
{
	if (len < 2U)
		return 0U;
	return MIN(ulog2(len - 1U), 31U);
}

static uint32_t cortexm_dwt_func(target_s *target, target_breakwatch_e type)
{
	uint32_t x = 0;

	if ((target->target_options & CORTEXM_TOPT_FLAVOUR_V6M) == 0)
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

static uint32_t cortexm_dwtv2_func(target_breakwatch_e type, size_t len)
{
	uint32_t value = CORTEXM_DWTv2_FUNC_ACTION_DEBUG_EVENT | CORTEXM_DWTv2_FUNC_LEN_VALUE(len);
	switch (type) {
	case TARGET_WATCH_WRITE:
		value |= CORTEXM_DWTv2_FUNC_MATCH_WRITE;
		break;
	case TARGET_WATCH_READ:
		value |= CORTEXM_DWTv2_FUNC_MATCH_READ;
		break;
	case TARGET_WATCH_ACCESS:
		value |= CORTEXM_DWTv2_FUNC_MATCH_ACCESS;
		break;
	default:
		return 0U;
	}
	return value;
}

static int cortexm_breakpoint_set(target_s *const target, breakwatch_s *const breakwatch)
{
	cortexm_priv_s *const priv = target->priv;

	target_addr32_t breakpoint = breakwatch->addr;
	/* If this is a FPB that can only set breakpoints in the first 512MiB of address space (version 1) */
	if (priv->flash_patch_revision == 0U) {
		/* Make sure all the reserved and special bits are cleared */
		breakpoint &= 0x1ffffffcU;
		/*
			* And then set whether the breakpoint should be for the lower or upper 16 bits of the 32 bit block
			* pointed to by the truncated address (masking the bottom nibble by `c` 4 byte aligns the address)
			*/
		breakpoint |= (breakwatch->addr & 2U) ? 0x80000000U : 0x40000000U;
	}
	/* Make sure the enable bit is set for the slot we're about to write */
	breakpoint |= 1U;

	/* Find the first available breakpoint slot */
	uint32_t slot = 0U;
	for (; slot < priv->base.breakpoints_available; ++slot) {
		if (!(priv->base.breakpoints_mask & (1U << slot)))
			break;
	}

	/* If we could not find any slots, inform the GDB server that we could not set the breakpoint */
	if (slot == priv->base.breakpoints_available)
		return -1;

	/* Otherwise, mark the slot chosen as used, and write it with the computed value */
	priv->base.breakpoints_mask |= 1U << slot;
	target_mem32_write32(target, CORTEXM_FPB_COMP(slot), breakpoint);
	breakwatch->reserved[0] = slot;
	return 0;
}

static int cortexm_watchpoint_set(target_s *const target, breakwatch_s *const breakwatch)
{
	cortexm_priv_s *const priv = target->priv;

	/* Find the first available watchpoint slot */
	uint32_t slot = 0U;
	for (; slot < priv->base.watchpoints_available; slot++) {
		if (!(priv->base.watchpoints_mask & (1U << slot)))
			break;
	}

	/* If we could not find any slots, inform the GDB server that we could not set the watchpoint */
	if (slot == priv->base.watchpoints_available)
		return -1;

	/* Otherwise, mark the slot chosen as used */
	priv->base.watchpoints_mask |= 1U << slot;
	/* Then set the watchpoint hardware up to observe the requested address in the requested way */
	if ((target->target_options & CORTEXM_TOPT_FLAVOUR_V8M)) {
		target_mem32_write32(target, CORTEXM_DWT_COMP(slot), breakwatch->addr);
		target_mem32_write32(target, CORTEXM_DWT_FUNC(slot), cortexm_dwtv2_func(breakwatch->type, breakwatch->size));
	} else {
		target_mem32_write32(target, CORTEXM_DWT_COMP(slot), breakwatch->addr);
		target_mem32_write32(target, CORTEXM_DWT_MASK(slot), cortexm_dwt_mask(breakwatch->size));
		target_mem32_write32(target, CORTEXM_DWT_FUNC(slot), cortexm_dwt_func(target, breakwatch->type));
	}
	breakwatch->reserved[0] = slot;
	return 0;
}

static int cortexm_breakwatch_set(target_s *const target, breakwatch_s *const breakwatch)
{
	switch (breakwatch->type) {
	case TARGET_BREAK_HARD:
		return cortexm_breakpoint_set(target, breakwatch);

	case TARGET_WATCH_WRITE:
	case TARGET_WATCH_READ:
	case TARGET_WATCH_ACCESS:
		return cortexm_watchpoint_set(target, breakwatch);
	default:
		return 1;
	}
}

static int cortexm_breakwatch_clear(target_s *const target, breakwatch_s *const breakwatch)
{
	cortexm_priv_s *const priv = target->priv;
	const uint32_t slot = breakwatch->reserved[0];

	switch (breakwatch->type) {
	case TARGET_BREAK_HARD:
		/* Unmark the slot this breakpoint uses as used */
		priv->base.breakpoints_mask &= ~(1U << slot);
		/*
		 * Disable the breakpoint, but also crucially make sure we keep the Flash patch system disabled(!).
		 * BE is bit 0, FE is bit 31. Bits 29 and 30 are reserved and must be zero when BE is 0.
		 * The ARMv8-M ARM requires this be a write to 0, specifically, as called out in §D1.2.107, FP_COMPn,
		 * Flash Patch Comparator Register, pg1653
		 */
		target_mem32_write32(target, CORTEXM_FPB_COMP(slot), 0U);
		return 0;
	case TARGET_WATCH_WRITE:
	case TARGET_WATCH_READ:
	case TARGET_WATCH_ACCESS:
		/* Unmark the slot this watchpoint uses as used and disable the watchpoint */
		priv->base.watchpoints_mask &= ~(1U << slot);
		target_mem32_write32(target, CORTEXM_DWT_FUNC(slot), 0U);
		return 0;
	default:
		return 1;
	}
}

static target_addr_t cortexm_check_watch(target_s *target)
{
	cortexm_priv_s *priv = target->priv;
	unsigned i;

	for (i = 0; i < priv->base.watchpoints_available; i++) {
		/* if SET and MATCHED then break */
		if ((priv->base.watchpoints_mask & (1U << i)) &&
			(target_mem32_read32(target, CORTEXM_DWT_FUNC(i)) & CORTEXM_DWT_FUNC_MATCHED))
			break;
	}

	if (i == priv->base.watchpoints_available)
		return 0;

	return target_mem32_read32(target, CORTEXM_DWT_COMP(i));
}

static bool cortexm_vector_catch(target_s *target, int argc, const char **argv)
{
	cortexm_priv_s *priv = target->priv;
	static const char *const vectors[] = {"reset", NULL, NULL, NULL, "mm", "nocp", "chk", "stat", "bus", "int", "hard"};
	uint32_t tmp = 0;

	if (argc < 3)
		tc_printf(target, "usage: monitor vector_catch (enable|disable) (hard|int|bus|stat|chk|nocp|mm|reset)\n");
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

			target_mem32_write32(target, CORTEXM_DEMCR, priv->demcr);
		}
	}

	tc_printf(target, "Catching vectors: ");
	for (size_t i = 0; i < ARRAY_LENGTH(vectors); i++) {
		if (!vectors[i])
			continue;
		if (priv->demcr & (1U << i))
			tc_printf(target, "%s ", vectors[i]);
	}
	tc_printf(target, "\n");
	return true;
}

static bool cortexm_hostio_request(target_s *const target)
{
	/* Read out the information from the target needed to complete the request */
	uint32_t syscall = 0U;
	target_reg_read(target, 0, &syscall, sizeof(syscall));
	uint32_t r1 = 0U;
	target_reg_read(target, 1, &r1, sizeof(r1));

	/* Hand off to the main semihosting implementation */
	const int32_t result = semihosting_request(target, syscall, r1);

	/* Write the result back to the target */
	target_reg_write(target, 0, &result, sizeof(result));
	/* Return if the request was in any way interrupted */
	return target->tc->interrupted;
}

/*
 * Generate the FPU section of the description.
 * When a Cortex-M core has the optional FPU, the following XML-equivalent is generated:
 *  <feature name="org.gnu.gdb.arm.vfp">
 *      <reg name="fpscr" bitsize="32"/>
 *      <reg name="d0" bitsize="64" type="float"/>
 *      <reg name="d1" bitsize="64" type="float"/>
 *      <reg name="d2" bitsize="64" type="float"/>
 *      <reg name="d3" bitsize="64" type="float"/>
 *      <reg name="d4" bitsize="64" type="float"/>
 *      <reg name="d5" bitsize="64" type="float"/>
 *      <reg name="d6" bitsize="64" type="float"/>
 *      <reg name="d7" bitsize="64" type="float"/>
 *      <reg name="d8" bitsize="64" type="float"/>
 *      <reg name="d9" bitsize="64" type="float"/>
 *      <reg name="d10" bitsize="64" type="float"/>
 *      <reg name="d11" bitsize="64" type="float"/>
 *      <reg name="d12" bitsize="64" type="float"/>
 *      <reg name="d13" bitsize="64" type="float"/>
 *      <reg name="d14" bitsize="64" type="float"/>
 *      <reg name="d15" bitsize="64" type="float"/>
 *  </feature>"
 */
static size_t cortexm_build_target_fpu_description(char *const buffer, const size_t max_length)
{
	size_t offset = 0U;
	size_t print_size = max_length;
	/*
	 * Start by ending the previous feature block and starting the new FPU one.
	 * This includes the FPSCR entry which has to come before the VFP registers
	 */
	offset += snprintf(buffer + offset, print_size,
		"</feature><feature name=\"org.gnu.gdb.arm.vfp\">"
		"<reg name=\"fpscr\" bitsize=\"32\"/>");

	/* After FPSCR, the rest of the VFP registers follow a regular format: d0-d15, bitsize 64, type float. */
	for (uint8_t i = 0U; i < 16U; ++i) {
		if (max_length != 0U)
			print_size = max_length - offset;
		/* Generate the register entry */
		offset += snprintf(buffer + offset, print_size, "<reg name=\"d%u\" bitsize=\"64\" type=\"float\"/>", i);
	}

	/*
	 * We then leave the closing feature tag off because that will get generated on
	 * returning to cortexm_build_target_description() by the logic that finishes off the XML block.
	 */
	return offset;
}

/*
 * Generate the secext section of the description.
 * When a ARMv8-M core has the security (TrustZone) extension, the following XML-equivalent must be generated:
 *  <feature name="org.gnu.gdb.arm.m-secext">
 *      <reg name="msp_ns" bitsize="32" save-restore="no" type="data_ptr"/>
 *      <reg name="psp_ns" bitsize="32" save-restore="no" type="data_ptr"/>
 *      <reg name="msp_s" bitsize="32" save-restore="no" type="data_ptr"/>
 *      <reg name="psp_s" bitsize="32" save-restore="no" type="data_ptr"/>
 *  </feature>
 */
static size_t cortexm_build_target_secext_description(char *const buffer, const size_t max_length)
{
	size_t offset = 0U;
	size_t print_size = max_length;
	/* Start by ending the previous feature block and starting the new secext one. */
	offset +=
		(size_t)snprintf(buffer + offset, print_size, "</feature><feature name=\"org.gnu.gdb.arm.m-%s\">", "secext");

	/* Loop through first the non-secure and then the secure registers */
	for (uint8_t mode = 0U; mode <= 1U; ++mode) {
		/* Then loop through the MSP and PSP entries */
		for (size_t i = 4U; i <= 5U; ++i) {
			if (max_length != 0U)
				print_size = max_length - offset;

			/* Extract the register type and save-restore status from the tables at the top of the file */
			gdb_reg_type_e type = cortex_m_spr_types[i];
			gdb_reg_save_restore_e save_restore = cortex_m_spr_save_restores[i];

			/* Build an appropriate entry for the register */
			offset += (size_t)snprintf(buffer + offset, print_size, "<reg name=\"%s_%ss\" bitsize=\"%u\"%s%s/>",
				cortex_m_spr_names[i], mode == 0U ? "n" : "", cortex_m_spr_bitsizes[i],
				gdb_reg_save_restore_strings[save_restore], gdb_reg_type_strings[type]);
		}
	}

	/*
	 * We then leave the closing feature tag off because that will get generated on
	 * returning to cortexm_build_target_description() by the logic that follows.
	 */
	return offset;
}

/*
 * This function creates the target description XML string for a Cortex-M part.
 * This is done this way to decrease string duplication adn thus code size, making it
 * unfortunately much less readable than the string literal it is equvalent to.
 *
 * The backbone of this approach is snprintf() which will write no more than max_len bytes
 * to the buffer and returns the amount of bytes written. Or, if max_len is 0, then this
 * function will return the amount of bytes that would be necessary to create this string.
 *
 * The string it creates is XML-equivalent to the following:
 *  <?xml version=\"1.0\"?>
 *  <!DOCTYPE target SYSTEM \"gdb-target.dtd\">
 *  <target>
 *      <architecture>arm</architecture>
 *      <feature name="org.gnu.gdb.arm.m-profile">
 *          <reg name="r0" bitsize="32"/>
 *          <reg name="r1" bitsize="32"/>
 *          <reg name="r2" bitsize="32"/>
 *          <reg name="r3" bitsize="32"/>
 *          <reg name="r4" bitsize="32"/>
 *          <reg name="r5" bitsize="32"/>
 *          <reg name="r6" bitsize="32"/>
 *          <reg name="r7" bitsize="32"/>
 *          <reg name="r8" bitsize="32"/>
 *          <reg name="r9" bitsize="32"/>
 *          <reg name="r10" bitsize="32"/>
 *          <reg name="r11" bitsize="32"/>
 *          <reg name="r12" bitsize="32"/>
 *          <reg name="sp" bitsize="32" type="data_ptr"/>
 *          <reg name="lr" bitsize="32" type="code_ptr"/>
 *          <reg name="pc" bitsize="32" type="code_ptr"/>
 *          <reg name="xpsr" bitsize="32" regnum="25"/>
 *      </feature>
 *      <feature name="org.gnu.gdb.arm.m-system">
 *          <reg name="msp" bitsize="32" save-restore="no" type="data_ptr"/>
 *          <reg name="psp" bitsize="32" save-restore="no" type="data_ptr"/>
 *          <reg name="primask" bitsize="8" save-restore="no"/>
 *          <reg name="basepri" bitsize="8" save-restore="no"/>
 *          <reg name="faultmask" bitsize="8" save-restore="no"/>
 *          <reg name="control" bitsize="8" save-restore="no"/>
 *      </feature>
 *  </target>
 */
static size_t cortexm_build_target_description(
	char *const buffer, const size_t max_length, const uint32_t target_options)
{
	/*
	 * Minor hack: technically snprintf returns an int for possibility of error, but in this case
	 * these functions are given static input that should not be able to fail -- and if it does,
	 * then there's nothing we can do about it, so we'll repatedly cast its result to size_t
	 * when updating this variable (see below).
	 */
	size_t offset = 0U;
	/*
	 * We can't just repeatedly pass max_length to snprintf, because we keep changing the start
	 * of buffer (effectively changing its size), so we have to repeatedly recompute the size
	 * passed to snprintf by subtracting the current total from max_length.
	 * ...Unless max_length is 0, in which case that subtraction will result in an (underflowed)
	 * negative number. So we also have to repatedly check if max_length is 0 before performing
	 * that subtraction.
	 */
	size_t print_size = max_length;

	/*
	 * Start with the "preamble", which is generic across ARM targets,
	 * ...save for one word, so we'll have to do the preamble in halves.
	 */
	offset += (size_t)snprintf(buffer, print_size, "%s target %sarm%s <feature name=\"org.gnu.gdb.arm.m-profile\">",
		gdb_xml_preamble_first, gdb_xml_preamble_second, gdb_xml_preamble_third);

	/* Then the general purpose registers, which have names of r0 to r12, and all the same bitsize. */
	for (uint8_t i = 0U; i <= 12U; ++i) {
		if (max_length != 0)
			print_size = max_length - offset;
		offset += (size_t)snprintf(buffer + offset, print_size, "<reg name=\"r%u\" bitsize=\"32\"/>", i);
	}

	/*
	 * Now for sp, lr, pc, xpsr, msp, psp, primask, basepri, faultmask, and control.
	 * These special purpose registers are a little more complicated.
	 * Some of them have different bitsizes, specified types, or specified save-restore values.
	 * We'll use the 'associative arrays' defined for those values.
	 * NOTE: unlike the other loops, this loop uses a size_t for its counter, as it's used to index into arrays.
	 */
	for (size_t i = 0U; i < ARRAY_LENGTH(cortex_m_spr_names); ++i) {
		if (max_length != 0U)
			print_size = max_length - offset;

		/* Extract the register type and save-restore status from the tables at the top of the file */
		gdb_reg_type_e type = cortex_m_spr_types[i];
		gdb_reg_save_restore_e save_restore = cortex_m_spr_save_restores[i];

		/*
		 * There is one special extra thing that has to happen here -
		 * xPSR (reg index 3) requires placement at register logical number 25
		 */
		offset += (size_t)snprintf(buffer + offset, print_size, "<reg name=\"%s\" bitsize=\"%u\"%s%s%s/>",
			cortex_m_spr_names[i], cortex_m_spr_bitsizes[i], gdb_reg_save_restore_strings[save_restore],
			gdb_reg_type_strings[type], i == 3U ? " regnum=\"25\"" : "");

		/* After the xPSR, then need to generate the system block to receive system regs */
		if (i == 3U) {
			if (max_length != 0U)
				print_size = max_length - offset;

			offset += (size_t)snprintf(
				buffer + offset, print_size, "</feature><feature name=\"org.gnu.gdb.arm.m-%s\">", "system");
		}
	}

	/* If the target implements TrustZone, include the extra stack pointers */
	if (target_options & CORTEXM_TOPT_TRUSTZONE) {
		if (max_length != 0U)
			print_size = max_length - offset;
		offset += cortexm_build_target_secext_description(buffer + offset, print_size);
	}

	/* If the target has a FPU, include that */
	if (target_options & CORTEXM_TOPT_FLAVOUR_FLOAT) {
		if (max_length != 0U)
			print_size = max_length - offset;
		offset += cortexm_build_target_fpu_description(buffer + offset, print_size);
	}

	/* Now generate the closing tags that are required */
	if (max_length != 0U)
		print_size = max_length - offset;
	offset += (size_t)snprintf(buffer + offset, print_size, "</feature></target>");

	/*
	 * Minor hack: technically snprintf returns an int for possibility of error, but in this case
	 * these functions are given static input that should not ever be able to fail -- and if it
	 * does, then there's nothing we can do about it, so we just discard the signedness when building
	 * the total in `offset` as we go.
	 */
	return offset;
}

static const char *cortexm_target_description(target_s *const target)
{
	const size_t description_length = cortexm_build_target_description(NULL, 0, target->target_options) + 1U;
	char *const description = malloc(description_length);
	if (description)
		cortexm_build_target_description(description, description_length, target->target_options);
	return description;
}
