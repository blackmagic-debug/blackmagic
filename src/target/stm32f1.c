/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

/* This file implements STM32 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * Refereces:
 * ST doc - RM0008
 *   Reference manual - STM32F101xx, STM32F102xx, STM32F103xx, STM32F105xx
 *   and STM32F107xx advanced ARM-based 32-bit MCUs
 * ST doc - RM0091
 *   Reference manual - STM32F0x1/STM32F0x2/STM32F0x8
 *   advanced ARM®-based 32-bit MCUs
 * ST doc - RM0360
 *   Reference manual - STM32F030x4/x6/x8/xC and STM32F070x6/xB
 * ST doc - PM0075
 *   Programming manual - STM32F10xxx Flash memory microcontrollers
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

static bool stm32f1_cmd_option(target *t, int argc, const char **argv);

const struct command_s stm32f1_cmd_list[] = {
	{"option", (cmd_handler)stm32f1_cmd_option, "Manipulate option bytes"},
	{NULL, NULL, NULL},
};

static bool stm32f1_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool stm32f1_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len);
static bool stm32f1_mass_erase(target *t);

/* Flash Program ad Erase Controller Register Map */
#define FPEC_BASE     0x40022000
#define FLASH_ACR     (FPEC_BASE + 0x00)
#define FLASH_KEYR    (FPEC_BASE + 0x04)
#define FLASH_OPTKEYR (FPEC_BASE + 0x08)
#define FLASH_SR      (FPEC_BASE + 0x0C)
#define FLASH_CR      (FPEC_BASE + 0x10)
#define FLASH_AR      (FPEC_BASE + 0x14)
#define FLASH_OBR     (FPEC_BASE + 0x1C)
#define FLASH_WRPR    (FPEC_BASE + 0x20)

#define FLASH_BANK2_OFFSET 0x40
#define FLASH_BANK_SPLIT   0x08080000

#define FLASH_CR_OBL_LAUNCH (1 << 13)
#define FLASH_CR_OPTWRE     (1 << 9)
#define FLASH_CR_LOCK       (1 << 7)
#define FLASH_CR_STRT       (1 << 6)
#define FLASH_CR_OPTER      (1 << 5)
#define FLASH_CR_OPTPG      (1 << 4)
#define FLASH_CR_MER        (1 << 2)
#define FLASH_CR_PER        (1 << 1)
#define FLASH_CR_PG         (1 << 0)

#define FLASH_OBR_RDPRT (1 << 1)

#define FLASH_SR_BSY (1 << 0)

#define FLASH_OBP_RDP        0x1FFFF800
#define FLASH_OBP_RDP_KEY    0x5aa5
#define FLASH_OBP_RDP_KEY_F3 0x55AA

#define KEY1 0x45670123
#define KEY2 0xCDEF89AB

#define SR_ERROR_MASK 0x14
#define SR_EOP        0x20

#define DBGMCU_IDCODE    0xE0042000
#define DBGMCU_IDCODE_F0 0x40015800

#define FLASHSIZE    0x1FFFF7E0
#define FLASHSIZE_F0 0x1FFFF7CC

static void stm32f1_add_flash(target *t, uint32_t addr, size_t length, size_t erasesize)
{
	target_flash_s *f = calloc(1, sizeof(*f));
	if (!f) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = addr;
	f->length = length;
	f->blocksize = erasesize;
	f->erase = stm32f1_flash_erase;
	f->write = stm32f1_flash_write;
	f->writesize = erasesize;
	f->erased = 0xff;
	target_add_flash(t, f);
}

