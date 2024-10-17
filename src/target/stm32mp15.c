/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023-2024 1BitSquared <info@1bitsquared.com>
 * Written by ALTracer <tolstov_den@mail.ru>
 * Modified by Rachel Mant <git@dragonmux.network>
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
 * This file implements STM32MP15 target specific functions for detecting
 * the device and providing the XML memory map.
 *
 * References:
 * RM0436 - STM32MP157 advanced Arm®-based 32-bit MPUs, Rev. 5
 *   https://www.st.com/resource/en/reference_manual/rm0436-stm32mp157-advanced-armbased-32bit-mpus-stmicroelectronics.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "stm32_common.h"
#include "adiv5.h"
#include "cortexar.h"

/* Memory map constants for STM32MP15x */
#define STM32MP15_CM4_RETRAM_BASE        0x00000000U
#define STM32MP15_CA7_RETRAM_BASE        0x38000000U
#define STM32MP15_RETRAM_SIZE            0x00010000U /* RETRAM, 64 KiB */
#define STM32MP15_AHBSRAM_BASE           0x10000000U
#define STM32MP15_CA7_AHBSRAM_ALIAS_BASE 0x30000000U
#define STM32MP15_AHBSRAM_SIZE           0x00060000U /* AHB SRAM 1+2+3+4, 128+128+64+64 KiB */
#define STM32MP15_SYSRAM_BASE            0x2ffc0000U
#define STM32MP15_SYSRAM_SIZE            0x00040000U
#define STM32MP15_CAN_SRAM_BASE          0x44011000U
#define STM32MP15_CAN_SRAM_SIZE          0x00002800U

/* Access from processor address space.
 * Access via the debug APB is at 0xe0081000 over AP1. */
#define STM32MP15_DBGMCU_BASE 0x50081000U
#define STM32MP15_UID_BASE    0x5c005234U

#define STM32MP15_DBGMCU_IDCODE      (STM32MP15_DBGMCU_BASE + 0x000U)
#define STM32MP15_DBGMCU_CONFIG      (STM32MP15_DBGMCU_BASE + 0x004U)
#define STM32MP15_DBGMCU_APB1FREEZE1 (STM32MP15_DBGMCU_BASE + 0x034U)
#define STM32MP15_DBGMCU_APB1FREEZE2 (STM32MP15_DBGMCU_BASE + 0x038U)

#define STM32MP15_DBGMCU_CONFIG_DBGSLEEP         (1U << 0U)
#define STM32MP15_DBGMCU_CONFIG_DBGSTOP          (1U << 1U)
#define STM32MP15_DBGMCU_CONFIG_DBGSTBY          (1U << 2U)
#define STM32MP15_DBGMCU_CONFIG_IWDG1_FREEZE_AND (1U << 24U)
// Freeze for WWDG1 when debugging the Cortex-A7 core
#define STM32MP15_DBGMCU_APB1FREEZE1_WWDG1 (1U << 10U)
// Freeze for WWDG1 when debugging the Cortex-M4 core
#define STM32MP15_DBGMCU_APB1FREEZE2_WWDG1 (1U << 10U)

#define STM32MP15_DBGMCU_IDCODE_DEV_MASK  0x00000fffU
#define STM32MP15_DBGMCU_IDCODE_REV_SHIFT 16U

/* Taken from DP_TARGETID.TPARTNO = 0x5000 in §66.8.3 of RM0436 rev 6, pg3669 */
/* Taken from DBGMCU_IDC.DEV_ID = 0x500 in §66.10.9 of RM0436 rev 6, pg3825 */
#define ID_STM32MP15x 0x500U
/* Taken from CM4ROM_PIDRx in 2.3.21 of ES0438 rev 7, pg18 */
#define ID_STM32MP15x_ERRATA 0x450U

#define SWO_BASE 0xe0083000
#define SWO_ACPR (SWO_BASE + 0x00010)
#define SWO_SPPR (SWO_BASE + 0x000f0)

typedef struct stm32mp15_priv {
	uint32_t dbgmcu_config;
} stm32mp15_priv_s;

