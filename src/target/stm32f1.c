/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
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
 * This file implements STM32F0/F1 + clones, and GF32E5 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * References:
 * RM0008 - STM32F101xx, STM32F102xx, STM32F103xx, STM32F105xx and STM32F107xx advanced Arm®-based 32-bit MCUs, Rev. 21
 *   https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * RM0091 - STM32F0x1/STM32F0x2/STM32F0x8 advanced ARM®-based 32-bit MCUs
 *   https://www.st.com/resource/en/reference_manual/rm0091-stm32f0x1stm32f0x2stm32f0x8-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * RM0360 - STM32F030x4/x6/x8/xC and STM32F070x6/xB
 *   https://www.st.com/resource/en/reference_manual/rm0360-stm32f030x4x6x8xc-and-stm32f070x6xb-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * PM0075 - STM32F10xxx Flash memory microcontrollers
 *   https://www.st.com/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf
 * GD32E50x Arm® Cortex®-M33 32-bit MCU User Manual, Rev. 1.8
 *   https://www.gigadevice.com.cn/Public/Uploads/uploadfile/files/20240407/GD32E50x_User_Manual_Rev1.8.pdf
 * GD32E51x Arm® Cortex®-M33 32-bit MCU User Manual, Rev. 1.2
 *   https://www.gigadevice.com.cn/Public/Uploads/uploadfile/files/20240611/GD32E51x_User_Manual_Rev1.2.pdf
 * GD32VF103 RISC-V 32-bit MCU User Manual, Rev. 1.5
 *   https://www.gigadevice.com.cn/Public/Uploads/uploadfile/files/20240407/GD32VF103_User_Manual_Rev1.5.pdf
 * MM32L0xx 32-bit Microcontroller Based on ARM Cortex M0 Core, Version 1.15_n
 *   https://www.mindmotion.com.cn/download/products/UM_MM32L0xx_n_EN.pdf
 * MM32F3270 32-bit Microcontroller Based on Arm®Cortex®-M3 Core, Version 1.04
 *   https://www.mindmotion.com.cn/download/products/UM_MM32F3270_EN.pdf
 * MM32F5270/MM32F5280 32-bit Microcontrollers based on Arm China STAR-MC1, Version 0.9
 *   https://www.mindmotion.com.cn/download/products/UM_MM32F5270_MM32F5280_EN.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#ifdef ENABLE_RISCV
#include "riscv_debug.h"
#endif
#include "jep106.h"
#include "stm32_common.h"

/* Flash Program ad Erase Controller Register Map */
#define FPEC_BASE     0x40022000U
#define FLASH_ACR     (FPEC_BASE + 0x00U)
#define FLASH_KEYR    (FPEC_BASE + 0x04U)
#define FLASH_OPTKEYR (FPEC_BASE + 0x08U)
#define FLASH_SR      (FPEC_BASE + 0x0cU)
#define FLASH_CR      (FPEC_BASE + 0x10U)
#define FLASH_AR      (FPEC_BASE + 0x14U)
#define FLASH_OBR     (FPEC_BASE + 0x1cU)
#define FLASH_WRPR    (FPEC_BASE + 0x20U)

#define FLASH_BANK1_OFFSET 0x00U
#define FLASH_BANK2_OFFSET 0x40U
#define FLASH_BANK_SPLIT   0x08080000U

#define FLASH_CR_OBL_LAUNCH (1U << 13U)
#define FLASH_CR_OPTWRE     (1U << 9U)
#define FLASH_CR_LOCK       (1U << 7U)
#define FLASH_CR_STRT       (1U << 6U)
#define FLASH_CR_OPTER      (1U << 5U)
#define FLASH_CR_OPTPG      (1U << 4U)
#define FLASH_CR_MER        (1U << 2U)
#define FLASH_CR_PER        (1U << 1U)
#define FLASH_CR_PG         (1U << 0U)

#define FLASH_OBR_RDPRT (1U << 1U)

#define FLASH_SR_BSY (1U << 0U)

#define FLASH_OBP_RDP        0x1ffff800U
#define FLASH_OBP_RDP_KEY    0x5aa5U
#define FLASH_OBP_RDP_KEY_F3 0x55aaU

#define KEY1 0x45670123U
#define KEY2 0xcdef89abU

#define SR_ERROR_MASK 0x14U
#define SR_PROG_ERROR 0x04U
#define SR_EOP        0x20U

#define STM32F1_DBGMCU_BASE   0xe0042000U
#define STM32F1_DBGMCU_IDCODE (STM32F1_DBGMCU_BASE + 0x000U)
#define STM32F1_DBGMCU_CONFIG (STM32F1_DBGMCU_BASE + 0x004U)

#define STM32F1_DBGMCU_CONFIG_DBG_SLEEP   (1U << 0U)
#define STM32F1_DBGMCU_CONFIG_DBG_STOP    (1U << 1U)
#define STM32F1_DBGMCU_CONFIG_DBG_STANDBY (1U << 2U)
#define STM32F1_DBGMCU_CONFIG_IWDG_STOP   (1U << 8U)
#define STM32F1_DBGMCU_CONFIG_WWDG_STOP   (1U << 9U)

#define STM32F0_DBGMCU_BASE       0x40015800U
#define STM32F0_DBGMCU_IDCODE     (STM32F0_DBGMCU_BASE + 0x000U)
#define STM32F0_DBGMCU_CONFIG     (STM32F0_DBGMCU_BASE + 0x004U)
#define STM32F0_DBGMCU_APB1FREEZE (STM32F0_DBGMCU_BASE + 0x008U)
#define STM32F0_DBGMCU_APB2FREEZE (STM32F0_DBGMCU_BASE + 0x00cU)

#define STM32F0_DBGMCU_CONFIG_DBG_STOP    (1U << 1U)
#define STM32F0_DBGMCU_CONFIG_DBG_STANDBY (1U << 2U)
#define STM32F0_DBGMCU_APB1FREEZE_WWDG    (1U << 11U)
#define STM32F0_DBGMCU_APB1FREEZE_IWDG    (1U << 12U)