/**
    \brief identify the correct gd32 f1/f3 chip
    GD32 : STM32 compatible chip
*/
bool gd32f1_probe(target *t)
{
	uint16_t device_id;
	if ((t->cpuid & CPUID_PARTNO_MASK) == CORTEX_M23)
		device_id = target_mem_read32(t, DBGMCU_IDCODE_F0) & 0xfff;
	else
		device_id = target_mem_read32(t, DBGMCU_IDCODE) & 0xfff;

	uint32_t signature = target_mem_read32(t, FLASHSIZE);
	uint32_t flashSize = signature & 0xFFFF;
	uint32_t ramSize = signature >> 16;

	switch (device_id) {
	case 0x414: /* Gigadevice gd32f303 */
	case 0x430:
		t->driver = "GD32F3";
		break;
	case 0x410: /* Gigadevice gd32f103, gd32e230 */
		if ((t->cpuid & CPUID_PARTNO_MASK) == CORTEX_M23)
			t->driver = "GD32E230";
		else if ((t->cpuid & CPUID_PARTNO_MASK) == CORTEX_M4)
			t->driver = "GD32F3";
		else
			t->driver = "GD32F1";
		break;
	default:
		return false;
	}

	t->part_id = device_id;
	t->mass_erase = stm32f1_mass_erase;
	target_add_ram(t, 0x20000000, ramSize * 1024);
	stm32f1_add_flash(t, 0x8000000, flashSize * 1024, 0x400);
	target_add_commands(t, stm32f1_cmd_list, t->driver);

	return true;
}

/**
	\brief identify at32fxx chips (Cortex-M4)
*/
bool at32fxx_probe(target *t)
{
	// Artery clones uses Cortex M4 cores
	if ((t->cpuid & CPUID_PARTNO_MASK) != CORTEX_M4)
		return false;
	// Artery chips use the complete idcode word for identification ()
	const uint32_t idcode = target_mem_read32(t, DBGMCU_IDCODE);
	// AT32F415 Series?
	if ((idcode & 0xfffff000U) == 0x70030000) {
		switch (idcode & 0x00000FFF) {
		case 0x0240: // LQFP64_10x10
		case 0x0241: // LQFP48_7x7
		case 0x0242: // QFN32_4x4
		case 0x0243: // LQFP64_7x7
		case 0x024c: // QFN48_6x6
			// Flash: 256 KB / 2KB per block
			stm32f1_add_flash(t, 0x08000000, 256 * 1024, 2 * 1024);
			break;
		case 0x01c4: // LQFP64_10x10
		case 0x01c5: // LQFP48_7x7
		case 0x01c6: // QFN32_4x4
		case 0x01c7: // LQFP64_7x7
		case 0x01cd: // QFN48_6x6
			// Flash: 128 KB / 2KB per block
			stm32f1_add_flash(t, 0x08000000, 128 * 1024, 2 * 1024);
			break;
		case 0x0108: // LQFP64_10x10
		case 0x0109: // LQFP48_7x7
		case 0x010a: // QFN32_4x4
			// Flash: 64 KB / 2KB per block
			stm32f1_add_flash(t, 0x08000000, 64 * 1024, 2 * 1024);
			break;
		// Unknown/undocumented
		default:
			return false;
		}
		// All parts have 32KB SRAM
		target_add_ram(t, 0x20000000, 32 * 1024);
		t->driver = "AT32F415";
	}
	// AT32F403A/407 Series?
	else if ((idcode & 0xfffff000U) == 0x70050000) {
		// Current driver supports only *default* memory layout (256 KB Flash / 96 KB SRAM)
		// (*) Support for external Flash for 512KB and 1024KB parts requires specific flash code (not implement)
		switch (idcode & 0x00000FFF) {
		case 0x0240: // AT32F403AVCT7 256KB / LQFP100
		case 0x0241: // AT32F403ARCT7 256KB / LQFP64
		case 0x0242: // AT32F403ACCT7 256KB / LQFP48
		case 0x0243: // AT32F403ACCU7 256KB / QFN48
		case 0x0249: // AT32F407VCT7 256KB / LQFP100
		case 0x024a: // AT32F407RCT7 256KB / LQFP64
		case 0x0254: // AT32F407AVCT7 256KB / LQFP100
		case 0x02cd: // AT32F403AVET7 512KB / LQFP100 (*)
		case 0x02ce: // AT32F403ARET7 512KB / LQFP64 (*)
		case 0x02cf: // AT32F403ACET7 512KB / LQFP48 (*)
		case 0x02d0: // AT32F403ACEU7 512KB / QFN48 (*)
		case 0x02d1: // AT32F407VET7 512KB / LQFP100 (*)
		case 0x02d2: // AT32F407RET7 512KB / LQFP64 (*)
		case 0x0344: // AT32F403AVGT7 1024KB / LQFP100 (*)
		case 0x0345: // AT32F403ARGT7 1024KB / LQFP64 (*)
		case 0x0346: // AT32F403ACGT7 1024KB / LQFP48 (*)
		case 0x0347: // AT32F403ACGU7 1024KB / QFN48 (found on BlackPill+ WeAct Studio) (*)
		case 0x034b: // AT32F407VGT7 1024KB / LQFP100 (*)
		case 0x034c: // AT32F407VGT7 1024KB / LQFP64 (*)
		case 0x0353: // AT32F407AVGT7 1024KB / LQFP100 (*)
			// Flash: 256 KB / 2KB per block
			stm32f1_add_flash(t, 0x08000000, 256 * 1024, 2 * 1024);
			break;
		// Unknown/undocumented
		default:
			return false;
		}
		// All parts have 96KB SRAM
		target_add_ram(t, 0x20000000, 96 * 1024);
		t->driver = "AT32F403A/407";
	} else
		return false;
	return true;
}

