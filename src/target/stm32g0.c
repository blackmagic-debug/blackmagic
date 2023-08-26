/*
 * This file is part of the Black Magic Debug project.
 *
 * MIT License
 *
 * Copyright (c) 2021-2023 Fabrice Prost-Boucle <fabalthazar@falbalab.fr>
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
 * RM0490 - Rev 3
 *   Reference manual - STM32C0x1 advanced ARM(R)-based 32-bit MCUs
 *                      (STM32C011/STM32C031)
 * STM32C0 shares the same technological platform as STM32G0.
 * PM0223 - Rev 6
 *   Programming manual - Cortex(R)-M0+ programming manual for STM32L0, STM32G0,
 *                        STM32C0, STM32WL and STM32WB Series
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "command.h"

/* Flash */
#define FLASH_START            0x08000000U
#define FLASH_MEMORY_SIZE      0x1fff75e0U
#define FLASH_PAGE_SIZE        0x800U
#define FLASH_BANK2_START_PAGE 256U
#define FLASH_OTP_START        0x1fff7000U
#define FLASH_OTP_SIZE         0x400U
#define FLASH_OTP_BLOCKSIZE    0x8U
#define FLASH_SIZE_MAX_G03_4   (64U * 1024U)  // 64kiB
#define FLASH_SIZE_MAX_G05_6   (64U * 1024U)  // 64kiB
#define FLASH_SIZE_MAX_G07_8   (128U * 1024U) // 128kiB
#define FLASH_SIZE_MAX_G0B_C   (512U * 1024U) // 512kiB

#define FLASH_SIZE_MAX_C01 (32U * 1024U) // 32kiB
#define FLASH_SIZE_MAX_C03 (32U * 1024U) // 32kiB

#define G0_FLASH_BASE   0x40022000U
#define FLASH_ACR       (G0_FLASH_BASE + 0x000U)
#define FLASH_ACR_EMPTY (1U << 16U)

#define FLASH_KEYR          (G0_FLASH_BASE + 0x008U)
#define FLASH_KEYR_KEY1     0x45670123U
#define FLASH_KEYR_KEY2     0xcdef89abU
#define FLASH_CR            (G0_FLASH_BASE + 0x014U)
#define FLASH_CR_LOCK       (1U << 31U)
#define FLASH_CR_OBL_LAUNCH (1U << 27U)
#define FLASH_CR_OPTSTART   (1U << 17U)
#define FLASH_CR_START      (1U << 16U)
#define FLASH_CR_MER2       (1U << 15U)
#define FLASH_CR_MER1       (1U << 2U)
#define FLASH_CR_BKER       (1U << 13U)
#define FLASH_CR_PNB_SHIFT  3U
#define FLASH_CR_PER        (1U << 1U)
#define FLASH_CR_PG         (1U << 0U)

#define FLASH_SR         (G0_FLASH_BASE + 0x010U)
#define FLASH_SR_BSY2    (1U << 17U)
#define FLASH_SR_BSY1    (1U << 16U)
#define FLASH_SR_OPTVERR (1U << 15U)
#define FLASH_SR_RDERR   (1U << 14U)
#define FLASH_SR_FASTERR (1U << 9U)
#define FLASH_SR_MISSERR (1U << 8U)
#define FLASH_SR_PGSERR  (1U << 7U)
#define FLASH_SR_SIZERR  (1U << 6U)
#define FLASH_SR_PGAERR  (1U << 5U)
#define FLASH_SR_WRPERR  (1U << 4U)
#define FLASH_SR_PROGERR (1U << 3U)
#define FLASH_SR_OPERR   (1U << 1U)
#define FLASH_SR_EOP     (1U << 0U)
#define FLASH_SR_ERROR_MASK                                                                                        \
	(FLASH_SR_OPTVERR | FLASH_SR_RDERR | FLASH_SR_FASTERR | FLASH_SR_MISSERR | FLASH_SR_PGSERR | FLASH_SR_SIZERR | \
		FLASH_SR_PGAERR | FLASH_SR_WRPERR | FLASH_SR_PROGERR | FLASH_SR_OPERR)
#define FLASH_SR_BSY_MASK (FLASH_SR_BSY2 | FLASH_SR_BSY1)

