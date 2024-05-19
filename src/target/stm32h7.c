/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2017-2020 Uwe Bonnes bon@elektron.ikp.physik.tu-darmstadt.de
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
 * This file implements STM32H7 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * References:
 * RM0399 - STM32H745/755 and STM32H747/757 advanced Arm®-based 32-bit MCUs, Rev. 4
 *   https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * RM0433 - STM32H742, STM32H743/753 and STM32H750 Value line advanced Arm®-based 32-bit MCUs, Rev. 8
 *   https://www.st.com/resource/en/reference_manual/dm00314099-stm32h742-stm32h743-753-and-stm32h750-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf
 * RM0455 - STM32H7A3/7B3 and STM32H7B0 Value line advanced Arm®-based 32-bit MCUs, Rev. 10
 *   https://www.st.com/resource/en/reference_manual/rm0455-stm32h7a37b3-and-stm32h7b0-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 * RM0468 - STM32H723/733, STM32H725/735 and STM32H730 Value line advanced Arm®-based 32-bit MCUs, Rev. 3
 *   https://www.st.com/resource/en/reference_manual/rm0468-stm32h723733-stm32h725735-and-stm32h730-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
 */

/*
 * While the ST document (RM 0433) claims that the STM32H750 only has 1 bank
 * with 1 sector (128k) of user main memory flash (pages 151-152), we were able
 * to write and successfully verify into other regions in bank 1 and also into
 * bank 2 (0x0810 0000 as indicated for the other chips).
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "stm32_common.h"
#include "buffer_utils.h"

#define STM32H7_FLASH_ACR        0x00U
#define STM32H7_FLASH_KEYR       0x04U
#define STM32H7_FLASH_OPTKEYR    0x08U
#define STM32H7_FLASH_CTRL       0x0cU
#define STM32H7_FLASH_STATUS     0x10U
#define STM32H7_FLASH_CLEAR_CTRL 0x14U
#define STM32H7_FLASH_OPTCR      0x18U
#define STM32H7_FLASH_OPTSR_CUR  0x1cU
#define STM32H7_FLASH_OPTSR      0x20U
#define STM32H7_FLASH_CRCCR      0x50U
#define STM32H7_FLASH_CRCDATA    0x5cU

/* Flash Program and Erase Controller Register Map */
#define STM32H7_FPEC1_BASE              0x52002000U
#define STM32H7_FPEC2_BASE              0x52002100U
#define STM32H7_FLASH_STATUS_BUSY       (1U << 0U)
#define STM32H7_FLASH_STATUS_WBNE       (1U << 1U)
#define STM32H7_FLASH_STATUS_QUEUE_WAIT (1U << 2U)
#define STM32H7_FLASH_STATUS_CRC_BUSY   (1U << 3U)
#define STM32H7_FLASH_STATUS_EOP        (1U << 16U)
#define STM32H7_FLASH_STATUS_WRPERR     (1U << 17U)
#define STM32H7_FLASH_STATUS_PGSERR     (1U << 18U)
#define STM32H7_FLASH_STATUS_STRBERR    (1U << 19U)
#define STM32H7_FLASH_STATUS_INCERR     (1U << 21U)
#define STM32H7_FLASH_STATUS_OPERR      (1U << 22U)
#define STM32H7_FLASH_STATUS_OPERR      (1U << 22U)
#define STM32H7_FLASH_STATUS_RDPERR     (1U << 23U)
#define STM32H7_FLASH_STATUS_RDSERR     (1U << 24U)
#define STM32H7_FLASH_STATUS_SNECCERR   (1U << 25U)
#define STM32H7_FLASH_STATUS_DBERRERR   (1U << 26U)
#define STM32H7_FLASH_STATUS_ERROR_READ                                                          \
	(STM32H7_FLASH_STATUS_RDPERR | STM32H7_FLASH_STATUS_RDSERR | STM32H7_FLASH_STATUS_SNECCERR | \
		STM32H7_FLASH_STATUS_DBERRERR)
#define STM32H7_FLASH_STATUS_ERROR_MASK                                                         \
	(STM32H7_FLASH_STATUS_WRPERR | STM32H7_FLASH_STATUS_PGSERR | STM32H7_FLASH_STATUS_STRBERR | \
		STM32H7_FLASH_STATUS_INCERR | STM32H7_FLASH_STATUS_OPERR | STM32H7_FLASH_STATUS_ERROR_READ)
#define STM32H7_FLASH_CTRL_LOCK               (1U << 0U)
#define STM32H7_FLASH_CTRL_PROGRAM            (1U << 1U)
#define STM32H7_FLASH_CTRL_SECTOR_ERASE       (1U << 2U)
#define STM32H7_FLASH_CTRL_BANK_ERASE         (1U << 3U)
#define STM32H7_FLASH_CTRL_PSIZE8             (0U << 4U)
#define STM32H7_FLASH_CTRL_PSIZE16            (1U << 4U)
#define STM32H7_FLASH_CTRL_PSIZE32            (2U << 4U)
#define STM32H7_FLASH_CTRL_PSIZE64            (3U << 4U)
#define STM32H7_FLASH_CTRL_PSIZE_SHIFT        4U
#define STM32H7_FLASH_CTRL_FORCE_WRITE        (1U << 6U)
#define STM32H7_FLASH_CTRL_START              (1U << 7U)
#define STM32H7_FLASH_CTRL_SECTOR_NUM_SHIFT   8U
#define STM32H7BX_FLASH_CTRL_SECTOR_NUM_SHIFT 6U
#define STM32H7_FLASH_CTRL_SECTOR_NUM_MASK    (3U << 8U)
#define STM32H7_FLASH_CTRL_CRC_EN             (1U << 15U)

