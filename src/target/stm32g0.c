/*
 * This file is part of the Black Magic Debug project.
 *
 * MIT License
 *
 * Copyright (c) 2021 Fabrice Prost-Boucle <fabalthazar@falbalab.fr>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * This file implements STM32G0 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * References:
 * RM0454 - Rev 5 (Value line)
 *   Reference manual - STM32G0x0 advanced ARM(R)-based 32-bit MCUs
 *                      (STM32G030/STM32G050/STM32G070/STM32G0B0)
 * RM0444 - Rev 5 (Access line)
 *   Reference manual - STM32G0x1 advanced ARM(R)-based 32-bit MCUs
 *                      (STM32G031/STM32G041/STM32G051/STM32G061/
 *                       STM32G071/STM32G081/STM32G0B1/STM32G0C1)
 * PM0223 - Rev 5
 *   Programming manual - Cortex(R)-M0+ programming manual for STM32L0, STM32G0,
 *                        STM32WL and STM32WB Series
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "command.h"

/* FLASH */
#define FLASH_START                     0x08000000
#define FLASH_MEMORY_SIZE               0x1FFF75E0
#define FLASH_PAGE_SIZE                 0x800
#define FLASH_BANK2_START_PAGE_NB       256U
#define FLASH_OTP_START                 0x1FFF7000
#define FLASH_OTP_SIZE                  0x400
#define FLASH_OTP_BLOCKSIZE             0x8
#define FLASH_SIZE_MAX_G03_4            (64U * 1024U)    // 64 kiB
#define FLASH_SIZE_MAX_G05_6            (64U * 1024U)    // 64 kiB
#define FLASH_SIZE_MAX_G07_8            (128U * 1024U)   // 128 kiB
#define FLASH_SIZE_MAX_G0B_C            (512U * 1024U)   // 512 kiB

#define G0_FLASH_BASE                   0x40022000
#define FLASH_ACR                       (G0_FLASH_BASE + 0x000)
#define FLASH_ACR_EMPTY                 (1U << 16U)

#define FLASH_KEYR                      (G0_FLASH_BASE + 0x008)
#define FLASH_KEYR_KEY1                 0x45670123
#define FLASH_KEYR_KEY2                 0xCDEF89AB
#define FLASH_CR                        (G0_FLASH_BASE + 0x014)
#define FLASH_CR_LOCK                   (1U << 31U)
#define FLASH_CR_OBL_LAUNCH             (1U << 27U)
#define FLASH_CR_OPTSTRT                (1U << 17U)
#define FLASH_CR_STRT                   (1U << 16U)
#define FLASH_CR_MER2                   (1U << 15U)
#define FLASH_CR_MER1                   (1U << 2U)
#define FLASH_CR_BKER                   (1U << 13U)
#define FLASH_CR_PNB_SHIFT              3U
#define FLASH_CR_PER                    (1U << 1U)
#define FLASH_CR_PG                     (1U << 0U)

#define FLASH_SR                        (G0_FLASH_BASE + 0x010)
#define FLASH_SR_BSY2                   (1U << 17U)
#define FLASH_SR_BSY1                   (1U << 16U)
#define FLASH_SR_OPTVERR                (1U << 15U)
#define FLASH_SR_RDERR                  (1U << 14U)
#define FLASH_SR_FASTERR                (1U << 9U)
#define FLASH_SR_MISSERR                (1U << 8U)
#define FLASH_SR_PGSERR                 (1U << 7U)
#define FLASH_SR_SIZERR                 (1U << 6U)
#define FLASH_SR_PGAERR                 (1U << 5U)
#define FLASH_SR_WRPERR                 (1U << 4U)
#define FLASH_SR_PROGERR                (1U << 3U)
#define FLASH_SR_OPERR                  (1U << 1U)
#define FLASH_SR_EOP                    (1U << 0U)
#define FLASH_SR_ERROR_MASK \
	(FLASH_SR_OPTVERR | FLASH_SR_RDERR | FLASH_SR_FASTERR | \
	 FLASH_SR_MISSERR | FLASH_SR_PGSERR | FLASH_SR_SIZERR | \
	 FLASH_SR_PGAERR | FLASH_SR_WRPERR | FLASH_SR_PROGERR | \
	 FLASH_SR_OPERR)
