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
 * RM0433 - STM32H742, STM32H743/753 and STM32H750 Value line advanced Arm®-based 32-bit MCUs, Rev. 8
 *   https://www.st.com/resource/en/reference_manual/dm00314099-stm32h742-stm32h743-753-and-stm32h750-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf
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

#define FLASH_ACR       0x00U
#define FLASH_KEYR      0x04U
#define FLASH_OPTKEYR   0x08U
#define FLASH_CR        0x0cU
#define FLASH_SR        0x10U
#define FLASH_CCR       0x14U
#define FLASH_OPTCR     0x18U
#define FLASH_OPTSR_CUR 0x1cU
#define FLASH_OPTSR     0x20U
#define FLASH_CRCCR     0x50U
#define FLASH_CRCDATA   0x5cU

/* Flash Program and Erase Controller Register Map */
#define H7_IWDG_BASE        0x58004c00U
#define FPEC1_BASE          0x52002000U
#define FPEC2_BASE          0x52002100U
#define FLASH_SR_BSY        (1U << 0U)
#define FLASH_SR_WBNE       (1U << 1U)
#define FLASH_SR_QW         (1U << 2U)
#define FLASH_SR_CRC_BUSY   (1U << 3U)
#define FLASH_SR_EOP        (1U << 16U)
#define FLASH_SR_WRPERR     (1U << 17U)
#define FLASH_SR_PGSERR     (1U << 18U)
#define FLASH_SR_STRBERR    (1U << 19U)
#define FLASH_SR_INCERR     (1U << 21U)
#define FLASH_SR_OPERR      (1U << 22U)
#define FLASH_SR_OPERR      (1U << 22U)
#define FLASH_SR_RDPERR     (1U << 23U)
#define FLASH_SR_RDSERR     (1U << 24U)
#define FLASH_SR_SNECCERR   (1U << 25U)
#define FLASH_SR_DBERRERR   (1U << 26U)
#define FLASH_SR_ERROR_READ (FLASH_SR_RDPERR | FLASH_SR_RDSERR | FLASH_SR_SNECCERR | FLASH_SR_DBERRERR)
#define FLASH_SR_ERROR_MASK \
	(FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR | FLASH_SR_INCERR | FLASH_SR_OPERR | FLASH_SR_ERROR_READ)
#define FLASH_CR_LOCK    (1U << 0U)
#define FLASH_CR_PG      (1U << 1U)
#define FLASH_CR_SER     (1U << 2U)
#define FLASH_CR_BER     (1U << 3U)
#define FLASH_CR_PSIZE8  (0U << 4U)
#define FLASH_CR_PSIZE16 (1U << 4U)
#define FLASH_CR_PSIZE32 (2U << 4U)
#define FLASH_CR_PSIZE64 (3U << 4U)
#define FLASH_CR_FW      (1U << 6U)
#define FLASH_CR_START   (1U << 7U)
#define FLASH_CR_SNB_1   (1U << 8U)
#define FLASH_CR_SNB     (3U << 8U)
#define FLASH_CR_CRC_EN  (1U << 15U)

#define FLASH_OPTCR_OPTLOCK (1U << 0U)
#define FLASH_OPTCR_OPTSTRT (1U << 1U)

#define FLASH_OPTSR_IWDG1_SW (1U << 4U)

#define FLASH_CRCCR_ALL_BANK    (1U << 7U)
#define FLASH_CRCCR_START_CRC   (1U << 16U)
#define FLASH_CRCCR_CLEAN_CRC   (1U << 17U)
#define FLASH_CRCCR_CRC_BURST_3 (3U << 20U)

#define STM32H7_FLASH_KEY1 0x45670123U
#define STM32H7_FLASH_KEY2 0xcdef89abU

#define STM32H7_OPT_KEY1 0x08192a3bU
#define STM32H7_OPT_KEY2 0x4c5d6e7fU

#define DBGMCU_IDCODE      0x5c001000U
#define STM32H7_FLASH_SIZE 0x1ff1e800U
/* Access from processor address space.
 * Access via the APB-D is at 0xe00e1000 */
