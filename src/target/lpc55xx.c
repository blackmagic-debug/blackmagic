/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * Based on prior work by Uwe Bones <bon@elektron.ikp.physik.tu-darmstadt.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

/*
 * For detailed documentation on how this code works and the IAP variant used here, see:
 * https://www.nxp.com/docs/en/data-sheet/LPC55S0x_LPC550x_DS.pdf
 * https://www.nxp.com/docs/en/nxp/data-sheets/LPC55S1x_LPC551x_DS.pdf
 * and (behind their login wall):
 * https://cache.nxp.com/secured/assets/documents/en/nxp/user-guides/UM11424.pdf?fileExt=.pdf
 * https://cache.nxp.com/secured/assets/documents/en/nxp/user-guides/UM11295.pdf?fileExt=.pdf
 */

#define LPC55_DMAP_IDR                 0x002a0000U
#define LPC55_DMAP_BULK_ERASE          0x02U
#define LPC55_DMAP_START_DEBUG_SESSION 0x07U

#define LPC55xx_FLASH_BASE     0x00000000U
#define LPC55xx_ERASE_KEY      0x6b65666cU
#define LPC55xx_CHIPID_ADDRESS 0x50000ff8U

#define LPC55xx_WRITE_SIZE 4096
#define LPC55xx_ERASE_SIZE 4096

/* Target memory layout for IAP calls, we will scribble over SRAM */
#define LPC55xx_FLASH_CONFIG_ADDRESS 0x04000000U
#define LPC55xx_CODE_PATCH_ADDRESS   0x0400003cU
#define LPC55xx_UUID_ADDRESS         0x04000040U
#define LPC55xx_WRITE_BUFFER_ADDRESS 0x20000000U
#define LPC55xx_SCRATCH_MEMORY_LEN   0x40
#define LPC55xx_UUID_LEN             0x10

/*
 * The ROM code seems to default to an MSP in the SRAM-X region, which
 * is code-only. This seems fairly safe, so do the same for IAP calls.
 *
 * This may not matter as supposedly IAP doesn't use the stack (?)
 */
#define LPC55xx_IAP_MSP_ADDRESS 0x04003000U
#define LPC55xx_IAP_FREQ_IN_MHZ 96

/* Device chip IDs */
#define LPC5502_CHIPID  0xa1003702U // UM11424
#define LPC5504_CHIPID  0xa1001504U // UM11424
#define LPC5506_CHIPID  0xa1000406U // UM11424
#define LPC5512_CHIPID  0xa100370cU // UM11295
#define LPC5514_CHIPID  0xa100150eU // UM11295
#define LPC5516_CHIPID  0xa1000410U // UM11295
#define LPC5524_CHIPID              // (unknown)
#define LPC5526_CHIPID  0xa010221aU // NXP forum
#define LPC5528_CHIPID  0xa010111cU // NXP forum
#define LPC55S04_CHIPID 0x51001584U // UM11424
#define LPC55S06_CHIPID 0x51000486U // UM11424
#define LPC55S14_CHIPID 0x5100158eU // UM11295
#define LPC55S16_CHIPID 0x51000490U // UM11295
#define LPC55S26_CHIPID 0xa010229aU // NXP forum
#define LPC55S28_CHIPID 0xa010119cU // NXP forum
#define LPC55S66_CHIPID             // (unknown)
#define LPC55S69_CHIPID 0x501000c5U // (read from MCU-Link)

/* The available IAP commands that we support, mostly flash access */
typedef enum lpc55xx_iap_cmd {
	IAP_CMD_FLASH_INIT,
	IAP_CMD_FLASH_ERASE,
	IAP_CMD_FLASH_PROGRAM,
	IAP_CMD_FFR_INIT,
	IAP_CMD_FFR_GET_UUID,
} lpc55xx_iap_cmd_e;