#define STM32H7_FLASH_OPTCR_OPTLOCK (1U << 0U)
#define STM32H7_FLASH_OPTCR_OPTSTRT (1U << 1U)

#define STM32H7_FLASH_OPTSR_IWDG1_SW (1U << 4U)

#define STM32H7_FLASH_CRCCR_ALL_BANK    (1U << 7U)
#define STM32H7_FLASH_CRCCR_START_CRC   (1U << 16U)
#define STM32H7_FLASH_CRCCR_CLEAN_CRC   (1U << 17U)
#define STM32H7_FLASH_CRCCR_CRC_BURST_3 (3U << 20U)

#define STM32H7_FLASH_KEY1 0x45670123U
#define STM32H7_FLASH_KEY2 0xcdef89abU

#define STM32H7_OPT_KEY1 0x08192a3bU
#define STM32H7_OPT_KEY2 0x4c5d6e7fU

#define STM32H7_FLASH_SIZE   0x1ff1e880U
#define STM32H7Bx_FLASH_SIZE 0x08fff80cU
#define STM32H7_CHIP_IDENT   0x1ff1e8c0U

/* WWDG base address and register map */
#define STM32H7_WWDG_BASE     0x50003000U
#define STM32H7_WWDG_CR       (STM32H7_WWDG_BASE + 0x00)
#define STM32H7_WWDG_CR_RESET 0x0000007fU

/* IWDG base address and register map */
#define STM32H7_IWDG_BASE      0x58004800U
#define STM32H7_IWDG_KEY       (STM32H7_IWDG_BASE + 0x00U)
#define STM32H7_IWDG_KEY_RESET 0x0000aaaaU

/*
 * Base address for the DBGMCU peripehral for access from the processor
 * address space. For access via AP2, use base address 0xe00e1000.
 */
#define STM32H7_DBGMCU_BASE       0x5c001000U
#define STM32H7_DBGMCU_IDCODE     (STM32H7_DBGMCU_BASE + 0x000U)
#define STM32H7_DBGMCU_CONFIG     (STM32H7_DBGMCU_BASE + 0x004U)
#define STM32H7_DBGMCU_APB3FREEZE (STM32H7_DBGMCU_BASE + 0x034U)
#define STM32H7_DBGMCU_APB4FREEZE (STM32H7_DBGMCU_BASE + 0x054U)

#define STM32H7_DBGMCU_CONFIG_DBGSLEEP_D1 (1U << 0U)
#define STM32H7_DBGMCU_CONFIG_DBGSTOP_D1  (1U << 1U)
#define STM32H7_DBGMCU_CONFIG_DBGSTBY_D1  (1U << 2U)
#define STM32H7_DBGMCU_CONFIG_DBGSTOP_D3  (1U << 7U)
#define STM32H7_DBGMCU_CONFIG_DBGSTBY_D3  (1U << 8U)
#define STM32H7_DBGMCU_CONFIG_D1DBGCKEN   (1U << 21U)
#define STM32H7_DBGMCU_CONFIG_D3DBGCKEN   (1U << 22U)
#define STM32H7_DBGMCU_APB3FREEZE_WWDG1   (1U << 6U)
#define STM32H7_DBGMCU_APB4FREEZE_IWDG1   (1U << 18U)

#define STM32H7_DBGMCU_IDCODE_DEV_MASK  0x00000fffU
#define STM32H7_DBGMCU_IDCODE_REV_SHIFT 16U

/*
 * Flash capacity in STM32 chips is indicated by this number/letter:
 * STM32H7B0VBT6 (STM32H74xxI, STM32H72xxE)
 *           ^
 * where known sizes for STM32H7 families are
 * 8: 64 KiB (H7Rx, H7Sx: DS14359-DS14360)
 * B: 128 KiB (Value line: DS12556, DS13315, DS13196)
 * E: 512 KiB
 * G: 1024 KiB
 * I: 2048 KiB
 * Refer to the (as of 2024) 17 datasheets, Ordering information.
 * DS12110/DS12117, DS12919/DS12923, DS12930-DS12931, DS13139/DS13195, DS13311-DS13314.
 */
#define STM32H7_FLASH_BANK1_BASE    0x08000000U
#define STM32H7_FLASH_BANK2_BASE    0x08100000U
#define STM32H7_FLASH_BANK_SIZE     0x00100000U
#define STM32H74xxG_FLASH_BANK_SIZE 0x00080000U
#define STM32H74xxG_FLASH_SIZE      0x00100000U
#define NUM_SECTOR_PER_BANK         8U
#define FLASH_SECTOR_SIZE           0x20000U

