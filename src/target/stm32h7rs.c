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
 * This file implements STM32H7R/S target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * References:
 * RM0477 - STM32H7Rx/7Sx Arm®-based 32-bit MCUs, Rev. 6
 *   https://www.st.com/resource/en/reference_manual/rm0477-stm32h7rx7sx-armbased-32bit-mcus-stmicroelectronics.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "stm32_common.h"

#define FLASH_ACR       0x000U
#define FLASH_KEYR      0x004U
#define FLASH_CR        0x010U
#define FLASH_SR        0x014U
#define FLASH_IER       0x020U
#define FLASH_ISR       0x024U
#define FLASH_ICR       0x028U
//#define FLASH_OPTKEYR   0x008U
//#define FLASH_CCR       0x014U
//#define FLASH_OPTCR     0x018U
//#define FLASH_OPTSR_CUR 0x01cU
//#define FLASH_OPTSR     0x020U
//#define FLASH_CRCCR     0x050U
//#define FLASH_CRCDATA   0x05cU

/* Flash Program and Erase Controller Register Map */
#define FPEC1_BASE          0x52002000U
#define FLASH_SR_BSY        (1U << 0U)
#define FLASH_SR_WBNE       (1U << 1U)
#define FLASH_SR_QW         (1U << 2U)
#define FLASH_SR_CRC_BUSY   (1U << 3U)
#define FLASH_ISR_EOP        (1U << 16U)
#define FLASH_ISR_WRPERR     (1U << 17U)
#define FLASH_ISR_PGSERR     (1U << 18U)
#define FLASH_ISR_STRBERR    (1U << 19U)
#define FLASH_ISR_INCERR     (1U << 21U)
#define FLASH_ISR_RDSERR     (1U << 24U)
#define FLASH_ISR_SNECCERR   (1U << 25U)
#define FLASH_ISR_DBECCERR   (1U << 26U)
#define FLASH_ISR_CRCEND     (1U << 27U)
#define FLASH_ISR_CRCRDERR   (1U << 28U)
#define FLASH_ISR_ERROR_READ (FLASH_ISR_RDSERR | FLASH_ISR_SNECCERR | FLASH_ISR_DBECCERR)
#define FLASH_ISR_ERROR_MASK \
	(FLASH_ISR_WRPERR | FLASH_ISR_PGSERR | FLASH_ISR_STRBERR | FLASH_ISR_INCERR | FLASH_ISR_ERROR_READ)
#define FLASH_CR_LOCK    (1U << 0U)
#define FLASH_CR_PG      (1U << 1U)
#define FLASH_CR_SER     (1U << 2U)
#define FLASH_CR_BER     (1U << 3U)
//#define FLASH_CR_PSIZE8  (0U << 4U)
//#define FLASH_CR_PSIZE16 (1U << 4U)
//#define FLASH_CR_PSIZE32 (2U << 4U)
//#define FLASH_CR_PSIZE64 (3U << 4U)
#define FLASH_CR_FW      (1U << 4U)
#define FLASH_CR_START   (1U << 5U)
#define FLASH_CR_SSN_SHIFT      6U
//#define FLASH_CR_SNB_1   (1U << 8U)
//#define FLASH_CR_SNB     (3U << 8U)
//#define FLASH_CR_CRC_EN  (1U << 15U)
//
//#define FLASH_OPTCR_OPTLOCK (1U << 0U)
//#define FLASH_OPTCR_OPTSTRT (1U << 1U)
//
//#define FLASH_OPTSR_IWDG1_SW (1U << 4U)
//
//#define FLASH_CRCCR_ALL_BANK    (1U << 7U)
//#define FLASH_CRCCR_START_CRC   (1U << 16U)
//#define FLASH_CRCCR_CLEAN_CRC   (1U << 17U)
//#define FLASH_CRCCR_CRC_BURST_3 (3U << 20U)

#define STM32H7RS_FLASH_KEY1 0x45670123U
#define STM32H7RS_FLASH_KEY2 0xcdef89abU

#define STM32H7RS_OPT_KEY1 0x08192a3bU
#define STM32H7RS_OPT_KEY2 0x4c5d6e7fU

#define DBGMCU_IDCODE        0x5c001000U
#define STM32H7RS_FLASH_SIZE   0x1ff1e880U
#define STM32H7Bx_FLASH_SIZE 0x08fff80cU
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

