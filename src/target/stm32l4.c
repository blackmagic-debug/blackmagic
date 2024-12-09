/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015, 2017-2022 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
 * Copyright (C) 2022-2024 1BitSquared <info@1bitsquared.com>
 * Written by Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
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
 * This file implements STM32L4 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * On L4, flash and options are written in DWORDs (8-Byte) only.
 *
 * References:
 * RM0351 - STM32L4x5 and STM32L4x6 advanced ARM®-based 32-bit MCUs Rev 9
 * - https://www.st.com/resource/en/reference_manual/rm0351-stm32l47xxx-stm32l48xxx-stm32l49xxx-and-stm32l4axxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * RM0394 - STM32L43xxx STM32L44xxx STM32L45xxx STM32L46xxxx advanced ARM®-based 32-bit MCUs Rev.4
 * - https://www.st.com/resource/en/reference_manual/dm00151940-stm32l41xxx42xxx43xxx44xxx45xxx46xxx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * RM0432 - STM32L4Rxxx and STM32L4Sxxx advanced Arm®-based 32-bit MCU. Rev 9
 * - https://www.st.com/resource/en/reference_manual/rm0432-stm32l4-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * RM0440 - STM32G4 Series advanced Arm®-based 32-bit MCU. Rev 7
 * - https://www.st.com/resource/en/reference_manual/rm0440-stm32g4-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * RM0438 - STM32L552xx and STM32L562xx advanced Arm®-based 32-bit MCUs Rev 7
 * - https://www.st.com/resource/en/reference_manual/dm00346336-stm32l552xx-and-stm32l562xx-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf
 * RM0456 - STM32U5 Series Arm®-based 32-bit MCUs - Reference manual Rev 4
 * - https://www.st.com/resource/en/reference_manual/rm0456-stm32u5-series-armbased-32bit-mcus-stmicroelectronics.pdf
 * RM0453 - STM32WL5x advanced Arm®-based 32-bit MCUs with sub-GHz radio solution Rev 3
 * - https://www.st.com/resource/en/reference_manual/rm0453-stm32wl5x-advanced-armbased-32bit-mcus-with-subghz-radio-solution-stmicroelectronics.pdf
 * RM0461 - STM32WLEx advanced Arm®-based 32-bit MCUs with sub-GHz radio solution Rev 5
 * - https://www.st.com/resource/en/reference_manual/rm0461-stm32wlex-advanced-armbased-32bit-mcus-with-subghz-radio-solution-stmicroelectronics.pdf
 * RM0434 - Multiprotocol wireless 32-bit MCU Arm®-based Cortex®-M4 with
 *        FPU, Bluetooth® Low-Energy and 802.15.4 radio solution Rev 10
 * - https://www.st.com/resource/en/reference_manual/rm0434-multiprotocol-wireless-32bit-mcu-armbased-cortexm4-with-fpu-bluetooth-lowenergy-and-802154-radio-solution-stmicroelectronics.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "stm32_common.h"
#include <limits.h>
#include <assert.h>

static bool stm32l4_cmd_erase_bank1(target_s *target, int argc, const char **argv);
static bool stm32l4_cmd_erase_bank2(target_s *target, int argc, const char **argv);
static bool stm32l4_cmd_option(target_s *target, int argc, const char **argv);
static bool stm32l4_cmd_uid(target_s *target, int argc, const char **argv);

static bool stm32l4_attach(target_s *target);
static void stm32l4_detach(target_s *target);
static bool stm32l4_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool stm32l4_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool stm32l4_mass_erase(target_s *target, platform_timeout_s *print_progess);

const command_s stm32l4_cmd_list[] = {
	{"erase_bank1", stm32l4_cmd_erase_bank1, "Erase entire bank1 flash memory"},
	{"erase_bank2", stm32l4_cmd_erase_bank2, "Erase entire bank2 flash memory"},
	{"option", stm32l4_cmd_option, "Manipulate option bytes"},
	{"uid", stm32l4_cmd_uid, "Print unique device ID"},
	{NULL, NULL, NULL},
};

/* Flash Program ad Erase Controller Register Map */
#define STM32L4_FPEC_BASE 0x40022000U
#define STM32L5_FPEC_BASE 0x40022000U
#define STM32WL_FPEC_BASE 0x58004000U
#define STM32WB_FPEC_BASE 0x58004000U

#define STM32L5_FLASH_OPTR_TZEN (1U << 31U)

#define FLASH_OPTR_ESE (1U << 8U)
#define PWR_CR4        0x5800040cU
#define PWR_CR4_C2BOOT (1U << 15U)

#define FLASH_CR_PG         (1U << 0U)
#define FLASH_CR_PER        (1U << 1U)
#define FLASH_CR_MER1       (1U << 2U)
#define FLASH_CR_PAGE_SHIFT 3U
#define FLASH_CR_BKER       (1U << 11U)
#define FLASH_CR_MER2       (1U << 15U)
#define FLASH_CR_STRT       (1U << 16U)
#define FLASH_CR_OPTSTRT    (1U << 17U)
#define FLASH_CR_FSTPG      (1U << 18U)
#define FLASH_CR_EOPIE      (1U << 24U)
#define FLASH_CR_ERRIE      (1U << 25U)
#define FLASH_CR_OBL_LAUNCH (1U << 27U)
#define FLASH_CR_OPTLOCK    (1U << 30U)
#define FLASH_CR_LOCK       (1U << 31U)

#define FLASH_SR_EOP        (1U << 0U)
#define FLASH_SR_OPERR      (1U << 1U)
#define FLASH_SR_PROGERR    (1U << 3U)
#define FLASH_SR_WRPERR     (1U << 4U)
#define FLASH_SR_PGAERR     (1U << 5U)
#define FLASH_SR_SIZERR     (1U << 6U)
#define FLASH_SR_PGSERR     (1U << 7U)
#define FLASH_SR_MSERR      (1U << 8U)
#define FLASH_SR_FASTERR    (1U << 9U)
#define FLASH_SR_RDERR      (1U << 14U)
#define FLASH_SR_OPTVERR    (1U << 15U)
#define FLASH_SR_ERROR_MASK 0xc3faU
#define FLASH_SR_BSY        (1U << 16U)

#define STM32L4_FLASH_BANK_1_BASE 0x08000000U
#define FLASH_SIZE_MAX_G4_CAT4    (512U * 1024U) // 512kiB

#define KEY1 0x45670123U
#define KEY2 0xcdef89abU

#define OPTKEY1 0x08192a3bU
#define OPTKEY2 0x4c5d6e7fU

#define SR_ERROR_MASK 0xf2U

/* Used in STM32L47 */
#define OR_DUALBANK (1U << 21U)
/* Used in STM32L47R */
#define OR_DB1M (1U << 21U)
/* Used in STM32L47R, STM32G47 and STM32L55 */
#define OR_DBANK (1U << 22U)