#define FLASH_SR_BSY_MASK               (FLASH_SR_BSY2 | FLASH_SR_BSY1)

#define FLASH_OPTKEYR                   (G0_FLASH_BASE + 0x00C)
#define FLASH_OPTKEYR_KEY1              0x08192A3B
#define FLASH_OPTKEYR_KEY2              0x4C5D6E7F
#define FLASH_OPTR                      (G0_FLASH_BASE + 0x020)
#define FLASH_OPTR_RDP_MASK             0xFF
#define FLASH_PCROP1ASR                 (G0_FLASH_BASE + 0x024)
#define FLASH_PCROP1AER                 (G0_FLASH_BASE + 0x028)
#define FLASH_WRP1AR                    (G0_FLASH_BASE + 0x02C)
#define FLASH_WRP1BR                    (G0_FLASH_BASE + 0x030)
#define FLASH_PCROP1BSR                 (G0_FLASH_BASE + 0x034)
#define FLASH_PCROP1BER                 (G0_FLASH_BASE + 0x038)
#define FLASH_PCROP2ASR                 (G0_FLASH_BASE + 0x044)
#define FLASH_PCROP2AER                 (G0_FLASH_BASE + 0x048)
#define FLASH_WRP2AR                    (G0_FLASH_BASE + 0x04C)
#define FLASH_WRP2BR                    (G0_FLASH_BASE + 0x050)
#define FLASH_PCROP2BSR                 (G0_FLASH_BASE + 0x054)
#define FLASH_PCROP2BER                 (G0_FLASH_BASE + 0x058)
#define FLASH_SECR                      (G0_FLASH_BASE + 0x080)

/* RAM */
#define RAM_START                       0x20000000
#define RAM_SIZE_G03_4                  (8U * 1024U)    // 8 kiB
#define RAM_SIZE_G05_6                  (18U * 1024U)   // 18 kiB
#define RAM_SIZE_G07_8                  (36U * 1024U)   // 36 kiB
#define RAM_SIZE_G0B_C                  (144U * 1024U)  // 144 kiB

/* RCC */
#define G0_RCC_BASE                     0x40021000
#define RCC_APBENR1                     (G0_RCC_BASE + 0x3C)
#define RCC_APBENR1_DBGEN               (1U << 27U)

/* DBG */
#define DBG_BASE                        0x40015800
#define DBG_CR                          (DBG_BASE + 0x04)
#define DBG_CR_DBG_STANDBY              (1U << 2U)
#define DBG_CR_DBG_STOP                 (1U << 1U)
#define DBG_APB_FZ1                     (DBG_BASE + 0x08)
#define DBG_APB_FZ1_DBG_IWDG_STOP       (1U << 12U)
#define DBG_APB_FZ1_DBG_WWDG_STOP       (1U << 11U)

enum STM32G0_DEV_ID {
	STM32G03_4 = 0x466,
	STM32G05_6 = 0x456,
	STM32G07_8 = 0x460,
	STM32G0B_C = 0x467
};
struct stm32g0_saved_regs_s {
	uint32_t rcc_apbenr1;
	uint32_t dbg_cr;
	uint32_t dbg_apb_fz1;
};
struct stm32g0_priv_s {
	struct stm32g0_saved_regs_s saved_regs;
	bool irreversible_enabled;
};

static bool stm32g0_attach(target *t);
static void stm32g0_detach(target *t);
static int stm32g0_flash_erase(struct target_flash *f, target_addr addr,
                               size_t len);
static int stm32g0_flash_write(struct target_flash *f, target_addr dest,
                               const void *src, size_t len);

/* Custom commands */
static bool stm32g0_cmd_erase_mass(target *t, int argc, const char **argv);
static bool stm32g0_cmd_option(target *t, int argc, const char **argv);
static bool stm32g0_cmd_irreversible(target *t, int argc, const char **argv);