#define GD32E5_DBGMCU_BASE   0xe0044000U
#define GD32E5_DBGMCU_IDCODE (GD32E5_DBGMCU_BASE + 0x000U)
#define GD32E5_DBGMCU_CONFIG (GD32E5_DBGMCU_BASE + 0x004U)

#define MM32L0_DBGMCU_BASE   0x40013400U
#define MM32L0_DBGMCU_IDCODE (MM32L0_DBGMCU_BASE + 0x000U)
#define MM32L0_DBGMCU_CONFIG (MM32L0_DBGMCU_BASE + 0x004U)

#define MM32F3_DBGMCU_BASE   0x40007080U
#define MM32F3_DBGMCU_IDCODE (MM32F3_DBGMCU_BASE + 0x000U)
#define MM32F3_DBGMCU_CONFIG (MM32F3_DBGMCU_BASE + 0x004U)

#define STM32F3_UID_BASE 0x1ffff7acU
#define STM32F1_UID_BASE 0x1ffff7e8U

#define GD32Fx_FLASHSIZE 0x1ffff7e0U
#define GD32F0_FLASHSIZE 0x1ffff7ccU

#define AT32F4x_IDCODE_SERIES_MASK 0xfffff000U
#define AT32F4x_IDCODE_PART_MASK   0x00000fffU
#define AT32F41_SERIES             0x70030000U
#define AT32F40_SERIES             0x70050000U

#define STM32F1_FLASH_BANK1_BASE 0x08000000U
#define STM32F1_FLASH_BANK2_BASE 0x08080000U
#define STM32F1_SRAM_BASE        0x20000000U

#define STM32F1_TOPT_32BIT_WRITES (1U << 8U)

typedef struct stm32f1_priv {
	target_addr32_t dbgmcu_config_taddr;
	uint32_t dbgmcu_config;
} stm32f1_priv_s;

static bool stm32f1_cmd_option(target_s *target, int argc, const char **argv);
static bool stm32f1_cmd_uid(target_s *target, int argc, const char **argv);

const command_s stm32f1_cmd_list[] = {
	{"option", stm32f1_cmd_option, "Manipulate option bytes"},
	{"uid", stm32f1_cmd_uid, "Print unique device ID"},
	{NULL, NULL, NULL},
};

static bool stm32f1_attach(target_s *target);
static void stm32f1_detach(target_s *target);
static bool stm32f1_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool stm32f1_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool stm32f1_mass_erase(target_s *target);

static void stm32f1_add_flash(target_s *target, uint32_t addr, size_t length, size_t erasesize)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = addr;
	flash->length = length;
	flash->blocksize = erasesize;
	flash->writesize = 1024U;
	flash->erase = stm32f1_flash_erase;
	flash->write = stm32f1_flash_write;
	flash->erased = 0xff;
	target_add_flash(target, flash);
}

static uint16_t stm32f1_read_idcode(target_s *const target, target_addr32_t *const config_taddr)
{
	if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M0 ||
		(target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M23) {
		*config_taddr = STM32F0_DBGMCU_CONFIG;
		return target_mem32_read32(target, STM32F0_DBGMCU_IDCODE) & 0xfffU;
	}
	/* Is this a Cortex-M33 core with STM32F1-style peripherals? (GD32E50x, GD32E51x) */
	if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M33) {
		*config_taddr = GD32E5_DBGMCU_CONFIG;
		return target_mem32_read32(target, GD32E5_DBGMCU_IDCODE) & 0xfffU;
	}

	*config_taddr = STM32F1_DBGMCU_CONFIG;
	return target_mem32_read32(target, STM32F1_DBGMCU_IDCODE) & 0xfffU;
}

static bool stm32f1_configure_dbgmcu(target_s *const target, const target_addr32_t dbgmcu_config)
{
	/* If we're in the probe phase */
	if (target->target_storage == NULL) {
		/* Allocate and save private storage */
		stm32f1_priv_s *const priv_storage = calloc(1, sizeof(*priv_storage));
		if (!priv_storage) { /* calloc failed: heap exhaustion */
			DEBUG_ERROR("calloc: failed in %s\n", __func__);
			return false;
		}
		priv_storage->dbgmcu_config_taddr = dbgmcu_config;
		/* Get the current value of the debug config register (and store it for later) */
		priv_storage->dbgmcu_config = target_mem32_read32(target, dbgmcu_config);
		target->target_storage = priv_storage;

		target->attach = stm32f1_attach;
		target->detach = stm32f1_detach;
	}

	const stm32f1_priv_s *const priv = (stm32f1_priv_s *)target->target_storage;
	const target_addr32_t dbgmcu_config_taddr = priv->dbgmcu_config_taddr;
	/* Figure out which style of DBGMCU we're working with */
	if (dbgmcu_config_taddr == STM32F0_DBGMCU_CONFIG) {
		/* Now we have a stable debug environment, make sure the WDTs can't bonk the processor out from under us */
		target_mem32_write32(
			target, STM32F0_DBGMCU_APB1FREEZE, STM32F0_DBGMCU_APB1FREEZE_IWDG | STM32F0_DBGMCU_APB1FREEZE_WWDG);
		/* Then Reconfigure the config register to prevent WFI/WFE from cutting debug access */
		target_mem32_write32(
			target, STM32F0_DBGMCU_CONFIG, STM32F0_DBGMCU_CONFIG_DBG_STANDBY | STM32F0_DBGMCU_CONFIG_DBG_STOP);
	} else
		/*
		 * Reconfigure the DBGMCU to prevent the WDTs causing havoc and problems
		 * and WFI/WFE from cutting debug access too
		 */
		target_mem32_write32(target, dbgmcu_config_taddr,
			priv->dbgmcu_config | STM32F1_DBGMCU_CONFIG_WWDG_STOP | STM32F1_DBGMCU_CONFIG_IWDG_STOP |
				STM32F1_DBGMCU_CONFIG_DBG_STANDBY | STM32F1_DBGMCU_CONFIG_DBG_STOP | STM32F1_DBGMCU_CONFIG_DBG_SLEEP);
	return true;
}