#define STM32L4_DBGMCU_BASE        0xe0042000U
#define STM32L4_DBGMCU_IDCODE      (STM32L4_DBGMCU_BASE + 0x000U)
#define STM32L4_DBGMCU_CONFIG      (STM32L4_DBGMCU_BASE + 0x004U)
#define STM32L4_DBGMCU_APB1FREEZE1 (STM32L4_DBGMCU_BASE + 0x008U)

#define STM32L5_DBGMCU_BASE        0xe0044000U
#define STM32L5_DBGMCU_IDCODE      (STM32L5_DBGMCU_BASE + 0x000U)
#define STM32L5_DBGMCU_CONFIG      (STM32L5_DBGMCU_BASE + 0x004U)
#define STM32L5_DBGMCU_APB1FREEZE1 (STM32L5_DBGMCU_BASE + 0x008U)

#define STM32L4_DBGMCU_CONFIG_DBG_SLEEP   (1U << 0U)
#define STM32L4_DBGMCU_CONFIG_DBG_STOP    (1U << 1U)
#define STM32L4_DBGMCU_CONFIG_DBG_STANDBY (1U << 2U)
#define STM32L4_DBGMCU_APB1FREEZE1_WWDG   (1U << 11U)
#define STM32L4_DBGMCU_APB1FREEZE1_IWDG   (1U << 12U)

#define STM32L4_UID_BASE       0x1fff7590U
#define STM32L4_FLASH_SIZE_REG 0x1fff75e0U
#define STM32L5_FLASH_SIZE_REG 0x0bfa05e0U
#define STM32U5_FLASH_SIZE_REG 0x0bfa07a0U

#define STM32L5_RCC_APB1ENR1       0x50021058U
#define STM32L5_RCC_APB1ENR1_PWREN (1U << 28U)
#define STM32L5_PWR_CR1            0x50007000U
#define STM32L5_PWR_CR1_VOS        (3U << 9U)

#define DUAL_BANK     0x80U
#define RAM_COUNT_MSK 0x07U

/* TODO: add block size constants for other MCUs */
#define STM32U5_FLASH_BLOCK_SIZE 0x2000U

/*
 * This first block of devices uses the AP part number, as that matches the DBGMCU
 * ID code value at address 0xe0042000. These are SWD-DPv1 parts.
 */
#define ID_STM32L41 0x464U /* RM0394, Rev.4 §46.6.1 DBGMCU_IDCODE pg 1560 */
#define ID_STM32L43 0x435U /* RM0394, Rev.4 §46.6.1 DBGMCU_IDCODE pg 1560 */
#define ID_STM32L45 0x462U /* RM0394, Rev.4 §46.6.1 DBGMCU_IDCODE pg 1560 */
#define ID_STM32L47 0x415U /* RM0351, Rev.9 §48.6.1 DBGMCU_IDCODE pg 1840 */
#define ID_STM32L49 0x461U /* RM0351, Rev.9 §48.6.1 DBGMCU_IDCODE pg 1840 */
#define ID_STM32L4R 0x470U /* RM0432, Rev.9 §57.6.1 DBGMCU_IDCODE pg 2245 */
#define ID_STM32L4P 0x471U /* RM0432, Rev.9 §57.6.1 DBGMCU_IDCODE pg 2245 */

#define ID_STM32G43 0x468U /* RM0440, Rev.7 §47.6.1 DBGMCU_IDCODE pg 2082 (Category 2) */
#define ID_STM32G47 0x469U /* RM0440, Rev.7 §47.6.1 DBGMCU_IDCODE pg 2082 (Category 3) */
#define ID_STM32G49 0x479U /* RM0440, Rev.7 §47.6.1 DBGMCU_IDCODE pg 2082 (Category 4) */
/*
 * The L55 series uses SWD-DPv2, which has a TARGETID register value of 0x0472.
 * As the part is also accessible over JTAG-DPv0 which does not have this register,
 * we consider other options as outlined below:
 * TARGETID then matches the DBGMCU_IDCODE value at 0xe0044000. This means the
 * default target part_id is just fine. These numbers also match the AP part number.
 * TARGETID is a ADIv5 DP register in bank 2 from the ADIv5.2 spec §B2.2.10
 * RM0438, Rev.7 §52.2.10 DP_TARGETID pg 2033
 * RM0438, Rev.7 §52.4.1 MCU ROM table PIDR pg 2047
 * RM0438, Rev.7 §52.11.1 DBGMCU_IDCODE pg 2157
 */
#define ID_STM32L55 0x0472U
/*
 * These next parts are likewise accessible over both SWD-DPv2 and JTAG-DPv0, however
 * many of them carry the typical ST TARGETID encoding error which puts the value off
 * by a nibble, so requiring identification by other means.
 *
 * References for the U5 parts:
 * - RM0456, Rev.5 §75.3.3 DP_TARGETID pg 3497
 * - RM0456, Rev.5 §75.5.1 MCU ROM table PIDR pg 3510
 * - RM0456, Rev.5 §75.12.4 DBGMCU_IDCODE pg 3604 (at address 0xe0044000)
 * References for the WL parts:
 * - RM0453, Rev.3 §38.4.5 DP_TARGETID pg 1333
 * - RM0453, Rev.3 §38.8.3 CPU1 ROM table PIDR pg 1381
 * - RM0453, Rev.3 §38.12.1 DBGMCU_IDCODE pg 1415 (at address 0xe0042000)
 * - RM0461, Rev.5 §36.4.5 DP_TARGETID pg 1226
 * - RM0461, Rev.5 §36.7.3 ROM table PIDR pg 1253
 * - RM0461, Rev.5 §36.11.1 DBGMCU_IDCODE pg 1287 (at address 0xe0042000)
 * References for WB35/WB55 parts:
 * - RM0434, Rev.10 §41.4.8 DP_TARGETID pg 1361
 * - RM0434, Rev.10 §41.8.1 DBGMCU_IDCODE pg 1394 (at address 0xe0042000)
 * - RM0434, Rev.10 §41.13.3 CPU1 ROM table PIDR pg 1441
 * References for WB1x parts:
 * - RM0473, Rev.10 §33.4.8 DP_TARGETID pg 1054
 * - RM0473, Rev.10 §33.8.1 DBGMCU_IDCODE pg 1086 (at address 0xe0042000)
 * - RM0473, Rev.10 §33.13.3 CPU1 ROM table PIDR pg 1132
 * - RM0478, Rev.8 §31.4.8 DP_TARGETID pg 980
 * - RM0478, Rev.8 §31.8.1 DBGMCU_IDCODE pg 1011 (at address 0xe0042000)
 * - RM0478, Rev.8 §31.13.3 CPU1 ROM table PIDR pg 1057
 *
 * NB: For WL5x parts, core 2's AP requires using DBGMCU_IDCODE for identification.
 * The outer ROM table for this core carries the ARM core ID, not the part ID.
 * NB: For WB35/WB55 parts, core 2's AP requires using DBGMCU_IDCODE for identification.
 * The outer ROM table for this core carries the ARM core ID, not the part ID.
 * NB: For the WB10CC, core2's AP requires using DBGMCU_IDCODE for identification.
 * The outer ROM table for this core carries the ARM core ID, not the part ID.
 */