#define FLASH_OPTKEYR       (G0_FLASH_BASE + 0x00cU)
#define FLASH_OPTKEYR_KEY1  0x08192a3bU
#define FLASH_OPTKEYR_KEY2  0x4c5d6e7fU
#define FLASH_OPTR          (G0_FLASH_BASE + 0x020U)
#define FLASH_OPTR_RDP_MASK 0xffU
#define FLASH_OPTR_G0x1_DEF 0xfffffeaaU
#define FLASH_OPTR_C0x1_DEF 0xffffffaaU
#define FLASH_PCROP1ASR     (G0_FLASH_BASE + 0x024U)
#define FLASH_PCROP1AER     (G0_FLASH_BASE + 0x028U)
#define FLASH_WRP1AR        (G0_FLASH_BASE + 0x02cU)
#define FLASH_WRP1BR        (G0_FLASH_BASE + 0x030U)
#define FLASH_PCROP1BSR     (G0_FLASH_BASE + 0x034U)
#define FLASH_PCROP1BER     (G0_FLASH_BASE + 0x038U)
#define FLASH_PCROP2ASR     (G0_FLASH_BASE + 0x044U)
#define FLASH_PCROP2AER     (G0_FLASH_BASE + 0x048U)
#define FLASH_WRP2AR        (G0_FLASH_BASE + 0x04cU)
#define FLASH_WRP2BR        (G0_FLASH_BASE + 0x050U)
#define FLASH_PCROP2BSR     (G0_FLASH_BASE + 0x054U)
#define FLASH_PCROP2BER     (G0_FLASH_BASE + 0x058U)
#define FLASH_SECR          (G0_FLASH_BASE + 0x080U)

/* RAM */
#define RAM_START      0x20000000U
#define RAM_SIZE_G03_4 (8U * 1024U)   // 8kiB
#define RAM_SIZE_G05_6 (18U * 1024U)  // 18kiB
#define RAM_SIZE_G07_8 (36U * 1024U)  // 36kiB
#define RAM_SIZE_G0B_C (144U * 1024U) // 144kiB

#define RAM_SIZE_C01 (6U * 1024U)  // 6kiB
#define RAM_SIZE_C03 (12U * 1024U) // 12kiB

/* RCC */
#define G0_RCC_BASE       0x40021000U
#define RCC_APBENR1       (G0_RCC_BASE + 0x3cU)
#define RCC_APBENR1_DBGEN (1U << 27U)

/* DBG */
#define DBG_BASE                  0x40015800U
#define DBG_IDCODE                (DBG_BASE + 0x00U)
#define DBG_CR                    (DBG_BASE + 0x04U)
#define DBG_CR_DBG_STANDBY        (1U << 2U)
#define DBG_CR_DBG_STOP           (1U << 1U)
#define DBG_APB_FZ1               (DBG_BASE + 0x08U)
#define DBG_APB_FZ1_DBG_IWDG_STOP (1U << 12U)
#define DBG_APB_FZ1_DBG_WWDG_STOP (1U << 11U)

/*
 * The underscores in these definitions represent /'s, this means
 * that STM32G03_4 is supposed to refer to the G03/4 aka the G03 and G04.
 */
#define STM32C011  0x443U
#define STM32C031  0x453U
#define STM32G03_4 0x466U
#define STM32G05_6 0x456U
#define STM32G07_8 0x460U
#define STM32G0B_C 0x467U

typedef struct stm32g0_saved_regs {
	uint32_t rcc_apbenr1;
	uint32_t dbg_cr;
	uint32_t dbg_apb_fz1;
} stm32g0_saved_regs_s;

typedef struct stm32g0_priv {
	stm32g0_saved_regs_s saved_regs;
	bool irreversible_enabled;
} stm32g0_priv_s;

static bool stm32g0_attach(target_s *t);
static void stm32g0_detach(target_s *t);
static bool stm32g0_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool stm32g0_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len);
static bool stm32g0_mass_erase(target_s *t);

/* Custom commands */
static bool stm32g0_cmd_erase_bank(target_s *t, int argc, const char **argv);
static bool stm32g0_cmd_option(target_s *t, int argc, const char **argv);
static bool stm32g0_cmd_irreversible(target_s *t, int argc, const char **argv);