/* The possible IAP errors are documented here for easy reference */
typedef enum lpc55xx_iap_status {
	IAP_STATUS_FLASH_SUCCESS = 0,
	IAP_STATUS_FLASH_INVALID_ARGUMENT = 4,
	IAP_STATUS_FLASH_SIZE_ERROR = 100,
	IAP_STATUS_FLASH_ALIGNMENT_ERROR = 101,
	IAP_STATUS_FLASH_ADDRESS_ERROR = 102,
	IAP_STATUS_FLASH_ACCESS_ERROR = 103,
	IAP_STATUS_FLASH_COMMAND_FAILURE = 105,
	IAP_STATUS_FLASH_UNKNOWN_PROPERTY = 106,
	IAP_STATUS_FLASH_ERASE_KEY_ERROR = 107,
	IAP_STATUS_FLASH_COMMAND_NOT_SUPPORTED = 111,
	IAP_STATUS_FLASH_ECC_ERROR = 116,
	IAP_STATUS_FLASH_COMPARE_ERROR = 117,
	IAP_STATUS_FLASH_REGULATION_LOSS = 118,
	IAP_STATUS_FLASH_INVALID_WAIT_STATE_CYCLES = 119,
	IAP_STATUS_FLASH_OUT_OF_DATE_CFPA_PAGE = 132,
	IAP_STATUS_FLASH_BLANK_IFR_PAGE_DATA = 133,
	IAP_STATUS_FLASH_ENCRYPTED_REGIONS_ERASE_NOT_DONE_AT_ONCE = 134,
	IAP_STATUS_FLASH_PROGRAM_VERIFICATION_NOT_ALLOWED = 135,
	IAP_STATUS_FLASH_HASH_CHECK_ERROR = 136,
	IAP_STATUS_FLASH_SEALED_FFR_REGION = 137,
	IAP_STATUS_FLASH_FFR_REGION_WRITE_BROKEN = 138,
	IAP_STATUS_FLASH_NMPA_ACCESS_NOT_ALLOWED = 139,
	IAP_STATUS_FLASH_CMPA_CFG_DIRECT_ERASE_NOT_ALLOWED = 140,
	IAP_STATUS_FLASH_FFR_BANK_IS_LOCKED = 141,
} lpc55xx_iap_status_e;

static target_addr_t lpc55xx_get_bootloader_tree_address(target_s *target)
{
	switch (target_mem_read32(target, LPC55xx_CHIPID_ADDRESS)) {
	//case LPC5512_CHIPID:
	//case LPC5514_CHIPID:
	//case LPC55S14_CHIPID:
	//case LPC5516_CHIPID:
	//case LPC55S16_CHIPID:
	//case LPC5524_CHIPID:
	case LPC5502_CHIPID:
	case LPC5504_CHIPID:
	case LPC5506_CHIPID:
	case LPC55S04_CHIPID:
	case LPC55S06_CHIPID:
		return 0x1301fe00U;

	case LPC55S69_CHIPID:
	case LPC5526_CHIPID:
	case LPC55S26_CHIPID:
	case LPC5528_CHIPID:
	case LPC55S28_CHIPID:
		//case LPC55S66_CHIPID:
		return 0x130010f0U;

	default:
		return 0;
	}
}

static const char *lpc55xx_get_device_name(uint32_t chipid)
{
	switch (chipid) {
	case LPC5502_CHIPID:
		return "LPC5502";
	case LPC5504_CHIPID:
		return "LPC5504";
	case LPC5506_CHIPID:
		return "LPC5506";
	case LPC55S04_CHIPID:
		return "LPC55S04";
	case LPC55S06_CHIPID:
		return "LPC55S06";
	case LPC5526_CHIPID:
		return "LPC5526";
	case LPC55S26_CHIPID:
		return "LPC55S26";
	case LPC5528_CHIPID:
		return "LPC5528";
	case LPC55S28_CHIPID:
		return "LPC55S28";
	case LPC55S69_CHIPID:
		return "LPC55S69";

	default:
		return "unknown";
	}
}

static int lpc55xx_get_rom_api_version(target_s *target, target_addr_t bootloader_tree_address)
{
	return ((target_mem_read32(target, bootloader_tree_address + 0x4) >> 16) & 0xff) == 3 ? 1 : 0;
}