static bool stm32mp15_uid(target_s *target, int argc, const char **argv);
static bool stm32mp15_cmd_rev(target_s *target, int argc, const char **argv);
static bool stm32mp15_cmd_swo(target_s *target, int argc, const char **argv);

const command_s stm32mp15_cmd_list[] = {
	{"uid", stm32mp15_uid, "Print unique device ID"},
	{"revision", stm32mp15_cmd_rev, "Returns the Device ID and Revision"},
	{"conf_swo", stm32mp15_cmd_swo, "Set up SWO mode <1/2> and divisor <0x42>"},
	{NULL, NULL, NULL},
};

static bool stm32mp15_cm4_attach(target_s *target);
static void stm32mp15_cm4_detach(target_s *target);

static bool stm32mp15_ident(target_s *const target, const bool cortexm)
{
	const adiv5_access_port_s *const ap = cortex_ap(target);
	/* Check if the part's a STM32MP15 */
	if (ap->partno != ID_STM32MP15x) {
		/* If it's not a Cortex-M core or it doesn't match the errata ID code, return false */
		if (!cortexm || ap->partno != ID_STM32MP15x_ERRATA)
			return false;
	}

	/* By now it's established that this is likely an MP15x_CM4, but check that it's not an H74x */
	const uint32_t idcode = target_mem32_read32(target, STM32MP15_DBGMCU_IDCODE);
	const uint16_t dev_id = idcode & STM32MP15_DBGMCU_IDCODE_DEV_MASK;
	DEBUG_TARGET(
		"%s: looking at device ID 0x%03x at 0x%08" PRIx32 "\n", __func__, dev_id, (uint32_t)STM32MP15_DBGMCU_IDCODE);
	/* If this probe routine ever runs ahead of stm32h7_probe, skip the H74x. */
	if (dev_id != ID_STM32MP15x)
		return false;

	/*
	 * We now know the part is either a Cortex-M core with the errata code, or matched the main ID code.
	 * Copy the correct (AP) part number over to the target structure to handle the difference between
	 * JTAG and SWD as ST has a different ID in the DP TARGETID register vs the ROM tables, which needs ignoring.
	 */
	target->part_id = ap->partno;
	return true;
}

static bool stm32mp15_cmd_swo(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* TODO: argv parsing for mode and baudrate */
	adiv5_access_port_s *const ap_apbd = (adiv5_access_port_s *)target->target_storage;
	/* Pin Protocol: change Manchester to UART */
	uint32_t sppr = 0;
	adiv5_mem_read(ap_apbd, &sppr, SWO_SPPR, 4);
	sppr &= ~(0x3U);
	sppr |= 0x2U;
	adiv5_mem_write(ap_apbd, SWO_SPPR, &sppr, 4);

	/*
	 * Prescaler: set to fixed 66; trace clk freq of 133/(66+1) gives ~2Mbaud (+-0.7%)
	 * assuming AXI clk of 266 and default divisor of 2
	 * Or, if you are not restricted by swlink 2.25M, set to fixed 32; 133/(32+1) is ~4Mbaud (+-0.7%)
	 */
	uint32_t acpr = 0;
	adiv5_mem_read(ap_apbd, &acpr, SWO_ACPR, 4);
	acpr = 32;
	adiv5_mem_write(ap_apbd, SWO_ACPR, &acpr, 4);

	return true;
}

static void stm32mp15_cm4_setup_apbd_ap(target_s *const target)
{
	adiv5_access_port_s *ap_apbd = calloc(1, sizeof(*ap_apbd));
	if (!ap_apbd) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}
	adiv5_access_port_s *const ap = cortex_ap(target);
	memcpy(ap_apbd, ap, sizeof(*ap_apbd));

	ap_apbd->apsel = 1; // Set to APB-D AP
	ap_apbd->idr = adiv5_ap_read(ap_apbd, ADIV5_AP_IDR);
	ap_apbd->base = adiv5_ap_read(ap_apbd, ADIV5_AP_BASE_LOW);
	ap_apbd->csw = adiv5_ap_read(ap_apbd, ADIV5_AP_CSW);

	adiv5_ap_ref(ap_apbd);
	target->target_storage = ap_apbd;
}