/**
    \brief identify the stm32f1 chip
*/
bool stm32f1_probe(target *t)
{
	uint16_t device_id;
	if ((t->cpuid & CPUID_PARTNO_MASK) == CORTEX_M0)
		device_id = target_mem_read32(t, DBGMCU_IDCODE_F0) & 0xfff;
	else
		device_id = target_mem_read32(t, DBGMCU_IDCODE) & 0xfff;

	t->mass_erase = stm32f1_mass_erase;
	size_t flash_size;
	size_t block_size = 0x400;

	switch (device_id) {
	case 0x29b: /* CS clone */
	case 0x410: /* Medium density */
	case 0x412: /* Low density */
	case 0x420: /* Value Line, Low-/Medium density */
		target_add_ram(t, 0x20000000, 0x5000);
		stm32f1_add_flash(t, 0x8000000, 0x20000, 0x400);
		target_add_commands(t, stm32f1_cmd_list, "STM32 LD/MD/VL-LD/VL-MD");
		/* Test for clone parts with Core rev 2*/
		ADIv5_AP_t *ap = cortexm_ap(t);
		if ((ap->idr >> 28) > 1) {
			t->driver = "STM32F1 (clone) medium density";
			DEBUG_WARN("Detected clone STM32F1\n");
		} else {
			t->driver = "STM32F1 medium density";
		}
		t->part_id = device_id;
		return true;

	case 0x414: /* High density */
	case 0x418: /* Connectivity Line */
	case 0x428: /* Value Line, High Density */
		t->driver = "STM32F1  VL density";
		t->part_id = device_id;
		target_add_ram(t, 0x20000000, 0x10000);
		stm32f1_add_flash(t, 0x8000000, 0x80000, 0x800);
		target_add_commands(t, stm32f1_cmd_list, "STM32 HF/CL/VL-HD");
		return true;

	case 0x430: /* XL-density */
		t->driver = "STM32F1  XL density";
		t->part_id = device_id;
		target_add_ram(t, 0x20000000, 0x18000);
		stm32f1_add_flash(t, 0x8000000, 0x80000, 0x800);
		stm32f1_add_flash(t, 0x8080000, 0x80000, 0x800);
		target_add_commands(t, stm32f1_cmd_list, "STM32 XL/VL-XL");
		return true;

	case 0x438: /* STM32F303x6/8 and STM32F328 */
	case 0x422: /* STM32F30x */
	case 0x446: /* STM32F303xD/E and STM32F398xE */
		target_add_ram(t, 0x10000000, 0x4000);
		/* fall through */

	case 0x432: /* STM32F37x */
	case 0x439: /* STM32F302C8 */
		t->driver = "STM32F3";
		t->part_id = device_id;
		target_add_ram(t, 0x20000000, 0x10000);
		stm32f1_add_flash(t, 0x8000000, 0x80000, 0x800);
		target_add_commands(t, stm32f1_cmd_list, "STM32F3");
		return true;

	case 0x444: /* STM32F03 RM0091 Rev.7, STM32F030x[4|6] RM0360 Rev. 4*/
		t->driver = "STM32F03";
		flash_size = 0x8000;
		break;

	case 0x445: /* STM32F04 RM0091 Rev.7, STM32F070x6 RM0360 Rev. 4*/
		t->driver = "STM32F04/F070x6";
		flash_size = 0x8000;
		break;

	case 0x440: /* STM32F05 RM0091 Rev.7, STM32F030x8 RM0360 Rev. 4*/
		t->driver = "STM32F05/F030x8";
		flash_size = 0x10000;
		break;

	case 0x448: /* STM32F07 RM0091 Rev.7, STM32F070xB RM0360 Rev. 4*/
		t->driver = "STM32F07";
		flash_size = 0x20000;
		block_size = 0x800;
		break;

	case 0x442: /* STM32F09 RM0091 Rev.7, STM32F030xC RM0360 Rev. 4*/
		t->driver = "STM32F09/F030xC";
		flash_size = 0x40000;
		block_size = 0x800;
		break;

	default: /* NONE */
		return false;
	}

	target_add_ram(t, 0x20000000, 0x5000);
	stm32f1_add_flash(t, 0x8000000, flash_size, block_size);
	target_add_commands(t, stm32f1_cmd_list, "STM32F0");

	t->part_id = device_id;

	return true;
}

