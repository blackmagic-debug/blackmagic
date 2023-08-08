/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
 * Written by mean00 <fixounet@free.fr>
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
 * This file implements CH32F1xx target specific functions.
 * The ch32 flash is rather slow so this code is using the so called fast mode (ch32 specific).
 * 128 bytes are copied to a write buffer, then the write buffer is committed to flash
 * /!\ There is some sort of bus stall/bus arbitration going on that does NOT work when
 * programmed through SWD/jtag
 * The workaround is to wait a few cycles before filling the write buffer. This is performed by reading the flash a few times
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortex.h"

extern const command_s stm32f1_cmd_list[]; // Reuse stm32f1 stuff

static bool ch32f1_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool ch32f1_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len);

// These are common with stm32f1/gd32f1/...
#define FPEC_BASE     0x40022000U
#define FLASH_ACR     (FPEC_BASE + 0x00U)
#define FLASH_KEYR    (FPEC_BASE + 0x04U)
#define FLASH_SR      (FPEC_BASE + 0x0cU)
#define FLASH_CR      (FPEC_BASE + 0x10U)
#define FLASH_AR      (FPEC_BASE + 0x14U)
#define FLASH_CR_LOCK (1U << 7U)
#define FLASH_CR_STRT (1U << 6U)
#define FLASH_SR_BSY  (1U << 0U)
#define KEY1          0x45670123U
#define KEY2          0xcdef89abU
#define SR_ERROR_MASK 0x14U
#define SR_EOP        0x20U
#define DBGMCU_IDCODE 0xe0042000U
#define FLASHSIZE     0x1ffff7e0U

// These are specific to ch32f1
#define FLASH_MAGIC              (FPEC_BASE + 0x34U)
#define FLASH_MODEKEYR_CH32      (FPEC_BASE + 0x24U) // Fast mode for CH32F10x
#define FLASH_CR_FLOCK_CH32      (1U << 15U)         // Fast unlock
#define FLASH_CR_FTPG_CH32       (1U << 16U)         // Fast page program
#define FLASH_CR_FTER_CH32       (1U << 17U)         // Fast page erase
#define FLASH_CR_BUF_LOAD_CH32   (1U << 18U)         // Buffer load
#define FLASH_CR_BUF_RESET_CH32  (1U << 19U)         // Buffer reset
#define FLASH_SR_EOP             (1U << 5U)          // End of programming
#define FLASH_BEGIN_ADDRESS_CH32 0x8000000U

/* "fast" Flash driver for CH32F10x chips */
static void ch32f1_add_flash(target_s *t, uint32_t addr, size_t length, size_t erasesize)
{
	target_flash_s *f = calloc(1, sizeof(*f));
	if (!f) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = addr;
	f->length = length;
	f->blocksize = erasesize;
	f->erase = ch32f1_flash_erase;
	f->write = ch32f1_flash_write;
	f->writesize = erasesize;
	f->erased = 0xffU;
	target_add_flash(t, f);
}

#define WAIT_BUSY()                                          \
	do {                                                     \
		sr = target_mem_read32(t, FLASH_SR);                 \
		if (target_check_error(t)) {                         \
			DEBUG_ERROR("ch32f1 flash write: comm error\n"); \
			return -1;                                       \
		}                                                    \
	} while (sr & FLASH_SR_BSY);

#define WAIT_EOP()                                           \
	do {                                                     \
		sr = target_mem_read32(t, FLASH_SR);                 \
		if (target_check_error(t)) {                         \
			DEBUG_ERROR("ch32f1 flash write: comm error\n"); \
			return -1;                                       \
		}                                                    \
	} while (!(sr & FLASH_SR_EOP));

#define CLEAR_EOP() target_mem_write32(t, FLASH_SR, FLASH_SR_EOP)

#define SET_CR(bit)                                                 \
	do {                                                            \
		const uint32_t cr = target_mem_read32(t, FLASH_CR) | (bit); \
		target_mem_write32(t, FLASH_CR, cr);                        \
	} while (0)

#define CLEAR_CR(bit)                                                  \
	do {                                                               \
		const uint32_t cr = target_mem_read32(t, FLASH_CR) & (~(bit)); \
		target_mem_write32(t, FLASH_CR, cr);                           \
	} while (0)

// Which one is the right value?
#define MAGIC_WORD 0x100U
// #define MAGIC_WORD 0x1000U
#define MAGIC(addr)                                        \
	do {                                                   \
		magic = target_mem_read32(t, (addr) ^ MAGIC_WORD); \
		target_mem_write32(t, FLASH_MAGIC, magic);         \
	} while (0)

/* Attempt unlock ch32f103 in fast mode */
static bool ch32f1_flash_unlock(target_s *t)
{
	DEBUG_INFO("CH32: flash unlock \n");

	target_mem_write32(t, FLASH_KEYR, KEY1);
	target_mem_write32(t, FLASH_KEYR, KEY2);
	// fast mode
	target_mem_write32(t, FLASH_MODEKEYR_CH32, KEY1);
	target_mem_write32(t, FLASH_MODEKEYR_CH32, KEY2);
	uint32_t cr = target_mem_read32(t, FLASH_CR);
	if (cr & FLASH_CR_FLOCK_CH32)
		DEBUG_ERROR("Fast unlock failed, cr: 0x%08" PRIx32 "\n", cr);
	return !(cr & FLASH_CR_FLOCK_CH32);
}

