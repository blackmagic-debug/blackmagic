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

typedef struct iap_config {
	uint32_t command;
	uint32_t params[4];
} iap_config_s;

typedef struct BMD_ALIGN_DECL(4) iap_frame {
	/* The start of an IAP stack frame is the opcode we set as the return point. */
	uint16_t opcode;
	/* There's then a hidden alignment field here, followed by the IAP call setup */
	iap_config_s config;
	iap_result_s result;
} iap_frame_s;

typedef struct lpc17xx_priv {
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
iap_status_e lpc17xx_iap_call(
	target_s *target, iap_result_s *result, platform_timeout_s *print_progess, iap_cmd_e cmd, ...);

static void lpc17xx_add_flash(
	target_s *const target, const uint32_t addr, const size_t len, const size_t erasesize, const uint8_t base_sector)
{
	lpc_flash_s *const flash = lpc_add_flash(target, addr, len, LPC17xx_IAP_PGM_CHUNKSIZE);
	flash->target_flash.blocksize = erasesize;
	flash->base_sector = base_sector;
	flash->target_flash.write = lpc_flash_write_magic_vect;
	flash->iap_entry = LPC17xx_IAP_ENTRYPOINT_LOCATION;
	flash->iap_ram = LPC17xx_IAP_RAM_BASE;
	flash->iap_msp = LPC17xx_IAP_RAM_BASE + LPC17xx_SRAM_SIZE_MIN - LPC17xx_SRAM_IAP_SIZE;
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

	/* Prepare Flash mode */
	lpc17xx_enter_flash_mode(target);
	/* Read the Part ID */
	iap_result_s result;
	lpc17xx_iap_call(target, &result, NULL, IAP_CMD_PARTID);
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
	iap_result_s result;

	if (lpc17xx_iap_call(target, &result, print_progess, IAP_CMD_PREPARE, 0, LPC17xx_FLASH_NUM_SECTOR - 1U)) {
		DEBUG_ERROR("%s: prepare failed %" PRIu32 "\n", __func__, result.return_code);
		return false;
	}

	if (lpc17xx_iap_call(
			target, &result, print_progess, IAP_CMD_ERASE, 0, LPC17xx_FLASH_NUM_SECTOR - 1U, CPU_CLK_KHZ)) {
		DEBUG_ERROR("%s: erase failed %" PRIu32 "\n", __func__, result.return_code);
		return false;
	}

	if (lpc17xx_iap_call(target, &result, print_progess, IAP_CMD_BLANKCHECK, 0, LPC17xx_FLASH_NUM_SECTOR - 1U)) {
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
	if (lpc17xx_iap_call(target, &result, NULL, IAP_CMD_READUID))
		return false;
	uint8_t uid[16U] = {0};
	memcpy(&uid, result.values, sizeof(uid));
	tc_printf(target, "UID: 0x");
	for (size_t i = 0; i < sizeof(uid); ++i)
		tc_printf(target, "%02x", uid[i]);
	tc_printf(target, "\n");
	return true;
}

void lpc17xx_save_state(target_s *const target, const uint32_t iap_ram, iap_frame_s *const frame, uint32_t *const regs)
{
	/* Save IAP RAM to restore after IAP call */
	target_mem32_read(target, frame, iap_ram, sizeof(iap_frame_s));
	/* Save registers to restore after IAP call */
	target_regs_read(target, regs);
}

void lpc17xx_restore_state(
	target_s *const target, const uint32_t iap_ram, const iap_frame_s *const frame, const uint32_t *const regs)
{
	target_mem32_write(target, iap_ram, frame, sizeof(iap_frame_s));
	target_regs_write(target, regs);
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

iap_status_e lpc17xx_iap_call(
	target_s *const target, iap_result_s *const result, platform_timeout_s *const print_progess, iap_cmd_e cmd, ...)
{
	lpc_flash_s *flash = (lpc_flash_s *)target->flash;

	/* Save IAP RAM and target registers to restore after IAP call */
	iap_frame_s saved_frame;
	/*
	 * Note, we allocate space for the float regs even if the CPU doesn't implement them.
	 * The Cortex register IO routines will avoid touching the unused slots and this avoids a VLA.
	 */
	uint32_t saved_regs[CORTEXM_GENERAL_REG_COUNT];
	lpc17xx_save_state(target, flash->iap_ram, &saved_frame, saved_regs);

	/* Set up our IAP frame with the break opcode and command to run */
	iap_frame_s frame = {
		.opcode = CORTEX_THUMB_BREAKPOINT,
		.config = {.command = cmd},
	};

	/* Fill out the remainder of the parameters */
	const size_t params_count = lpc17xx_iap_params(cmd);
	va_list params;
	va_start(params, cmd);
	for (size_t i = 0U; i < params_count; ++i)
		frame.config.params[i] = va_arg(params, uint32_t);
	va_end(params);
	for (size_t i = params_count; i < 4U; ++i)
		frame.config.params[i] = 0U;

	/* Set the result code to something notable to help with checking if the call ran */
	frame.result.return_code = cmd;

	DEBUG_INFO("%s: cmd %d (%x), params: %08" PRIx32 " %08" PRIx32 " %08" PRIx32 " %08" PRIx32 "\n", __func__, cmd, cmd,
		frame.config.params[0], frame.config.params[1], frame.config.params[2], frame.config.params[3]);

	/* Copy the structure to RAM */
	target_mem32_write(target, flash->iap_ram, &frame, sizeof(iap_frame_s));
	const uint32_t iap_results_addr = flash->iap_ram + offsetof(iap_frame_s, result);

	/* Set up for the call to the IAP ROM */
	uint32_t regs[CORTEXM_GENERAL_REG_COUNT];
	memset(regs, 0, target->regs_size);
	/* Point r0 to the start of the config block */
	regs[0U] = flash->iap_ram + offsetof(iap_frame_s, config);
	/* And r1 to the same so we re-use the same memory for the results */
	regs[1U] = iap_results_addr;
	/* Set the top of stack to the top of the RAM block we're using */
	regs[CORTEX_REG_MSP] = flash->iap_msp;
	/* Point the return address to our breakpoint opcode (thumb mode) */
	regs[CORTEX_REG_LR] = flash->iap_ram | 1U;
	/* And set the program counter to the IAP ROM entrypoint */
	regs[CORTEX_REG_PC] = flash->iap_entry;
	/* Finally set up xPSR to indicate a suitable instruction mode, no fault */
	regs[CORTEX_REG_XPSR] = CORTEXM_XPSR_THUMB;
	target_regs_write(target, regs);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500U);
	/* Start the target and wait for it to halt again */
	target_halt_resume(target, false);
	while (!target_halt_poll(target, NULL)) {
		if (print_progess)
			target_print_progress(print_progess);
		else if (cmd == IAP_CMD_PARTID && platform_timeout_is_expired(&timeout)) {
			target_halt_request(target);
			/* Restore the original data in RAM and registers */
			lpc17xx_restore_state(target, flash->iap_ram, &saved_frame, saved_regs);
			return IAP_STATUS_INVALID_COMMAND;
		}
	}

	/* Copy back just the results */
	target_mem32_read(target, result, iap_results_addr, sizeof(iap_result_s));

	/* Restore the original data in RAM and registers */
	lpc17xx_restore_state(target, flash->iap_ram, &saved_frame, saved_regs);
	return result->return_code;
}