#define ID_STM32H74x 0x450U /* RM0433, RM0399 */
#define ID_STM32H7Bx 0x480U /* RM0455 */
#define ID_STM32H72x 0x483U /* RM0468 */

#define STM32H7_NAME_MAX_LENGTH 10U

typedef struct stm32h7_flash {
	target_flash_s target_flash;
	align_e psize;
	uint32_t regbase;
} stm32h7_flash_s;

typedef struct stm32h7_priv {
	uint32_t dbgmcu_config;
	char name[STM32H7_NAME_MAX_LENGTH];
} stm32h7_priv_s;

/* static bool stm32h7_cmd_option(target_s *t, int argc, const char **argv); */
static bool stm32h7_uid(target_s *target, int argc, const char **argv);
static bool stm32h7_crc(target_s *target, int argc, const char **argv);
static bool stm32h7_cmd_psize(target_s *target, int argc, const char **argv);
static bool stm32h7_cmd_rev(target_s *target, int argc, const char **argv);

const command_s stm32h7_cmd_list[] = {
	/*{"option", stm32h7_cmd_option, "Manipulate option bytes"},*/
	{"psize", stm32h7_cmd_psize, "Configure flash write parallelism: (x8|x16|x32|x64(default))"},
	{"uid", stm32h7_uid, "Print unique device ID"},
	{"crc", stm32h7_crc, "Print CRC of both banks"},
	{"revision", stm32h7_cmd_rev, "Returns the Device ID and Revision"},
	{NULL, NULL, NULL},
};

static bool stm32h7_attach(target_s *target);
static void stm32h7_detach(target_s *target);
static bool stm32h7_flash_erase(target_flash_s *target_flash, target_addr_t addr, size_t len);
static bool stm32h7_flash_write(target_flash_s *target_flash, target_addr_t dest, const void *src, size_t len);
static bool stm32h7_flash_prepare(target_flash_s *target_flash);
static bool stm32h7_flash_done(target_flash_s *target_flash);
static bool stm32h7_mass_erase(target_s *target);

static uint32_t stm32h7_flash_bank_base(const uint32_t addr)
{
	if (addr >= STM32H7_FLASH_BANK2_BASE)
		return STM32H7_FPEC2_BASE;
	return STM32H7_FPEC1_BASE;
}

static void stm32h7_add_flash(target_s *target, uint32_t addr, size_t length, size_t blocksize)
{
	stm32h7_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *target_flash = &flash->target_flash;
	target_flash->start = addr;
	target_flash->length = length;
	target_flash->blocksize = blocksize;
	target_flash->erase = stm32h7_flash_erase;
	target_flash->write = stm32h7_flash_write;
	target_flash->prepare = stm32h7_flash_prepare;
	target_flash->done = stm32h7_flash_done;
	target_flash->writesize = 2048;
	target_flash->erased = 0xffU;
	flash->regbase = stm32h7_flash_bank_base(addr);
	flash->psize = ALIGN_64BIT;
	target_add_flash(target, target_flash);
}

static void stm32h7_configure_wdts(target_s *const target)
{
	/*
	 * Feed the watchdogs to ensure things are stable - though note that the DBGMCU writes
	 * in the probe routine to the APB freeze registers mean they are halted with the CPU core.
	 */
	target_mem32_write32(target, STM32H7_WWDG_CR, STM32H7_WWDG_CR_RESET);
	target_mem32_write32(target, STM32H7_IWDG_KEY, STM32H7_IWDG_KEY_RESET);
}

