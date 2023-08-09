/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012 Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2023 Vegard Storheil Eriksen <zyp@jvnv.net>
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

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortex.h"
#include "lpc_common.h"
#include "adiv5.h"

/*
 * For detailed documentation on how this code works and the IAP variant used here, see:
 * https://www.nxp.com/docs/en/data-sheet/LPC1759_58_56_54_52_51.pdf
 * and (behind their login wall):
 * https://cache.nxp.com/secured/assets/documents/en/user-guide/UM10360.pdf?fileExt=.pdf
 */

#define IAP_PGM_CHUNKSIZE 4096U

#define MIN_RAM_SIZE               8192U // LPC1751
#define RAM_USAGE_FOR_IAP_ROUTINES 32U   // IAP routines use 32 bytes at top of ram

#define IAP_ENTRYPOINT 0x1fff1ff1U
#define IAP_RAM_BASE   0x10000000U

#define LPC40xx_MEMMAP   UINT32_C(0x400fc040)
#define LPC40xx_MPU_BASE UINT32_C(0xe000ed90)
#define LPC40xx_MPU_CTRL (LPC40xx_MPU_BASE + 0x04U)

#define FLASH_NUM_SECTOR 30U

typedef struct iap_config {
	uint32_t command;
	uint32_t params[4];
} iap_config_s;

typedef struct __attribute__((aligned(4))) iap_frame {
	/* The start of an IAP stack frame is the opcode we set as the return point. */
	uint16_t opcode;
	/* There's then a hidden alignment field here, followed by the IAP call setup */
	iap_config_s config;
} iap_frame_s;

typedef struct lpc40xx_priv {
	uint32_t mpu_ctrl_state;
	uint32_t memmap_state;
} lpc40xx_priv_s;

static void lpc40xx_extended_reset(target_s *target);
static bool lpc40xx_enter_flash_mode(target_s *target);
static bool lpc40xx_exit_flash_mode(target_s *target);
static bool lpc40xx_mass_erase(target_s *target);
iap_status_e lpc40xx_iap_call(target_s *target, iap_result_s *result, iap_cmd_e cmd, ...);

static void lpc40xx_add_flash(target_s *target, uint32_t addr, size_t len, size_t erasesize, uint8_t base_sector)
{
	lpc_flash_s *flash = lpc_add_flash(target, addr, len, IAP_PGM_CHUNKSIZE);
	flash->f.blocksize = erasesize;
	flash->base_sector = base_sector;
	flash->f.write = lpc_flash_write_magic_vect;
	flash->iap_entry = IAP_ENTRYPOINT;
	flash->iap_ram = IAP_RAM_BASE;
	flash->iap_msp = IAP_RAM_BASE + MIN_RAM_SIZE - RAM_USAGE_FOR_IAP_ROUTINES;
}

bool lpc40xx_probe(target_s *target)
{
	if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) != CORTEX_M4)
		return false;

	/*
	 * Now that we're sure it's a Cortex-M3, we need to halt the
	 * target and make an IAP call to get the part number.
	 * There appears to have no other method of reading the part number.
	 */
	target_halt_request(target);

	/* Allocate private storage so the flash mode entry/exit routines can save state */
	lpc40xx_priv_s *priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	target->target_storage = priv;

	/* Prepare Flash mode */
	lpc40xx_enter_flash_mode(target);
	/* Read the Part ID */
	iap_result_s result;
	lpc40xx_iap_call(target, &result, IAP_CMD_PARTID);
	/* Transition back to normal mode and resume the target */
	lpc40xx_exit_flash_mode(target);
	target_halt_resume(target, false);

	/*
	 * If we got an error response, it cannot be a LPC40xx as the only response
	 * a real device gives is IAP_STATUS_CMD_SUCCESS.
	 */
	if (result.return_code) {
		free(priv);
		target->target_storage = NULL;
		return false;
	}

	switch (result.values[0]) {
	case 0x481d3f47U: /* LPC4088 */
	case 0x47193f47U: /* LPC4078 */
	case 0x47191f43U: /* LPC4076 */
	case 0x47011132U: /* LPC4074 */
		target->driver = "LPC40xx";
		target->extended_reset = lpc40xx_extended_reset;
		target->mass_erase = lpc40xx_mass_erase;
		target->enter_flash_mode = lpc40xx_enter_flash_mode;
		target->exit_flash_mode = lpc40xx_exit_flash_mode;
		target_add_ram(target, 0x10000000U, 0x10000U);
		target_add_ram(target, 0x2007c000U, 0x4000U);
		target_add_ram(target, 0x20080000U, 0x4000U);
		lpc40xx_add_flash(target, 0x00000000U, 0x10000U, 0x1000U, 0);
		lpc40xx_add_flash(target, 0x00010000U, 0x70000U, 0x8000U, 16);
		return true;
	}

	return false;
}