const struct command_s stm32g0_cmd_list[] = {
	{ "erase_mass [1|2]", (cmd_handler)stm32g0_cmd_erase_mass,
	  "Erase entire flash memory or specified bank" },
	{ "option", (cmd_handler)stm32g0_cmd_option,
	  "Manipulate option bytes" },
	{ "irreversible", (cmd_handler)stm32g0_cmd_irreversible,
	  "Allow irreversible operations: (enable|disable)" },
	{ NULL, NULL, NULL }
};

static void stm32g0_add_flash(target *t, uint32_t addr, size_t length,
                              size_t blocksize)
{
	struct target_flash *f = calloc(1, sizeof(*f));
	if (!f) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = addr;
	f->length = length;
	f->blocksize = blocksize;
	f->erase = stm32g0_flash_erase;
	f->write = stm32g0_flash_write;
	f->buf_size = blocksize;
	f->erased = 0xFF;
	target_add_flash(t, f);
}

/*
 * Probe for a known STM32G0 MCU.
 * Populate the memory map and add custom commands.
 * Single bank devices are populated with their maximal flash capacity to allow
 * users to program devices with more flash than announced.
 */
bool stm32g0_probe(target *t)
{
	uint32_t ram_size = 0U;
	size_t flash_size = 0U;

	target_mem_map_free(t);

	switch (t->idcode) {
	case STM32G03_4:
		/* SRAM 8 kiB, Flash up to 64 kiB */
		ram_size = (uint32_t)RAM_SIZE_G03_4;
		flash_size = (uint32_t)FLASH_SIZE_MAX_G03_4;
		t->driver = "STM32G03/4";
		break;
	case STM32G05_6:
		/* SRAM 18 kiB, Flash up to 64 kiB */
		ram_size = (uint32_t)RAM_SIZE_G05_6;
		flash_size = (uint32_t)FLASH_SIZE_MAX_G05_6;
		t->driver = "STM32G05/6";
		break;
	case STM32G07_8:
		/* SRAM 36 kiB, Flash up to 128 kiB */
		ram_size = (uint32_t)RAM_SIZE_G07_8;
		flash_size = (uint32_t)FLASH_SIZE_MAX_G07_8;
		t->driver = "STM32G07/8";
		break;
	case STM32G0B_C:
		/* SRAM 144 kiB, Flash up to 512 kiB */
		ram_size = (uint32_t)RAM_SIZE_G0B_C;
		flash_size = (size_t)target_mem_read16(t, FLASH_MEMORY_SIZE);
		flash_size *= 1024U;
		t->driver = "STM32G0B/C";
		break;
	default:
		return false;
	}
	target_add_ram(t, RAM_START, ram_size);
	/* Dual banks: contiguous in memory */
	stm32g0_add_flash(t, FLASH_START, flash_size, FLASH_PAGE_SIZE);

	t->attach = stm32g0_attach;
	t->detach = stm32g0_detach;
	target_add_commands(t, stm32g0_cmd_list, t->driver);

	/* Save private storage */
	struct stm32g0_priv_s *priv_storage = calloc(1, sizeof(*priv_storage));
	priv_storage->irreversible_enabled = false;
	t->target_storage = (void*)priv_storage;

	/* OTP Flash area */
	stm32g0_add_flash(t, FLASH_OTP_START, FLASH_OTP_SIZE, FLASH_OTP_BLOCKSIZE);

	return true;
}

/*
 * In addition to attaching the debug core with cortexm_attach(), this function
 * keeps the FCLK and HCLK clocks running in Standby and Stop modes while
 * debugging.
 * The watchdogs (IWDG and WWDG) are stopped when the core is halted. This
 * allows basic Flash operations (erase/write) if the watchdog is started by
 * hardware or by a previous program without prior power cycle.
 */
