/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012 Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022-2025 1BitSquared <info@1bitsquared.com>
 * Written by Akila Ravihansa Perera <ravihansa3000@gmail.com>
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
 * This file implements support for LPC17xx series devices, providing
 * memory maps and Flash programming routines.
 *
 * References and details about the IAP variant used here:
 * LPC1759/58/56/54/52/51 32-bit ARM Cortex-M3, Product data sheet, Rev. 8.7
 *   https://www.nxp.com/docs/en/data-sheet/LPC1759_58_56_54_52_51.pdf
 * and (behind their login wall):
 * UM10360 - LPC176x/5x User manual
 *   https://www.nxp.com/webapp/Download?colCode=UM10360&location=null
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "lpc_common.h"

#define LPC17xx_SRAM_SIZE_MIN 8192U // LPC1751
#define LPC17xx_SRAM_IAP_SIZE 32U   // IAP routines use 32 bytes at top of ram

#define LPC17xx_IAP_ENTRYPOINT_LOCATION 0x1fff1ff1U
#define LPC17xx_IAP_RAM_BASE            0x10000000U

#define LPC17xx_IAP_PGM_CHUNKSIZE 4096U

#define LPC17xx_FLASH_NUM_SECTOR 30U

#define LPC17xx_MEMMAP   UINT32_C(0x400fc040)
#define LPC17xx_MPU_BASE UINT32_C(0xe000ed90)
#define LPC17xx_MPU_CTRL (LPC17xx_MPU_BASE + 0x04U)

typedef struct lpc17xx_priv {
	lpc_priv_s base;
	uint32_t mpu_ctrl_state;
	uint32_t memmap_state;
} lpc17xx_priv_s;

static bool lpc17xx_read_uid(target_s *target, int argc, const char **argv);

const command_s lpc17xx_cmd_list[] = {
	{"readuid", lpc17xx_read_uid, "Read out the 16-byte UID."},
	{NULL, NULL, NULL},
};

static void lpc17xx_extended_reset(target_s *target);
static bool lpc17xx_enter_flash_mode(target_s *target);
static bool lpc17xx_exit_flash_mode(target_s *target);
static bool lpc17xx_mass_erase(target_s *target, platform_timeout_s *print_progess);

static size_t lpc17xx_iap_params(iap_cmd_e cmd);

static void lpc17xx_add_flash(
	target_s *const target, const uint32_t addr, const size_t len, const size_t erasesize, const uint8_t base_sector)
{
	lpc_flash_s *const flash = lpc_add_flash(target, addr, len, LPC17xx_IAP_PGM_CHUNKSIZE);
	flash->target_flash.blocksize = erasesize;
	flash->base_sector = base_sector;
	flash->target_flash.write = lpc_flash_write_magic_vect;
}

bool lpc17xx_probe(target_s *const target)
{
	if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) != CORTEX_M3)
		return false;

	/*
	 * Now that we're sure it's a Cortex-M3, we need to halt the
	 * target and make an IAP call to get the part number.
	 * There appears to have no other method of reading the part number.
	 */
	target_halt_request(target);

	/* Allocate private storage so the flash mode entry/exit routines can save state */
	lpc17xx_priv_s *const priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	target->target_storage = priv;

	/* Set the structure up for this target */
	priv->base.iap_params = lpc17xx_iap_params;
	priv->base.iap_entry = LPC17xx_IAP_ENTRYPOINT_LOCATION;
	priv->base.iap_ram = LPC17xx_IAP_RAM_BASE;
	priv->base.iap_msp = LPC17xx_IAP_RAM_BASE + LPC17xx_SRAM_SIZE_MIN - LPC17xx_SRAM_IAP_SIZE;

	/* Prepare Flash mode */
	lpc17xx_enter_flash_mode(target);
	/* Read the Part ID */
	iap_result_s result;
	lpc_iap_call(target, &result, IAP_CMD_PARTID);
	/* Transition back to normal mode and resume the target */
	lpc17xx_exit_flash_mode(target);
	target_halt_resume(target, false);

	/*
	 * If we got an error response, it cannot be a LPC17xx as the only response
	 * a real device gives is IAP_STATUS_CMD_SUCCESS.
	 */
	if (result.return_code) {
		free(priv);
		target->target_storage = NULL;
		return false;
	}

	switch (result.values[0]) {
	case 0x26113f37U: /* LPC1769 */
	case 0x26013f37U: /* LPC1768 */
	case 0x26012837U: /* LPC1767 */
	case 0x26013f33U: /* LPC1766 */
	case 0x26013733U: /* LPC1765 */
	case 0x26011922U: /* LPC1764 */
	case 0x25113737U: /* LPC1759 */
	case 0x25013f37U: /* LPC1758 */
	case 0x25011723U: /* LPC1756 */
	case 0x25011722U: /* LPC1754 */
	case 0x25001121U: /* LPC1752 */
	case 0x25001118U: /* LPC1751 */
	case 0x25001110U: /* LPC1751 (No CRP) */
		break;
	default:
		return false;
	}

	target->driver = "LPC17xx";
	target->extended_reset = lpc17xx_extended_reset;
	target->mass_erase = lpc17xx_mass_erase;
	target->enter_flash_mode = lpc17xx_enter_flash_mode;
	target->exit_flash_mode = lpc17xx_exit_flash_mode;
	target_add_ram32(target, 0x10000000U, 0x8000U);
	target_add_ram32(target, 0x2007c000U, 0x4000U);
	target_add_ram32(target, 0x20080000U, 0x4000U);
	lpc17xx_add_flash(target, 0x00000000U, 0x10000U, 0x1000U, 0);
	lpc17xx_add_flash(target, 0x00010000U, 0x70000U, 0x8000U, 16);
	target_add_commands(target, lpc17xx_cmd_list, target->driver);
	return true;
}