static bool lpc40xx_enter_flash_mode(target_s *const target)
{
	lpc40xx_priv_s *const priv = (lpc40xx_priv_s *)target->target_storage;
	/* Disable the MPU, if enabled */
	priv->mpu_ctrl_state = target_mem_read32(target, LPC40xx_MPU_CTRL);
	target_mem_write32(target, LPC40xx_MPU_CTRL, 0);
	/* And store the memory mapping state */
	priv->memmap_state = target_mem_read32(target, LPC40xx_MEMMAP);
	return true;
}

static bool lpc40xx_exit_flash_mode(target_s *const target)
{
	const lpc40xx_priv_s *const priv = (lpc40xx_priv_s *)target->target_storage;
	/* Restore the memory mapping and MPU state (in that order!) */
	target_mem_write32(target, LPC40xx_MEMMAP, priv->memmap_state);
	target_mem_write32(target, LPC40xx_MPU_CTRL, priv->mpu_ctrl_state);
	return true;
}

static bool lpc40xx_mass_erase(target_s *target)
{
	iap_result_s result;
	lpc40xx_enter_flash_mode(target);

	if (lpc40xx_iap_call(target, &result, IAP_CMD_PREPARE, 0, FLASH_NUM_SECTOR - 1U)) {
		lpc40xx_exit_flash_mode(target);
		DEBUG_ERROR("lpc40xx_cmd_erase: prepare failed %" PRIu32 "\n", result.return_code);
		return false;
	}

	if (lpc40xx_iap_call(target, &result, IAP_CMD_ERASE, 0, FLASH_NUM_SECTOR - 1U, CPU_CLK_KHZ)) {
		lpc40xx_exit_flash_mode(target);
		DEBUG_ERROR("lpc40xx_cmd_erase: erase failed %" PRIu32 "\n", result.return_code);
		return false;
	}

	if (lpc40xx_iap_call(target, &result, IAP_CMD_BLANKCHECK, 0, FLASH_NUM_SECTOR - 1U)) {
		lpc40xx_exit_flash_mode(target);
		DEBUG_ERROR("lpc40xx_cmd_erase: blankcheck failed %" PRIu32 "\n", result.return_code);
		return false;
	}

	lpc40xx_exit_flash_mode(target);
	tc_printf(target, "Erase OK.\n");
	return true;
}

/*
 * Target has been reset, make sure to remap the boot ROM
 * from 0x00000000 leaving the user flash visible
 */
static void lpc40xx_extended_reset(target_s *target)
{
	/*
	 * Transition the memory map to user mode (if it wasn't already) to ensure
	 * the correct environment is seen by the user
	 * See §33.6 Debug memory re-mapping, pg655 of UM10360 for more details.
	 */
	target_mem_write32(target, LPC40xx_MEMMAP, 1);
}

static size_t lpc40xx_iap_params(const iap_cmd_e cmd)
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

iap_status_e lpc40xx_iap_call(target_s *target, iap_result_s *result, iap_cmd_e cmd, ...)
{
	/* Set up our IAP frame with the break opcode and command to run */
	iap_frame_s frame = {
		.opcode = CORTEX_THUMB_BREAKPOINT,
		{.command = cmd},
	};

	/* Fill out the remainder of the parameters */
	const size_t params_count = lpc40xx_iap_params(cmd);
	va_list params;
	va_start(params, cmd);
	for (size_t i = 0; i < params_count; ++i)
		frame.config.params[i] = va_arg(params, uint32_t);
	va_end(params);
	for (size_t i = params_count; i < 4; ++i)
		frame.config.params[i] = 0U;

	/* Copy the structure to RAM */
	target_mem_write(target, IAP_RAM_BASE, &frame, sizeof(iap_frame_s));
	const uint32_t iap_params_addr = IAP_RAM_BASE + offsetof(iap_frame_s, config);

	/* Set up for the call to the IAP ROM */
	uint32_t regs[CORTEXM_GENERAL_REG_COUNT];
	target_regs_read(target, regs);
	/* Point r0 to the start of the config block */
	regs[0] = iap_params_addr;
	/* And r1 to the same so we re-use the same memory for the results */
	regs[1] = iap_params_addr;
	/* Set the top of stack to the top of the RAM block we're using */
	regs[CORTEX_REG_MSP] = IAP_RAM_BASE + MIN_RAM_SIZE;
	/* Point the return address to our breakpoint opcode (thumb mode) */
	regs[CORTEX_REG_LR] = IAP_RAM_BASE | 1;
	/* And set the program counter to the IAP ROM entrypoint */
	regs[CORTEX_REG_PC] = IAP_ENTRYPOINT;
	target_regs_write(target, regs);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	/* Start the target and wait for it to halt again */
	target_halt_resume(target, false);
	while (!target_halt_poll(target, NULL)) {
		if (cmd == IAP_CMD_ERASE)
			target_print_progress(&timeout);
		else if (cmd == IAP_CMD_PARTID && platform_timeout_is_expired(&timeout)) {
			target_halt_request(target);
			return IAP_STATUS_INVALID_COMMAND;
		}
	}

	/* Copy back just the results */
	target_mem_read(target, result, iap_params_addr, sizeof(iap_result_s));
	return result->return_code;
}