/* Identify GD32F1, GD32F2, GD32F3, GD32E230 and GD32E5 chips */
bool gd32f1_probe(target_s *target)
{
	target_addr32_t dbgmcu_config_taddr;
	const uint16_t device_id = stm32f1_read_idcode(target, &dbgmcu_config_taddr);
	size_t block_size = 0x400;

	switch (device_id) {
	case 0x414U: /* GD32F30x_HD, High density */
	case 0x430U: /* GD32F30x_XD, XL-density */
		target->driver = "GD32F3";
		block_size = 0x800;
		break;
	case 0x418U: /* Connectivity Line */
		target->driver = "GD32F2";
		block_size = 0x800;
		break;
	case 0x410U: /* Medium density */
		if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M23)
			target->driver = "GD32E230"; /* GD32E230, 64 KiB max in 1 KiB pages */
		else if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M4) {
			target->driver = "GD32F3";
			block_size = 0x800;
		} else
			target->driver = "GD32F1"; /* GD32F103, 1 KiB pages */
		break;
	case 0x444U: /* GD32E50x_CL, 512 KiB max in 8 KiB pages */
		target->driver = "GD32E5";
		block_size = 0x2000;
		break;
	default:
		return false;
	}

	const uint32_t signature = target_mem32_read32(target, GD32Fx_FLASHSIZE);
	const uint16_t flash_size = signature & 0xffffU;
	const uint16_t ram_size = signature >> 16U;

	/*
	 * GD32F303x User Manual Rev2.9, 2.3.1 Flash memory architecture:
	 * HD and <=512 KiB CL devices have only bank0 with 2 KiB sized pages
	 * XD and  >512 KiB CL devices also have bank1 with 4 KiB sized pages
	 * Same boundaries found in other families.
	 * XXX: This driver currently only supports parts with a FLASH_BANK_SPLIT
	 * at the 512 KiB boundary (i.e. 0x08080000) like STM32F1 XL-density.
	 */
	if (flash_size > 512U) {
		const uint16_t flash_size_bank1 = flash_size - 512U;
		stm32f1_add_flash(target, STM32F1_FLASH_BANK1_BASE, 512U * 1024U, block_size);
		stm32f1_add_flash(target, STM32F1_FLASH_BANK2_BASE, flash_size_bank1 * 1024U, 0x1000U);
	} else
		stm32f1_add_flash(target, STM32F1_FLASH_BANK1_BASE, (size_t)flash_size * 1024U, block_size);

	target->part_id = device_id;
	target->target_options |= STM32F1_TOPT_32BIT_WRITES;
	target->mass_erase = stm32f1_mass_erase;
	target_add_ram32(target, STM32F1_SRAM_BASE, ram_size * 1024U);
	target_add_commands(target, stm32f1_cmd_list, target->driver);

	/* Now we have a stable debug environment, make sure the WDTs + WFI and WFE instructions can't cause problems */
	return stm32f1_configure_dbgmcu(target, dbgmcu_config_taddr);
}

#ifdef ENABLE_RISCV
static bool gd32vf1_attach(target_s *target);
static void gd32vf1_detach(target_s *target);

/* Identify RISC-V GD32VF1 chips */
bool gd32vf1_probe(target_s *const target)
{
	/* Make sure the architecture ID matches */
	if (target->cpuid != 0x80000022U)
		return false;
	/* Then read out the device ID */
	const uint16_t device_id = target_mem32_read32(target, STM32F1_DBGMCU_IDCODE) & 0xfffU;
	switch (device_id) {
	case 0x410U: /* GD32VF103 */
		target->driver = "GD32VF1";
		break;
	default:
		return false;
	}

	const uint32_t signature = target_mem32_read32(target, GD32Fx_FLASHSIZE);
	const uint16_t flash_size = signature & 0xffffU;
	const uint16_t ram_size = signature >> 16U;

	target->part_id = device_id;
	target->mass_erase = stm32f1_mass_erase;
	target_add_ram32(target, STM32F1_SRAM_BASE, ram_size * 1024U);
	stm32f1_add_flash(target, STM32F1_FLASH_BANK1_BASE, (size_t)flash_size * 1024U, 0x400U);
	target_add_commands(target, stm32f1_cmd_list, target->driver);

	/* Now we have a stable debug environment, make sure the WDTs + sleep instructions can't cause problems */
	const bool result = stm32f1_configure_dbgmcu(target, STM32F1_DBGMCU_CONFIG);
	target->attach = gd32vf1_attach;
	target->detach = gd32vf1_detach;
	return result;
}

static bool gd32vf1_attach(target_s *const target)
{
	/*
	 * Try to attach to the part, and then ensure that the WDTs + WFI and WFE
	 * instructions can't cause problems (this is duplicated as it's undone by detach.)
	 */
	return riscv_attach(target) && stm32f1_configure_dbgmcu(target, 0U);
}

static void gd32vf1_detach(target_s *const target)
{
	const stm32f1_priv_s *const priv = (stm32f1_priv_s *)target->target_storage;
	/* Reverse all changes to the DBGMCU config register */
	target_mem32_write32(target, priv->dbgmcu_config_taddr, priv->dbgmcu_config);
	/* Now defer to the normal Cortex-M detach routine to complete the detach */
	riscv_detach(target);
}
#endif

static bool at32f40_is_dual_bank(const uint16_t part_id)
{
	switch (part_id) {
	case 0x0344U: // AT32F403AVGT7 / LQFP100
	case 0x0345U: // AT32F403ARGT7 / LQFP64
	case 0x0346U: // AT32F403ACGT7 / LQFP48
	case 0x0347U: // AT32F403ACGU7 / QFN48 (found on BlackPill+ WeAct Studio)
	case 0x034bU: // AT32F407VGT7 / LQFP100
	case 0x034cU: // AT32F407VGT7 / LQFP64
	case 0x0353U: // AT32F407AVGT7 / LQFP100
		// Flash (G): 1024 KiB / 2 KiB per block, dual-bank
		return true;
	}
	return false;
}