static bool stm32g0_attach(target *t)
{
	struct stm32g0_priv_s *ps = (struct stm32g0_priv_s*)t->target_storage;

	if (!cortexm_attach(t))
		return false;

	ps->saved_regs.rcc_apbenr1 = target_mem_read32(t, RCC_APBENR1);
	target_mem_write32(t, RCC_APBENR1, ps->saved_regs.rcc_apbenr1 |
	                   RCC_APBENR1_DBGEN);
	ps->saved_regs.dbg_cr = target_mem_read32(t, DBG_CR);
	target_mem_write32(t, DBG_CR, ps->saved_regs.dbg_cr |
	                   (DBG_CR_DBG_STANDBY | DBG_CR_DBG_STOP));
	ps->saved_regs.dbg_apb_fz1 = target_mem_read32(t, DBG_APB_FZ1);
	target_mem_write32(t, DBG_APB_FZ1, ps->saved_regs.dbg_apb_fz1 |
	                   (DBG_APB_FZ1_DBG_IWDG_STOP | DBG_APB_FZ1_DBG_WWDG_STOP));

	return true;
}

/*
 * Restore the modified registers and detach the debug core.
 * The registers are restored as is to leave the target in the same state as
 * before attachment.
 */
static void stm32g0_detach(target *t)
{
	struct stm32g0_priv_s *ps = (struct stm32g0_priv_s*)t->target_storage;

	target_mem_write32(t, DBG_APB_FZ1, ps->saved_regs.dbg_apb_fz1);
	target_mem_write32(t, DBG_CR, ps->saved_regs.dbg_cr);
	target_mem_write32(t, RCC_APBENR1, ps->saved_regs.rcc_apbenr1);

	cortexm_detach(t);
}

static void stm32g0_flash_unlock(target *t)
{
	target_mem_write32(t, FLASH_KEYR, FLASH_KEYR_KEY1);
	target_mem_write32(t, FLASH_KEYR, FLASH_KEYR_KEY2);
}

static void stm32g0_flash_lock(target *t)
{
	uint32_t flash_cr = target_mem_read32(t, FLASH_CR);
	flash_cr |= (uint32_t)FLASH_CR_LOCK;
	target_mem_write32(t, FLASH_CR, flash_cr);
}

/*
 * Flash erasure function.
 * OTP case: this function clears any previous error and returns.
 */
static int stm32g0_flash_erase(struct target_flash *f, target_addr addr,
                               size_t len)
{
	target *t = f->t;
	target_addr end = addr + len - 1U;
	uint16_t page_nb = 0U;
	uint16_t nb_pages_to_erase = 0U;
	uint16_t bank1_end_page_nb = FLASH_BANK2_START_PAGE_NB - 1U; // Max
	bool on_bank2 = false;
	int ret = 0;

	if (end > (f->start + f->length - 1U))
		goto exit_error;
	if (len == (size_t)0U)
		goto exit_cleanup;

	/* Wait for Flash ready */
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY_MASK) {
		if (target_check_error(t))
			goto exit_error;
	}

	/* Clear any previous programming error */
	target_mem_write32(t, FLASH_SR, target_mem_read32(t, FLASH_SR));

	if (addr >= (target_addr)FLASH_OTP_START)
		goto exit_cleanup;

	nb_pages_to_erase = (uint16_t)((len - 1U) / f->blocksize) + 1U;
	if (t->idcode == STM32G0B_C) // Dual-bank devices
		bank1_end_page_nb = ((f->length / 2U) - 1U) / f->blocksize;
	page_nb = (uint16_t)((addr - f->start) / f->blocksize);

	stm32g0_flash_unlock(t);

	do {
		if (!on_bank2 && (page_nb > bank1_end_page_nb)) {
			/* Jump on bank 2 */
			on_bank2 = true;
			page_nb = FLASH_BANK2_START_PAGE_NB;
		}
		/* Erase */
		uint32_t flash_cr = (uint32_t)((page_nb << FLASH_CR_PNB_SHIFT) |
		                               FLASH_CR_PER);
		if (on_bank2)
			flash_cr |= (uint32_t)(FLASH_CR_BKER);

		target_mem_write32(t, FLASH_CR, flash_cr);

		flash_cr |= (uint32_t)FLASH_CR_STRT;
		target_mem_write32(t, FLASH_CR, flash_cr);

		while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY_MASK) {
			if (target_check_error(t))
				goto exit_error;
		}

		page_nb++;
		nb_pages_to_erase--;
	} while (nb_pages_to_erase > 0U);

	/* Check for error */
	uint32_t flash_sr = target_mem_read32(t, FLASH_SR);
	if (flash_sr & FLASH_SR_ERROR_MASK) {
		DEBUG_WARN("stm32g0 flash erase error: sr 0x%" PRIx32 "\n",
		           flash_sr);
		goto exit_error;
	}
	goto exit_cleanup;