static target_addr_t lpc55xx_get_flash_table_address(target_s *target, target_addr_t bootloader_tree_address)
{
	return target_mem_read32(target, bootloader_tree_address + 0x10);
}

static target_addr_t lpc55xx_get_flash_init_address(target_s *target)
{
	target_addr_t bootloader_tree_address = lpc55xx_get_bootloader_tree_address(target);

	target_addr_t flash_table_address = lpc55xx_get_flash_table_address(target, bootloader_tree_address);
	return target_mem_read32(target, flash_table_address + sizeof(uint32_t));
}

static target_addr_t lpc55xx_get_flash_erase_address(target_s *target)
{
	target_addr_t bootloader_tree_address = lpc55xx_get_bootloader_tree_address(target);

	if (lpc55xx_get_rom_api_version(target, bootloader_tree_address) == 0)
		return 0x1300413bU; // UNTESTED: found in SDK, not referenced in UM

	target_addr_t flash_table_address = lpc55xx_get_flash_table_address(target, bootloader_tree_address);
	return target_mem_read32(target, flash_table_address + 2 * sizeof(uint32_t));
}

static target_addr_t lpc55xx_get_flash_program_address(target_s *target)
{
	target_addr_t bootloader_tree_address = lpc55xx_get_bootloader_tree_address(target);

	if (lpc55xx_get_rom_api_version(target, bootloader_tree_address) == 0)
		return 0x1300419dU; // UNTESTED: found in SDK, not referenced in UM

	target_addr_t flash_table_address = lpc55xx_get_flash_table_address(target, bootloader_tree_address);
	return target_mem_read32(target, flash_table_address + 3 * sizeof(uint32_t));
}

static target_addr_t lpc55xx_get_ffr_init_address(target_s *target)
{
	target_addr_t bootloader_tree_address = lpc55xx_get_bootloader_tree_address(target);
	target_addr_t flash_table_address = lpc55xx_get_flash_table_address(target, bootloader_tree_address);

	if (lpc55xx_get_rom_api_version(target, bootloader_tree_address) == 0)
		return target_mem_read32(target, flash_table_address + 7 * sizeof(uint32_t));
	return target_mem_read32(target, flash_table_address + 10 * sizeof(uint32_t));
}

static target_addr_t lpc55xx_get_ffr_get_uuid_address(target_s *target)
{
	target_addr_t bootloader_tree_address = lpc55xx_get_bootloader_tree_address(target);
	target_addr_t flash_table_address = lpc55xx_get_flash_table_address(target, bootloader_tree_address);

	if (lpc55xx_get_rom_api_version(target, bootloader_tree_address) == 0)
		return target_mem_read32(target, flash_table_address + 10 * sizeof(uint32_t));
	return target_mem_read32(target, flash_table_address + 13 * sizeof(uint32_t));
}