#define DBGMCU_IDC  (DBGMCU_IDCODE + 0U)
#define DBGMCU_CR   (DBGMCU_IDCODE + 4U)
#define DBGSLEEP_D1 (1U << 0U)
#define DBGSTOP_D1  (1U << 1U)
#define DBGSTBY_D1  (1U << 2U)
#define DBGSTOP_D3  (1U << 7U)
#define DBGSTBY_D3  (1U << 8U)
#define D1DBGCKEN   (1U << 21U)
#define D3DBGCKEN   (1U << 22U)

#define STM32H7_FLASH_BANK1_BASE    0x08000000U
#define STM32H7_FLASH_BANK2_BASE    0x08100000U
#define STM32H7_FLASH_BANK_SIZE     0x00100000U
#define STM32H74xxG_FLASH_BANK_SIZE 0x00080000U
#define STM32H74xxG_FLASH_SIZE      0x00100000U
#define NUM_SECTOR_PER_BANK         8U
#define FLASH_SECTOR_SIZE           0x20000U

#define ID_STM32H74x 0x4500U /* RM0433, RM0399 */
#define ID_STM32H7Bx 0x4800U /* RM0455 */
#define ID_STM32H72x 0x4830U /* RM0468 */

typedef struct stm32h7_flash {
	target_flash_s target_flash;
	align_e psize;
	uint32_t regbase;
} stm32h7_flash_s;

typedef struct stm32h7_priv {
	uint32_t dbg_cr;
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
static bool stm32h7_mass_erase(target_s *target);

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
	target_flash->writesize = 2048;
	target_flash->erased = 0xffU;
	if (addr < STM32H7_FLASH_BANK2_BASE)
		flash->regbase = FPEC1_BASE;
	else
		flash->regbase = FPEC2_BASE;
	flash->psize = ALIGN_64BIT;
	target_add_flash(target, target_flash);
}

bool stm32h7_probe(target_s *target)
{
	if (target->part_id != ID_STM32H74x && target->part_id != ID_STM32H7Bx && target->part_id != ID_STM32H72x)
		return false;

	target->driver = "STM32H7";
	target->attach = stm32h7_attach;
	target->detach = stm32h7_detach;
	target->mass_erase = stm32h7_mass_erase;
	target_add_commands(target, stm32h7_cmd_list, target->driver);

	/* Save private storage */
	stm32h7_priv_s *priv_storage = calloc(1, sizeof(*priv_storage));
	if (!priv_storage) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	priv_storage->dbg_cr = target_mem_read32(target, DBGMCU_CR);
	target->target_storage = priv_storage;

	/* Build the RAM map */
	/* Table 7. Memory map and default device memory area attributes RM0433, pg130 */
	target_add_ram(target, 0x00000000, 0x10000); /* ITCM RAM,   64kiB */
	target_add_ram(target, 0x20000000, 0x20000); /* DTCM RAM,  128kiB */
	target_add_ram(target, 0x24000000, 0x80000); /* AXI RAM,   512kiB */
	target_add_ram(target, 0x30000000, 0x20000); /* AHB SRAM1, 128kiB */
	target_add_ram(target, 0x30020000, 0x20000); /* AHB SRAM2, 128kiB */
	target_add_ram(target, 0x30040000, 0x08000); /* AHB SRAM3,  32kiB */
	target_add_ram(target, 0x38000000, 0x10000); /* AHB SRAM4,  64kiB */

	/* Build the Flash map */
	switch (target->part_id) {
	case ID_STM32H74x: {
		/* Read the Flash size from the device (expressed in kiB) and multiply it by 1024 */
		const uint32_t flash_size = target_mem_read32(target, STM32H7_FLASH_SIZE) << 10U;
		/* STM32H750nB */
		if (flash_size == FLASH_SECTOR_SIZE)
			stm32h7_add_flash(target, STM32H7_FLASH_BANK1_BASE, flash_size, FLASH_SECTOR_SIZE);
		/* STM32H742xG/H743xG */
		else if (flash_size == STM32H74xxG_FLASH_SIZE) {
			stm32h7_add_flash(target, STM32H7_FLASH_BANK1_BASE, STM32H74xxG_FLASH_BANK_SIZE, FLASH_SECTOR_SIZE);
			stm32h7_add_flash(target, STM32H7_FLASH_BANK2_BASE, STM32H74xxG_FLASH_BANK_SIZE, FLASH_SECTOR_SIZE);
		}
		/* STM32H742xL/H743xL/H753xL */
		else {
			stm32h7_add_flash(target, STM32H7_FLASH_BANK1_BASE, STM32H7_FLASH_BANK_SIZE, FLASH_SECTOR_SIZE);
			stm32h7_add_flash(target, STM32H7_FLASH_BANK2_BASE, STM32H7_FLASH_BANK_SIZE, FLASH_SECTOR_SIZE);
		}
		break;
	}
	default:
		stm32h7_add_flash(target, STM32H7_FLASH_BANK1_BASE, STM32H7_FLASH_BANK_SIZE, FLASH_SECTOR_SIZE);
		stm32h7_add_flash(target, STM32H7_FLASH_BANK2_BASE, STM32H7_FLASH_BANK_SIZE, FLASH_SECTOR_SIZE);
	}

	/* RM0433 Rev 4 is not really clear, what bits are needed in DBGMCU_CR. Maybe more flags needed? */
	const uint32_t dbgmcu_ctrl = DBGSLEEP_D1 | D1DBGCKEN;
	target_mem_write32(target, DBGMCU_CR, dbgmcu_ctrl);
	return true;
}