const command_s stm32g0_cmd_list[] = {
	{"erase_bank 1|2", stm32g0_cmd_erase_bank, "Erase specified Flash bank"},
	{"option", stm32g0_cmd_option, "Manipulate option bytes"},
	{"irreversible", stm32g0_cmd_irreversible, "Allow irreversible operations: (enable|disable)"},
	{NULL, NULL, NULL},
};

static void stm32g0_add_flash(target_s *t, uint32_t addr, size_t length, size_t blocksize)
{
	target_flash_s *f = calloc(1, sizeof(*f));
	if (!f) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = addr;
	f->length = length;
	f->blocksize = blocksize;
	f->erase = stm32g0_flash_erase;
	f->write = stm32g0_flash_write;
	f->writesize = blocksize;
	f->erased = 0xffU;
	target_add_flash(t, f);
}

/*
 * Probe for a known STM32G0 series part.
 * Populate the memory map and add custom commands.
 * Single bank devices are populated with their maximal flash capacity to allow
 * users to program devices with more flash than announced.
 */
bool stm32g0_probe(target_s *t)
{
	uint32_t ram_size = 0U;
	size_t flash_size = 0U;

	switch (t->part_id) {
	case STM32G03_4:;
		const uint16_t dev_id = target_mem_read32(t, DBG_IDCODE) & 0xfffU;
		switch (dev_id) {
		case STM32G03_4:
			/* SRAM 8kiB, Flash up to 64kiB */
			ram_size = RAM_SIZE_G03_4;
			flash_size = FLASH_SIZE_MAX_G03_4;
			t->driver = "STM32G03/4";
			break;
		case STM32C011:
			/* SRAM 6kiB, Flash up to 32kiB */
			ram_size = RAM_SIZE_C01;
			flash_size = FLASH_SIZE_MAX_C01;
			t->driver = "STM32C011";
			break;
		case STM32C031:
			/* SRAM 12kiB, Flash up to 32kiB */
			ram_size = RAM_SIZE_C03;
			flash_size = FLASH_SIZE_MAX_C03;
			t->driver = "STM32C031";
			break;
		default:
			return false;
		}
		t->part_id = dev_id;
		break;
	case STM32G05_6:
		/* SRAM 18kiB, Flash up to 64kiB */
		ram_size = RAM_SIZE_G05_6;
		flash_size = FLASH_SIZE_MAX_G05_6;
		t->driver = "STM32G05/6";
		break;
	case STM32G07_8:
		/* SRAM 36kiB, Flash up to 128kiB */
		ram_size = RAM_SIZE_G07_8;
		flash_size = FLASH_SIZE_MAX_G07_8;
		t->driver = "STM32G07/8";
		break;
	case STM32G0B_C:
		/* SRAM 144kiB, Flash up to 512kiB */
		ram_size = RAM_SIZE_G0B_C;
		flash_size = target_mem_read16(t, FLASH_MEMORY_SIZE) * 1024U;
		t->driver = "STM32G0B/C";
		break;
	default:
		return false;
	}

	target_add_ram(t, RAM_START, ram_size);
	/* Even dual Flash bank devices have a contiguous Flash memory space */
	stm32g0_add_flash(t, FLASH_START, flash_size, FLASH_PAGE_SIZE);

	t->attach = stm32g0_attach;
	t->detach = stm32g0_detach;
	t->mass_erase = stm32g0_mass_erase;
	target_add_commands(t, stm32g0_cmd_list, t->driver);

	/* Save private storage */
	stm32g0_priv_s *priv_storage = calloc(1, sizeof(*priv_storage));
	if (!priv_storage) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	t->target_storage = priv_storage;
	priv_storage->irreversible_enabled = false;

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
static bool stm32g0_attach(target_s *t)
{
	stm32g0_priv_s *ps = (stm32g0_priv_s *)t->target_storage;

	if (!cortexm_attach(t))
		return false;

	ps->saved_regs.rcc_apbenr1 = target_mem_read32(t, RCC_APBENR1);
	target_mem_write32(t, RCC_APBENR1, ps->saved_regs.rcc_apbenr1 | RCC_APBENR1_DBGEN);
	ps->saved_regs.dbg_cr = target_mem_read32(t, DBG_CR);
	target_mem_write32(t, DBG_CR, ps->saved_regs.dbg_cr | (DBG_CR_DBG_STANDBY | DBG_CR_DBG_STOP));
	ps->saved_regs.dbg_apb_fz1 = target_mem_read32(t, DBG_APB_FZ1);
	target_mem_write32(
		t, DBG_APB_FZ1, ps->saved_regs.dbg_apb_fz1 | (DBG_APB_FZ1_DBG_IWDG_STOP | DBG_APB_FZ1_DBG_WWDG_STOP));

	return true;
}

/*
 * Restore the modified registers and detach the debug core.
 * The registers are restored as is to leave the target in the same state as
 * before attachment.
 */
static void stm32g0_detach(target_s *t)
{
	stm32g0_priv_s *ps = (stm32g0_priv_s *)t->target_storage;

	/*
	 * First re-enable DBGEN clock, in case it got disabled in the meantime
	 * (happens during flash), so that writes to DBG_* registers below succeed.
	 */
	target_mem_write32(t, RCC_APBENR1, ps->saved_regs.rcc_apbenr1 | RCC_APBENR1_DBGEN);

	/* Then restore the DBG_* registers and clock settings. */
	target_mem_write32(t, DBG_APB_FZ1, ps->saved_regs.dbg_apb_fz1);
	target_mem_write32(t, DBG_CR, ps->saved_regs.dbg_cr);
	target_mem_write32(t, RCC_APBENR1, ps->saved_regs.rcc_apbenr1);

	cortexm_detach(t);
}

static void stm32g0_flash_unlock(target_s *t)
{
	target_mem_write32(t, FLASH_KEYR, FLASH_KEYR_KEY1);
	target_mem_write32(t, FLASH_KEYR, FLASH_KEYR_KEY2);
}

static void stm32g0_flash_lock(target_s *t)
{
	const uint32_t ctrl = target_mem_read32(t, FLASH_CR) | FLASH_CR_LOCK;
	target_mem_write32(t, FLASH_CR, ctrl);
}

static bool stm32g0_wait_busy(target_s *const t, platform_timeout_s *const timeout)
{
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY_MASK) {
		if (target_check_error(t))
			return false;
		if (timeout)
			target_print_progress(timeout);
	}
	return true;
}