exit_error:
	ret = -1;
exit_cleanup:
	target_mem_write32(t, FLASH_SR, (uint32_t)FLASH_SR_EOP); // Clear EOP
	stm32g0_flash_lock(t);
	return ret;
}

/*
 * Flash programming function.
 * The SR is supposed to be ready and free of any error.
 * After a successful programming, the EMPTY bit is cleared to allow rebooting
 * in Main Flash memory without power cycle.
 * OTP area is programmed as the "program" area. It can be programmed 8-bytes
 * by 8-bytes.
 */
static int stm32g0_flash_write(struct target_flash *f, target_addr dest,
                               const void *src, size_t len)
{
	target *t = f->t;
	int ret = 0;
	struct stm32g0_priv_s *ps = (struct stm32g0_priv_s*)t->target_storage;

	if ((dest >= (target_addr)FLASH_OTP_START) && !ps->irreversible_enabled) {
		tc_printf(t, "Irreversible operations disabled\n");
		goto exit_error;
	}

	stm32g0_flash_unlock(t);

	target_mem_write32(t, FLASH_CR, FLASH_CR_PG);
	target_mem_write(t, dest, src, len);
	/* Wait for completion or an error */
	uint32_t flash_sr;
	do {
		flash_sr = target_mem_read32(t, FLASH_SR);
		if (target_check_error(t)) {
			DEBUG_WARN("stm32g0 flash write: comm error\n");
			goto exit_error;
		}
	} while (flash_sr & FLASH_SR_BSY_MASK);

	if (flash_sr & FLASH_SR_ERROR_MASK) {
		DEBUG_WARN("stm32g0 flash write error: sr 0x%" PRIx32 "\n",
		           flash_sr);
		goto exit_error;
	}
	if ((dest == (target_addr)FLASH_START) &&
	    target_mem_read32(t, FLASH_START) != 0xFFFFFFFF) {
		uint32_t flash_acr = target_mem_read32(t, FLASH_ACR);
		flash_acr &= ~(uint32_t)FLASH_ACR_EMPTY;
		target_mem_write32(t, FLASH_ACR, flash_acr);
	}
	goto exit_cleanup;

exit_error:
	ret = -1;
exit_cleanup:
	target_mem_write32(t, FLASH_SR, (uint32_t)FLASH_SR_EOP); // Clear EOP
	/* Clear PG: half-word access not to clear unwanted bits */
	target_mem_write16(t, FLASH_CR, (uint16_t)0x0);
	stm32g0_flash_lock(t);
	return ret;
}

/*******************
 * Custom commands
 *******************/

static bool stm32g0_cmd_erase_mass(target *t, int argc, const char **argv)
{
	uint32_t flash_cr = 0U;
	bool ret = true;

	if (argc == 2) {
		switch (argv[1][0]) {
		case '1':
			flash_cr = (uint32_t)FLASH_CR_MER1 | FLASH_CR_STRT;
			break;
		case '2':
			flash_cr = (uint32_t)FLASH_CR_MER2 | FLASH_CR_STRT;
			break;
		default:
			goto exit_error;
			break;
		}
	} else {
		flash_cr = (uint32_t)(FLASH_CR_MER1 | FLASH_CR_MER2 |
		                      FLASH_CR_STRT);
	}

	stm32g0_flash_unlock(t);

	target_mem_write32(t, FLASH_CR, flash_cr);

	/* Read FLASH_SR to poll for BSY bits */
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY_MASK) {
		if (target_check_error(t))
			goto exit_error;
	}

	/* Check for error */
	uint16_t flash_sr = target_mem_read32(t, FLASH_SR);
	if (flash_sr & FLASH_SR_ERROR_MASK)
		goto exit_error;
	goto exit_cleanup;

exit_error:
	ret = false;