bool stm32h7_probe(target_s *target)
{
	const adiv5_access_port_s *const ap = cortex_ap(target);
	/* Use the partno from the AP always to handle the difference between JTAG and SWD */
	if (ap->partno != ID_STM32H72x && ap->partno != ID_STM32H74x && ap->partno != ID_STM32H7Bx)
		return false;

	/* By now it's established that this is likely an H7, but check that it's not an MP15x_CM4 with an errata in AP part code */
	const uint32_t idcode = target_mem32_read32(target, STM32H7_DBGMCU_IDCODE);
	const uint16_t dev_id = idcode & STM32H7_DBGMCU_IDCODE_DEV_MASK;
	DEBUG_TARGET(
		"%s: looking at device ID 0x%03x at 0x%08" PRIx32 "\n", __func__, dev_id, (uint32_t)STM32H7_DBGMCU_IDCODE);
	/* MP15x_CM4 errata: has a partno of 0x450. SoC DBGMCU says 0x500. */
	if (dev_id != ID_STM32H72x && dev_id != ID_STM32H74x && dev_id != ID_STM32H7Bx)
		return false;

	target->part_id = ap->partno;

	/* Save private storage */
	stm32h7_priv_s *priv_storage = calloc(1, sizeof(*priv_storage));
	if (!priv_storage) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	priv_storage->dbgmcu_config = target_mem32_read32(target, STM32H7_DBGMCU_CONFIG);
	target->target_storage = priv_storage;

	memcpy(priv_storage->name, "STM32", 5U);
	switch (target->part_id) {
	case ID_STM32H72x:
		write_be4((uint8_t *)priv_storage->name, 5U, target_mem32_read32(target, STM32H7_CHIP_IDENT));
		priv_storage->name[9] = '\0';
		break;
	case ID_STM32H74x:
		memcpy(priv_storage->name + 5U, "H74x", 5U); /* H742/H743/H753/H750 */
		break;
	case ID_STM32H7Bx:
		memcpy(priv_storage->name + 5U, "H7Bx", 5U); /* H7A3/H7B3/H7B0 */
		break;
	default:
		memcpy(priv_storage->name + 5U, "H7", 3U);
		break;
	}

	target->driver = priv_storage->name;
	target->attach = stm32h7_attach;
	target->detach = stm32h7_detach;
	target->mass_erase = stm32h7_mass_erase;
	target_add_commands(target, stm32h7_cmd_list, target->driver);

	/* Now we have a stable debug environment, make sure the WDTs can't bonk the processor out from under us */
	target_mem32_write32(target, STM32H7_DBGMCU_APB3FREEZE, STM32H7_DBGMCU_APB3FREEZE_WWDG1);
	target_mem32_write32(target, STM32H7_DBGMCU_APB4FREEZE, STM32H7_DBGMCU_APB4FREEZE_IWDG1);
	/*
	 * Make sure that both domain D1 and D3 debugging are enabled and that we can keep
	 * debugging through sleep, stop and standby states for domain D1
	 */
	target_mem32_write32(target, STM32H7_DBGMCU_CONFIG,
		priv_storage->dbgmcu_config | STM32H7_DBGMCU_CONFIG_DBGSLEEP_D1 | STM32H7_DBGMCU_CONFIG_DBGSTOP_D1 |
			STM32H7_DBGMCU_CONFIG_DBGSTBY_D1 | STM32H7_DBGMCU_CONFIG_D1DBGCKEN | STM32H7_DBGMCU_CONFIG_D3DBGCKEN);
	stm32h7_configure_wdts(target);

	/* Build the RAM map */
	target_add_ram32(target, 0x00000000, 0x10000); /* ITCM RAM,   64 KiB */
	target_add_ram32(target, 0x20000000, 0x20000); /* DTCM RAM,  128 KiB */
	switch (target->part_id) {
	case ID_STM32H72x: {
		/* Table 6. Memory map and default device memory area attributes RM0468, pg133 */
		target_add_ram32(target, 0x24000000, 0x20000); /* AXI RAM,    128 KiB */
		target_add_ram32(target, 0x24020000, 0x30000); /* AXI RAM,    192 KiB (TCM_AXI_SHARED) */
		target_add_ram32(target, 0x30000000, 0x8000);  /* AHB SRAM1+2, 32 KiB [16+16] contiguous */
		target_add_ram32(target, 0x38000000, 0x4000);  /* AHB SRAM4,   16 KiB, D3 domain */
		break;
	}
	case ID_STM32H74x: {
		/* Table 7. Memory map and default device memory area attributes RM0433, pg130 */
		target_add_ram32(target, 0x24000000, 0x80000); /* AXI RAM,       512 KiB */
		target_add_ram32(target, 0x30000000, 0x48000); /* AHB SRAM1+2+3, 288 KiB [128+128+32] contiguous */
		target_add_ram32(target, 0x38000000, 0x10000); /* AHB SRAM4,      64 KiB, D3 domain */
		break;
	}
	case ID_STM32H7Bx: {
		/* Table 6. Memory map and default device memory area attributes RM0455, pg131 */
		target_add_ram32(target, 0x24000000, 0x100000); /* AXI RAM1+2+3, 1024 KiB [256+384+384] contiguous, */
		target_add_ram32(target, 0x30000000, 0x10000);  /* AHB SRAM1+2,   128 KiB [64+64] contiguous, */
		target_add_ram32(target, 0x38000000, 0x8000);   /* SRD SRAM4,      32 KiB, Smart run domain */
		break;
	}
	default:
		break;
	}

	/*
	 * Note on SRD from AN5293, 3. System architecture differences between STM32F7 and STM32H7 Series
	 * > The D3 domain evolved into a domain called SRD domain (or smart-run domain).
	 */

	/* Build the Flash map */
	switch (target->part_id) {
	case ID_STM32H74x: {
		/* Read the Flash size from the device (expressed in kiB) and multiply it by 1024 */
		const uint32_t flash_size = target_mem32_read32(target, STM32H7_FLASH_SIZE) << 10U;
		/* STM32H750nB: 128 KiB, single sector of first bank */
		if (flash_size == FLASH_SECTOR_SIZE)
			stm32h7_add_flash(target, STM32H7_FLASH_BANK1_BASE, flash_size, FLASH_SECTOR_SIZE);
		/* STM32H742xG/H743xG: two banks, each 512 KiB in only 4 sectors of 128 KiB, (and a hole in 0x08080000-0x080fffff), no crypto */
		else if (flash_size == STM32H74xxG_FLASH_SIZE) {
			stm32h7_add_flash(target, STM32H7_FLASH_BANK1_BASE, STM32H74xxG_FLASH_BANK_SIZE, FLASH_SECTOR_SIZE);
			stm32h7_add_flash(target, STM32H7_FLASH_BANK2_BASE, STM32H74xxG_FLASH_BANK_SIZE, FLASH_SECTOR_SIZE);
		}
		/* STM32H742xI/H743xI/H753xI: two banks, each 1024 KiB in 8 sectors of 128 KiB */
		else {
			stm32h7_add_flash(target, STM32H7_FLASH_BANK1_BASE, STM32H7_FLASH_BANK_SIZE, FLASH_SECTOR_SIZE);
			stm32h7_add_flash(target, STM32H7_FLASH_BANK2_BASE, STM32H7_FLASH_BANK_SIZE, FLASH_SECTOR_SIZE);
		}
		break;
	}
	case ID_STM32H7Bx: {
		/* Read the Flash size from the device (expressed in KiB) and multiply it by 1024 */
		const uint32_t flash_size = target_mem32_read16(target, STM32H7Bx_FLASH_SIZE) << 10U;
		/* STM32H7B0nB: 128 KiB in 16 sectors of 8 KiB */
		if (flash_size == 0x20000U)
			stm32h7_add_flash(target, STM32H7_FLASH_BANK1_BASE, flash_size, 0x2000U);
		/* STM32H7A3xG: 1024 KiB in 128 sectors of 8 KiB, single bank, no crypto */
		else if (flash_size == 1048576U)
			stm32h7_add_flash(target, STM32H7_FLASH_BANK1_BASE, 1048576U, 0x2000U);
		/* STM32H7A3xI/H7B3xI: two banks, each 1024 KiB in 128 sectors of 8 KiB */
		else if (flash_size == 2097152U) {
			stm32h7_add_flash(target, STM32H7_FLASH_BANK1_BASE, 1048576U, 0x2000U);
			stm32h7_add_flash(target, STM32H7_FLASH_BANK2_BASE, 1048576U, 0x2000U);
		}
		break;
	}
	case ID_STM32H72x: {
		/* Read the Flash size from the device (expressed in kiB) and multiply it by 1024 */
		const uint32_t flash_size = target_mem32_read32(target, STM32H7_FLASH_SIZE) << 10U;
		/*
		 * None of the H72x and H73x parts have more than one Flash bank, making this simple.
		 * NB: STM32H73xB has just one Flash sector though this should be automatically taken care of here.
		 * STM32H723xE/H725xE: 512 KiB in 4 sectors of 128 KiB, single bank, no crypto
		 * STM32H72xxG (H723xG/H733xG, H725xG/H735xG): 1024 KiB in 8 sectors of 128 KiB, single bank
		 */
		stm32h7_add_flash(target, STM32H7_FLASH_BANK1_BASE, flash_size, FLASH_SECTOR_SIZE);
		break;
	}
	default:
		break;
	}
	return true;
}

