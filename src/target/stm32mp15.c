/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by ALTracer <tolstov_den@mail.ru>
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

/* Memory map constants for STM32MP15x */
#define STM32MP15_CM4_RETRAM_BASE  0x00000000U
#define STM32MP15_RETRAM_SIZE      0x00001000U /* RETRAM, 64 KiB */
#define STM32MP15_CM4_AHBSRAM_BASE 0x10000000U
#define STM32MP15_AHBSRAM_SIZE     0x00060000U /* AHB SRAM 1+2+3+4, 128+128+64+64 KiB */

/* Access from processor address space.
 * Access via the debug APB is at 0xe0081000 over AP1. */
#define STM32MP15_DBGMCU_BASE 0x50081000U
#define STM32MP15_UID_BASE    0x5c005234U

#define DBGMCU_IDCODE        (STM32MP15_DBGMCU_BASE + 0U)
#define DBGMCU_CTRL          (STM32MP15_DBGMCU_BASE + 4U)
#define DBGMCU_CTRL_DBGSLEEP (1U << 0U)
#define DBGMCU_CTRL_DBGSTOP  (1U << 1U)
#define DBGMCU_CTRL_DBGSTBY  (1U << 2U)

/* Taken from DP_TARGETID.TPARTNO = 0x5000 in §66.8.3 of RM0436 rev 6, pg3669 */
/* Taken from DBGMCU_IDC.DEV_ID = 0x500 in §66.10.9 of RM0436 rev 6, pg3825 */
#define ID_STM32MP15x 0x5000U
/* Taken from CM4ROM_PIDRx in 2.3.21 of ES0438 rev 7, pg18 */
#define ID_STM32MP15x_ERRATA 0x450U

typedef struct stm32mp15_priv {
	uint32_t dbgmcu_ctrl;
} stm32mp15_priv_s;

static bool stm32mp15_uid(target_s *target, int argc, const char **argv);
static bool stm32mp15_cmd_rev(target_s *target, int argc, const char **argv);

const command_s stm32mp15_cmd_list[] = {
	{"uid", stm32mp15_uid, "Print unique device ID"},
	{"revision", stm32mp15_cmd_rev, "Returns the Device ID and Revision"},
	{NULL, NULL, NULL},
};

static bool stm32mp15_attach(target_s *target)
{
	if (!cortexm_attach(target))
		return false;

	/* Save DBGMCU_CR to restore it when detaching */
	stm32mp15_priv_s *const priv = (stm32mp15_priv_s *)target->target_storage;
	priv->dbgmcu_ctrl = target_mem_read32(target, DBGMCU_CTRL);

	/* Disable C-Sleep, C-Stop, C-Standby for debugging */
	target_mem_write32(target, DBGMCU_CTRL, DBGMCU_CTRL_DBGSLEEP | DBGMCU_CTRL_DBGSTOP | DBGMCU_CTRL_DBGSTBY);

	return true;
}

static void stm32mp15_detach(target_s *target)
{
	stm32mp15_priv_s *priv = (stm32mp15_priv_s *)target->target_storage;
	target_mem_write32(target, DBGMCU_CTRL, priv->dbgmcu_ctrl);
	cortexm_detach(target);
}

bool stm32mp15_cm4_probe(target_s *target)
{
	if (target->part_id != ID_STM32MP15x && target->part_id != ID_STM32MP15x_ERRATA)
		return false;

	target->driver = "STM32MP15";
	target->attach = stm32mp15_attach;
	target->detach = stm32mp15_detach;
	target_add_commands(target, stm32mp15_cmd_list, target->driver);

	/* Allocate private storage */
	stm32mp15_priv_s *priv = calloc(1, sizeof(*priv));
	target->target_storage = priv;

	/* Figure 4. Memory map from §2.5.2 in RM0436 rev 6, pg158 */
	target_add_ram(target, STM32MP15_CM4_RETRAM_BASE, STM32MP15_RETRAM_SIZE);
	target_add_ram(target, STM32MP15_CM4_AHBSRAM_BASE, STM32MP15_AHBSRAM_SIZE);

	return true;
}

/*
 * Print the Unique device ID.
 * Can be reused for other STM32 devices with uid as parameter.
 */
static bool stm32mp15_uid(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	tc_printf(target, "0x");
	for (size_t i = 0; i < 12U; i += 4U) {
		const uint32_t value = target_mem_read32(target, STM32MP15_UID_BASE + i);
		tc_printf(target, "%02X%02X%02X%02X", (value >> 24U) & 0xffU, (value >> 16U) & 0xffU, (value >> 8U) & 0xffU,
			value & 0xffU);
	}
	tc_printf(target, "\n");
	return true;
}

static const struct {
	uint16_t rev_id;
	char revision;
} stm32mp15x_revisions[] = {
	{0x2000U, 'B'},
	{0x2001U, 'Z'},
};

static bool stm32mp15_cmd_rev(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* DBGMCU identity code register */
	const uint32_t dbgmcu_idcode = target_mem_read32(target, DBGMCU_IDCODE);
	const uint16_t rev_id = (dbgmcu_idcode >> 16U) & 0xffffU;
	const uint16_t dev_id = (dbgmcu_idcode & 0xfffU) << 4U;

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