static bool at32f40_detect(target_s *target, const uint16_t part_id)
{
	// Current driver supports only *default* memory layout (256 KB ZW Flash / 96 KB SRAM)
	// XXX: Support for external Flash on SPIM requires specific flash code (not implemented)
	switch (part_id) {
	case 0x0240U: // AT32F403AVCT7 / LQFP100
	case 0x0241U: // AT32F403ARCT7 / LQFP64
	case 0x0242U: // AT32F403ACCT7 / LQFP48
	case 0x0243U: // AT32F403ACCU7 / QFN48
	case 0x0249U: // AT32F407VCT7 / LQFP100
	case 0x024aU: // AT32F407RCT7 / LQFP64
	case 0x0254U: // AT32F407AVCT7 / LQFP100
		// Flash (C): 256 KiB / 2 KiB per block
		stm32f1_add_flash(target, STM32F1_FLASH_BANK1_BASE, 256U * 1024U, 2U * 1024U);
		break;
	case 0x02cdU: // AT32F403AVET7 / LQFP100
	case 0x02ceU: // AT32F403ARET7 / LQFP64
	case 0x02cfU: // AT32F403ACET7 / LQFP48
	case 0x02d0U: // AT32F403ACEU7 / QFN48
	case 0x02d1U: // AT32F407VET7 / LQFP100
	case 0x02d2U: // AT32F407RET7 / LQFP64
		// Flash (E): 512 KiB / 2 KiB per block
		stm32f1_add_flash(target, STM32F1_FLASH_BANK1_BASE, 512U * 1024U, 2U * 1024U);
		break;
	default:
		if (at32f40_is_dual_bank(part_id)) {
			// Flash (G): 1024 KiB / 2 KiB per block, dual-bank
			stm32f1_add_flash(target, STM32F1_FLASH_BANK1_BASE, 512U * 1024U, 2U * 1024U);
			stm32f1_add_flash(target, STM32F1_FLASH_BANK2_BASE, 512U * 1024U, 2U * 1024U);
			break;
		} else // Unknown/undocumented
			return false;
	}
	// All parts have 96 KiB SRAM
	target_add_ram32(target, STM32F1_SRAM_BASE, 96U * 1024U);
	target->driver = "AT32F403A/407";
	target->part_id = part_id;
	target->target_options |= STM32F1_TOPT_32BIT_WRITES;
	target->mass_erase = stm32f1_mass_erase;

	/* Now we have a stable debug environment, make sure the WDTs + WFI and WFE instructions can't cause problems */
	return stm32f1_configure_dbgmcu(target, STM32F1_DBGMCU_CONFIG);
}

static bool at32f41_detect(target_s *target, const uint16_t part_id)
{
	switch (part_id) {
	case 0x0240U: // LQFP64_10x10
	case 0x0241U: // LQFP48_7x7
	case 0x0242U: // QFN32_4x4
	case 0x0243U: // LQFP64_7x7
	case 0x024cU: // QFN48_6x6
		// Flash (C): 256 KiB / 2 KiB per block
		stm32f1_add_flash(target, STM32F1_FLASH_BANK1_BASE, 256U * 1024U, 2U * 1024U);
		break;
	case 0x01c4U: // LQFP64_10x10
	case 0x01c5U: // LQFP48_7x7
	case 0x01c6U: // QFN32_4x4
	case 0x01c7U: // LQFP64_7x7
	case 0x01cdU: // QFN48_6x6
		// Flash (B): 128 KiB / 2 KiB per block
		stm32f1_add_flash(target, STM32F1_FLASH_BANK1_BASE, 128U * 1024U, 2U * 1024U);
		break;
	case 0x0108U: // LQFP64_10x10
	case 0x0109U: // LQFP48_7x7
	case 0x010aU: // QFN32_4x4
		// Flash (8): 64 KiB / 2 KiB per block
		stm32f1_add_flash(target, STM32F1_FLASH_BANK1_BASE, 64U * 1024U, 2U * 1024U);
		break;
	// Unknown/undocumented
	default:
		return false;
	}
	// All parts have 32 KiB SRAM
	target_add_ram32(target, STM32F1_SRAM_BASE, 32U * 1024U);
	target->driver = "AT32F415";
	target->part_id = part_id;
	target->target_options |= STM32F1_TOPT_32BIT_WRITES;
	target->mass_erase = stm32f1_mass_erase;

	/* Now we have a stable debug environment, make sure the WDTs + WFI and WFE instructions can't cause problems */
	return stm32f1_configure_dbgmcu(target, STM32F1_DBGMCU_CONFIG);
}

/* Identify AT32F40x "Mainstream" line devices (Cortex-M4) */
bool at32f40x_probe(target_s *target)
{
	// Artery clones use Cortex M4 cores
	if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) != CORTEX_M4)
		return false;

	// Artery chips use the complete idcode word for identification
	const uint32_t idcode = target_mem32_read32(target, STM32F1_DBGMCU_IDCODE);
	const uint32_t series = idcode & AT32F4x_IDCODE_SERIES_MASK;
	const uint16_t part_id = idcode & AT32F4x_IDCODE_PART_MASK;

	if (series == AT32F40_SERIES)
		return at32f40_detect(target, part_id);
	if (series == AT32F41_SERIES)
		return at32f41_detect(target, part_id);
	return false;
}

/* Pack data from the source value into a uint32_t based on data alignment */
const void *mm32l0_pack_data(const void *const src, uint32_t *const data, const align_e align)
{
	switch (align) {
	case ALIGN_8BIT: {
		uint8_t value;
		/* Copy the data to pack in from the source buffer */
		memcpy(&value, src, sizeof(value));
		/* Then broadcast it to all 4 bytes of the uint32_t */
		*data = (uint32_t)value | ((uint32_t)value << 8U) | ((uint32_t)value << 16U) | ((uint32_t)value << 24U);
		break;
	}
	case ALIGN_16BIT: {
		uint16_t value;
		/* Copy the data to pack in from the source buffer (avoids unaligned read issues) */
		memcpy(&value, src, sizeof(value));
		/* Then broadcast it to both halfs of the uint32_t */
		*data = (uint32_t)value | ((uint32_t)value << 16U);
		break;
	}
	default:
		/*
		 * 32- and 64-bit aligned reads don't need to do anything special beyond using memcpy()
		 * to avoid doing  an unaligned read of src, or any UB casts.
		 */
		memcpy(data, src, sizeof(*data));
		break;
	}
	return (const uint8_t *)src + (1U << align);
}