static bool stm32h7_attach(target_s *target)
{
	if (!cortexm_attach(target))
		return false;
	/*
	 * Make sure that both domain D1 and D3 debugging are enabled and that we can keep
	 * debugging through sleep, stop and standby states for domain D1 - this is duplicated as it's undone by detach.
	 */
	target_mem32_write32(target, STM32H7_DBGMCU_CONFIG,
		STM32H7_DBGMCU_CONFIG_DBGSLEEP_D1 | STM32H7_DBGMCU_CONFIG_DBGSTOP_D1 | STM32H7_DBGMCU_CONFIG_DBGSTBY_D1 |
			STM32H7_DBGMCU_CONFIG_D1DBGCKEN | STM32H7_DBGMCU_CONFIG_D3DBGCKEN);
	stm32h7_configure_wdts(target);
	return true;
}

static void stm32h7_detach(target_s *target)
{
	stm32h7_priv_s *priv = (stm32h7_priv_s *)target->target_storage;
	target_mem32_write32(target, STM32H7_DBGMCU_CONFIG, priv->dbgmcu_config);
	cortexm_detach(target);
}

static bool stm32h7_flash_wait_complete(target_s *const target, const uint32_t regbase)
{
	uint32_t status = STM32H7_FLASH_STATUS_QUEUE_WAIT;
	/* Loop waiting for thewait queue bits to clear and EOP to set, indicating completion of all ongoing operations */
	while (!(status & STM32H7_FLASH_STATUS_EOP) && (status & STM32H7_FLASH_STATUS_QUEUE_WAIT)) {
		status = target_mem32_read32(target, regbase + STM32H7_FLASH_STATUS);
		/* If an error occurs, make noises */
		if (target_check_error(target)) {
			DEBUG_ERROR("%s: error reading status\n", __func__);
			return false;
		}
	}
	/* Now the operation's complete, we can check the error bits */
	if (status & STM32H7_FLASH_STATUS_ERROR_MASK)
		DEBUG_ERROR("%s: Flash error: %08" PRIx32 "\n", __func__, status);
	target_mem32_write32(target, regbase + STM32H7_FLASH_CLEAR_CTRL,
		status & (STM32H7_FLASH_STATUS_ERROR_MASK | STM32H7_FLASH_STATUS_EOP));
	/* Return whether any errors occured */
	return !(status & STM32H7_FLASH_STATUS_ERROR_MASK);
}