#define ID_STM32U535 0x455U /* STM32U535/545 */
#define ID_STM32U5Fx 0x476U /* STM32U5Fx/5Gx */
#define ID_STM32U59x 0x481U /* STM32U59x/5Ax */
#define ID_STM32U575 0x482U /* STM32U575/585 */
#define ID_STM32WLxx 0x497U
#define ID_STM32WB35 0x495U /* STM32WB35/55 */
#define ID_STM32WB1x 0x494U

typedef enum stm32l4_family {
	STM32L4_FAMILY_L4xx,
	STM32L4_FAMILY_L4Rx,
	STM32L4_FAMILY_WBxx,
	STM32L4_FAMILY_G4xx,
	STM32L4_FAMILY_L55x,
	STM32L4_FAMILY_U5xx,
	STM32L4_FAMILY_WLxx,
} stm32l4_family_e;

/*
 * XXX: Recommend augmenting this structure with flash block size,
 * which would get rid of the hard-coded magic numbers in
 * stm32l4_attach, but I have not implemented that yet
 */
typedef struct stm32l4_device_info {
	const char *designator;
	uint16_t sram1; /* Normal SRAM mapped at 0x20000000 */
	uint16_t sram2; /* SRAM at 0x10000000, mapped after sram1 (not L47) */
	uint16_t sram3; /* SRAM mapped after SRAM1 and SRAM2 */
	uint8_t flags;  /* Only DUAL_BANK is evaluated for now. */
	uint16_t device_id;
	stm32l4_family_e family;
	const uint32_t *flash_regs_map;
	const uint32_t flash_size_reg;
} stm32l4_device_info_s;

typedef struct stm32l4_flash {
	target_flash_s flash;
	uint32_t bank1_start;
} stm32l4_flash_s;

typedef struct stm32l4_priv {
	const stm32l4_device_info_s *device;
} stm32l4_priv_s;

typedef struct stm32l4_option_bytes_info {
	const uint8_t *offsets;
	const uint32_t *default_values;
	const uint8_t word_count;
} stm32l4_option_bytes_info_s;

typedef enum stm32l4_flash_reg {
	FLASH_KEYR,
	FLASH_OPTKEYR,
	FLASH_SR,
	FLASH_CR,
	FLASH_OPTR,
	FLASH_REGS_COUNT
} stm32l4_flash_reg_e;

static const uint32_t stm32l4_flash_regs_map[FLASH_REGS_COUNT] = {
	STM32L4_FPEC_BASE + 0x08U, /* KEYR */
	STM32L4_FPEC_BASE + 0x0cU, /* OPTKEYR */
	STM32L4_FPEC_BASE + 0x10U, /* SR */
	STM32L4_FPEC_BASE + 0x14U, /* CR */
	STM32L4_FPEC_BASE + 0x20U, /* OPTR */
};

static const uint32_t stm32l5_flash_regs_map[FLASH_REGS_COUNT] = {
	STM32L5_FPEC_BASE + 0x08U, /* KEYR */
	STM32L5_FPEC_BASE + 0x10U, /* OPTKEYR */
	STM32L5_FPEC_BASE + 0x20U, /* SR */
	STM32L5_FPEC_BASE + 0x28U, /* CR */
	STM32L5_FPEC_BASE + 0x40U, /* OPTR */
};

static const uint32_t stm32wl_flash_regs_map[FLASH_REGS_COUNT] = {
	STM32WL_FPEC_BASE + 0x08U, /* KEYR */
	STM32WL_FPEC_BASE + 0x0cU, /* OPTKEYR */
	STM32WL_FPEC_BASE + 0x10U, /* SR */
	STM32WL_FPEC_BASE + 0x14U, /* CR */
	STM32WL_FPEC_BASE + 0x20U, /* OPTR */
};

static const uint32_t stm32wb_flash_regs_map[FLASH_REGS_COUNT] = {
	STM32WB_FPEC_BASE + 0x08U, /* KEYR */
	STM32WB_FPEC_BASE + 0x0cU, /* OPTKEYR */
	STM32WB_FPEC_BASE + 0x10U, /* SR */
	STM32WB_FPEC_BASE + 0x14U, /* CR */
	STM32WB_FPEC_BASE + 0x20U, /* OPTR */
};