exit_cleanup:
	stm32g0_flash_lock(t);
	return ret;
}

static void stm32g0_flash_option_unlock(target *t)
{
	target_mem_write32(t, FLASH_OPTKEYR, FLASH_OPTKEYR_KEY1);
	target_mem_write32(t, FLASH_OPTKEYR, FLASH_OPTKEYR_KEY2);
}

enum option_bytes_registers {
	OPTR_ENUM = 0,
	PCROP1ASR_ENUM,
	PCROP1AER_ENUM,
	WRP1AR_ENUM,
	WRP1BR_ENUM,
	PCROP1BSR_ENUM,
	PCROP1BER_ENUM,
	PCROP2ASR_ENUM,
	PCROP2AER_ENUM,
	WRP2AR_ENUM,
	WRP2BR_ENUM,
	PCROP2BSR_ENUM,
	PCROP2BER_ENUM,
	SECR_ENUM,

	NB_REG_OPT
};

struct registers_s {
	uint32_t addr;
	uint32_t val;
};

/*
 * G0x1: OPTR = FFFFFEAA
 * 1111 1111 1111 1111 1111 1110 1010 1010
 * G0x0: OPTR = DFFFE1AA
 * 1101 1111 1111 1111 1110 0001 1010 1010
 *   *IRHEN               * ****BOREN
 * IRH and BOR are reserved on G0x0, it is safe to apply G0x1 options.
 * The same for PCROP and SECR.
 */
static const struct registers_s options_def[NB_REG_OPT] = {
	[OPTR_ENUM]           = { FLASH_OPTR,          0xFFFFFEAA },
	[PCROP1ASR_ENUM]      = { FLASH_PCROP1ASR,     0xFFFFFFFF },
	[PCROP1AER_ENUM]      = { FLASH_PCROP1AER,     0x00000000 },
	[WRP1AR_ENUM]         = { FLASH_WRP1AR,        0x000000FF },
	[WRP1BR_ENUM]         = { FLASH_WRP1BR,        0x000000FF },
	[PCROP1BSR_ENUM]      = { FLASH_PCROP1BSR,     0xFFFFFFFF },
	[PCROP1BER_ENUM]      = { FLASH_PCROP1BER,     0x00000000 },
	[PCROP2ASR_ENUM]      = { FLASH_PCROP2ASR,     0xFFFFFFFF },
	[PCROP2AER_ENUM]      = { FLASH_PCROP2AER,     0x00000000 },
	[WRP2AR_ENUM]         = { FLASH_WRP2AR,        0x000000FF },
	[WRP2BR_ENUM]         = { FLASH_WRP2BR,        0x000000FF },
	[PCROP2BSR_ENUM]      = { FLASH_PCROP2BSR,     0xFFFFFFFF },
	[PCROP2BER_ENUM]      = { FLASH_PCROP2BER,     0x00000000 },
	[SECR_ENUM]           = { FLASH_SECR,          0x00000000 }
};

static void write_registers(target *t, const struct registers_s *regs,
                            uint8_t nb_regs)
{
	for (uint8_t i = 0U; i < nb_regs; i++) {
		if (regs[i].addr > 0U)
			target_mem_write32(t, regs[i].addr, regs[i].val);
	}
}

/*
 * Option bytes programming.
 */
static bool stm32g0_option_write(target *t,
                                 const struct registers_s *options_req)
{
	stm32g0_flash_unlock(t);
	stm32g0_flash_option_unlock(t);

	/* Wait for Flash ready */
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY_MASK) {
		if (target_check_error(t))
			goto exit_error;
	}

	write_registers(t, options_req, NB_REG_OPT);

	target_mem_write32(t, FLASH_CR, FLASH_CR_OPTSTRT);
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY_MASK) {
		if (target_check_error(t))
			goto exit_error;
	}

	/* Option bytes loading generates a system reset */
	target_mem_write32(t, FLASH_CR, FLASH_CR_OBL_LAUNCH);
	tc_printf(t, "Scan and attach again\n");
	return true;

exit_error:
	stm32g0_flash_lock(t); // Also locks option bytes
	return false;
}