static lpc55xx_iap_status_e iap_call_raw(target_s *target, lpc55xx_iap_cmd_e cmd, uint32_t r1, uint32_t r2, uint32_t r3)
{
	/* Prepare the registers for the IAP call. R0 is always flash_config */
	uint32_t regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];
	target_regs_read(target, regs);

	regs[CORTEX_REG_MSP] = LPC55xx_IAP_MSP_ADDRESS;
	regs[0] = LPC55xx_FLASH_CONFIG_ADDRESS;
	regs[1] = r1;
	regs[2] = r2;
	regs[3] = r3;

	/* Locate the correct IAP function address based on silicon revision */
	switch (cmd) {
	case IAP_CMD_FLASH_INIT:
		regs[CORTEX_REG_PC] = lpc55xx_get_flash_init_address(target);
		break;
	case IAP_CMD_FLASH_ERASE:
		regs[CORTEX_REG_PC] = lpc55xx_get_flash_erase_address(target);
		break;
	case IAP_CMD_FLASH_PROGRAM:
		regs[CORTEX_REG_PC] = lpc55xx_get_flash_program_address(target);
		break;
	case IAP_CMD_FFR_INIT:
		regs[CORTEX_REG_PC] = lpc55xx_get_ffr_init_address(target);
		break;
	case IAP_CMD_FFR_GET_UUID:
		regs[CORTEX_REG_PC] = lpc55xx_get_ffr_get_uuid_address(target);
		break;
	default:
		DEBUG_ERROR("LPC55xx: bad IAP command\n");
		return IAP_STATUS_FLASH_INVALID_ARGUMENT;
	}

	/*
	 * Setting a dummy LR does not seem to work as it makes the target
	 * hard-fault. Instead, set LR to a word known to contain the BKPT
	 * instruction, so that we can safely halt on IAP function return.
	 */
	target_mem_write16(target, LPC55xx_CODE_PATCH_ADDRESS, CORTEX_THUMB_BREAKPOINT);
	regs[CORTEX_REG_LR] = LPC55xx_CODE_PATCH_ADDRESS | 1; // set the ARM thumb call bit

	/* Write the registers to the target and perform the IAP call */
	target_regs_write(target, regs);
	target_halt_resume(target, false);
	while (!target_halt_poll(target, NULL))
		continue;

	/* Read back the status code from r0 and return */
	target_regs_read(target, regs);
	return (lpc55xx_iap_status_e)regs[0];
}

typedef struct lpc55xx_flash_config {
	uint32_t flash_block_base;
	uint32_t flash_total_size;
	uint32_t flash_block_count;
	uint32_t flash_page_size;
	uint32_t flash_sector_size;

	uint32_t reserved0[5];
	uint32_t sys_freq_mhz;
	uint32_t reserved1[4];
} lpc55xx_flash_config_s;

static void lpc55xx_prepare_flash_config(target_s *target, target_addr_t address)
{
	/*
	 * The flash config structure is 60 bytes in size, zero it out as that
	 * is what the SDK does. For some reason you have to fill in the clock
	 * speed field ("sys_freq_mhz") before flash_init. Set it to 96MHz (?)
	 */
	lpc55xx_flash_config_s config = {
		.sys_freq_mhz = LPC55xx_IAP_FREQ_IN_MHZ,
	};

	target_mem_write(target, address, &config, sizeof(config));
}

static bool lpc55xx_flash_init(target_s *target, lpc55xx_flash_config_s *config)
{
	uint8_t backup_memory[LPC55xx_SCRATCH_MEMORY_LEN];
	uint32_t regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];

	target_regs_read(target, regs);
	target_mem_read(target, backup_memory, LPC55xx_FLASH_CONFIG_ADDRESS, sizeof(backup_memory));

	bool success = false;

	lpc55xx_prepare_flash_config(target, LPC55xx_FLASH_CONFIG_ADDRESS);

	const lpc55xx_iap_status_e status = iap_call_raw(target, IAP_CMD_FLASH_INIT, 0, 0, 0);
	if (status != IAP_STATUS_FLASH_SUCCESS) {
		DEBUG_ERROR("LPC55xx: IAP error: FLASH_INIT (%d)\n", status);
		goto exit;
	}

	target_mem_read(target, config, LPC55xx_FLASH_CONFIG_ADDRESS, sizeof(*config));

	success = true;

exit:
	target_mem_write(target, LPC55xx_FLASH_CONFIG_ADDRESS, backup_memory, sizeof(backup_memory));
	target_regs_write(target, regs);

	return success;
}