#define STM32H7RS_DBGMCU_IDCODE_DEV_MASK  0x00000fffU
#define STM32H7RS_DBGMCU_IDCODE_REV_SHIFT 16U

#define STM32H7RS_FLASH_BANK1_BASE    0x08000000U
#define STM32H7RS_FLASH_BANK_SIZE     0x00100000U
#define NUM_SECTOR_PER_BANK         8U
#define FLASH_SECTOR_SIZE           0x20000U

#define ID_STM32H7RS 0x485U /* RM0477 */

typedef struct stm32h7rs_flash {
	target_flash_s target_flash;
	align_e psize;
	uint32_t regbase;
} stm32h7rs_flash_s;

typedef struct stm32h7rs_priv {
	uint32_t dbg_cr;
} stm32h7rs_priv_s;

static bool stm32h7rs_uid(target_s *target, int argc, const char **argv);
//static bool stm32h7rs_crc(target_s *target, int argc, const char **argv);
static bool stm32h7rs_cmd_psize(target_s *target, int argc, const char **argv);
static bool stm32h7rs_cmd_rev(target_s *target, int argc, const char **argv);

const command_s stm32h7rs_cmd_list[] = {
	{"psize", stm32h7rs_cmd_psize, "Configure flash write parallelism: (x8|x16|x32|x64(default))"},
	{"uid", stm32h7rs_uid, "Print unique device ID"},
	//{"crc", stm32h7rs_crc, "Print CRC of both banks"},
	{"revision", stm32h7rs_cmd_rev, "Returns the Device ID and Revision"},
	{NULL, NULL, NULL},
};

static bool stm32h7rs_attach(target_s *target);
static void stm32h7rs_detach(target_s *target);
static bool stm32h7rs_flash_erase(target_flash_s *target_flash, target_addr_t addr, size_t len);
static bool stm32h7rs_flash_write(target_flash_s *target_flash, target_addr_t dest, const void *src, size_t len);
static bool stm32h7rs_mass_erase(target_s *target, platform_timeout_s *const print_progess);

static void stm32h7rs_add_flash(target_s *target, uint32_t addr, size_t length, size_t blocksize)
{
	stm32h7rs_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *target_flash = &flash->target_flash;
	target_flash->start = addr;
	target_flash->length = length;
	target_flash->blocksize = blocksize;
	target_flash->erase = stm32h7rs_flash_erase;
	target_flash->write = stm32h7rs_flash_write;
	target_flash->writesize = 2048;
	target_flash->erased = 0xffU;
	flash->regbase = FPEC1_BASE;
	flash->psize = ALIGN_64BIT;
	target_add_flash(target, target_flash);
}