static int stm32f1_flash_unlock(target *t, uint32_t bank_offset)
{
	target_mem_write32(t, FLASH_KEYR + bank_offset, KEY1);
	target_mem_write32(t, FLASH_KEYR + bank_offset, KEY2);
	uint32_t cr = target_mem_read32(t, FLASH_CR);
	if (cr & FLASH_CR_LOCK) {
		DEBUG_WARN("unlock failed, cr: 0x%08" PRIx32 "\n", cr);
		return -1;
	}

	return 0;
}

static inline void stm32f1_flash_clear_eop(target *const t, const uint32_t bank_offset)
{
	const uint32_t status = target_mem_read32(t, FLASH_SR + bank_offset);
	target_mem_write32(t, FLASH_SR + bank_offset, status | SR_EOP); /* EOP is W1C */
}

static bool stm32f1_flash_busy_wait(target *const t, const uint32_t bank_offset, platform_timeout *const timeout)
{
	/* Read FLASH_SR to poll for BSY bit */
	uint32_t status = FLASH_SR_BSY;
	/*
	 * Please note that checking EOP here is only legal because every operation is preceeded by
	 * a call to stm32f1_flash_clear_eop. Without this the flag could be stale from a previous
	 * operation and is always set at the end of every program/erase operation.
	 * For more information, see FLASH_SR register description §3.4 pg 25.
	 * https://www.st.com/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf
	 */
	while (!(status & SR_EOP) && (status & FLASH_SR_BSY)) {
		status = target_mem_read32(t, FLASH_SR + bank_offset);
		if (target_check_error(t)) {
			DEBUG_WARN("Lost communications with target");
			return false;
		}
		if (timeout)
			target_print_progress(timeout);
	};
	if (status & SR_ERROR_MASK)
		DEBUG_WARN("stm32f1 flash error 0x%" PRIx32 "\n", status);
	return !(status & SR_ERROR_MASK);
}

static bool stm32f1_flash_erase(target_flash_s *f, target_addr_t addr, size_t len)
{
	target *t = f->t;
	target_addr_t end = addr + len - 1;

	if (t->part_id == 0x430 && end >= FLASH_BANK_SPLIT)
		if (stm32f1_flash_unlock(t, FLASH_BANK2_OFFSET))
			return false;

	if (addr < FLASH_BANK_SPLIT)
		if (stm32f1_flash_unlock(t, 0))
			return false;

	while (len) {
		uint32_t bank_offset = 0;
		if (addr >= FLASH_BANK_SPLIT)
			bank_offset = FLASH_BANK2_OFFSET;

		stm32f1_flash_clear_eop(t, bank_offset);

		/* Flash page erase instruction */
		target_mem_write32(t, FLASH_CR + bank_offset, FLASH_CR_PER);
		/* write address to FMA */
		target_mem_write32(t, FLASH_AR + bank_offset, addr);
		/* Flash page erase start instruction */
		target_mem_write32(t, FLASH_CR + bank_offset, FLASH_CR_STRT | FLASH_CR_PER);

		/* Wait for completion or an error */
		if (!stm32f1_flash_busy_wait(t, bank_offset, NULL))
			return false;

		if (len > f->blocksize)
			len -= f->blocksize;
		else
			len = 0;

		addr += f->blocksize;
	}

	return true;
}