static bool stm32mp15_cm4_configure_dbgmcu(target_s *const target)
{
	/* If we're in the probe phase */
	if (target->target_storage == NULL) {
		/* Allocate target-specific storage */
		stm32mp15_priv_s *const priv_storage = calloc(1, sizeof(*priv_storage));
		if (!priv_storage) { /* calloc failed: heap exhaustion */
			DEBUG_ERROR("calloc: failed in %s\n", __func__);
			return false;
		}
		target->target_storage = priv_storage;
		/* Get the current value of the debug control register (and store it for later) */
		priv_storage->dbgmcu_config = target_mem32_read32(target, STM32MP15_DBGMCU_CONFIG);

		/* Finally set up the attach/detach functions needed */
		target->attach = stm32mp15_cm4_attach;
		target->detach = stm32mp15_cm4_detach;
	}

	const stm32mp15_priv_s *const priv = (stm32mp15_priv_s *)target->target_storage;
	/* Disable C-Sleep, C-Stop, C-Standby for debugging, and make sure IWDG1 freezes when any core is halted */
	target_mem32_write32(target, STM32MP15_DBGMCU_CONFIG,
		(priv->dbgmcu_config & ~STM32MP15_DBGMCU_CONFIG_IWDG1_FREEZE_AND) | STM32MP15_DBGMCU_CONFIG_DBGSLEEP |
			STM32MP15_DBGMCU_CONFIG_DBGSTOP | STM32MP15_DBGMCU_CONFIG_DBGSTBY);
	/* And make sure the WDTs stay synchronised to the run state of the processor */
	target_mem32_write32(target, STM32MP15_DBGMCU_APB1FREEZE2, STM32MP15_DBGMCU_APB1FREEZE2_WWDG1);
	return true;
}

bool stm32mp15_cm4_probe(target_s *const target)
{
	/* Try to identify the part */
	if (!stm32mp15_ident(target, true))
		return false;

	/* Now we have a stable debug environment, make sure the WDTs + WFI and WFE instructions can't cause problems */
	if (!stm32mp15_cm4_configure_dbgmcu(target))
		return false;

	target->driver = "STM32MP15";
	target_add_commands(target, stm32mp15_cmd_list, target->driver);

	/* Figure 4. Memory map from §2.5.2 in RM0436 rev 6, pg158 */
	target_add_ram32(target, STM32MP15_CM4_RETRAM_BASE, STM32MP15_RETRAM_SIZE);
	target_add_ram32(target, STM32MP15_AHBSRAM_BASE, STM32MP15_AHBSRAM_SIZE);
	return true;
}

#ifdef ENABLE_CORTEXAR
/*
 * Override memory r/w operations to go via the MEM-AP
 * (instead of halting the core and using DTRTX, which cortexar_mem_read/write do by default)
 */
static void stm32mp15_ca7_mem_read(target_s *target, void *dest, target_addr64_t src, size_t len)
{
	adiv5_access_port_s *const ap_axi = (adiv5_access_port_s *)target->target_storage;
	adiv5_mem_read(ap_axi, dest, src, len);
}

static void stm32mp15_ca7_mem_write(target_s *target, target_addr64_t dest, const void *src, size_t len)
{
	adiv5_access_port_s *const ap_axi = (adiv5_access_port_s *)target->target_storage;
	adiv5_mem_write(ap_axi, dest, src, len);
}

static void stm32mp15_ca7_setup_axi_ap(target_s *const target)
{
	adiv5_access_port_s *ap_axi = calloc(1, sizeof(*ap_axi));
	if (!ap_axi) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}
	adiv5_access_port_s *const ap = cortex_ap(target);
	memcpy(ap_axi, ap, sizeof(*ap_axi));

	ap_axi->apsel = 0; // Set to AXI-AP
	ap_axi->idr = adiv5_ap_read(ap_axi, ADIV5_AP_IDR);
	ap_axi->base = adiv5_ap_read(ap_axi, ADIV5_AP_BASE_LOW);
	ap_axi->csw = adiv5_ap_read(ap_axi, ADIV5_AP_CSW);

	adiv5_ap_ref(ap_axi);
	target->target_storage = ap_axi;
}