bool stm32h7rs_probe(target_s *target)
{
	const adiv5_access_port_s *const ap = cortex_ap(target);
	if (ap->partno != ID_STM32H7RS)
		return false;
	
	target->part_id = ap->partno;
	
	/* Save private storage */
	stm32h7rs_priv_s *priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	target->target_storage = priv;
	priv->dbg_cr = target_mem32_read32(target, DBGMCU_CR);
	
	target->driver = "STM32H7R/S";
	target->attach = stm32h7rs_attach;
	target->detach = stm32h7rs_detach;
	target->mass_erase = stm32h7rs_mass_erase;
	target_add_commands(target, stm32h7rs_cmd_list, target->driver);
	
	/* EMEB - Decide which of these is correct */
#if 1
	/* RM0433 Rev 4 is not really clear, what bits are needed in DBGMCU_CR. Maybe more flags needed? */
	const uint32_t dbgmcu_ctrl = DBGSLEEP_D1 | D1DBGCKEN;
	target_mem32_write32(target, DBGMCU_CR, dbgmcu_ctrl);
#else
	/* Now we have a stable debug environment, make sure the WDTs can't bonk the processor out from under us */
	target_mem32_write32(target, STM32H7_DBGMCU_APB3FREEZE, STM32H7_DBGMCU_APB3FREEZE_WWDG1);
	target_mem32_write32(target, STM32H7_DBGMCU_APB4FREEZE, STM32H7_DBGMCU_APB4FREEZE_IWDG1);
	/*
	 * Make sure that both domain D1 and D3 debugging are enabled and that we can keep
	 * debugging through sleep, stop and standby states for domain D1
	 */
	target_mem32_write32(target, STM32H7_DBGMCU_CONFIG,
		target_mem32_read32(target, STM32H7_DBGMCU_CONFIG) | STM32H7_DBGMCU_CONFIG_DBGSLEEP_D1 |
			STM32H7_DBGMCU_CONFIG_DBGSTOP_D1 | STM32H7_DBGMCU_CONFIG_DBGSTBY_D1 | STM32H7_DBGMCU_CONFIG_D1DBGCKEN |
			STM32H7_DBGMCU_CONFIG_D3DBGCKEN);
	stm32h7_configure_wdts(target);
#endif
	
	/* Build the RAM map - EMEB: ITCM/DTCM are too big -shared w/ AXI */
	switch (target->part_id) {
	case ID_STM32H7RS: {
		/* Table 6. Memory map and default device memory area attributes RM0477, pg151 */
		target_add_ram32(target, 0x00000000, 0x30000); /* ITCM RAM,       192 KiB */
		target_add_ram32(target, 0x20000000, 0x30000); /* DTCM RAM,       192 KiB */
		target_add_ram32(target, 0x24000000, 0x72000); /* AXI RAM1+2+3+4, 456 KiB [128+128+128+72] contiguous, */
		target_add_ram32(target, 0x30000000, 0x8000);  /* AHB SRAM1+2,     32 KiB [16+16] contiguous, */
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
	case ID_STM32H7RS:
		stm32h7rs_add_flash(target, STM32H7RS_FLASH_BANK1_BASE, 0x10000U, 0x2000U);
		break;
	default:
		break;
	}

	return true;
}

static bool stm32h7rs_attach(target_s *target)
{
	if (!cortexm_attach(target))
		return false;
	/*
	 * If IWDG runs as HARDWARE watchdog (§44.3.4) erase
	 * will be aborted by the Watchdog and erase fails!
	 * Setting IWDG_KR to 0xaaaa does not seem to help!
	 */
	//const uint32_t optsr = target_mem_read32(target, FPEC1_BASE + FLASH_OPTSR);
	//if (!(optsr & FLASH_OPTSR_IWDG1_SW))
	//	tc_printf(target, "Hardware IWDG running. Expect failure. Set IWDG1_SW!");
	return true;
}

static void stm32h7rs_detach(target_s *target)
{
	//stm32h7rs_priv_s *ps = (stm32h7rs_priv_s *)target->target_storage;
	//target_mem_write32(target, DBGMCU_CR, ps->dbg_cr);
	cortexm_detach(target);
}

static bool stm32h7rs_flash_busy_wait(target_s *const target, const uint32_t regbase)
{
	uint32_t status = FLASH_SR_BSY | FLASH_SR_QW;
	while (status & (FLASH_SR_BSY | FLASH_SR_QW)) {
		status = target_mem32_read32(target, regbase + FLASH_SR);
		uint32_t istatus = target_mem32_read32(target, regbase + FLASH_ISR);
		if ((istatus & FLASH_ISR_ERROR_MASK) || target_check_error(target)) {
			DEBUG_ERROR("%s: error status %08" PRIx32 "\n", __func__, istatus);
			target_mem32_write32(target, regbase + FLASH_ICR, istatus & FLASH_ISR_ERROR_MASK);
			return false;
		}
	}
	return true;
}

static bool stm32h7rs_flash_unlock(target_s *const target, const uint32_t addr)
{
	(void)addr;
	const uint32_t regbase = FPEC1_BASE;
	/* Wait for any pending operations to complete */
	if (!stm32h7rs_flash_busy_wait(target, regbase))
		return false;
	/* Unlock the device Flash if not already unlocked (it's an error to re-key the controller if it is) */
	if (target_mem32_read32(target, regbase + FLASH_CR) & FLASH_CR_LOCK) {
		/* Enable Flash controller access */
		target_mem32_write32(target, regbase + FLASH_KEYR, STM32H7RS_FLASH_KEY1);
		target_mem32_write32(target, regbase + FLASH_KEYR, STM32H7RS_FLASH_KEY2);
	}
	/* Return whether we were able to put the device into unlocked mode */
	return !(target_mem32_read32(target, regbase + FLASH_CR) & FLASH_CR_LOCK);
}

///* Helper for offsetting FLASH_CR bits correctly */
//static uint32_t stm32h7rs_flash_cr(uint32_t sector_size, const uint32_t ctrl, int snb)
//{
//	uint32_t command = ctrl;
//	/* H74x, H72x IP: 128 KiB and has PSIZE */
//	if (sector_size == FLASH_SECTOR_SIZE) {
//		command = ctrl | (snb * FLASH_CR_SNB_1);
//		DEBUG_TARGET("%s: patching FLASH_CR from 0x%08" PRIx32 " to 0x%08" PRIx32 "\n", __func__, ctrl, command);
//		return command;
//	}
//
//	/* H7Bx IP: 8 KiB and no PSIZE */
//	/* Save and right-shift FW, START bits */
//	const uint32_t temp_fw_start = command & (FLASH_CR_FW | FLASH_CR_START);
//	/* Parallelism is ignored */
//	command &= ~(FLASH_CR_PSIZE64 | FLASH_CR_FW | FLASH_CR_START);
//	/* Restore FW, START to H7Bx-correct bits */
//	command |= (temp_fw_start >> 2U);
//	/* SNB offset is different, too */
//	command |= (snb << 6U);
//	DEBUG_TARGET("%s: patching FLASH_CR from 0x%08" PRIx32 " to 0x%08" PRIx32 "\n", __func__, ctrl, command);
//	return command;
//}

static bool stm32h7rs_flash_erase(target_flash_s *const target_flash, target_addr_t addr, const size_t len)
{
	const uint32_t sector_size = target_flash->blocksize;
	target_s *target = target_flash->t;
	const stm32h7rs_flash_s *const flash = (stm32h7rs_flash_s *)target_flash;
	/* Unlock the Flash */
	if (!stm32h7rs_flash_unlock(target, addr))
		return false;
	/* We come out of reset with HSI 64 MHz. Adapt FLASH_ACR.*/
	target_mem32_write32(target, flash->regbase + FLASH_ACR, 0);
	/* Calculate SNB span */
	addr &= target_flash->length - 1U;
	const size_t end_sector = (addr + len - 1U) / sector_size;
	//const align_e psize = flash->psize;
	const uint32_t reg_base = flash->regbase;

	for (size_t begin_sector = addr / sector_size; begin_sector <= end_sector; ++begin_sector) {
		/* Erase the current Flash sector */
		//const uint32_t ctrl = stm32h7rs_flash_cr(sector_size, (psize * FLASH_CR_PSIZE16) | FLASH_CR_SER, begin_sector);
		const uint32_t ctrl = FLASH_CR_SER | (begin_sector << FLASH_CR_SSN_SHIFT);
		target_mem32_write32(target, reg_base + FLASH_CR, ctrl);
		//const uint32_t ctrl_start = stm32h7rs_flash_cr(sector_size, ctrl | FLASH_CR_START, begin_sector);
		const uint32_t ctrl_start = ctrl | FLASH_CR_START;
		target_mem32_write32(target, reg_base + FLASH_CR, ctrl_start);

		/* Wait for the operation to complete and report errors */
		DEBUG_INFO("Erasing, ctrl = %08" PRIx32 " status = %08" PRIx32 "\n",
			target_mem32_read32(target, reg_base + FLASH_CR), target_mem32_read32(target, reg_base + FLASH_SR));

		if (!stm32h7rs_flash_busy_wait(target, reg_base))
			return false;
	}
	return true;
}

static bool stm32h7rs_flash_write(
	target_flash_s *const target_flash, const target_addr_t dest, const void *const src, const size_t len)
{
	target_s *target = target_flash->t;
	const stm32h7rs_flash_s *const flash = (stm32h7rs_flash_s *)target_flash;
	/* Unlock the Flash */
	if (!stm32h7rs_flash_unlock(target, dest))
		return false;

	/* Prepare the Flash write operation */
	//const uint32_t ctrl = stm32h7rs_flash_cr(target_flash->blocksize, flash->psize * FLASH_CR_PSIZE16, 0);
	//target_mem_write32(target, flash->regbase + FLASH_CR, ctrl);
	//const uint32_t ctrl_pg = stm32h7rs_flash_cr(target_flash->blocksize, ctrl | FLASH_CR_PG, 0);
	const uint32_t ctrl_pg = FLASH_CR_PG;
	target_mem32_write32(target, flash->regbase + FLASH_CR, ctrl_pg);
	/* does H7 stall?*/

	/* Write the data to the Flash */
	target_mem32_write(target, dest, src, len);

	/* Wait for the operation to complete and report errors */
	if (!stm32h7rs_flash_busy_wait(target, flash->regbase))
		return false;

	/* Close write windows */
	target_mem32_write32(target, flash->regbase + FLASH_CR, 0);
	return true;
}

static bool stm32h7rs_erase_bank(
	target_s *const target, const align_e psize, const uint32_t start_addr, const uint32_t reg_base)
{
	if (!stm32h7rs_flash_unlock(target, start_addr)) {
		DEBUG_ERROR("Bank erase: Unlock bank failed\n");
		return false;
	}
	/* BER and start can be merged (§3.3.10). */
	//const uint32_t ctrl = stm32h7rs_flash_cr(target->flash->blocksize, (psize * FLASH_CR_PSIZE16) | FLASH_CR_BER | FLASH_CR_START, 0);
	const uint32_t ctrl = FLASH_CR_BER | FLASH_CR_START;
	target_mem32_write32(target, reg_base + FLASH_CR, ctrl);
	DEBUG_INFO("Mass erase of bank started\n");
	return true;
}

static bool stm32h7rs_wait_erase_bank(target_s *const target, platform_timeout_s *const timeout, const uint32_t reg_base)
{
	while (target_mem32_read32(target, reg_base + FLASH_SR) & FLASH_SR_QW) {
		if (target_check_error(target)) {
			DEBUG_ERROR("mass erase bank: comm failed\n");
			return false;
		}
		target_print_progress(timeout);
	}
	return true;
}

static bool stm32h7rs_check_bank(target_s *const target, const uint32_t reg_base)
{
	uint32_t status = target_mem32_read32(target, reg_base + FLASH_ISR);
	if (status & FLASH_ISR_ERROR_MASK)
		DEBUG_ERROR("mass erase bank: error sr %" PRIx32 "\n", status);
	return !(status & FLASH_ISR_ERROR_MASK);
}

/* Both banks are erased in parallel.*/
static bool stm32h7rs_mass_erase(target_s *target, platform_timeout_s *const print_progess)
{
	align_e psize = ALIGN_64BIT;
	/*
	 * XXX: What is this and why does it exist?
	 * A dry-run walk-through says it'll pull out the psize for the first Flash region added by stm32h7rs_probe()
	 * because all Flash regions added by stm32h7rs_add_flash match the if condition. This looks redundant and wrong.
	 */
	for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
		if (flash->write == stm32h7rs_flash_write)
			psize = ((struct stm32h7rs_flash *)flash)->psize;
	}
	/* Send mass erase Flash start instruction */
	if (!stm32h7rs_erase_bank(target, psize, STM32H7RS_FLASH_BANK1_BASE, FPEC1_BASE))
		return false;

	/* Wait for the banks to finish erasing */
	if (!stm32h7rs_wait_erase_bank(target, print_progess, FPEC1_BASE))
		return false;

	/* Check the banks for final errors */
	return stm32h7rs_check_bank(target, FPEC1_BASE);
}