static void stm32g0_flash_op_finish(target_s *t)
{
	target_mem_write32(t, FLASH_SR, FLASH_SR_EOP); // Clear EOP
	/* Clear PG: half-word access not to clear unwanted bits */
	target_mem_write16(t, FLASH_CR, 0);
	stm32g0_flash_lock(t);
}

static size_t stm32g0_bank1_end_page(target_flash_s *f)
{
	target_s *const t = f->t;
	/* If the part is dual banked, compute the end of the first bank */
	if (t->part_id == STM32G0B_C)
		return ((f->length / 2U) - 1U) / f->blocksize;
	/* Single banked devices have a fixed bank end */
	return FLASH_BANK2_START_PAGE - 1U;
}

/* Erase pages of Flash. In the OTP case, this function clears any previous error and returns. */
static bool stm32g0_flash_erase(target_flash_s *f, const target_addr_t addr, const size_t len)
{
	target_s *const t = f->t;

	/* Wait for Flash ready */
	if (!stm32g0_wait_busy(t, NULL)) {
		stm32g0_flash_op_finish(t);
		return false;
	}

	/* Clear any previous programming error */
	target_mem_write32(t, FLASH_SR, target_mem_read32(t, FLASH_SR));

	if (addr >= FLASH_OTP_START) {
		stm32g0_flash_op_finish(t);
		return true;
	}

	const size_t pages_to_erase = ((len - 1U) / f->blocksize) + 1U;
	const size_t bank1_end_page = stm32g0_bank1_end_page(f);
	uint32_t page = (addr - f->start) / f->blocksize;

	stm32g0_flash_unlock(t);

	for (size_t pages_erased = 0U; pages_erased < pages_to_erase; ++pages_erased, ++page) {
		/* If the page to erase is after the end of bank 1 but not yet in bank 2, skip */
		if (page < FLASH_BANK2_START_PAGE && page > bank1_end_page)
			page = FLASH_BANK2_START_PAGE;

		/* Erase the current page */
		const uint32_t ctrl =
			(page << FLASH_CR_PNB_SHIFT) | FLASH_CR_PER | (page >= FLASH_BANK2_START_PAGE ? FLASH_CR_BKER : 0);
		target_mem_write32(t, FLASH_CR, ctrl);
		target_mem_write32(t, FLASH_CR, ctrl | FLASH_CR_START);

		/* Wait for the operation to finish and report errors */
		if (!stm32g0_wait_busy(t, NULL)) {
			stm32g0_flash_op_finish(t);
			return false;
		}
	}

	/* Check for error */
	const uint32_t status = target_mem_read32(t, FLASH_SR);
	if (status & FLASH_SR_ERROR_MASK)
		DEBUG_ERROR("stm32g0 flash erase error: sr 0x%" PRIx32 "\n", status);
	stm32g0_flash_op_finish(t);
	return !(status & FLASH_SR_ERROR_MASK);
}