static stm32l4_device_info_s const stm32l4_device_info[] = {
	{
		.device_id = ID_STM32L41,
		.family = STM32L4_FAMILY_L4xx,
		.designator = "STM32L41x",
		.sram1 = 32U,
		.sram2 = 8U,
		.flags = 2U,
		.flash_regs_map = stm32l4_flash_regs_map,
		.flash_size_reg = STM32L4_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32L43,
		.family = STM32L4_FAMILY_L4xx,
		.designator = "STM32L43x",
		.sram1 = 48U,
		.sram2 = 16U,
		.flags = 2U,
		.flash_regs_map = stm32l4_flash_regs_map,
		.flash_size_reg = STM32L4_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32L45,
		.family = STM32L4_FAMILY_L4xx,
		.designator = "STM32L45x",
		.sram1 = 128U,
		.sram2 = 32U,
		.flags = 2U,
		.flash_regs_map = stm32l4_flash_regs_map,
		.flash_size_reg = STM32L4_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32L47,
		.family = STM32L4_FAMILY_L4xx,
		.designator = "STM32L47x",
		.sram1 = 96U,
		.sram2 = 32U,
		.flags = 2U | DUAL_BANK,
		.flash_regs_map = stm32l4_flash_regs_map,
		.flash_size_reg = STM32L4_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32L49,
		.family = STM32L4_FAMILY_L4xx,
		.designator = "STM32L49x",
		.sram1 = 256U,
		.sram2 = 64U,
		.flags = 2U | DUAL_BANK,
		.flash_regs_map = stm32l4_flash_regs_map,
		.flash_size_reg = STM32L4_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32L4R,
		.family = STM32L4_FAMILY_L4Rx,
		.designator = "STM32L4Rx",
		.sram1 = 192U,
		.sram2 = 64U,
		.sram3 = 384U,
		.flags = 3U | DUAL_BANK,
		.flash_regs_map = stm32l4_flash_regs_map,
		.flash_size_reg = STM32L4_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32G43,
		.family = STM32L4_FAMILY_G4xx,
		.designator = "STM32G43",
		.sram1 = 22U,
		.sram2 = 10U,
		.flash_regs_map = stm32l4_flash_regs_map,
		.flash_size_reg = STM32L4_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32G47,
		.family = STM32L4_FAMILY_G4xx,
		.designator = "STM32G47",
		.sram1 = 96U, /* SRAM1 and SRAM2 are mapped continuous */
		.sram2 = 32U, /* CCM SRAM is mapped as per SRAM2 on G4 */
		.flags = 2U,
		.flash_regs_map = stm32l4_flash_regs_map,
		.flash_size_reg = STM32L4_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32G49,
		.family = STM32L4_FAMILY_G4xx,
		.designator = "STM32G49",
		.sram1 = 96U, /* SRAM1 and SRAM2 are mapped continuously */
		.sram2 = 16U, /* CCM SRAM is mapped as per SRAM2 on G4 */
		.flags = 2U,
		.flash_regs_map = stm32l4_flash_regs_map,
		.flash_size_reg = STM32L4_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32L55,
		.family = STM32L4_FAMILY_L55x,
		.designator = "STM32L55",
		.sram1 = 192U, /* SRAM1 and SRAM2 are mapped continuous */
		.sram2 = 64U,
		.flags = 2U,
		.flash_regs_map = stm32l5_flash_regs_map,
		.flash_size_reg = STM32L5_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32U535,
		.family = STM32L4_FAMILY_U5xx,
		.designator = "STM32U535/545",
		.sram1 = 192U + 64U, /* SRAM1+2 continuous */
		.flags = 2U | DUAL_BANK,
		.flash_regs_map = stm32l5_flash_regs_map,
		.flash_size_reg = STM32U5_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32U575,
		.family = STM32L4_FAMILY_U5xx,
		.designator = "STM32U575/585",
		.sram1 = 192U + 64U + 512U, /* SRAM1+2+3 continuous */
		.flags = 2U | DUAL_BANK,
		.flash_regs_map = stm32l5_flash_regs_map,
		.flash_size_reg = STM32U5_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32U59x,
		.family = STM32L4_FAMILY_U5xx,
		.designator = "STM32U59x/5Ax",
		.sram1 = 786U + 64U + 832U + 832U, /* SRAM1+2+3+5 continuous */
		.flags = 2U | DUAL_BANK,
		.flash_regs_map = stm32l5_flash_regs_map,
		.flash_size_reg = STM32U5_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32U5Fx,
		.family = STM32L4_FAMILY_U5xx,
		.designator = "STM32U5Fx/5Gx",
		.sram1 = 786U + 64U + 832U + 832U + 512U, /* SRAM1+2+3+5+6 continuous */
		.flags = 2U | DUAL_BANK,
		.flash_regs_map = stm32l5_flash_regs_map,
		.flash_size_reg = STM32U5_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32WLxx,
		.family = STM32L4_FAMILY_WLxx,
		.designator = "STM32WLxx",
		.sram1 = 32U,
		.sram2 = 32U,
		.flags = 2U,
		.flash_regs_map = stm32wl_flash_regs_map,
		.flash_size_reg = STM32L4_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32WB35,
		.family = STM32L4_FAMILY_WBxx,
		.designator = "STM32WB35/55",
		.sram1 = 192U,
		.sram2 = 64U,
		.flags = 2U,
		.flash_regs_map = stm32wb_flash_regs_map,
		.flash_size_reg = STM32L4_FLASH_SIZE_REG,
	},
	{
		.device_id = ID_STM32WB1x,
		.family = STM32L4_FAMILY_WBxx,
		.designator = "STM32WB1x",
		.sram1 = 12U,
		.sram2 = 36U,
		.flags = 2U,
		.flash_regs_map = stm32wb_flash_regs_map,
		.flash_size_reg = STM32L4_FLASH_SIZE_REG,
	},
	{
		/* Sentinel entry */
		.device_id = 0,
	},
};

static const uint8_t stm32l4_opt_reg_offsets[9] = {0x20, 0x24, 0x28, 0x2c, 0x30, 0x44, 0x48, 0x4c, 0x50};
static const uint8_t stm32g4_opt_reg_offsets[11] = {0x20, 0x24, 0x28, 0x2c, 0x30, 0x70, 0x44, 0x48, 0x4c, 0x50, 0x74};
static const uint8_t stm32wl_opt_reg_offsets[7] = {0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38};
static const uint8_t stm32wb_opt_reg_offsets[10] = {0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38, 0x3c, 0x80, 0x84};

static const uint32_t stm32l4_default_options_values[9] = {
	0xffeff8aaU,
	0xffffffffU,
	0x00000000U,
	0x000000ffU,
	0x000000ffU,
	0xffffffffU,
	0x00000000U,
	0x000000ffU,
	0x000000ffU,
};

/*
 * SEC_SIZE1 occupies 9 bits on G49/G4A (cat 4),
 * 8 bits on cat 3 and 7 bits on cat 2.
 * It is safe to write 0xff00fe00 (cat 4 value) in FLASH_SEC1R
 */
static const uint32_t stm32g4_default_options_values[11] = {
	0xffeff8aaU,
	0xffffffffU,
	0x00ff0000U,
	0xff00ffffU,
	0xff00ffffU,
	0xff00fe00U,
	0xffffffffU,
	0xffffffffU,
	0xff00ffffU,
	0xff00ffffU,
	0xff00ff00U,
};

static const uint32_t stm32wl_default_options_values[7] = {
	0x3feff0aaU,
	0xffffffffU,
	0xffffff00U,
	0xff80ffffU,
	0xff80ffffU,
	0xffffffffU,
	0xffffff00U,
};

static const uint32_t stm32wb_default_options_values[10] = {
	0x2d8f71aaU, // User and read protection option bytes
	0x000001ffU, // PCROP1A start address option bytes
	0x80000000U, // PCROP1A end address option bytes
	0x000000ffU, // WRP Area A address option bytes
	0x000000ffU, // WRP Area B address option bytes
	0x000001ffU, // PCROP1B start address option bytes
	0x00000000U, // PCROP1B end address option bytes
	0x00000000U, // IPCC mailbox data buffer address option bytes
	0x00001000U, // Secure flash memory start address option bytes
	0x00000000U, // Secure SRAM2 start address and CPU2 reset vector option bytes
};

static_assert(ARRAY_LENGTH(stm32l4_opt_reg_offsets) == ARRAY_LENGTH(stm32l4_default_options_values),
	"Number of stm32l4 option registers must match number of default values");
static_assert(ARRAY_LENGTH(stm32g4_opt_reg_offsets) == ARRAY_LENGTH(stm32g4_default_options_values),
	"Number of stm32g4 option registers must match number of default values");
static_assert(ARRAY_LENGTH(stm32wl_opt_reg_offsets) == ARRAY_LENGTH(stm32wl_default_options_values),
	"Number of stm32wl option registers must match number of default values");