/*
 * Perform a memory write. Unlike with fully compliant ADIv5 devices, MM32 ones, like the
 * Cortex-M0 based MM32L0 parts always use the lowest lane (0:7, 0:15, etc) for the data.
 * Broadcasting the value to write to all lanes is harmless though and works for both
 * MM32 devices and STM32 devices which comply properly with ADIv5.
 */
void mm32l0_mem_write_sized(adiv5_access_port_s *ap, target_addr64_t dest, const void *src, size_t len, align_e align)
{
	/* Do nothing and return if there's nothing to write */
	if (len == 0U)
		return;
	/* Calculate the extent of the transfer (NB: no MM32 parts are 64-bit, so truncate) */
	target_addr32_t begin = (target_addr32_t)dest;
	const target_addr32_t end = begin + len;
	/* Calculate how much each loop will increment the destination address by */
	const uint8_t stride = 1U << align;
	/* Set up the transfer */
	adiv5_mem_access_setup(ap, dest, align);
	/* Now loop through the data and move it 1 stride at a time to the target */
	for (; begin < end; begin += stride) {
		/*
		 * Check if the address doesn't overflow the 10-bit auto increment bound for TAR,
		 * if it's not the first transfer (offset == 0)
		 */
		if (begin != dest && (begin & 0x00000effU) == 0U)
			/* Update TAR to adjust the upper bits */
			adiv5_dp_write(ap->dp, ADIV5_AP_TAR_LOW, begin);
		/* Pack the data for transfer */
		uint32_t value = 0;
		src = mm32l0_pack_data(src, &value, align);
		/* And copy the result to the target */
		adiv5_dp_write(ap->dp, ADIV5_AP_DRW, value);
	}
	/* Make sure this write is complete by doing a dummy read */
	adiv5_dp_read(ap->dp, ADIV5_DP_RDBUFF);
}

/* Identify MM32 devices (Cortex-M0) */
bool mm32l0xx_probe(target_s *target)
{
	size_t flash_kbyte = 0;
	size_t ram_kbyte = 0;
	size_t block_size = 0x400U;

	const uint32_t mm32_id = target_mem32_read32(target, MM32L0_DBGMCU_IDCODE);
	if (target_check_error(target)) {
		DEBUG_ERROR("%s: read error at 0x%" PRIx32 "\n", __func__, (uint32_t)MM32L0_DBGMCU_IDCODE);
		return false;
	}
	switch (mm32_id) {
	case 0xcc568091U:
		target->driver = "MM32L07x";
		flash_kbyte = 128;
		ram_kbyte = 8;
		break;
	case 0xcc4460b1:
		target->driver = "MM32SPIN05";
		flash_kbyte = 32;
		ram_kbyte = 4;
		break;
	case 0xcc56a097U:
		target->driver = "MM32SPIN27";
		flash_kbyte = 128;
		ram_kbyte = 12;
		break;
	case 0x00000000U:
	case 0xffffffffU:
		return false;
	default:
		DEBUG_WARN("%s: unknown mm32 dev_id 0x%" PRIx32 "\n", __func__, mm32_id);
		return false;
	}
	target->part_id = mm32_id & 0xfffU;
	target->mass_erase = stm32f1_mass_erase;
	target_add_ram32(target, STM32F1_SRAM_BASE, ram_kbyte * 1024U);
	stm32f1_add_flash(target, STM32F1_FLASH_BANK1_BASE, flash_kbyte * 1024U, block_size);
	target_add_commands(target, stm32f1_cmd_list, target->driver);
	cortex_ap(target)->dp->mem_write = mm32l0_mem_write_sized;

	/* Now we have a stable debug environment, make sure the WDTs + WFI and WFE instructions can't cause problems */
	return stm32f1_configure_dbgmcu(target, MM32L0_DBGMCU_CONFIG);
}

/* Identify MM32 devices (Cortex-M3, Star-MC1) */
bool mm32f3xx_probe(target_s *target)
{
	size_t flash_kbyte = 0;
	size_t ram1_kbyte = 0; /* ram at 0x20000000 */
	size_t ram2_kbyte = 0; /* ram at 0x30000000 */
	size_t block_size = 0x400U;

	const uint32_t mm32_id = target_mem32_read32(target, MM32F3_DBGMCU_IDCODE);
	if (target_check_error(target)) {
		DEBUG_ERROR("%s: read error at 0x%" PRIx32 "\n", __func__, (uint32_t)MM32F3_DBGMCU_IDCODE);
		return false;
	}
	switch (mm32_id) {
	case 0xcc9aa0e7U:
		target->driver = "MM32F327";
		flash_kbyte = 512;
		ram1_kbyte = 128;
		break;
	case 0x4d4d0800U:
		target->driver = "MM32F52";
		flash_kbyte = 256;
		ram1_kbyte = 32;
		ram2_kbyte = 128;
		break;
	case 0x00000000U:
	case 0xffffffffU:
		return false;
	default:
		DEBUG_WARN("%s: unknown mm32 ID code 0x%" PRIx32 "\n", __func__, mm32_id);
		return false;
	}

	target->part_id = mm32_id & 0xfffU;
	target->mass_erase = stm32f1_mass_erase;
	if (ram1_kbyte != 0)
		target_add_ram32(target, STM32F1_SRAM_BASE, ram1_kbyte * 1024U);
	if (ram2_kbyte != 0)
		target_add_ram32(target, 0x30000000U, ram2_kbyte * 1024U);
	stm32f1_add_flash(target, STM32F1_FLASH_BANK1_BASE, flash_kbyte * 1024U, block_size);
	target_add_commands(target, stm32f1_cmd_list, target->driver);

	/* Now we have a stable debug environment, make sure the WDTs + WFI and WFE instructions can't cause problems */
	return stm32f1_configure_dbgmcu(target, MM32F3_DBGMCU_CONFIG);
}