static bool stm32h7_flash_unlock(target_s *const target, const uint32_t regbase)
{
	/* Read out the Flash status and tend to any pending conditions */
	const uint32_t status = target_mem32_read32(target, regbase + STM32H7_FLASH_STATUS);
	/* Start by checking if there are any pending ongoing operations */
	if (status & STM32H7_FLASH_STATUS_QUEUE_WAIT) {
		/* Wait for any pending operations to complete */
		if (!stm32h7_flash_wait_complete(target, regbase))
			return false;
	}
	/* Clear any pending errors so we're in a good state */
	else if (status & STM32H7_FLASH_STATUS_ERROR_MASK)
		target_mem32_write32(target, regbase + STM32H7_FLASH_CLEAR_CTRL,
			status & (STM32H7_FLASH_STATUS_ERROR_MASK | STM32H7_FLASH_STATUS_EOP));

	/* Unlock the device Flash if not already unlocked (it's an error to re-key the controller if it is) */
	if (target_mem32_read32(target, regbase + STM32H7_FLASH_CTRL) & STM32H7_FLASH_CTRL_LOCK) {
		/* Enable Flash controller access */
		target_mem32_write32(target, regbase + STM32H7_FLASH_KEYR, STM32H7_FLASH_KEY1);
		target_mem32_write32(target, regbase + STM32H7_FLASH_KEYR, STM32H7_FLASH_KEY2);
	}
	/* Return whether we were able to put the device into unlocked mode */
	return !(target_mem32_read32(target, regbase + STM32H7_FLASH_CTRL) & STM32H7_FLASH_CTRL_LOCK);
}

static bool stm32h7_flash_prepare(target_flash_s *target_flash)
{
	target_s *target = target_flash->t;
	const stm32h7_flash_s *const flash = (stm32h7_flash_s *)target_flash;

	/* Unlock the Flash controller to prepare it for operations */
	return stm32h7_flash_unlock(target, flash->regbase);
}

static bool stm32h7_flash_done(target_flash_s *target_flash)
{
	target_s *target = target_flash->t;
	const stm32h7_flash_s *const flash = (stm32h7_flash_s *)target_flash;
	/* Lock the Flash controller to complete operations */
	target_mem32_write32(target, flash->regbase + STM32H7_FLASH_CTRL,
		(flash->psize << STM32H7_FLASH_CTRL_PSIZE_SHIFT) | STM32H7_FLASH_CTRL_LOCK);
	return true;
}

/* Helper for offsetting FLASH_CR bits correctly */
static uint32_t stm32h7_flash_cr(uint32_t sector_size, const uint32_t ctrl, const uint8_t sector_number)
{
	uint32_t command = ctrl;
	/* H74x, H72x IP: 128 KiB and has PSIZE */
	if (sector_size == FLASH_SECTOR_SIZE) {
		command |= sector_number << STM32H7_FLASH_CTRL_SECTOR_NUM_SHIFT;
		DEBUG_TARGET("%s: patching FLASH_CR from 0x%08" PRIx32 " to 0x%08" PRIx32 "\n", __func__, ctrl, command);
		return command;
	}

	/* H7Bx IP: 8 KiB and no PSIZE */
	/* Save and right-shift FW, START bits */
	const uint32_t temp_fw_start = command & (STM32H7_FLASH_CTRL_FORCE_WRITE | STM32H7_FLASH_CTRL_START);
	/* Parallelism is ignored */
	command &= ~(STM32H7_FLASH_CTRL_PSIZE64 | STM32H7_FLASH_CTRL_FORCE_WRITE | STM32H7_FLASH_CTRL_START);
	/* Restore FW, START to H7Bx-correct bits */
	command |= temp_fw_start >> 2U;
	/* SNB offset is different, too */
	command |= sector_number << STM32H7BX_FLASH_CTRL_SECTOR_NUM_SHIFT;
	DEBUG_TARGET("%s: patching FLASH_CR from 0x%08" PRIx32 " to 0x%08" PRIx32 "\n", __func__, ctrl, command);
	return command;
}

static bool stm32h7_flash_erase(target_flash_s *const target_flash, target_addr_t addr, const size_t len)
{
	(void)len;
	/* Erases are always done one sector at a time - the target Flash API guarantees this */
	target_s *target = target_flash->t;
	const stm32h7_flash_s *const flash = (stm32h7_flash_s *)target_flash;

	/* Calculate the sector to erase and set the operation runnning */
	const uint32_t sector = (addr - target_flash->start) / target_flash->blocksize;
	const uint32_t ctrl = (flash->psize << STM32H7_FLASH_CTRL_PSIZE_SHIFT) | STM32H7_FLASH_CTRL_SECTOR_ERASE;
	target_mem32_write32(
		target, flash->regbase + STM32H7_FLASH_CTRL, stm32h7_flash_cr(target_flash->blocksize, ctrl, sector));
	target_mem32_write32(target, flash->regbase + STM32H7_FLASH_CTRL,
		stm32h7_flash_cr(target_flash->blocksize, ctrl | STM32H7_FLASH_CTRL_START, sector));

	/* Wait for the operation to complete and report errors */
	return stm32h7_flash_wait_complete(target, flash->regbase);
}