static bool lpc55xx_get_uuid(target_s *target, uint8_t *uuid)
{
	uint8_t backup_memory[LPC55xx_SCRATCH_MEMORY_LEN + LPC55xx_UUID_LEN];
	uint32_t regs[CORTEXM_GENERAL_REG_COUNT + CORTEX_FLOAT_REG_COUNT];

	target_regs_read(target, regs);
	target_mem_read(target, backup_memory, LPC55xx_FLASH_CONFIG_ADDRESS, sizeof(backup_memory));

	bool success = false;

	lpc55xx_prepare_flash_config(target, LPC55xx_FLASH_CONFIG_ADDRESS);

	lpc55xx_iap_status_e status = iap_call_raw(target, IAP_CMD_FLASH_INIT, 0, 0, 0);
	if (status != IAP_STATUS_FLASH_SUCCESS) {
		DEBUG_ERROR("LPC55xx: IAP error: FLASH_INIT (%d)\n", status);
		goto exit;
	}

	status = iap_call_raw(target, IAP_CMD_FFR_INIT, 0, 0, 0);
	if (status != IAP_STATUS_FLASH_SUCCESS) {
		DEBUG_ERROR("LPC55xx: IAP error: FFR_INIT (%d)\n", status);
		goto exit;
	}

	status = iap_call_raw(target, IAP_CMD_FFR_GET_UUID, LPC55xx_UUID_ADDRESS, 0, 0);
	if (status != IAP_STATUS_FLASH_SUCCESS) {
		DEBUG_ERROR("LPC55xx: IAP error: FFR_GET_UUID (%d)\n", status);
		goto exit;
	}

	target_mem_read(target, uuid, LPC55xx_UUID_ADDRESS, LPC55xx_UUID_LEN);

	success = true;

exit:
	target_mem_write(target, LPC55xx_FLASH_CONFIG_ADDRESS, backup_memory, sizeof(backup_memory));
	target_regs_write(target, regs);

	return success;
}

static bool lpc55xx_enter_flash_mode(target_s *target)
{
	// NOTE! The usual way to go about this would be to just reset the target to
	// put it back into a known state. Unfortunately target_reset hangs for this
	// target and I'm not sure why, so the below is a viable workaround for now.

	const uint32_t reg_pc_value = LPC55xx_CODE_PATCH_ADDRESS | 1;

	// Execute a small binary patch which just disables interrupts and then hits
	// a breakpoint, to allow the flash IAP calls to run undisturbed. This patch
	// consists of the instructions CPSID I; BKPT; in ARM Thumb encoding.
	const uint32_t CODE_PATCH = 0xbe00b672U;

	target_mem_write32(target, LPC55xx_CODE_PATCH_ADDRESS, CODE_PATCH);
	target_reg_write(target, CORTEX_REG_PC, &reg_pc_value, sizeof(uint32_t));

	target_halt_resume(target, false);
	// Wait for the target to halt on the BKPT instruction
	while (!target_halt_poll(target, NULL))
		continue;

	return true;
}

static bool lpc55xx_flash_prepare(target_flash_s *flash)
{
	lpc55xx_prepare_flash_config(flash->t, LPC55xx_FLASH_CONFIG_ADDRESS);

	// Initialize the IAP flash context once in a predefined location
	// of SRAM, the flash erase/write functions assume it is present.

	const lpc55xx_iap_status_e status = iap_call_raw(flash->t, IAP_CMD_FLASH_INIT, 0, 0, 0);
	if (status != IAP_STATUS_FLASH_SUCCESS)
		DEBUG_ERROR("LPC55xx: IAP error: FLASH_INIT (%d)\n", status);
	return status == IAP_STATUS_FLASH_SUCCESS;
}

static bool lpc55xx_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len)
{
	const lpc55xx_iap_status_e status =
		iap_call_raw(flash->t, IAP_CMD_FLASH_ERASE, addr, (uint32_t)len, LPC55xx_ERASE_KEY);
	if (status != IAP_STATUS_FLASH_SUCCESS)
		DEBUG_ERROR("LPC55xx: IAP error: FLASH_ERASE (%d)\n", status);
	return status == IAP_STATUS_FLASH_SUCCESS;
}

static bool lpc55xx_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	target_mem_write(flash->t, LPC55xx_WRITE_BUFFER_ADDRESS, src, len);

	const lpc55xx_iap_status_e status =
		iap_call_raw(flash->t, IAP_CMD_FLASH_PROGRAM, dest, LPC55xx_WRITE_BUFFER_ADDRESS, (uint32_t)len);
	if (status != IAP_STATUS_FLASH_SUCCESS)
		DEBUG_ERROR("LPC55xx: IAP error: FLASH_PROGRAM (%d)\n", status);
	return status == IAP_STATUS_FLASH_SUCCESS;
}