/* Identify real STM32F0/F1/F3 devices */
bool stm32f1_probe(target_s *target)
{
	target_addr32_t dbgmcu_config_taddr;
	const uint16_t device_id = stm32f1_read_idcode(target, &dbgmcu_config_taddr);

	uint32_t ram_size = 0;
	size_t flash_size = 0;
	size_t block_size = 0;

	switch (device_id) {
	case 0x29bU: /* CS clone */
	case 0x410U: /* Medium density */
	case 0x412U: /* Low density */
	case 0x420U: /* Value Line, Low-/Medium density */
		ram_size = 0x5000;
		flash_size = 0x20000;
		block_size = 0x400;
		/* Test for clone parts with Core rev 2 */
		adiv5_access_port_s *ap = cortex_ap(target);
		if ((ap->idr >> 28U) > 1U) {
			target->driver = "Clone STM32F1 medium density";
			DEBUG_WARN("Detected clone STM32F1\n");
		} else
			target->driver = "STM32F1 L/M density";
		break;

	case 0x414U: /* High density */
	case 0x418U: /* Connectivity Line */
	case 0x428U: /* Value Line, High Density */
		target->driver = "STM32F1 VL density";
		ram_size = 0x10000;
		flash_size = 0x80000;
		block_size = 0x800;
		break;

	case 0x430U: /* XL-density */
		target->driver = "STM32F1 XL density";
		ram_size = 0x18000;
		flash_size = 0x80000;
		block_size = 0x800;
		stm32f1_add_flash(target, STM32F1_FLASH_BANK2_BASE, flash_size, block_size);
		break;

	case 0x438U: /* STM32F303x6/8 and STM32F328 */
	case 0x422U: /* STM32F30x */
	case 0x446U: /* STM32F303xD/E and STM32F398xE */
		target_add_ram32(target, 0x10000000, 0x4000);

		BMD_FALLTHROUGH
	case 0x432U: /* STM32F37x */
	case 0x439U: /* STM32F302C8 */
		target->driver = "STM32F3";
		ram_size = 0x10000;
		flash_size = 0x80000;
		block_size = 0x800;
		target_add_commands(target, stm32f1_cmd_list, "STM32F3");
		break;

	case 0x444U: /* STM32F03 RM0091 Rev. 7, STM32F030x[4|6] RM0360 Rev. 4 */
		target->driver = "STM32F03";
		ram_size = 0x5000;
		flash_size = 0x8000;
		block_size = 0x400;
		break;

	case 0x445U: /* STM32F04 RM0091 Rev. 7, STM32F070x6 RM0360 Rev. 4 */
		target->driver = "STM32F04/F070x6";
		ram_size = 0x5000;
		flash_size = 0x8000;
		block_size = 0x400;
		break;

	case 0x440U: /* STM32F05 RM0091 Rev. 7, STM32F030x8 RM0360 Rev. 4 */
		target->driver = "STM32F05/F030x8";
		ram_size = 0x5000;
		flash_size = 0x10000;
		block_size = 0x400;
		break;

	case 0x448U: /* STM32F07 RM0091 Rev. 7, STM32F070xb RM0360 Rev. 4 */
		target->driver = "STM32F07";
		ram_size = 0x5000;
		flash_size = 0x20000;
		block_size = 0x800;
		break;

	case 0x442U: /* STM32F09 RM0091 Rev. 7, STM32F030xc RM0360 Rev. 4 */
		target->driver = "STM32F09/F030xc";
		ram_size = 0x5000;
		flash_size = 0x40000;
		block_size = 0x800;
		break;

	default: /* NONE */
		return false;
	}

	target->part_id = device_id;
	target->mass_erase = stm32f1_mass_erase;
	target_add_ram32(target, STM32F1_SRAM_BASE, ram_size);
	stm32f1_add_flash(target, STM32F1_FLASH_BANK1_BASE, flash_size, block_size);
	target_add_commands(target, stm32f1_cmd_list, target->driver);

	/* Now we have a stable debug environment, make sure the WDTs + WFI and WFE instructions can't cause problems */
	return stm32f1_configure_dbgmcu(target, dbgmcu_config_taddr);
}

static bool stm32f1_attach(target_s *const target)
{
	/*
	 * Try to attach to the part, and then ensure that the WDTs + WFI and WFE
	 * instructions can't cause problems (this is duplicated as it's undone by detach.)
	 */
	return cortexm_attach(target) && stm32f1_configure_dbgmcu(target, 0U);
}

static void stm32f1_detach(target_s *const target)
{
	const stm32f1_priv_s *const priv = (stm32f1_priv_s *)target->target_storage;
	/* Reverse all changes to the DBGMCU config register */
	target_mem32_write32(target, priv->dbgmcu_config_taddr, priv->dbgmcu_config);
	/* Now defer to the normal Cortex-M detach routine to complete the detach */
	cortexm_detach(target);
}

static bool stm32f1_flash_unlock(target_s *target, uint32_t bank_offset)
{
	target_mem32_write32(target, FLASH_KEYR + bank_offset, KEY1);
	target_mem32_write32(target, FLASH_KEYR + bank_offset, KEY2);
	uint32_t ctrl = target_mem32_read32(target, FLASH_CR + bank_offset);
	if (ctrl & FLASH_CR_LOCK)
		DEBUG_ERROR("unlock failed, cr: 0x%08" PRIx32 "\n", ctrl);
	return !(ctrl & FLASH_CR_LOCK);
}

static inline void stm32f1_flash_clear_eop(target_s *const target, const uint32_t bank_offset)
{
	const uint32_t status = target_mem32_read32(target, FLASH_SR + bank_offset);
	target_mem32_write32(target, FLASH_SR + bank_offset, status | SR_EOP); /* EOP is W1C */
}

static bool stm32f1_flash_busy_wait(
	target_s *const target, const uint32_t bank_offset, platform_timeout_s *const timeout)
{
	/* Read FLASH_SR to poll for BSY bit */
	uint32_t status = FLASH_SR_BSY;
	/*
	 * Please note that checking EOP here is only legal because every operation is preceded by
	 * a call to stm32f1_flash_clear_eop. Without this the flag could be stale from a previous
	 * operation and is always set at the end of every program/erase operation.
	 * For more information, see FLASH_SR register description §3.4 pg 25.
	 * https://www.st.com/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf
	 */
	while (!(status & SR_EOP) && (status & FLASH_SR_BSY)) {
		status = target_mem32_read32(target, FLASH_SR + bank_offset);
		if (target_check_error(target)) {
			DEBUG_ERROR("Lost communications with target");
			return false;
		}
		if (timeout)
			target_print_progress(timeout);
	};
	if (status & SR_ERROR_MASK)
		DEBUG_ERROR("stm32f1 flash error 0x%" PRIx32 "\n", status);
	return !(status & SR_ERROR_MASK);
}