static bool stm32h7_flash_write(
	target_flash_s *const target_flash, const target_addr_t dest, const void *const src, const size_t len)
{
	target_s *target = target_flash->t;
	const stm32h7_flash_s *const flash = (stm32h7_flash_s *)target_flash;

	/* Prepare the Flash write operation */
	const uint32_t ctrl = stm32h7_flash_cr(
		target_flash->blocksize, (flash->psize << STM32H7_FLASH_CTRL_PSIZE_SHIFT) | STM32H7_FLASH_CTRL_PROGRAM, 0);
	target_mem32_write32(target, flash->regbase + STM32H7_FLASH_CTRL, ctrl);

	/* Write the data to the Flash */
	for (size_t offset = 0U; offset < len; offset += 32U) {
		const size_t amount = MIN(len - offset, 32U);
		target_mem32_write(target, dest + offset, ((const uint8_t *)src) + offset, amount);
		/*
		 * If this is the final chunk and the amount is not a multiple of 32 bytes,
		 * make sure the write is forced to complete per RM0468 §4.3.9 "Single write sequence" pg164
		 */
		if (amount < 32U)
			target_mem32_write32(target, flash->regbase + STM32H7_FLASH_CTRL,
				stm32h7_flash_cr(target_flash->blocksize, ctrl | STM32H7_FLASH_CTRL_FORCE_WRITE, 0U));
		while (target_mem32_read32(target, flash->regbase + STM32H7_FLASH_STATUS) & STM32H7_FLASH_STATUS_QUEUE_WAIT)
			continue;
	}

	/* Wait for the operation to complete and report errors */
	return stm32h7_flash_wait_complete(target, flash->regbase);
}

static bool stm32h7_erase_bank(target_s *const target, const align_e psize, const uint32_t reg_base)
{
	if (!stm32h7_flash_unlock(target, reg_base)) {
		DEBUG_ERROR("Bank erase: Unlock bank failed\n");
		return false;
	}
	/* BER and start can be merged per §4.3.10 "Standard flash bank erase sequence" of RM0433 rev8, pg166. */
	const uint32_t ctrl = stm32h7_flash_cr(target->flash->blocksize,
		(psize << STM32H7_FLASH_CTRL_PSIZE_SHIFT) | STM32H7_FLASH_CTRL_BANK_ERASE | STM32H7_FLASH_CTRL_START, 0);
	target_mem32_write32(target, reg_base + STM32H7_FLASH_CTRL, ctrl);
	DEBUG_INFO("Mass erase of bank started\n");
	return true;
}

static bool stm32h7_wait_erase_bank(target_s *const target, platform_timeout_s *const timeout, const uint32_t reg_base)
{
	while (target_mem32_read32(target, reg_base + STM32H7_FLASH_STATUS) & STM32H7_FLASH_STATUS_QUEUE_WAIT) {
		if (target_check_error(target)) {
			DEBUG_ERROR("mass erase bank: comm failed\n");
			return false;
		}
		target_print_progress(timeout);
	}
	return true;
}

static bool stm32h7_check_bank(target_s *const target, const uint32_t reg_base)
{
	uint32_t status = target_mem32_read32(target, reg_base + STM32H7_FLASH_STATUS);
	if (status & STM32H7_FLASH_STATUS_ERROR_MASK)
		DEBUG_ERROR("mass erase bank: error sr %" PRIx32 "\n", status);
	return !(status & STM32H7_FLASH_STATUS_ERROR_MASK);
}

/* Both banks are erased in parallel.*/
static bool stm32h7_mass_erase(target_s *target)
{
	align_e psize = ALIGN_64BIT;
	/*
	 * XXX: What is this and why does it exist?
	 * A dry-run walk-through says it'll pull out the psize for the first Flash region added by stm32h7_probe()
	 * because all Flash regions added by stm32h7_add_flash match the if condition. This looks redundant and wrong.
	 */
	for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
		if (flash->write == stm32h7_flash_write)
			psize = ((struct stm32h7_flash *)flash)->psize;
	}
	/* Send mass erase Flash start instruction */
	if (!stm32h7_erase_bank(target, psize, STM32H7_FPEC1_BASE) ||
		!stm32h7_erase_bank(target, psize, STM32H7_FPEC2_BASE))
		return false;

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	/* Wait for the banks to finish erasing */
	if (!stm32h7_wait_erase_bank(target, &timeout, STM32H7_FPEC1_BASE) ||
		!stm32h7_wait_erase_bank(target, &timeout, STM32H7_FPEC2_BASE))
		return false;

	/* Check the banks for final errors */
	return stm32h7_check_bank(target, STM32H7_FPEC1_BASE) && stm32h7_check_bank(target, STM32H7_FPEC2_BASE);
}

static uint32_t stm32h7_part_uid_addr(target_s *const target)
{
	if (target->part_id == ID_STM32H7Bx)
		return 0x08fff800U; /* 7B3/7A3/7B0 */
	return 0x1ff1e800U;
}

static bool stm32h7_uid(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	const uint32_t uid_addr = stm32h7_part_uid_addr(target);
	return stm32_uid(target, uid_addr);
}