/*
 * lock ch32f103 in fast mode
 */
static bool ch32f1_flash_lock(target_s *t)
{
	DEBUG_INFO("CH32: flash lock \n");
	/*
	 * The LOCK (bit 7) and FLOCK (bit 15) must be set (1) in the same write
	 * operation, if not FLOCK will be read back as unset (0).
	 */
	SET_CR(FLASH_CR_LOCK | FLASH_CR_FLOCK_CH32);
	const uint32_t cr = target_mem_read32(t, FLASH_CR);
	if (!(cr & FLASH_CR_FLOCK_CH32))
		DEBUG_ERROR("Fast lock failed, cr: 0x%08" PRIx32 "\n", cr);
	return cr & FLASH_CR_FLOCK_CH32;
}

/*
 *check fast_unlock is there, if so it is a CH32fx
 */
static bool ch32f1_has_fast_unlock(target_s *t)
{
	DEBUG_INFO("CH32: has fast unlock \n");
	// reset fast unlock
	SET_CR(FLASH_CR_FLOCK_CH32);
	platform_delay(1); // The flash controller is timing sensitive
	if (!(target_mem_read32(t, FLASH_CR) & FLASH_CR_FLOCK_CH32))
		return false;
	// send unlock sequence
	target_mem_write32(t, FLASH_KEYR, KEY1);
	target_mem_write32(t, FLASH_KEYR, KEY2);
	platform_delay(1); // The flash controller is timing sensitive
	// send fast unlock sequence
	target_mem_write32(t, FLASH_MODEKEYR_CH32, KEY1);
	target_mem_write32(t, FLASH_MODEKEYR_CH32, KEY2);
	platform_delay(1); // The flash controller is timing sensitive
	return !(target_mem_read32(t, FLASH_CR) & FLASH_CR_FLOCK_CH32);
}

/*
 * Try to identify the ch32f1 chip family
 * (Actually grab all Cortex-M3 with designer == ARM not caught earlier...)
 */
bool ch32f1_probe(target_s *t)
{
	if ((t->cpuid & CORTEX_CPUID_PARTNO_MASK) != CORTEX_M3)
		return false;

	const uint32_t dbgmcu_idcode = target_mem_read32(t, DBGMCU_IDCODE);
	const uint32_t device_id = dbgmcu_idcode & 0x00000fffU;
	const uint32_t revision_id = (dbgmcu_idcode & 0xffff0000U) >> 16U;

	DEBUG_WARN("DBGMCU_IDCODE 0x%" PRIx32 ", DEVID 0x%" PRIx32 ", REVID 0x%" PRIx32 " \n", dbgmcu_idcode, device_id,
		revision_id);

	if (device_id != 0x410U) // ch32f103, cks32f103, apm32f103
		return false;

	if (revision_id != 0x2000U) // (Hopefully!) only ch32f103
		return false;

	// Try to flock (if this fails it is not a CH32 chip)
	if (!ch32f1_has_fast_unlock(t))
		return false;

	t->part_id = device_id;
	uint32_t signature = target_mem_read32(t, FLASHSIZE);
	/* Some ch32f103c8t6 MCU's found on Blue Pill boards have a zero (0) in the flash memory capacity register */
	if (signature == 0) {
		signature = 64;
		DEBUG_WARN("CH32: FLASHSIZE = 0, assuming CH32F103C8T6 MCU, seting FLASHSIZE = 64\n");
	}
	uint32_t flashSize = signature & 0xffffU;

	target_add_ram(t, 0x20000000, 0x5000);
	ch32f1_add_flash(t, FLASH_BEGIN_ADDRESS_CH32, flashSize * 1024U, 128);
	target_add_commands(t, stm32f1_cmd_list, "STM32 LD/MD/VL-LD/VL-MD");
	t->driver = "CH32F1 medium density (stm32f1 clone)";
	return true;
}

/* Fast erase of CH32 devices */
bool ch32f1_flash_erase(target_flash_s *f, target_addr_t addr, size_t len)
{
	volatile uint32_t sr, magic;
	target_s *t = f->t;
	DEBUG_INFO("CH32: flash erase \n");

	if (!ch32f1_flash_unlock(t)) {
		DEBUG_ERROR("CH32: Unlock failed\n");
		return false;
	}
	// Fast Erase 128 bytes pages (ch32 mode)
	while (len) {
		SET_CR(FLASH_CR_FTER_CH32); // CH32 PAGE_ER
		/* Write address to FMA */
		target_mem_write32(t, FLASH_AR, addr);
		/* Flash page erase start instruction */
		SET_CR(FLASH_CR_STRT);
		WAIT_EOP();
		CLEAR_EOP();
		CLEAR_CR(FLASH_CR_STRT);
		// Magic
		MAGIC(addr);
		if (len > 128U)
			len -= 128U;
		else
			len = 0;
		addr += 128U;
	}
	sr = target_mem_read32(t, FLASH_SR);
	ch32f1_flash_lock(t);
	if (sr & SR_ERROR_MASK)
		DEBUG_ERROR("ch32f1 flash erase error 0x%" PRIx32 "\n", sr);
	return !(sr & SR_ERROR_MASK);
}