static bool lpc17xx_enter_flash_mode(target_s *const target)
{
	lpc17xx_priv_s *const priv = (lpc17xx_priv_s *)target->target_storage;
	/* Disable the MPU, if enabled */
	priv->mpu_ctrl_state = target_mem32_read32(target, LPC17xx_MPU_CTRL);
	target_mem32_write32(target, LPC17xx_MPU_CTRL, 0);
	/* And store the memory mapping state */
	priv->memmap_state = target_mem32_read32(target, LPC17xx_MEMMAP);
	return true;
}

static bool lpc17xx_exit_flash_mode(target_s *const target)
{
	const lpc17xx_priv_s *const priv = (lpc17xx_priv_s *)target->target_storage;
	/* Restore the memory mapping and MPU state (in that order!) */
	target_mem32_write32(target, LPC17xx_MEMMAP, priv->memmap_state);
	target_mem32_write32(target, LPC17xx_MPU_CTRL, priv->mpu_ctrl_state);
	return true;
}

static bool lpc17xx_mass_erase(target_s *const target, platform_timeout_s *const print_progess)
{
	(void)print_progess;
	iap_result_s result;

	if (lpc_iap_call(target, &result, IAP_CMD_PREPARE, 0, LPC17xx_FLASH_NUM_SECTOR - 1U)) {
		DEBUG_ERROR("%s: prepare failed %" PRIu32 "\n", __func__, result.return_code);
		return false;
	}

	if (lpc_iap_call(target, &result, IAP_CMD_ERASE, 0, LPC17xx_FLASH_NUM_SECTOR - 1U, CPU_CLK_KHZ)) {
		DEBUG_ERROR("%s: erase failed %" PRIu32 "\n", __func__, result.return_code);
		return false;
	}

	if (lpc_iap_call(target, &result, IAP_CMD_BLANKCHECK, 0, LPC17xx_FLASH_NUM_SECTOR - 1U)) {
		DEBUG_ERROR("%s: blankcheck failed %" PRIu32 "\n", __func__, result.return_code);
		return false;
	}

	return true;
}

/*
 * Target has been reset, make sure to remap the boot ROM
 * from 0x00000000 leaving the user flash visible
 */
static void lpc17xx_extended_reset(target_s *const target)
{
	/*
	 * Transition the memory map to user mode (if it wasn't already) to ensure
	 * the correct environment is seen by the user
	 * See §33.6 Debug memory re-mapping, pg655 of UM10360 for more details.
	 */
	target_mem32_write32(target, LPC17xx_MEMMAP, 1);
}

static bool lpc17xx_read_uid(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;
	iap_result_s result = {0};
	if (lpc_iap_call(target, &result, IAP_CMD_READUID))
		return false;
	uint8_t uid[16U] = {0};
	memcpy(&uid, result.values, sizeof(uid));
	tc_printf(target, "UID: 0x");
	for (size_t i = 0; i < sizeof(uid); ++i)
		tc_printf(target, "%02x", uid[i]);
	tc_printf(target, "\n");
	return true;
}

static size_t lpc17xx_iap_params(const iap_cmd_e cmd)
{
	switch (cmd) {
	case IAP_CMD_PREPARE:
	case IAP_CMD_BLANKCHECK:
		return 2U;
	case IAP_CMD_ERASE:
		return 3U;
	default:
		return 0U;
	}
}