/*
 * Print the Unique device ID.
 * Can be reused for other STM32 devices with uid as parameter.
 */
static bool stm32h7rs_uid(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	const uint32_t uid_addr = 0x08fff800U;

	tc_printf(target, "0x");
	for (size_t i = 0; i < 12U; i += 4U) {
		const uint32_t value = target_mem32_read32(target, uid_addr + i);
		tc_printf(target, "%02X%02X%02X%02X", (value >> 24U) & 0xffU, (value >> 16U) & 0xffU, (value >> 8U) & 0xffU,
			value & 0xffU);
	}
	tc_printf(target, "\n");
	return true;
}

//static bool stm32h7rs_crc_bank(target_s *target, uint32_t addr)
//{
//	const uint32_t reg_base = FPEC1_BASE;
//	if (!stm32h7rs_flash_unlock(target, addr))
//		return false;
//
//	target_mem_write32(target, reg_base + FLASH_CR, FLASH_CR_CRC_EN);
//	const uint32_t crc_ctrl = FLASH_CRCCR_CRC_BURST_3 | FLASH_CRCCR_CLEAN_CRC | FLASH_CRCCR_ALL_BANK;
//	target_mem_write32(target, reg_base + FLASH_CRCCR, crc_ctrl);
//	target_mem_write32(target, reg_base + FLASH_CRCCR, crc_ctrl | FLASH_CRCCR_START_CRC);
//	uint32_t status = FLASH_SR_CRC_BUSY;
//#if ENABLE_DEBUG == 1
//	const uint8_t bank = reg_base == FPEC1_BASE ? 1 : 2;
//#endif
//	while (status & FLASH_SR_CRC_BUSY) {
//		status = target_mem_read32(target, reg_base + FLASH_SR);
//		if (target_check_error(target)) {
//			DEBUG_ERROR("CRC bank %u: comm failed\n", bank);
//			return false;
//		}
//		if (status & FLASH_SR_ERROR_READ) {
//			DEBUG_ERROR("CRC bank %u: error status %08" PRIx32 "\n", bank, status);
//			return false;
//		}
//	}
//	return true;
//}
//
//static bool stm32h7rs_crc(target_s *target, int argc, const char **argv)
//{
//	(void)argc;
//	(void)argv;
//	if (!stm32h7rs_crc_bank(target, STM32H7RS_FLASH_BANK1_BASE))
//		return false;
//	uint32_t crc1 = target_mem_read32(target, FPEC1_BASE + FLASH_CRCDATA);
//	if (!stm32h7rs_crc_bank(target, STM32H7RS_FLASH_BANK2_BASE))
//		return false;
//	uint32_t crc2 = 0;//TODO: remove: target_mem_read32(target, FPEC2_BASE + FLASH_CRCDATA);
//	tc_printf(target, "CRC: bank1 0x%08lx, bank2 0x%08lx\n", crc1, crc2);
//	return true;
//}