static bool stm32h7_crc_bank(target_s *target, uint32_t addr)
{
	const uint32_t reg_base = stm32h7_flash_bank_base(addr);
	if (!stm32h7_flash_unlock(target, reg_base))
		return false;

	target_mem32_write32(target, reg_base + STM32H7_FLASH_CTRL, STM32H7_FLASH_CTRL_CRC_EN);
	const uint32_t crc_ctrl =
		STM32H7_FLASH_CRCCR_CRC_BURST_3 | STM32H7_FLASH_CRCCR_CLEAN_CRC | STM32H7_FLASH_CRCCR_ALL_BANK;
	target_mem32_write32(target, reg_base + STM32H7_FLASH_CRCCR, crc_ctrl);
	target_mem32_write32(target, reg_base + STM32H7_FLASH_CRCCR, crc_ctrl | STM32H7_FLASH_CRCCR_START_CRC);
	uint32_t status = STM32H7_FLASH_STATUS_CRC_BUSY;
#if ENABLE_DEBUG == 1
	const uint8_t bank = reg_base == STM32H7_FPEC1_BASE ? 1 : 2;
#endif
	while (status & STM32H7_FLASH_STATUS_CRC_BUSY) {
		status = target_mem32_read32(target, reg_base + STM32H7_FLASH_STATUS);
		if (target_check_error(target)) {
			DEBUG_ERROR("CRC bank %u: comm failed\n", bank);
			return false;
		}
		if (status & STM32H7_FLASH_STATUS_ERROR_READ) {
			DEBUG_ERROR("CRC bank %u: error status %08" PRIx32 "\n", bank, status);
			return false;
		}
	}
	return true;
}

static bool stm32h7_crc(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	if (!stm32h7_crc_bank(target, STM32H7_FLASH_BANK1_BASE))
		return false;
	uint32_t crc1 = target_mem32_read32(target, STM32H7_FPEC1_BASE + STM32H7_FLASH_CRCDATA);
	if (!stm32h7_crc_bank(target, STM32H7_FLASH_BANK2_BASE))
		return false;
	uint32_t crc2 = target_mem32_read32(target, STM32H7_FPEC2_BASE + STM32H7_FLASH_CRCDATA);
	tc_printf(target, "CRC: bank1 0x%08" PRIx32 ", bank2 0x%08" PRIx32 " \n", crc1, crc2);
	return true;
}

static bool stm32h7_cmd_psize(target_s *target, int argc, const char **argv)
{
	if (argc == 1) {
		align_e psize = ALIGN_64BIT;
		/*
		 * XXX: What is this and why does it exist?
		 * A dry-run walk-through says it'll pull out the psize for the first Flash region added by stm32h7_probe()
		 * because all Flash regions added by stm32h7_add_flash match the if condition. This looks redundant and wrong.
		 */
		for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
			if (flash->write == stm32h7_flash_write)
				psize = ((stm32h7_flash_s *)flash)->psize;
		}
		tc_printf(target, "Flash write parallelism: %s\n", stm32_psize_to_string(psize));
	} else {
		align_e psize;
		if (!stm32_psize_from_string(target, argv[1], &psize))
			return false;

		/*
		 * XXX: What is this and why does it exist?
		 * A dry-run walk-through says it'll overwrite psize for every Flash region added by stm32h7_probe()
		 * because all Flash regions added by stm32h7_add_flash match the if condition. This looks redundant and wrong.
		 */
		for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
			if (flash->write == stm32h7_flash_write)
				((stm32h7_flash_s *)flash)->psize = psize;
		}
	}
	return true;
}

static const struct {
	uint16_t rev_id;
	char revision;
} stm32h7xx_revisions[] = {
	{0x1000U, 'A'},
	{0x1001U, 'Z'},
	{0x1003U, 'Y'},
	{0x1007U, 'X'}, /* RM0455 */
	{0x2001U, 'X'},
	{0x2003U, 'V'},
};

static bool stm32h7_cmd_rev(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	const uint32_t idcode = target_mem32_read32(target, STM32H7_DBGMCU_IDCODE);
	const uint16_t rev_id = idcode >> STM32H7_DBGMCU_IDCODE_REV_SHIFT;
	const uint16_t dev_id = idcode & STM32H7_DBGMCU_IDCODE_DEV_MASK;

	/* Print device */
	switch (dev_id) {
	case ID_STM32H74x:
		tc_printf(target, "STM32H74x/75x\n");
		break;
	case ID_STM32H7Bx:
		tc_printf(target, "STM32H7B3/7A3/7B0\n");
		break;
	case ID_STM32H72x:
		tc_printf(target, "%s\n", target->driver);
		break;
	default:
		tc_printf(target, "Unknown %s (%03x). BMP may not correctly support it!\n", target->driver, dev_id);
		return false;
	}
	/* Print revision */
	char rev = '?';
	for (size_t i = 0; i < ARRAY_LENGTH(stm32h7xx_revisions); i++) {
		/* Check for matching revision */
		if (stm32h7xx_revisions[i].rev_id == rev_id)
			rev = stm32h7xx_revisions[i].revision;
	}
	tc_printf(target, "Revision %c\n", rev);

	return true;
}