static bool stm32f1_is_dual_bank(const uint16_t part_id)
{
	if (part_id == 0x430U) /* XL-density */
		return true;
	if (at32f40_is_dual_bank(part_id))
		return true;
	return false;
}

static uint32_t stm32f1_bank_offset_for(target_addr_t addr)
{
	if (addr >= FLASH_BANK_SPLIT)
		return FLASH_BANK2_OFFSET;
	return FLASH_BANK1_OFFSET;
}

static bool stm32f1_flash_erase(target_flash_s *flash, target_addr_t addr, size_t length)
{
	target_s *target = flash->t;
	target_addr_t end = addr + length - 1U;
	DEBUG_TARGET("%s: at %08" PRIx32 "\n", __func__, addr);

	/* Unlocked an appropriate flash bank */
	if ((stm32f1_is_dual_bank(target->part_id) && end >= FLASH_BANK_SPLIT &&
			!stm32f1_flash_unlock(target, FLASH_BANK2_OFFSET)) ||
		(addr < FLASH_BANK_SPLIT && !stm32f1_flash_unlock(target, 0)))
		return false;

	const uint32_t bank_offset = stm32f1_bank_offset_for(addr);
	stm32f1_flash_clear_eop(target, bank_offset);

	/* Flash page erase instruction */
	target_mem32_write32(target, FLASH_CR + bank_offset, FLASH_CR_PER);
	/* write address to FMA */
	target_mem32_write32(target, FLASH_AR + bank_offset, addr);
	/* Flash page erase start instruction */
	target_mem32_write32(target, FLASH_CR + bank_offset, FLASH_CR_STRT | FLASH_CR_PER);

	/* Wait for completion or an error */
	return stm32f1_flash_busy_wait(target, bank_offset, NULL);
}

static size_t stm32f1_bank1_length(target_addr_t addr, size_t len)
{
	if (addr >= FLASH_BANK_SPLIT)
		return 0;
	if (addr + len > FLASH_BANK_SPLIT)
		return FLASH_BANK_SPLIT - addr;
	return len;
}

static bool stm32f1_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	target_s *target = flash->t;
	const size_t offset = stm32f1_bank1_length(dest, len);
	DEBUG_TARGET("%s: at %08" PRIx32 " for %zu bytes\n", __func__, dest, len);

	/* Allow wider writes on Gigadevices and Arterytek */
	const align_e psize = (target->target_options & STM32F1_TOPT_32BIT_WRITES) ? ALIGN_32BIT : ALIGN_16BIT;

	/* Start by writing any bank 1 data */
	if (offset) {
		stm32f1_flash_clear_eop(target, FLASH_BANK1_OFFSET);

		target_mem32_write32(target, FLASH_CR, FLASH_CR_PG);
		/* Use the target API instead of a direct Cortex-M call for GD32VF103 parts */
		if (target->designer_code == JEP106_MANUFACTURER_RV_GIGADEVICE && target->cpuid == 0x80000022U)
			target_mem32_write(target, dest, src, offset);
		else
			cortexm_mem_write_aligned(target, dest, src, offset, psize);

		/* Wait for completion or an error */
		if (!stm32f1_flash_busy_wait(target, FLASH_BANK1_OFFSET, NULL))
			return false;
	}

	/* If there's anything to write left over and we're on a part with a second bank, write to bank 2 */
	const size_t remainder = len - offset;
	if (stm32f1_is_dual_bank(target->part_id) && remainder) {
		const uint8_t *data = src;
		stm32f1_flash_clear_eop(target, FLASH_BANK2_OFFSET);

		target_mem32_write32(target, FLASH_CR + FLASH_BANK2_OFFSET, FLASH_CR_PG);
		/* Use the target API instead of a direct Cortex-M call for GD32VF103 parts */
		if (target->designer_code == JEP106_MANUFACTURER_RV_GIGADEVICE && target->cpuid == 0x80000022U)
			target_mem32_write(target, dest + offset, data + offset, remainder);
		else
			cortexm_mem_write_aligned(target, dest + offset, data + offset, remainder, psize);

		/* Wait for completion or an error */
		if (!stm32f1_flash_busy_wait(target, FLASH_BANK2_OFFSET, NULL))
			return false;
	}

	return true;
}

static bool stm32f1_mass_erase_bank(
	target_s *const target, const uint32_t bank_offset, platform_timeout_s *const timeout)
{
	/* Unlock the bank */
	if (!stm32f1_flash_unlock(target, bank_offset))
		return false;
	stm32f1_flash_clear_eop(target, bank_offset);

	/* Flash mass erase start instruction */
	target_mem32_write32(target, FLASH_CR + bank_offset, FLASH_CR_MER);
	target_mem32_write32(target, FLASH_CR + bank_offset, FLASH_CR_STRT | FLASH_CR_MER);

	/* Wait for completion or an error */
	return stm32f1_flash_busy_wait(target, bank_offset, timeout);
}

static bool stm32f1_mass_erase(target_s *target)
{
	if (!stm32f1_flash_unlock(target, 0))
		return false;

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	if (!stm32f1_mass_erase_bank(target, FLASH_BANK1_OFFSET, &timeout))
		return false;

	/* If we're on a part that has a second bank, mass erase that bank too */
	if (stm32f1_is_dual_bank(target->part_id))
		return stm32f1_mass_erase_bank(target, FLASH_BANK2_OFFSET, &timeout);
	return true;
}

static uint16_t stm32f1_flash_readable_key(const target_s *const target)
{
	switch (target->part_id) {
	case 0x422U: /* STM32F30x */
	case 0x432U: /* STM32F37x */
	case 0x438U: /* STM32F303x6/8 and STM32F328 */
	case 0x440U: /* STM32F0 */
	case 0x446U: /* STM32F303xD/E and STM32F398xE */
	case 0x445U: /* STM32F04 RM0091 Rev.7, STM32F070x6 RM0360 Rev. 4*/
	case 0x448U: /* STM32F07 RM0091 Rev.7, STM32F070xb RM0360 Rev. 4*/
	case 0x442U: /* STM32F09 RM0091 Rev.7, STM32F030xc RM0360 Rev. 4*/
		return FLASH_OBP_RDP_KEY_F3;
	}
	return FLASH_OBP_RDP_KEY;
}