static_assert(ARRAY_LENGTH(stm32wb_opt_reg_offsets) == ARRAY_LENGTH(stm32wb_default_options_values),
	"Number of stm32wb option registers must match number of default values");

/* Retrieve device basic information, just add to the vector to extend */
static const stm32l4_device_info_s *stm32l4_get_device_info(const uint16_t device_id)
{
	const stm32l4_device_info_s *device_info = stm32l4_device_info;
	for (; device_info->device_id; ++device_info) {
		if (device_info->device_id == device_id)
			break;
	}
	/* If we haven't found a valid entry this returns the sentinel */
	return device_info;
}

static inline uint32_t stm32l4_read_flash_size(target_s *const target)
{
	stm32l4_priv_s *priv = (stm32l4_priv_s *)target->target_storage;
	const stm32l4_device_info_s *const device = priv->device;
	return target_mem32_read16(target, device->flash_size_reg);
}

static inline uint32_t stm32l4_flash_read32(target_s *const target, const stm32l4_flash_reg_e reg)
{
	stm32l4_priv_s *priv = (stm32l4_priv_s *)target->target_storage;
	const stm32l4_device_info_s *const device = priv->device;
	return target_mem32_read32(target, device->flash_regs_map[reg]);
}

static inline void stm32l4_flash_write32(target_s *const target, const stm32l4_flash_reg_e reg, const uint32_t value)
{
	stm32l4_priv_s *priv = (stm32l4_priv_s *)target->target_storage;
	const stm32l4_device_info_s *const device = priv->device;
	target_mem32_write32(target, device->flash_regs_map[reg], value);
}

static void stm32l4_add_flash(target_s *const target, const uint32_t addr, const size_t length, const size_t blocksize,
	const uint32_t bank1_start)
{
	stm32l4_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *target_flash = &flash->flash;
	target_flash->start = addr;
	target_flash->length = length;
	target_flash->blocksize = blocksize;
	target_flash->erase = stm32l4_flash_erase;
	target_flash->write = stm32l4_flash_write;
	target_flash->writesize = 2048;
	target_flash->erased = 0xffU;
	flash->bank1_start = bank1_start;
	target_add_flash(target, target_flash);
}

/* For flash programming, L5 needs to be in VOS 0 or 1 while reset set 2 (or even 3?) */
static void stm32l5_flash_enable(target_s *const target)
{
	target_mem32_write32(target, STM32L5_RCC_APB1ENR1, STM32L5_RCC_APB1ENR1_PWREN);
	const uint32_t pwr_ctrl1 = target_mem32_read32(target, STM32L5_PWR_CR1) & ~STM32L5_PWR_CR1_VOS;
	target_mem32_write32(target, STM32L5_PWR_CR1, pwr_ctrl1);
}

static uint32_t stm32l4_main_sram_length(const target_s *const target)
{
	const stm32l4_priv_s *const priv = (const stm32l4_priv_s *)target->target_storage;
	const stm32l4_device_info_s *const device = priv->device;
	/* All L4 beside L47 alias SRAM2 after SRAM1.*/
	if (target->part_id == ID_STM32L47)
		return device->sram1 * 1024U;
	return (device->sram1 + device->sram2 + device->sram3) * 1024U;
}

static bool stm32l4_configure_dbgmcu(target_s *const target, const stm32l4_device_info_s *const device)
{
	/* If we're in the probe phase */
	if (target->target_storage == NULL) {
		/* Allocate and save private storage */
		stm32l4_priv_s *const priv_storage = calloc(1, sizeof(*priv_storage));
		if (!priv_storage) { /* calloc failed: heap exhaustion */
			DEBUG_ERROR("calloc: failed in %s\n", __func__);
			return false;
		}
		/* Save the device we're configuring for */
		priv_storage->device = device;
		target->target_storage = priv_storage;

		target->attach = stm32l4_attach;
		target->detach = stm32l4_detach;
	}

	const stm32l4_priv_s *const priv = (stm32l4_priv_s *)target->target_storage;
	/*
	 * Now we have a stable debug environment, make sure the WDTs can't bonk the processor out from under us,
	 * then Reconfigure the config register to prevent WFI/WFE from cutting debug access
	 */
	if (priv->device->family == STM32L4_FAMILY_L55x || priv->device->family == STM32L4_FAMILY_U5xx) {
		target_mem32_write32(target, STM32L5_DBGMCU_APB1FREEZE1,
			target_mem32_read32(target, STM32L5_DBGMCU_APB1FREEZE1) | STM32L4_DBGMCU_APB1FREEZE1_IWDG |
				STM32L4_DBGMCU_APB1FREEZE1_WWDG);
		target_mem32_write32(target, STM32L5_DBGMCU_CONFIG,
			target_mem32_read32(target, STM32L5_DBGMCU_CONFIG) | STM32L4_DBGMCU_CONFIG_DBG_STANDBY |
				STM32L4_DBGMCU_CONFIG_DBG_STOP);
	} else {
		target_mem32_write32(target, STM32L4_DBGMCU_APB1FREEZE1,
			target_mem32_read32(target, STM32L4_DBGMCU_APB1FREEZE1) | STM32L4_DBGMCU_APB1FREEZE1_IWDG |
				STM32L4_DBGMCU_APB1FREEZE1_WWDG);
		target_mem32_write32(target, STM32L4_DBGMCU_CONFIG,
			target_mem32_read32(target, STM32L4_DBGMCU_CONFIG) | STM32L4_DBGMCU_CONFIG_DBG_STANDBY |
				STM32L4_DBGMCU_CONFIG_DBG_STOP | STM32L4_DBGMCU_CONFIG_DBG_SLEEP);
	}
	return true;
}

static void stm32l4_deconfigure_dbgmcu(target_s *const target)
{
	const stm32l4_priv_s *const priv = (stm32l4_priv_s *)target->target_storage;
	/* Reverse all changes to the DBGMCU control and freeze registers */
	if (priv->device->family == STM32L4_FAMILY_L55x || priv->device->family == STM32L4_FAMILY_U5xx) {
		target_mem32_write32(target, STM32L5_DBGMCU_APB1FREEZE1,
			target_mem32_read32(target, STM32L5_DBGMCU_APB1FREEZE1) &
				~(STM32L4_DBGMCU_APB1FREEZE1_IWDG | STM32L4_DBGMCU_APB1FREEZE1_WWDG));
		target_mem32_write32(target, STM32L5_DBGMCU_CONFIG,
			target_mem32_read32(target, STM32L5_DBGMCU_CONFIG) &
				~(STM32L4_DBGMCU_CONFIG_DBG_STANDBY | STM32L4_DBGMCU_CONFIG_DBG_STOP));
	} else {
		target_mem32_write32(target, STM32L4_DBGMCU_APB1FREEZE1,
			target_mem32_read32(target, STM32L4_DBGMCU_APB1FREEZE1) &
				~(STM32L4_DBGMCU_APB1FREEZE1_IWDG | STM32L4_DBGMCU_APB1FREEZE1_WWDG));
		target_mem32_write32(target, STM32L4_DBGMCU_CONFIG,
			target_mem32_read32(target, STM32L4_DBGMCU_CONFIG) &
				~(STM32L4_DBGMCU_CONFIG_DBG_STANDBY | STM32L4_DBGMCU_CONFIG_DBG_STOP |
					STM32L4_DBGMCU_CONFIG_DBG_SLEEP));
	}
}