/*
 * Wait a bit for the previous operation to finish.
 * As per test result we need a time similar to 10 read operation over SWD
 * We do 32 to have a bit of headroom, then we check we read ffff (erased flash)
 * NB: Just reading fff is not enough as it could be a transient previous operation value
 */
static bool ch32f1_wait_flash_ready(target_s *t, uint32_t addr)
{
	uint32_t flash_val = 0;
	/* Certain ch32f103c8t6 MCU's found on Blue Pill boards need some uninterrupted time (no SWD link activity) */
	platform_delay(2);
	for (size_t cnt = 0; cnt < 32U && flash_val != 0xffffffffU; ++cnt)
		flash_val = target_mem_read32(t, addr);
	if (flash_val != 0xffffffffU) {
		DEBUG_ERROR("ch32f1 Not erased properly at %" PRIx32 " or flash access issue\n", addr);
		return false;
	}
	return true;
}

/* Fast flash for ch32. Load 128 bytes chunk and then write them */
static int ch32f1_upload(target_s *t, uint32_t dest, const void *src, uint32_t offset)
{
	volatile uint32_t sr, magic;
	const uint32_t *ss = (const uint32_t *)(src + offset);
	uint32_t dd = dest + offset;

	SET_CR(FLASH_CR_FTPG_CH32);
	target_mem_write32(t, dd + 0, ss[0]);
	target_mem_write32(t, dd + 4U, ss[1]);
	target_mem_write32(t, dd + 8U, ss[2]);
	target_mem_write32(t, dd + 12U, ss[3]);
	SET_CR(FLASH_CR_BUF_LOAD_CH32); /* BUF LOAD */
	WAIT_EOP();
	CLEAR_EOP();
	CLEAR_CR(FLASH_CR_FTPG_CH32);
	MAGIC(dest + offset);
	return 0;
}

/* Clear the write buffer */
static int ch32f1_buffer_clear(target_s *t)
{
	volatile uint32_t sr;
	SET_CR(FLASH_CR_FTPG_CH32);      // Fast page program 4-
	SET_CR(FLASH_CR_BUF_RESET_CH32); // BUF_RESET 5-
	WAIT_BUSY();                     // 6-
	CLEAR_CR(FLASH_CR_FTPG_CH32);    // Fast page program 4-
	return 0;
}

/*
 * CH32 implementation of Flash write using the CH32-specific fast write
 */
static bool ch32f1_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	volatile uint32_t sr;
	target_s *t = f->t;
	size_t length = len;
#ifdef CH32_VERIFY
	target_addr_t org_dest = dest;
	const void *org_src = src;
#endif
	DEBUG_INFO("CH32: flash write 0x%" PRIx32 " ,size=%" PRIu32 "\n", dest, (uint32_t)len);

	while (length > 0) {
		if (!ch32f1_flash_unlock(t)) {
			DEBUG_ERROR("ch32f1 cannot fast unlock\n");
			return false;
		}
		WAIT_BUSY();

		// Buffer reset...
		ch32f1_buffer_clear(t);
		// Load 128 bytes to buffer
		if (!ch32f1_wait_flash_ready(t, dest))
			return false;

		for (size_t i = 0; i < 8U; i++) {
			if (ch32f1_upload(t, dest, src, i * 16U)) {
				DEBUG_ERROR("Cannot upload to buffer\n");
				return false;
			}
		}
		// write buffer
		SET_CR(FLASH_CR_FTPG_CH32);
		target_mem_write32(t, FLASH_AR, dest); // 10
		SET_CR(FLASH_CR_STRT);                 // 11 Start
		WAIT_EOP();                            // 12
		CLEAR_EOP();
		CLEAR_CR(FLASH_CR_FTPG_CH32);

		uint32_t magic;
		MAGIC(dest);

		// next
		if (length > 128U)
			length -= 128U;
		else
			length = 0;
		dest += 128U;
		src += 128U;

		sr = target_mem_read32(t, FLASH_SR); // 13
		ch32f1_flash_lock(t);
		if (sr & SR_ERROR_MASK) {
			DEBUG_ERROR("ch32f1 flash write error 0x%" PRIx32 "\n", sr);
			return false;
		}
	}

#ifdef CH32_VERIFY
	DEBUG_INFO("Verifying\n");
	for (size_t i = 0; i < len; i += 4U) {
		const uint32_t expected = *(uint32_t *)(org_src + i);
		const uint32_t actual = target_mem_read32(t, org_dest + i);
		if (expected != actual) {
			DEBUG_ERROR(">>>>write mismatch at address 0x%" PRIx32 "\n", org_dest + i);
			DEBUG_ERROR(">>>>expected: 0x%" PRIx32 "\n", expected);
			DEBUG_ERROR(">>>>  actual: 0x%" PRIx32 "\n", actual);
			return false;
		}
	}
#endif

	return true;
}