static bool stm32f1_option_erase(target_s *target)
{
	stm32f1_flash_clear_eop(target, FLASH_BANK1_OFFSET);

	/* Erase option bytes instruction */
	target_mem32_write32(target, FLASH_CR, FLASH_CR_OPTER | FLASH_CR_OPTWRE);
	target_mem32_write32(target, FLASH_CR, FLASH_CR_STRT | FLASH_CR_OPTER | FLASH_CR_OPTWRE);

	/* Wait for completion or an error */
	return stm32f1_flash_busy_wait(target, FLASH_BANK1_OFFSET, NULL);
}

static bool stm32f1_option_write_erased(
	target_s *const target, const size_t offset, const uint16_t value, const bool write16_broken)
{
	if (value == 0xffffU)
		return true;

	stm32f1_flash_clear_eop(target, FLASH_BANK1_OFFSET);

	/* Erase option bytes instruction */
	target_mem32_write32(target, FLASH_CR, FLASH_CR_OPTPG | FLASH_CR_OPTWRE);

	const uint32_t addr = FLASH_OBP_RDP + (offset * 2U);
	if (write16_broken)
		target_mem32_write32(target, addr, 0xffff0000U | value);
	else
		target_mem32_write16(target, addr, value);

	/* Wait for completion or an error */
	const bool result = stm32f1_flash_busy_wait(target, FLASH_BANK1_OFFSET, NULL);
	if (result || offset != 0U)
		return result;
	/*
	 * In the case that the write failed and we're handling option byte 0 (RDP),
	 * check if we got a status of "Program Error" in FLASH_SR, indicating the target
	 * refused to erase the read protection option bytes (and turn it into a truthy return).
	 */
	const uint8_t status = target_mem32_read32(target, FLASH_SR) & SR_ERROR_MASK;
	return status == SR_PROG_ERROR;
}

static bool stm32f1_option_write(target_s *const target, const uint32_t addr, const uint16_t value)
{
	const uint32_t index = (addr - FLASH_OBP_RDP) >> 1U;
	/* If index would be negative, the high most bit is set, so we get a giant positive number. */
	if (index > 7U)
		return false;

	uint16_t opt_val[8];
	/* Retrieve old values */
	for (size_t i = 0U; i < 16U; i += 4U) {
		const size_t offset = i >> 1U;
		uint32_t val = target_mem32_read32(target, FLASH_OBP_RDP + i);
		opt_val[offset] = val & 0xffffU;
		opt_val[offset + 1U] = val >> 16U;
	}

	if (opt_val[index] == value)
		return true;

	/* Check for erased value */
	if (opt_val[index] != 0xffffU && !stm32f1_option_erase(target))
		return false;
	opt_val[index] = value;

	/*
	 * Write changed values, taking into account if we can use 32- or have to use 16-bit writes.
	 * GD32E230 is a special case as target_mem32_write16 does not work
	 */
	const bool write16_broken = target->part_id == 0x410U && (target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M23;
	for (size_t i = 0U; i < 8U; ++i) {
		if (!stm32f1_option_write_erased(target, i, opt_val[i], write16_broken))
			return false;
	}

	return true;
}

static bool stm32f1_cmd_option(target_s *target, int argc, const char **argv)
{
	const uint32_t read_protected = target_mem32_read32(target, FLASH_OBR) & FLASH_OBR_RDPRT;
	const bool erase_requested = argc == 2 && strcmp(argv[1], "erase") == 0;
	/* Fast-exit if the Flash is not readable and the user didn't ask us to erase the option bytes */
	if (read_protected && !erase_requested) {
		tc_printf(target, "Device is Read Protected\nUse `monitor option erase` to unprotect and erase device\n");
		return true;
	}

	/* Unprotect the option bytes so we can modify them */
	if (!stm32f1_flash_unlock(target, FLASH_BANK1_OFFSET))
		return false;
	target_mem32_write32(target, FLASH_OPTKEYR, KEY1);
	target_mem32_write32(target, FLASH_OPTKEYR, KEY2);

	if (erase_requested) {
		/* When the user asks us to erase the option bytes, kick of an erase */
		if (!stm32f1_option_erase(target))
			return false;
		/*
		 * Write the option bytes Flash readable key, taking into account if we can
		 * use 32- or have to use 16-bit writes.
		 * GD32E230 is a special case as target_mem32_write16 does not work
		 */
		const bool write16_broken =
			target->part_id == 0x410U && (target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M23;
		if (!stm32f1_option_write_erased(target, 0U, stm32f1_flash_readable_key(target), write16_broken))
			return false;
	} else if (argc == 3) {
		/* If 3 arguments are given, assume the second is an address, and the third a value */
		const uint32_t addr = strtoul(argv[1], NULL, 0);
		const uint32_t val = strtoul(argv[2], NULL, 0);
		/* Try and program the new option value to the requested option byte */
		if (!stm32f1_option_write(target, addr, val))
			return false;
	} else
		tc_printf(target, "usage: monitor option erase\nusage: monitor option <addr> <value>\n");

	/* When all gets said and done, display the current option bytes values */
	for (size_t i = 0U; i < 16U; i += 4U) {
		const uint32_t addr = FLASH_OBP_RDP + i;
		const uint32_t val = target_mem32_read32(target, addr);
		tc_printf(target, "0x%08" PRIX32 ": 0x%04" PRIX32 "\n", addr, val & 0xffffU);
		tc_printf(target, "0x%08" PRIX32 ": 0x%04" PRIX32 "\n", addr + 2U, val >> 16U);
	}

	return true;
}

static bool stm32f1_cmd_uid(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	target_addr_t uid_base = STM32F1_UID_BASE;
	/* These parts have their UID elsewhere */
	if (stm32f1_flash_readable_key(target) == FLASH_OBP_RDP_KEY_F3)
		uid_base = STM32F3_UID_BASE;
	return stm32_uid(target, uid_base);
}