static target_flash_s *lpc55xx_add_flash(target_s *target)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return NULL;
	}

	lpc55xx_flash_config_s config;
	if (!lpc55xx_flash_init(target, &config)) {
		free(flash);
		return NULL;
	}

	DEBUG_INFO("LPC55xx: Detected flash with %" PRIu32 " bytes, %" PRIu32 "-byte pages\n", config.flash_total_size,
		config.flash_page_size);

	target->enter_flash_mode = lpc55xx_enter_flash_mode;

	flash->blocksize = LPC55xx_ERASE_SIZE;
	flash->writesize = LPC55xx_WRITE_SIZE;

	/* all flash operations must be aligned to the flash page size */

	if (flash->blocksize < config.flash_page_size)
		flash->blocksize = config.flash_page_size;

	if (flash->writesize < config.flash_page_size)
		flash->writesize = config.flash_page_size;

	flash->start = LPC55xx_FLASH_BASE;
	flash->length = config.flash_total_size;
	flash->prepare = lpc55xx_flash_prepare;
	flash->erase = lpc55xx_flash_erase;
	flash->write = lpc55xx_flash_write;
	flash->erased = 0xff;

	target_add_flash(target, flash);

	return flash;
}

static bool lpc55xx_read_uid(target_s *target, int argc, const char *argv[])
{
	(void)argc;
	(void)argv;

	uint8_t uuid[LPC55xx_UUID_LEN];
	if (!lpc55xx_get_uuid(target, uuid))
		return false;

	tc_printf(target, "UID: 0x");
	for (size_t i = 0; i < sizeof(uuid); ++i)
		tc_printf(target, "%02x", uuid[i]);
	tc_printf(target, "\n");

	return true;
}

static const command_s lpc55xx_cmd_list[] = {
	{"readuid", lpc55xx_read_uid, "Read out the 16-byte UID."},
	{NULL, NULL, NULL},
};

static bool lpc55_dmap_cmd(adiv5_access_port_s *ap, uint32_t cmd);
static bool lpc55_dmap_mass_erase(target_s *target);
static void lpc55_dmap_ap_free(void *priv);

void lpc55_dp_prepare(adiv5_debug_port_s *const dp)
{
	/* Reading targetid again here upsets the LPC55 and STM32U5 */
	/*
	 * UM11126, 51.6.1
	 * Debug session with uninitialized/invalid flash image or ISP mode
	 */
	adiv5_dp_abort(dp, ADIV5_DP_ABORT_DAPABORT);
	/* Set up a dummy Access Port on the stack */
	adiv5_access_port_s ap = {0};
	ap.dp = dp;
	ap.apsel = 2;
	/* Read out the ID register and check it's the LPC55's Debug Mailbox ID */
	ap.idr = adiv5_ap_read(&ap, ADIV5_AP_IDR);
	if (ap.idr != LPC55_DMAP_IDR)
		return; /* Return early if this likely is not an LPC55 */

	/* Try reading out the AP 0 IDR */
	ap.apsel = 0;
	ap.idr = adiv5_ap_read(&ap, ADIV5_AP_IDR);
	/* If that failed, then we have to activate the debug mailbox */
	if (ap.idr == 0) {
		DEBUG_INFO("Running LPC55 activation sequence\n");
		ap.apsel = 2;
		adiv5_ap_write(&ap, ADIV5_AP_CSW, 0x21);
		lpc55_dmap_cmd(&ap, LPC55_DMAP_START_DEBUG_SESSION);
	}
	/* At this point we assume that we've got access to the debug mailbox and can continue normally. */
}