/*
 * This fonction adds a register given on the command line to a table.
 * This table is further written to the target.
 * The register is added only if its address is valid.
 */
static bool add_reg_value(struct registers_s *reg_req,
                          const struct registers_s *reg_def,
                          uint8_t reg_def_len, uint32_t addr, uint32_t val)
{
	for (uint8_t j = 0U; j < reg_def_len; j++) {
		if (addr == reg_def[j].addr) {
			reg_req[j].addr = addr;
			reg_req[j].val = val;
			return true;
		}
	}
	return false;
}

/*
 * Parse (address, value) register pairs given on the command line.
 */
static bool parse_cmdline_registers(int args_nb, const char **reg_str,
                                    struct registers_s *reg_req,
                                    const struct registers_s *reg_def,
                                    uint8_t reg_def_len)
{
	uint32_t addr = 0U;
	uint32_t val = 0U;
	uint8_t valid_regs_nb = 0U;

	for (uint8_t i = 0U; i < args_nb; i += 2U) {
		addr = strtoul(reg_str[i], NULL, 0);
		val = strtoul(reg_str[i + 1], NULL, 0);
		if (add_reg_value(reg_req, reg_def, reg_def_len, addr, val))
			valid_regs_nb++;
	}

	if (valid_regs_nb > 0U)
		return true;
	else
		return false;
}

/*
 * Validates option bytes.
 * Prevents RDP level 2 request if not explicitly allowed.
 */
static bool validate_options(target *t, const struct registers_s *options_req)
{
	struct stm32g0_priv_s *ps = (struct stm32g0_priv_s*)t->target_storage;

	if (((options_req[OPTR_ENUM].val & FLASH_OPTR_RDP_MASK) ==
	     (uint32_t)0xCC) &&
	    !ps->irreversible_enabled) {
		tc_printf(t, "Irreversible operations disabled\n");
		return false;
	}
	return true;
}

static void display_registers(target *t, const struct registers_s *reg_def,
                              uint8_t len)
{
	uint32_t val = 0U;

	for (uint8_t i = 0U; i < len; i++) {
		val = target_mem_read32(t, reg_def[i].addr);
		tc_printf(t, "0x%08X: 0x%08X\n", reg_def[i].addr, val);
	}
}

/*
 * Option bytes manipulating.
 * Erasure has to be done in two steps if Proprietary Code Read Out Protection
 * is active:
 * Step 1: increase RDP level to 1 and set PCROP_RDP if not already the case;
 * Step 2: reset to defaults.
 */
static bool stm32g0_cmd_option(target *t, int argc, const char **argv)
{
	struct registers_s options_req[NB_REG_OPT] = { { 0U, 0U } };

	if ((argc == 2) && !strcmp(argv[1], "erase")) {
		if (!stm32g0_option_write(t, options_def))
			goto exit_error;
	} else if ((argc > 2) && (argc % 2U == 0U) &&
	           !strcmp(argv[1], "write")) {
		if (!parse_cmdline_registers(argc - 2, argv + 2, options_req,
		                             options_def, NB_REG_OPT))
			goto exit_error;
		if (!validate_options(t, options_req))
			goto exit_error;
		if (!stm32g0_option_write(t, options_req))
			goto exit_error;
	} else {
		tc_printf(t, "usage: monitor option erase\n");
		tc_printf(t, "usage: monitor option write <addr> <val> [<addr> <val>]...\n");
		display_registers(t, options_def, NB_REG_OPT);
	}
	return true;

exit_error:
	tc_printf(t, "Writing options failed!\n");
	return false;
}

/*
 * Enables irreversible operations:
 * RDP level 2 read protection.
 */
static bool stm32g0_cmd_irreversible(target *t, int argc, const char **argv)
{
	struct stm32g0_priv_s *ps = (struct stm32g0_priv_s*)t->target_storage;
	bool ret = true;

	if (argc == 2) {
		if (!parse_enable_or_disable(argv[1],
		                   &(ps->irreversible_enabled)))
			ret = false;
	}
	tc_printf(t, "Irreversible operations: %s\n",
	          ps->irreversible_enabled ? "enabled" : "disabled");
	return ret;
}