static bool stm32h7_attach(target_s *target)
{
	if (!cortexm_attach(target))
		return false;
	/*
	 * If IWDG runs as HARDWARE watchdog (§44.3.4) erase
	 * will be aborted by the Watchdog and erase fails!
	 * Setting IWDG_KR to 0xaaaa does not seem to help!
	 */
	const uint32_t optsr = target_mem_read32(target, FPEC1_BASE + FLASH_OPTSR);
	if (!(optsr & FLASH_OPTSR_IWDG1_SW))
		tc_printf(target, "Hardware IWDG running. Expect failure. Set IWDG1_SW!");
	return true;
}

static void stm32h7_detach(target_s *target)
{
	stm32h7_priv_s *ps = (stm32h7_priv_s *)target->target_storage;
	target_mem_write32(target, DBGMCU_CR, ps->dbg_cr);
	cortexm_detach(target);
}

static bool stm32h7_flash_busy_wait(target_s *const target, const uint32_t regbase)
{
	uint32_t status = FLASH_SR_BSY | FLASH_SR_QW;
	while (status & (FLASH_SR_BSY | FLASH_SR_QW)) {
		status = target_mem_read32(target, regbase + FLASH_SR);
		if ((status & FLASH_SR_ERROR_MASK) || target_check_error(target)) {
			DEBUG_ERROR("%s: error status %08" PRIx32 "\n", __func__, status);
			target_mem_write32(target, regbase + FLASH_CCR, status & FLASH_SR_ERROR_MASK);
			return false;
		}
	}
	return true;
}

static uint32_t stm32h7_flash_bank_base(const uint32_t addr)
{
	if (addr >= STM32H7_FLASH_BANK2_BASE)
		return FPEC2_BASE;
	return FPEC1_BASE;
}

static bool stm32h7_flash_unlock(target_s *const target, const uint32_t addr)
{
	const uint32_t regbase = stm32h7_flash_bank_base(addr);
	/* Wait for any pending operations to complete */
	if (!stm32h7_flash_busy_wait(target, regbase))
		return false;
	/* Unlock the device Flash if not already unlocked (it's an error to re-key the controller if it is) */
	if (target_mem_read32(target, regbase + FLASH_CR) & FLASH_CR_LOCK) {
		/* Enable Flash controller access */
		target_mem_write32(target, regbase + FLASH_KEYR, STM32H7_FLASH_KEY1);
		target_mem_write32(target, regbase + FLASH_KEYR, STM32H7_FLASH_KEY2);
	}
	/* Return whether we were able to put the device into unlocked mode */
	return !(target_mem_read32(target, regbase + FLASH_CR) & FLASH_CR_LOCK);
}