bool lpc55xx_probe(target_s *const target)
{
	const adiv5_access_port_s *const ap = cortex_ap(target);
	if (ap->apsel == 1)
		return false;

	const uint32_t chipid = target_mem_read32(target, LPC55xx_CHIPID_ADDRESS);
	DEBUG_WARN("Chip ID: %08" PRIx32 "\n", chipid);

	target->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
	target->driver = lpc55xx_get_device_name(chipid);

	switch (chipid) {
	case LPC5502_CHIPID:
	case LPC5512_CHIPID:
		target_add_ram(target, 0x04000000U, 0x4000U); // SRAM_X
		target_add_ram(target, 0x20000000U, 0x8000U); // SRAM_0
		break;
	case LPC5504_CHIPID:
	case LPC55S04_CHIPID:
	case LPC5514_CHIPID:
	case LPC55S14_CHIPID:
		target_add_ram(target, 0x04000000U, 0x4000U); // SRAM_X
		target_add_ram(target, 0x20000000U, 0x8000U); // SRAM_0
		target_add_ram(target, 0x20008000U, 0x4000U); // SRAM_1
		target_add_ram(target, 0x2000c000U, 0x4000U); // SRAM_2
		break;
	case LPC5506_CHIPID:
	case LPC55S06_CHIPID:
	case LPC5516_CHIPID:
	case LPC55S16_CHIPID:
	case LPC55S69_CHIPID:
		target_add_ram(target, 0x04000000U, 0x4000U); // SRAM_X
		target_add_ram(target, 0x20000000U, 0x8000U); // SRAM_0
		target_add_ram(target, 0x20008000U, 0x4000U); // SRAM_1
		target_add_ram(target, 0x2000c000U, 0x4000U); // SRAM_2
		target_add_ram(target, 0x20010000U, 0x4000U); // SRAM_3
		break;
	default:
		// TODO: not enough testing to enable other devices
		DEBUG_WARN("LPC55xx: add support for this device!");
		return false;
	}

	// If we got here, we're happy enough about the device
	// to go ahead and start Flash detection and IAP calls

	lpc55xx_add_flash(target);
	target_add_commands(target, lpc55xx_cmd_list, "LPC55xx");

	return true;
}

bool lpc55_dmap_probe(adiv5_access_port_s *ap)
{
	if (ap->idr != LPC55_DMAP_IDR)
		return false;

	target_s *target = target_new();
	if (!target)
		return false;

	adiv5_ap_ref(ap);
	target->priv = ap;
	target->priv_free = lpc55_dmap_ap_free;

	target->driver = "LPC55 Debug Mailbox";
	target->regs_size = 0;
	target->mass_erase = lpc55_dmap_mass_erase;

	return true;
}

static void lpc55_dmap_ap_free(void *priv)
{
	adiv5_ap_unref(priv);
}

static bool lpc55_dmap_cmd(adiv5_access_port_s *const ap, const uint32_t cmd)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 20);
	while (true) {
		const uint32_t csw = adiv5_ap_read(ap, ADIV5_AP_CSW);
		if (csw == 0)
			break;
		if (platform_timeout_is_expired(&timeout))
			return false;
	}

	adiv5_ap_write(ap, ADIV5_AP_TAR, cmd);

	platform_timeout_set(&timeout, 20);
	while (true) {
		const uint16_t value = (uint16_t)adiv5_ap_read(ap, ADIV5_AP_DRW);
		if (value == 0)
			return true;
		if (platform_timeout_is_expired(&timeout)) {
			DEBUG_ERROR("LPC55 cmd %" PRIx32 " failed\n", cmd);
			return false;
		}
	}
}

static bool lpc55_dmap_mass_erase(target_s *target)
{
	/*
	 * TODO: This doesn't actually work at least on the LPC550x, there seems to be
	 * a lot more to figure out about the debug mailbox before this code can work.
	 *
	 * In the meantime, if you get your chip into a bad state where you cannot
	 * communicate with the AP to debug the core, your best chance is probably
	 * to try and drive low the ISP pin (PIO0_5 on LPC550x) during power-on.
	*/
	return lpc55_dmap_cmd(cortex_ap(target), LPC55_DMAP_BULK_ERASE);
}