/*
 * Write data to erased Flash.
 * The status register is supposed to be ready and free of any error.
 * After successful programming, the EMPTY bit is cleared to allow rebooting
 * into the main Flash memory without power cycle.
 * OTP area is programmed as the "program" area. It can be programmed 8-bytes at a time.
 */
static bool stm32g0_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	target_s *const t = f->t;
	stm32g0_priv_s *ps = (stm32g0_priv_s *)t->target_storage;

	if (f->start == FLASH_OTP_START && !ps->irreversible_enabled) {
		tc_printf(t, "Irreversible operations disabled\n");
		stm32g0_flash_op_finish(t);
		return false;
	}

	stm32g0_flash_unlock(t);
	/* Write data to Flash */
	target_mem_write32(t, FLASH_CR, FLASH_CR_PG);
	target_mem_write(t, dest, src, len);
	/* Wait for completion or an error */
	if (!stm32g0_wait_busy(t, NULL)) {
		DEBUG_ERROR("stm32g0 flash write: comm error\n");
		stm32g0_flash_op_finish(t);
		return false;
	}

	const uint32_t status = target_mem_read32(t, FLASH_SR);
	if (status & FLASH_SR_ERROR_MASK) {
		DEBUG_ERROR("stm32g0 flash write error: sr 0x%" PRIx32 "\n", status);
		stm32g0_flash_op_finish(t);
		return false;
	}

	if (dest == FLASH_START && target_mem_read32(t, FLASH_START) != 0xffffffffU) {
		const uint32_t acr = target_mem_read32(t, FLASH_ACR) & ~FLASH_ACR_EMPTY;
		target_mem_write32(t, FLASH_ACR, acr);
	}

	stm32g0_flash_op_finish(t);
	return true;
}

static bool stm32g0_mass_erase(target_s *t)
{
	const uint32_t ctrl = FLASH_CR_MER1 | FLASH_CR_MER2 | FLASH_CR_START;

	stm32g0_flash_unlock(t);
	target_mem_write32(t, FLASH_CR, ctrl);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	/* Wait for completion or an error */
	if (!stm32g0_wait_busy(t, &timeout)) {
		stm32g0_flash_op_finish(t);
		return false;
	}

	/* Check for error */
	const uint16_t status = target_mem_read32(t, FLASH_SR);
	stm32g0_flash_op_finish(t);
	return !(status & FLASH_SR_ERROR_MASK);
}

static bool stm32g0_cmd_erase_bank(target_s *t, int argc, const char **argv)
{
	uint32_t ctrl = 0U;
	if (argc == 2) {
		switch (argv[1][0]) {
		case '1':
			ctrl = FLASH_CR_MER1 | FLASH_CR_START;
			break;
		case '2':
			ctrl = FLASH_CR_MER2 | FLASH_CR_START;
			break;
		}
	}

	if (!ctrl) {
		tc_printf(t, "Must specify which bank to erase\n");
		return false;
	}

	/* Erase the Flash bank requested */
	stm32g0_flash_unlock(t);
	target_mem_write32(t, FLASH_CR, ctrl);

	/* Wait for completion or an error */
	if (!stm32g0_wait_busy(t, NULL)) {
		stm32g0_flash_lock(t);
		return false;
	}

	/* Check for error */
	const uint16_t status = target_mem_read32(t, FLASH_SR);
	stm32g0_flash_op_finish(t);
	return !(status & FLASH_SR_ERROR_MASK);
}