static void stm32mp15_ca7_detach(target_s *target)
{
	/* Deallocate any extra AP */
	adiv5_access_port_s *ap = (adiv5_access_port_s *)target->target_storage;
	adiv5_ap_unref(ap);
	cortexar_detach(target);
}

bool stm32mp15_ca7_probe(target_s *const target)
{
	if (!stm32mp15_ident(target, false))
		return false;

	target->driver = "STM32MP15";
	stm32mp15_ca7_setup_axi_ap(target);
	target->mem_read = stm32mp15_ca7_mem_read;
	target->mem_write = stm32mp15_ca7_mem_write;
	target->detach = stm32mp15_ca7_detach;
	target_add_commands(target, stm32mp15_cmd_list, target->driver);

	/* Figure 4. Memory map from §2.5.2 in RM0436 rev 6, pg158 */
	target_add_ram32(target, STM32MP15_CA7_RETRAM_BASE, STM32MP15_RETRAM_SIZE);
	target_add_ram32(target, STM32MP15_AHBSRAM_BASE, STM32MP15_AHBSRAM_SIZE);
	/*
	 * The SRAM appears twice in the map as it's mapped to both the main SRAM
	 * window and the alias window on the Cortex-A7 cores.
	 * (Unlike the RETRAM which only appears in the alias window)
	 */
	target_add_ram32(target, STM32MP15_CA7_AHBSRAM_ALIAS_BASE, STM32MP15_AHBSRAM_SIZE);
	target_add_ram32(target, STM32MP15_SYSRAM_BASE, STM32MP15_SYSRAM_SIZE);
	target_add_ram32(target, STM32MP15_CAN_SRAM_BASE, STM32MP15_CAN_SRAM_SIZE);
	return true;
}
#endif

static bool stm32mp15_cm4_attach(target_s *const target)
{
	/*
	 * Try to attach to the part, and then ensure that the WDTs + WFI and WFE
	 * instructions can't cause problems (this is duplicated as it's undone by detach.)
	 */
	if (!cortexm_attach(target))
		return false;
	if (!stm32mp15_cm4_configure_dbgmcu(target))
		return false;
	/* Reference the APB-D in target storage for 0xe0000000 region manipulations */
	stm32mp15_cm4_setup_apbd_ap(target);
	return true;
}

static void stm32mp15_cm4_detach(target_s *const target)
{
	stm32mp15_priv_s *priv = (stm32mp15_priv_s *)target->target_storage;
	/* Reverse all changes to the DBGMCU config register */
	target_mem32_write32(target, STM32MP15_DBGMCU_CONFIG, priv->dbgmcu_config);

	/* Deallocate any extra AP */
	adiv5_access_port_s *ap = (adiv5_access_port_s *)target->target_storage;
	adiv5_ap_unref(ap);

	/* Now defer to the normal Cortex-M detach routine to complete the detach */
	cortexm_detach(target);
}

static bool stm32mp15_uid(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;
	return stm32_uid(target, STM32MP15_UID_BASE);
}

static const struct {
	uint16_t rev_id;
	char revision;
} stm32mp15x_revisions[] = {
	{0x2000U, 'B'},
	{0x2001U, 'Z'},
};

static bool stm32mp15_cmd_rev(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;
	/* DBGMCU identity code register */
	const uint32_t dbgmcu_idcode = target_mem32_read32(target, STM32MP15_DBGMCU_IDCODE);
	const uint16_t rev_id = dbgmcu_idcode >> STM32MP15_DBGMCU_IDCODE_REV_SHIFT;
	const uint16_t dev_id = dbgmcu_idcode & STM32MP15_DBGMCU_IDCODE_DEV_MASK;

	/* Print device */
	switch (dev_id) {
	case ID_STM32MP15x:
		tc_printf(target, "STM32MP15x\n");

		/* Print revision */
		char rev = '?';
		for (size_t i = 0; i < ARRAY_LENGTH(stm32mp15x_revisions); i++) {
			/* Check for matching revision */
			if (stm32mp15x_revisions[i].rev_id == rev_id)
				rev = stm32mp15x_revisions[i].revision;
		}
		tc_printf(target, "Revision %c\n", rev);
		break;

	default:
		tc_printf(target, "Unknown %s. BMP may not correctly support it!\n", target->driver);
	}

	return true;
}