bool stm32l4_probe(target_s *const target)
{
	adiv5_access_port_s *const ap = cortex_ap(target);
	/*
	 * If the part is SWD-DPv1, nothing special needs to happen.. however, if it is SWD-DPv2, we have to deal
	 * with the mess that is the dual-core parts like the WL5x and WB35/WB55 series. These have no
	 * viable identification available on their second core, and require either using the TARGETID register
	 * or using DBGMCU_IDCODE to form a positive identification on the part. Additionally, the TARGETID
	 * value is bit shifted by a nibble left and only available under SWD, not JTAG. So.. if we can't find the
	 * part by using ap->partno, we try again reading the L4 DBGMCU_IDCODE address.
	 */
	const stm32l4_device_info_s *device = stm32l4_get_device_info(ap->partno);
	if (!device)
		device = stm32l4_get_device_info(target_mem32_read16(target, STM32L4_DBGMCU_IDCODE) & 0xfffU);

	/*
	 * If the call returned the sentinel both times, it's not a supported L4 device.
	 * Now we have a stable debug environment, make sure the WDTs + WFI and WFE instructions can't cause problems
	 */
	if (!device->device_id || !stm32l4_configure_dbgmcu(target, device))
		return false;

	const uint16_t device_id = device->device_id;
	target->part_id = device_id;
	target->driver = device->designator;

	switch (device_id) {
	case ID_STM32WLxx:
	case ID_STM32WB35:
	case ID_STM32WB1x:
		if ((stm32l4_flash_read32(target, FLASH_OPTR)) & FLASH_OPTR_ESE) {
			DEBUG_WARN("STM32W security enabled\n");
			target->driver = device_id == ID_STM32WLxx ? "STM32WLxx (secure)" : "STM32WBxx (secure)";
		}
		if (ap->apsel == 0) {
			/*
			 * Enable CPU2 from CPU1.
			 * CPU2 does not boot after reset w/o C2BOOT set.
			 * RM0453/RM0434, §6.6.4. PWR control register 4 (PWR_CR4)
			 */
			const uint32_t pwr_ctrl4 = target_mem32_read32(target, PWR_CR4);
			target_mem32_write32(target, PWR_CR4, pwr_ctrl4 | PWR_CR4_C2BOOT);
		}
		break;
	case ID_STM32L55:
		if ((stm32l4_flash_read32(target, FLASH_OPTR)) & STM32L5_FLASH_OPTR_TZEN) {
			DEBUG_WARN("STM32L5 Trust Zone enabled\n");
			target->core = "M33+TZ";
		}
		break;
	default:
		break;
	}

	target->mass_erase = stm32l4_mass_erase;
	target_add_commands(target, stm32l4_cmd_list, device->designator);
	return true;
}

static bool stm32l4_attach(target_s *const target)
{
	/*
	 * Try to attach to the part, and then ensure that the WDTs + WFI and WFE
	 * instructions can't cause problems (this is duplicated as it's undone by detach.)
	 */
	if (!cortexm_attach(target) || !stm32l4_configure_dbgmcu(target, NULL))
		return false;

	/* Extract the device structure from the priv storage and enable the Flash if on an L55 part */
	const stm32l4_device_info_s *const device = ((stm32l4_priv_s *)target->priv)->device;
	if (device->family == STM32L4_FAMILY_L55x)
		stm32l5_flash_enable(target);

	/* Free any previously built memory map */
	target_mem_map_free(target);
	/* And rebuild the RAM map */
	if (device->family == STM32L4_FAMILY_L55x || device->family == STM32L4_FAMILY_U5xx)
		target_add_ram32(target, 0x0a000000, (device->sram1 + device->sram2) * 1024U);
	else
		target_add_ram32(target, 0x10000000, device->sram2 * 1024U);
	target_add_ram32(target, 0x20000000, stm32l4_main_sram_length(target));

	const uint16_t flash_len = stm32l4_read_flash_size(target);
	const uint32_t options = stm32l4_flash_read32(target, FLASH_OPTR);

	/* Now we have a base RAM map, rebuild the Flash map */
	if (device->family == STM32L4_FAMILY_WBxx) {
		if (device->device_id == ID_STM32WB1x)
			stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE, flash_len * 1024U, 0x0800, UINT32_MAX);
		else
			stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE, flash_len * 1024U, 0x1000, UINT32_MAX);
	} else if (device->family == STM32L4_FAMILY_L4Rx) {
		/* RM0432 Rev. 2 does not mention 1MiB devices or explain DB1M.*/
		if (options & OR_DBANK) {
			stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE, 0x00100000, 0x1000, 0x08100000);
			stm32l4_add_flash(target, 0x08100000, 0x00100000, 0x1000, 0x08100000);
		} else
			stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE, 0x00200000, 0x2000, UINT32_MAX);
	} else if (device->family == STM32L4_FAMILY_L55x) {
		/* FIXME: Test behaviour on 256kiB devices */
		if (options & OR_DBANK) {
			stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE, 0x00040000, 0x0800, 0x08040000);
			stm32l4_add_flash(target, 0x08040000, 0x00040000, 0x0800, 0x08040000);
		} else
			stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE, 0x00080000, 0x0800, UINT32_MAX);
	} else if (device->family == STM32L4_FAMILY_G4xx) {
		/*
		 * RM0440 describes G43x/G44x as Category 2, G47x/G48x as Category 3 and G49x/G4Ax as Category 4 devices
		 * Cat 2 is always 128kiB with 2kiB pages, single bank
		 * Cat 3 is dual bank with an option bit to choose single 512kiB bank with 4kiB pages or
		 *     dual bank as 2x256kiB with 2kiB pages
		 * Cat 4 is single bank with up to 512kiB of 2kiB pages
		 */
		if (device->device_id == ID_STM32G43) {
			const uint32_t bank_len = flash_len * 1024U;
			stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE, bank_len, 0x0800, UINT32_MAX);
		} else if (device->device_id == ID_STM32G49) {
			/* Announce maximum possible flash length on this device */
			stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE, FLASH_SIZE_MAX_G4_CAT4, 0x0800, UINT32_MAX);
		} else {
			if (options & OR_DBANK) {
				const uint32_t bank_len = flash_len * 512U;
				stm32l4_add_flash(
					target, STM32L4_FLASH_BANK_1_BASE, bank_len, 0x0800, STM32L4_FLASH_BANK_1_BASE + bank_len);
				stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE + bank_len, bank_len, 0x0800,
					STM32L4_FLASH_BANK_1_BASE + bank_len);
			} else {
				const uint32_t bank_len = flash_len * 1024U;
				stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE, bank_len, 0x1000, UINT32_MAX);
			}
		}
	} else if (device->flags & DUAL_BANK) {
		if (options & OR_DUALBANK) {
			const uint32_t bank_len = flash_len * 512U;
			if (device->family == STM32L4_FAMILY_U5xx) {
				stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE, bank_len, STM32U5_FLASH_BLOCK_SIZE,
					STM32L4_FLASH_BANK_1_BASE + bank_len);
				stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE + bank_len, bank_len, STM32U5_FLASH_BLOCK_SIZE,
					STM32L4_FLASH_BANK_1_BASE + bank_len);
			} else {
				stm32l4_add_flash(
					target, STM32L4_FLASH_BANK_1_BASE, bank_len, 0x0800, STM32L4_FLASH_BANK_1_BASE + bank_len);
				stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE + bank_len, bank_len, 0x0800,
					STM32L4_FLASH_BANK_1_BASE + bank_len);
			}
		} else {
			const uint32_t bank_len = flash_len * 1024U;
			stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE, bank_len, 0x0800, UINT32_MAX);
		}
	} else
		stm32l4_add_flash(target, STM32L4_FLASH_BANK_1_BASE, flash_len * 1024U, 0x800, UINT32_MAX);

	/* Clear all errors in the status register. */
	stm32l4_flash_write32(target, FLASH_SR, stm32l4_flash_read32(target, FLASH_SR));
	return true;
}