static void stm32g0_flash_option_unlock(target_s *t)
{
	target_mem_write32(t, FLASH_OPTKEYR, FLASH_OPTKEYR_KEY1);
	target_mem_write32(t, FLASH_OPTKEYR, FLASH_OPTKEYR_KEY2);
}

typedef enum option_bytes_registers {
	OPT_REG_OPTR,
	OPT_REG_PCROP1ASR,
	OPT_REG_PCROP1AER,
	OPT_REG_WRP1AR,
	OPT_REG_WRP1BR,
	OPT_REG_PCROP1BSR,
	OPT_REG_PCROP1BER,
	OPT_REG_PCROP2ASR,
	OPT_REG_PCROP2AER,
	OPT_REG_WRP2AR,
	OPT_REG_WRP2BR,
	OPT_REG_PCROP2BSR,
	OPT_REG_PCROP2BER,
	OPT_REG_SECR,

	OPT_REG_COUNT
} option_bytes_registers_e;

typedef struct option_register {
	uint32_t addr;
	uint32_t val;
} option_register_s;

/*
 * G0x1: OPTR = FFFFFEAA
 * 1111 1111 1111 1111 1111 1110 1010 1010
 * G0x0: OPTR = DFFFE1AA
 * 1101 1111 1111 1111 1110 0001 1010 1010
 *   *IRHEN               * ****BOREN
 * C0x1: OPTR = FFFFFFAA
 * 1111 1111 1111 1111 1111 1111 1010 1010
 *                             *BOREN
 * IRH and BOR are reserved on G0x0, it is safe to apply G0x1 options on G0x0.
 * The same for PCROP and SECR.
 * This is not true for C0x1 which has BOREN set.
 */
static option_register_s options_def[OPT_REG_COUNT] = {
	[OPT_REG_OPTR] = {FLASH_OPTR, FLASH_OPTR_G0x1_DEF},
	[OPT_REG_PCROP1ASR] = {FLASH_PCROP1ASR, 0xffffffff},
	[OPT_REG_PCROP1AER] = {FLASH_PCROP1AER, 0x00000000},
	[OPT_REG_WRP1AR] = {FLASH_WRP1AR, 0x000000ff},
	[OPT_REG_WRP1BR] = {FLASH_WRP1BR, 0x000000ff},
	[OPT_REG_PCROP1BSR] = {FLASH_PCROP1BSR, 0xffffffff},
	[OPT_REG_PCROP1BER] = {FLASH_PCROP1BER, 0x00000000},
	[OPT_REG_PCROP2ASR] = {FLASH_PCROP2ASR, 0xffffffff},
	[OPT_REG_PCROP2AER] = {FLASH_PCROP2AER, 0x00000000},
	[OPT_REG_WRP2AR] = {FLASH_WRP2AR, 0x000000ff},
	[OPT_REG_WRP2BR] = {FLASH_WRP2BR, 0x000000ff},
	[OPT_REG_PCROP2BSR] = {FLASH_PCROP2BSR, 0xffffffff},
	[OPT_REG_PCROP2BER] = {FLASH_PCROP2BER, 0x00000000},
	[OPT_REG_SECR] = {FLASH_SECR, 0x00000000},
};

static void write_registers(target_s *const t, const option_register_s *const regs, const size_t nb_regs)
{
	for (size_t reg = 0U; reg < nb_regs; ++reg) {
		if (regs[reg].addr > 0U)
			target_mem_write32(t, regs[reg].addr, regs[reg].val);
	}
}

/* Program the option bytes. */
static bool stm32g0_option_write(target_s *const t, const option_register_s *const options_req)
{
	/* Unlock the option bytes Flash */
	stm32g0_flash_unlock(t);
	stm32g0_flash_option_unlock(t);

	/* Wait for completion or an error */
	if (!stm32g0_wait_busy(t, NULL))
		goto exit_error;

	/* Write the new option register values and begin the programming operation */
	write_registers(t, options_req, OPT_REG_COUNT);
	target_mem_write32(t, FLASH_CR, FLASH_CR_OPTSTART);

	/* Wait for completion or an error */
	if (!stm32g0_wait_busy(t, NULL))
		goto exit_error;

	/* Ask the device to reload its options bytes */
	target_mem_write32(t, FLASH_CR, FLASH_CR_OBL_LAUNCH);
	/* Option bytes loading generates a system reset */
	tc_printf(t, "Scan and attach again\n");
	return true;

exit_error:
	/* If we encounter any errors, relock the Flash */
	stm32g0_flash_op_finish(t);
	return false;
}

