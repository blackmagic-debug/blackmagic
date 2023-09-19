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
#define STM32MP15_RETRAM_BASE  0x00000000U
#define STM32MP15_RETRAM_SIZE  0x00001000U /* RETRAM, 64 KiB */
#define STM32MP15_AHBSRAM_BASE 0x10000000U
#define STM32MP15_AHBSRAM_SIZE 0x00060000U /* AHB SRAM 1+2+3+4, 128+128+64+64 KiB */

/* Taken from DP_TARGETID.TPARTNO = 0x5000 in §66.8.3 of RM0436 rev 6, pg3669 */
/* Taken from DBGMCU_IDC.DEV_ID = 0x500 in §66.10.9 of RM0436 rev 6, pg3825 */
#define ID_STM32MP15x 0x5000U
/* Taken from CM4ROM_PIDRx in 2.3.21 of ES0438 rev 7, pg18 */
#define ID_STM32MP15x_ERRATA 0x450U

bool stm32mp15_cm4_probe(target_s *target)
{
	if (target->part_id != ID_STM32MP15x && target->part_id != ID_STM32MP15x_ERRATA)
		return false;

	target->driver = "STM32MP15";
	target->attach = cortexm_attach;
	target->detach = cortexm_detach;

	/* Figure 4. Memory map from §2.5.2 in RM0436 rev 6, pg158 */
	target_add_ram(target, STM32MP15_RETRAM_BASE, STM32MP15_RETRAM_SIZE);
	target_add_ram(target, STM32MP15_AHBSRAM_BASE, STM32MP15_AHBSRAM_SIZE);

	return true;
}