static bool stm32h7rs_cmd_psize(target_s *target, int argc, const char **argv)
{
	if (argc == 1) {
		align_e psize = ALIGN_64BIT;
		/*
		 * XXX: What is this and why does it exist?
		 * A dry-run walk-through says it'll pull out the psize for the first Flash region added by stm32h7rs_probe()
		 * because all Flash regions added by stm32h7rs_add_flash match the if condition. This looks redundant and wrong.
		 */
		for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
			if (flash->write == stm32h7rs_flash_write)
				psize = ((stm32h7rs_flash_s *)flash)->psize;
		}
		tc_printf(target, "Flash write parallelism: %s\n", stm32_psize_to_string(psize));
	} else {
		align_e psize;
		if (!stm32_psize_from_string(target, argv[1], &psize))
			return false;

		/*
		 * XXX: What is this and why does it exist?
		 * A dry-run walk-through says it'll overwrite psize for every Flash region added by stm32h7rs_probe()
		 * because all Flash regions added by stm32h7rs_add_flash match the if condition. This looks redundant and wrong.
		 */
		for (target_flash_s *flash = target->flash; flash; flash = flash->next) {
			if (flash->write == stm32h7rs_flash_write)
				((stm32h7rs_flash_s *)flash)->psize = psize;
		}
	}
	return true;
}

static const struct {
	uint16_t rev_id;
	char revision;
} stm32h7rs_revisions[] = {
	{0x1003U, 'Y'},
};

static bool stm32h7rs_cmd_rev(target_s *target, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* DBGMCU identity code register */
	const uint32_t dbgmcu_idc = target_mem32_read32(target, DBGMCU_IDC);
	const uint16_t rev_id = dbgmcu_idc >> STM32H7RS_DBGMCU_IDCODE_REV_SHIFT;
	const uint16_t dev_id = dbgmcu_idc & STM32H7RS_DBGMCU_IDCODE_DEV_MASK;

	/* Print device */
	switch (dev_id) {
	case ID_STM32H7RS:
		tc_printf(target, "STM32H7Rx/Sx\n");
		break;
	default:
		tc_printf(target, "Unknown %s. BMP may not correctly support it!\n", target->driver);
		return false;
	}
	/* Print revision */
	char rev = '?';
	for (size_t i = 0; i < ARRAY_LENGTH(stm32h7rs_revisions); i++) {
		/* Check for matching revision */
		if (stm32h7rs_revisions[i].rev_id == rev_id)
			rev = stm32h7rs_revisions[i].revision;
	}
	tc_printf(target, "Revision %c\n", rev);

	return true;
}