static bool stm32h7_flash_erase(target_flash_s *const target_flash, target_addr_t addr, const size_t len)
{
	target_s *target = target_flash->t;
	const stm32h7_flash_s *const flash = (stm32h7_flash_s *)target_flash;
	/* Unlock the Flash */
	if (!stm32h7_flash_unlock(target, addr))
		return false;
	/* We come out of reset with HSI 64 MHz. Adapt FLASH_ACR.*/
	target_mem_write32(target, flash->regbase + FLASH_ACR, 0);
	addr &= (NUM_SECTOR_PER_BANK * FLASH_SECTOR_SIZE) - 1U;
	const size_t end_sector = (addr + len - 1U) / FLASH_SECTOR_SIZE;
	const align_e psize = flash->psize;
	const uint32_t reg_base = flash->regbase;

	for (size_t begin_sector = addr / FLASH_SECTOR_SIZE; begin_sector <= end_sector; ++begin_sector) {
		/* Erase the current Flash sector */
		const uint32_t ctrl = (psize * FLASH_CR_PSIZE16) | FLASH_CR_SER | (begin_sector * FLASH_CR_SNB_1);
		target_mem_write32(target, reg_base + FLASH_CR, ctrl);
		target_mem_write32(target, reg_base + FLASH_CR, ctrl | FLASH_CR_START);

		/* Wait for the operation to complete and report errors */
		DEBUG_INFO("Erasing, ctrl = %08" PRIx32 " status = %08" PRIx32 "\n",
			target_mem_read32(target, reg_base + FLASH_CR), target_mem_read32(target, reg_base + FLASH_SR));

		if (!stm32h7_flash_busy_wait(target, reg_base))
			return false;
	}
	return true;
}

static bool stm32h7_flash_write(
	target_flash_s *const target_flash, const target_addr_t dest, const void *const src, const size_t len)
{
	target_s *target = target_flash->t;
	const stm32h7_flash_s *const flash = (stm32h7_flash_s *)target_flash;
	/* Unlock the Flash */
	if (!stm32h7_flash_unlock(target, dest))
		return false;

	/* Prepare the Flash write operation */
	const uint32_t ctrl = flash->psize * FLASH_CR_PSIZE16;
	target_mem_write32(target, flash->regbase + FLASH_CR, ctrl);
	target_mem_write32(target, flash->regbase + FLASH_CR, ctrl | FLASH_CR_PG);
	/* does H7 stall?*/

	/* Write the data to the Flash */
	target_mem_write(target, dest, src, len);

	/* Wait for the operation to complete and report errors */
	if (!stm32h7_flash_busy_wait(target, flash->regbase))
		return false;

	/* Close write windows */
	target_mem_write32(target, flash->regbase + FLASH_CR, 0);
	return true;
}

static bool stm32h7_erase_bank(
	target_s *const target, const align_e psize, const uint32_t start_addr, const uint32_t reg_base)
{
	if (!stm32h7_flash_unlock(target, start_addr)) {
		DEBUG_ERROR("Bank erase: Unlock bank failed\n");
		return false;
	}
	/* BER and start can be merged (§3.3.10). */
	const uint32_t ctrl = (psize * FLASH_CR_PSIZE16) | FLASH_CR_BER | FLASH_CR_START;
	target_mem_write32(target, reg_base + FLASH_CR, ctrl);
	DEBUG_INFO("Mass erase of bank started\n");
	return true;
}