static void stm32l4_detach(target_s *const target)
{
	/* Reverse all changes to the appropriate STM32Lx_DBGMCU_CONFIG */
	stm32l4_deconfigure_dbgmcu(target);
	cortexm_detach(target);
}

static void stm32l4_flash_unlock(target_s *const target)
{
	if ((stm32l4_flash_read32(target, FLASH_CR)) & FLASH_CR_LOCK) {
		/* Enable FPEC controller access */
		stm32l4_flash_write32(target, FLASH_KEYR, KEY1);
		stm32l4_flash_write32(target, FLASH_KEYR, KEY2);
	}
}

static bool stm32l4_flash_busy_wait(target_s *const target, platform_timeout_s *const print_progess)
{
	/* Read FLASH_SR to poll for BSY bit */
	uint32_t status = FLASH_SR_BSY;
	while (status & FLASH_SR_BSY) {
		status = stm32l4_flash_read32(target, FLASH_SR);
		if ((status & FLASH_SR_ERROR_MASK) || target_check_error(target)) {
			DEBUG_ERROR("stm32l4 Flash error: status 0x%" PRIx32 "\n", status);
			return false;
		}
		if (print_progess)
			target_print_progress(print_progess);
	}
	return true;
}

static bool stm32l4_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t len)
{
	target_s *target = flash->t;
	const stm32l4_flash_s *const sf = (stm32l4_flash_s *)flash;

	/* STM32WBXX ERRATA ES0394 2.2.9: OPTVERR flag is always set after system reset */
	stm32l4_flash_write32(target, FLASH_SR, stm32l4_flash_read32(target, FLASH_SR));

	/* Unlock the Flash and wait for the operation to complete, reporting any errors */
	stm32l4_flash_unlock(target);
	if (!stm32l4_flash_busy_wait(target, NULL))
		return false;

	/* Erase the requested chunk of flash, one page at a time. */
	for (size_t offset = 0; offset < len; offset += flash->blocksize) {
		const uint32_t page = (addr + offset - STM32L4_FLASH_BANK_1_BASE) / flash->blocksize;
		const uint32_t bank_flags = addr + offset >= sf->bank1_start ? FLASH_CR_BKER : 0;
		const uint32_t ctrl = FLASH_CR_PER | (page << FLASH_CR_PAGE_SHIFT) | bank_flags;
		/* Flash page erase instruction */
		stm32l4_flash_write32(target, FLASH_CR, ctrl);
		/* write address to FMA */
		stm32l4_flash_write32(target, FLASH_CR, ctrl | FLASH_CR_STRT);

		/* Wait for completion or an error */
		if (!stm32l4_flash_busy_wait(target, NULL))
			return false;
	}
	return true;
}

static bool stm32l4_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	target_s *target = flash->t;
	stm32l4_flash_write32(target, FLASH_CR, FLASH_CR_PG);
	target_mem32_write(target, dest, src, len);

	/* Wait for completion or an error */
	return stm32l4_flash_busy_wait(target, NULL);
}

static bool stm32l4_cmd_erase(target_s *const target, const uint32_t action, platform_timeout_s *const print_progess)
{
	stm32l4_flash_unlock(target);
	/* Erase time is 25 ms. Timeout logic shouldn't get fired.*/
	/* Flash erase action start instruction */
	stm32l4_flash_write32(target, FLASH_CR, action);
	stm32l4_flash_write32(target, FLASH_CR, action | FLASH_CR_STRT);

	/* Wait for completion or an error */
	return stm32l4_flash_busy_wait(target, print_progess);
}

static bool stm32l4_mass_erase(target_s *const target, platform_timeout_s *const print_progess)
{
	return stm32l4_cmd_erase(target, FLASH_CR_MER1 | FLASH_CR_MER2, print_progess);
}

static bool stm32l4_cmd_erase_bank1(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;
	tc_printf(target, "Erasing bank %u: ", 1U);
	const bool result = stm32l4_cmd_erase(target, FLASH_CR_MER1, NULL);
	tc_printf(target, "done\n");
	return result;
}

static bool stm32l4_cmd_erase_bank2(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;
	tc_printf(target, "Erasing bank %u: ", 2U);
	const bool result = stm32l4_cmd_erase(target, FLASH_CR_MER2, NULL);
	tc_printf(target, "done\n");
	return result;
}

static bool stm32l4_option_write(target_s *const target, const uint32_t *const values, const size_t len,
	const uint32_t fpec_base, const uint8_t *const opt_reg_offsets)
{
	/* Unlock the option registers Flash */
	stm32l4_flash_unlock(target);
	stm32l4_flash_write32(target, FLASH_OPTKEYR, OPTKEY1);
	stm32l4_flash_write32(target, FLASH_OPTKEYR, OPTKEY2);
	/* Wait for the operation to complete and report any errors */
	if (!stm32l4_flash_busy_wait(target, NULL))
		return true;

	/* Write the new option register values and begin the programming operation */
	for (size_t i = 0; i < len; i++)
		target_mem32_write32(target, fpec_base + opt_reg_offsets[i], values[i]);
	stm32l4_flash_write32(target, FLASH_CR, FLASH_CR_OPTSTRT);
	/* Wait for the operation to complete and report any errors */
	if (!stm32l4_flash_busy_wait(target, NULL))
		return false;

	tc_printf(target, "Scan and attach again\n");
	/* Ask the device to reload its options bytes */
	stm32l4_flash_write32(target, FLASH_CR, FLASH_CR_OBL_LAUNCH);
	while (stm32l4_flash_read32(target, FLASH_CR) & FLASH_CR_OBL_LAUNCH) {
		if (target_check_error(target))
			return true;
	}
	/* Re-lock Flash */
	stm32l4_flash_write32(target, FLASH_CR, FLASH_CR_LOCK);
	return false;
}