static bool stm32f1_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	target *t = f->t;
	size_t length = 0;

	if (dest < FLASH_BANK_SPLIT) {
		if ((dest + len - 1) >= FLASH_BANK_SPLIT)
			length = FLASH_BANK_SPLIT - dest;
		else
			length = len;

		stm32f1_flash_clear_eop(t, 0);

		target_mem_write32(t, FLASH_CR, FLASH_CR_PG);
		cortexm_mem_write_sized(t, dest, src, length, ALIGN_HALFWORD);

		/* Wait for completion or an error */
		if (!stm32f1_flash_busy_wait(t, 0, NULL))
			return false;

		dest += length;
		src += length;
	}

	length = len - length;
	if (t->part_id == 0x430 && length) { /* Write on bank 2 */

		stm32f1_flash_clear_eop(t, FLASH_BANK2_OFFSET);

		target_mem_write32(t, FLASH_CR + FLASH_BANK2_OFFSET, FLASH_CR_PG);
		cortexm_mem_write_sized(t, dest, src, length, ALIGN_HALFWORD);

		/* Wait for completion or an error */
		if (!stm32f1_flash_busy_wait(t, FLASH_BANK2_OFFSET, NULL))
			return false;
	}

	return true;
}

static bool stm32f1_mass_erase(target *t)
{
	if (stm32f1_flash_unlock(t, 0))
		return false;

	platform_timeout timeout;
	platform_timeout_set(&timeout, 500);

	stm32f1_flash_clear_eop(t, 0);

	/* Flash mass erase start instruction */
	target_mem_write32(t, FLASH_CR, FLASH_CR_MER);
	target_mem_write32(t, FLASH_CR, FLASH_CR_STRT | FLASH_CR_MER);

	/* Wait for completion or an error */
	if (!stm32f1_flash_busy_wait(t, 0, &timeout))
		return false;

	if (t->part_id == 0x430) {
		if (stm32f1_flash_unlock(t, FLASH_BANK2_OFFSET))
			return false;

		stm32f1_flash_clear_eop(t, FLASH_BANK2_OFFSET);

		/* Flash mass erase start instruction on bank 2*/
		target_mem_write32(t, FLASH_CR + FLASH_BANK2_OFFSET, FLASH_CR_MER);
		target_mem_write32(t, FLASH_CR + FLASH_BANK2_OFFSET, FLASH_CR_STRT | FLASH_CR_MER);

		/* Wait for completion or an error */
		if (!stm32f1_flash_busy_wait(t, FLASH_BANK2_OFFSET, &timeout))
			return false;
	}

	return true;
}

static bool stm32f1_option_erase(target *t)
{
	stm32f1_flash_clear_eop(t, 0);

	/* Erase option bytes instruction */
	target_mem_write32(t, FLASH_CR, FLASH_CR_OPTER | FLASH_CR_OPTWRE);
	target_mem_write32(t, FLASH_CR, FLASH_CR_STRT | FLASH_CR_OPTER | FLASH_CR_OPTWRE);

	/* Wait for completion or an error */
	return stm32f1_flash_busy_wait(t, 0, NULL);
}

static bool stm32f1_option_write_erased(target *t, uint32_t addr, uint16_t value, bool width_word)
{
	if (value == 0xffff)
		return true;

	stm32f1_flash_clear_eop(t, 0);

	/* Erase option bytes instruction */
	target_mem_write32(t, FLASH_CR, FLASH_CR_OPTPG | FLASH_CR_OPTWRE);

	if (width_word)
		target_mem_write32(t, addr, 0xFFFF0000 | value);
	else
		target_mem_write16(t, addr, value);

	/* Wait for completion or an error */
	return stm32f1_flash_busy_wait(t, 0, NULL);
}