static bool stm32h7_wait_erase_bank(target_s *const target, platform_timeout_s *const timeout, const uint32_t reg_base)
{
	while (target_mem_read32(target, reg_base + FLASH_SR) & FLASH_SR_QW) {
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
	uint32_t status = target_mem_read32(target, reg_base + FLASH_SR);
	if (status & FLASH_SR_ERROR_MASK)
		DEBUG_ERROR("mass erase bank: error sr %" PRIx32 "\n", status);
	return !(status & FLASH_SR_ERROR_MASK);
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
	if (!stm32h7_erase_bank(target, psize, STM32H7_FLASH_BANK1_BASE, FPEC1_BASE) ||
		!stm32h7_erase_bank(target, psize, STM32H7_FLASH_BANK2_BASE, FPEC2_BASE))
		return false;

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	/* Wait for the banks to finish erasing */
	if (!stm32h7_wait_erase_bank(target, &timeout, FPEC1_BASE) ||
		!stm32h7_wait_erase_bank(target, &timeout, FPEC2_BASE))
		return false;

	/* Check the banks for final errors */
	return stm32h7_check_bank(target, FPEC1_BASE) && stm32h7_check_bank(target, FPEC2_BASE);
}

static uint32_t stm32h7_part_uid_addr(target_s *const target)
{
	if (target->part_id == ID_STM32H7Bx)
		return 0x08fff800U; /* 7B3/7A3/7B0 */
	return 0x1ff1e800U;
}

/*
 * Print the Unique device ID.
 * Can be reused for other STM32 devices with uid as parameter.
 */
static bool stm32h7_uid(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	const uint32_t uid_addr = stm32h7_part_uid_addr(target);

	tc_printf(target, "0x");
	for (size_t i = 0; i < 12U; i += 4U) {
		const uint32_t value = target_mem_read32(target, uid_addr + i);
		tc_printf(target, "%02X%02X%02X%02X", (value >> 24U) & 0xffU, (value >> 16U) & 0xffU, (value >> 8U) & 0xffU,
			value & 0xffU);
	}
	tc_printf(target, "\n");
	return true;
}

static bool stm32h7_crc_bank(target_s *target, uint32_t addr)
{
	const uint32_t reg_base = stm32h7_flash_bank_base(addr);
	if (!stm32h7_flash_unlock(target, addr))
		return false;

	target_mem_write32(target, reg_base + FLASH_CR, FLASH_CR_CRC_EN);
	const uint32_t crc_ctrl = FLASH_CRCCR_CRC_BURST_3 | FLASH_CRCCR_CLEAN_CRC | FLASH_CRCCR_ALL_BANK;
	target_mem_write32(target, reg_base + FLASH_CRCCR, crc_ctrl);
	target_mem_write32(target, reg_base + FLASH_CRCCR, crc_ctrl | FLASH_CRCCR_START_CRC);
	uint32_t status = FLASH_SR_CRC_BUSY;
#ifdef ENABLE_DEBUG
	const uint8_t bank = reg_base == FPEC1_BASE ? 1 : 2;
#endif
	while (status & FLASH_SR_CRC_BUSY) {
		status = target_mem_read32(target, reg_base + FLASH_SR);
		if (target_check_error(target)) {
			DEBUG_ERROR("CRC bank %u: comm failed\n", bank);
			return false;
		}
		if (status & FLASH_SR_ERROR_READ) {
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
	uint32_t crc1 = target_mem_read32(target, FPEC1_BASE + FLASH_CRCDATA);
	if (!stm32h7_crc_bank(target, STM32H7_FLASH_BANK2_BASE))
		return false;
	uint32_t crc2 = target_mem_read32(target, FPEC2_BASE + FLASH_CRCDATA);
	tc_printf(target, "CRC: bank1 0x%08lx, bank2 0x%08lx\n", crc1, crc2);
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
	{0x2001U, 'X'},
	{0x2003U, 'V'},
};

static bool stm32h7_cmd_rev(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* DBGMCU identity code register */
	const uint32_t dbgmcu_idc = target_mem_read32(target, DBGMCU_IDC);
	const uint16_t rev_id = (dbgmcu_idc >> 16U) & 0xffffU;
	const uint16_t dev_id = (dbgmcu_idc & 0xfffU) << 4U;

	/* Print device */
	switch (dev_id) {
	case ID_STM32H74x:
		tc_printf(target, "STM32H74x/75x\n");

		/* Print revision */
		char rev = '?';
		for (size_t i = 0; i < ARRAY_LENGTH(stm32h7xx_revisions); i++) {
			/* Check for matching revision */
			if (stm32h7xx_revisions[i].rev_id == rev_id)
				rev = stm32h7xx_revisions[i].revision;
		}
		tc_printf(target, "Revision %c\n", rev);
		break;

	case ID_STM32H7Bx:
		tc_printf(target, "STM32H7B3/7A3/7B0\n");
		break;
	case ID_STM32H72x:
		tc_printf(target, "STM32H72x/73x\n");
		break;
	default:
		tc_printf(target, "Unknown %s. BMP may not correctly support it!\n", target->driver);
	}

	return true;
}