/*
 * This function adds a register given on the command line to a table.
 * This table is further written to the target.
 * The register is added only if its address is valid.
 */
static bool stm32g0_add_reg_value(option_register_s *const options_regs, const uint32_t addr, const uint32_t val)
{
	for (size_t reg = 0U; reg < OPT_REG_COUNT; ++reg) {
		if (options_def[reg].addr == addr) {
			options_regs[reg].addr = addr;
			options_regs[reg].val = val;
			return true;
		}
	}
	return false;
}

/* Parse (address, value) register pairs given on the command line. */
static bool stm32g0_parse_cmdline_registers(
	const uint32_t argc, const char *const *const argv, option_register_s *const options_regs)
{
	uint32_t valid_regs = 0U;

	for (uint32_t i = 0U; i < argc; i += 2U) {
		const uint32_t addr = strtoul(argv[i], NULL, 0);
		const uint32_t val = strtoul(argv[i + 1U], NULL, 0);
		if (stm32g0_add_reg_value(options_regs, addr, val))
			++valid_regs;
	}

	return valid_regs;
}

/* Validates option bytes settings. Only allow level 2 device protection if explicitly allowed. */
static bool stm32g0_validate_options(target_s *t, const option_register_s *options_req)
{
	stm32g0_priv_s *ps = (stm32g0_priv_s *)t->target_storage;
	const bool valid = (options_req[OPT_REG_OPTR].val & FLASH_OPTR_RDP_MASK) != 0xccU || ps->irreversible_enabled;
	if (!valid)
		tc_printf(t, "Irreversible operations disabled\n");
	return valid;
}

static void stm32g0_display_registers(target_s *t)
{
	for (size_t i = 0; i < OPT_REG_COUNT; ++i) {
		const uint32_t val = target_mem_read32(t, options_def[i].addr);
		tc_printf(t, "0x%08X: 0x%08X\n", options_def[i].addr, val);
	}
}

/*
 * Erasure has to be done in two steps if proprietary code read out protection is active:
 * 1. Increase device protection to level 1 and set PCROP_RDP if not already the case.
 * 2. Reset to defaults.
 */
static bool stm32g0_cmd_option(target_s *t, int argc, const char **argv)
{
	option_register_s options_req[OPT_REG_COUNT] = {{0}};

	if (argc == 2 && strcasecmp(argv[1], "erase") == 0) {
		if (t->part_id == STM32C011 || t->part_id == STM32C031)
			options_def[OPT_REG_OPTR].val = FLASH_OPTR_C0x1_DEF;
		if (!stm32g0_option_write(t, options_def))
			goto exit_error;
	} else if (argc > 2 && (argc & 1U) == 0U && strcasecmp(argv[1], "write") == 0) {
		if (!stm32g0_parse_cmdline_registers((uint32_t)argc - 2U, argv + 2U, options_req) ||
			!stm32g0_validate_options(t, options_req) || !stm32g0_option_write(t, options_req))
			goto exit_error;
	} else {
		tc_printf(t, "usage: monitor option erase\n");
		tc_printf(t, "usage: monitor option write <addr> <val> [<addr> <val>]...\n");
		stm32g0_display_registers(t);
	}
	return true;

exit_error:
	tc_printf(t, "Writing options failed!\n");
	return false;
}

/* Enables the irreversible operation that is level 2 device protection. */
static bool stm32g0_cmd_irreversible(target_s *t, int argc, const char **argv)
{
	stm32g0_priv_s *ps = (stm32g0_priv_s *)t->target_storage;
	const bool ret = argc != 2 || parse_enable_or_disable(argv[1], &ps->irreversible_enabled);
	tc_printf(t, "Irreversible operations: %s\n", ps->irreversible_enabled ? "enabled" : "disabled");
	return ret;
}