static bool stm32f1_option_write(target *t, uint32_t addr, uint16_t value)
{
	uint16_t opt_val[8];
	int index;

	index = (addr - FLASH_OBP_RDP) / 2;
	if (index < 0 || index > 7)
		return false;

	/* Retrieve old values */
	for (size_t i = 0; i < 16; i += 4) {
		uint32_t val = target_mem_read32(t, FLASH_OBP_RDP + i);
		opt_val[i / 2] = val & 0xffff;
		opt_val[i / 2 + 1] = val >> 16;
	}

	if (opt_val[index] == value)
		return true;

	/* Check for erased value */
	if (opt_val[index] != 0xffff)
		if (!stm32f1_option_erase(t))
			return false;

	opt_val[index] = value;

	/* Write changed values*/
	bool width_word = false;
	if (t->part_id == 0x410 && (t->cpuid & CPUID_PARTNO_MASK) == CORTEX_M23) {
		/* GD32E230 special case, target_mem_write16 does not work */
		width_word = true;
	}

	for (size_t i = 0; i < 8; i++)
		if (!stm32f1_option_write_erased(t, FLASH_OBP_RDP + i * 2, opt_val[i], width_word))
			return false;

	return true;
}

static bool stm32f1_cmd_option(target *t, int argc, const char **argv)
{
	uint32_t addr, val;
	uint32_t flash_obp_rdp_key;
	uint32_t rdprt;

	switch (t->part_id) {
	case 0x422: /* STM32F30x */
	case 0x432: /* STM32F37x */
	case 0x438: /* STM32F303x6/8 and STM32F328 */
	case 0x440: /* STM32F0 */
	case 0x446: /* STM32F303xD/E and STM32F398xE */
	case 0x445: /* STM32F04 RM0091 Rev.7, STM32F070x6 RM0360 Rev. 4*/
	case 0x448: /* STM32F07 RM0091 Rev.7, STM32F070xB RM0360 Rev. 4*/
	case 0x442: /* STM32F09 RM0091 Rev.7, STM32F030xC RM0360 Rev. 4*/
		flash_obp_rdp_key = FLASH_OBP_RDP_KEY_F3;
		break;
	default:
		flash_obp_rdp_key = FLASH_OBP_RDP_KEY;
	}

	rdprt = target_mem_read32(t, FLASH_OBR) & FLASH_OBR_RDPRT;

	if (stm32f1_flash_unlock(t, 0))
		return false;

	target_mem_write32(t, FLASH_OPTKEYR, KEY1);
	target_mem_write32(t, FLASH_OPTKEYR, KEY2);

	if (argc == 2 && strcmp(argv[1], "erase") == 0) {
		stm32f1_option_erase(t);
		bool width_word = false;
		if (t->part_id == 0x410 && (t->cpuid & CPUID_PARTNO_MASK) == CORTEX_M23) {
			/* GD32E230 special case, target_mem_write16 does not work */
			width_word = true;
		}
		stm32f1_option_write_erased(t, FLASH_OBP_RDP, flash_obp_rdp_key, width_word);
	} else if (rdprt) {
		tc_printf(t, "Device is Read Protected\n");
		tc_printf(t, "Use \"monitor option erase\" to unprotect, erasing device\n");
		return true;
	} else if (argc == 3) {
		addr = strtol(argv[1], NULL, 0);
		val = strtol(argv[2], NULL, 0);
		stm32f1_option_write(t, addr, val);
	} else {
		tc_printf(t, "usage: monitor option erase\n");
		tc_printf(t, "usage: monitor option <addr> <value>\n");
	}

	for (size_t i = 0; i < 16; i += 4) {
		addr = FLASH_OBP_RDP + i;
		val = target_mem_read32(t, addr);
		tc_printf(t, "0x%08X: 0x%04X\n", addr, val & 0xFFFF);
		tc_printf(t, "0x%08X: 0x%04X\n", addr + 2, val >> 16);
	}

	return true;
}