static uint32_t stm32l4_fpec_base_addr(const target_s *const target)
{
	if (target->part_id == ID_STM32WLxx)
		return STM32WL_FPEC_BASE;
	if (target->part_id == ID_STM32WB35)
		return STM32WB_FPEC_BASE;
	return STM32L4_FPEC_BASE;
}

static stm32l4_option_bytes_info_s stm32l4_get_opt_bytes_info(const uint16_t part_id)
{
	switch (part_id) {
	case ID_STM32L43:
		return (stm32l4_option_bytes_info_s){
			.word_count = 5,
			.offsets = stm32l4_opt_reg_offsets,
			.default_values = stm32l4_default_options_values,
		};
	case ID_STM32G47:
		return (stm32l4_option_bytes_info_s){
			.word_count = ARRAY_LENGTH(stm32g4_default_options_values),
			.offsets = stm32g4_opt_reg_offsets,
			.default_values = stm32g4_default_options_values,
		};
	case ID_STM32G43:
	case ID_STM32G49:
		return (stm32l4_option_bytes_info_s){
			.word_count = 6,
			.offsets = stm32g4_opt_reg_offsets,
			.default_values = stm32g4_default_options_values,
		};
	case ID_STM32WLxx:
		return (stm32l4_option_bytes_info_s){
			.word_count = ARRAY_LENGTH(stm32wl_default_options_values),
			.offsets = stm32wl_opt_reg_offsets,
			.default_values = stm32wl_default_options_values,
		};
	case ID_STM32WB35:
		return (stm32l4_option_bytes_info_s){
			.word_count = ARRAY_LENGTH(stm32wb_default_options_values),
			.offsets = stm32wb_opt_reg_offsets,
			.default_values = stm32wb_default_options_values,
		};
	default:
		return (stm32l4_option_bytes_info_s){
			.word_count = ARRAY_LENGTH(stm32l4_default_options_values),
			.offsets = stm32l4_opt_reg_offsets,
			.default_values = stm32l4_default_options_values,
		};
	}
}

/*
 * Chip:      L43X/mask  L43x/def   L47x/mask  L47x/def   G47x/mask  G47x/def
 *                                  L49x/mask  L49x/def   G48x/mask  G48x/def
 * Address
 * 0x1fff7800 0x0f8f77ff 0xffeff8aa 0x0fdf77ff 0xffeff8aa 0x0fdf77ff 0xffeff8aa
 * 0x1fff7808 0x0000ffff 0xffffffff 0x0000ffff 0xffffffff 0x00007fff 0xffffffff
 * 0x1fff7810 0x8000ffff 0          0x8000ffff 0          0x80007fff 0x00ff0000
 * 0x1fff7818 0x00ff00ff 0x000000ff 0x00ff00ff 0x000000ff 0x007f007f 0xff00ffff
 * 0x1fff7820 0x00ff00ff 0x000000ff 0x00ff00ff 0x000000ff 0x007f007f 0xff00ffff
 * 0x1fff7828 0          0          0          0          0x000100ff 0xff00ff00
 * 0x1ffff808 0          0          0x8000ffff 0xffffffff 0x00007fff 0xffffffff
 * 0x1ffff810 0          0          0x8000ffff 0          0x00007fff 0xffffffff
 * 0x1ffff818 0          0          0x00ff00ff 0          0x00ff00ff 0xff00ffff
 * 0x1ffff820 0          0          0x00ff00ff 0x000000ff 0x00ff00ff 0xff00ffff
 * 0x1ffff828 0          0          0          0          0x000000ff 0xff00ff00
 */

static bool stm32l4_cmd_option(target_s *target, int argc, const char **argv)
{
	if (target->part_id == ID_STM32L55) {
		tc_printf(target, "%s options not implemented!\n", "STM32L5");
		return false;
	}
	if (target->part_id == ID_STM32WB1x) {
		tc_printf(target, "%s options not implemented!\n", "STM32WB1x");
		return false;
	}
	if (target->part_id == ID_STM32WLxx) {
		tc_printf(target, "%s options not implemented!\n", "STM32WLxx");
		return false;
	}

	const stm32l4_option_bytes_info_s info = stm32l4_get_opt_bytes_info(target->part_id);
	const uint32_t fpec_base = stm32l4_fpec_base_addr(target);
	const uint8_t *const opt_reg_offsets = info.offsets;

	const size_t word_count = info.word_count;
	uint32_t values[11] = {0};
	for (size_t i = 0; i < word_count; ++i)
		values[i] = info.default_values[i];

	bool result = false;
	if (argc == 2 && strcmp(argv[1], "erase") == 0)
		result = stm32l4_option_write(target, values, word_count, fpec_base, opt_reg_offsets);
	else if (argc > 2 && strcmp(argv[1], "write") == 0) {
		const size_t option_words = MIN((size_t)argc - 2U, word_count);
		for (size_t i = 0; i < option_words; ++i)
			values[i] = strtoul(argv[i + 2U], NULL, 0);

		for (size_t i = option_words; i < word_count; ++i)
			values[i] = target_mem32_read32(target, fpec_base + opt_reg_offsets[i]);

		if ((values[0] & 0xffU) == 0xccU) {
			++values[0];
			tc_printf(target, "Changing level 2 protection request to level 1!");
		}
		result = stm32l4_option_write(target, values, word_count, fpec_base, opt_reg_offsets);
	} else {
		tc_printf(target, "usage: monitor option erase\n");
		tc_printf(target, "usage: monitor option write <value> ...\n");
	}

	if (result) {
		tc_printf(target, "Writing options failed!\n");
		return false;
	}

	for (size_t i = 0; i < word_count; ++i) {
		const uint32_t addr = fpec_base + opt_reg_offsets[i];
		const uint32_t val = target_mem32_read32(target, fpec_base + opt_reg_offsets[i]);
		tc_printf(target, "0x%08" PRIX32 ": 0x%08" PRIX32 "\n", addr, val);
	}
	return true;
}

/* Read and decode Unique Device ID register of L4 and G4 */
static bool stm32l4_cmd_uid(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	return stm32_uid(target, STM32L4_UID_BASE);
}
