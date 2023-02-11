/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022-2023 1BitSquared <info@1bitsquared.com>
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
 * This file implements STM32 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * References:
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
#include "jep106.h"

static bool stm32f1_cmd_option(target_s *target, int argc, const char **argv);

const command_s stm32f1_cmd_list[] = {
	{"option", stm32f1_cmd_option, "Manipulate option bytes"},
	{NULL, NULL, NULL},
};

static bool stm32f1_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool stm32f1_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool stm32f1_mass_erase(target_s *target);

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

#define DBGMCU_IDCODE        0xe0042000U
#define DBGMCU_IDCODE_F0     0x40015800U
#define DBGMCU_IDCODE_GD32E5 0xe0044000U

#define GD32Fx_FLASHSIZE 0x1ffff7e0U
#define GD32F0_FLASHSIZE 0x1ffff7ccU

#define AT32F4x_IDCODE_SERIES_MASK 0xfffff000U
#define AT32F4x_IDCODE_PART_MASK   0x00000fffU
#define AT32F41_SERIES             0x70030000U
#define AT32F40_SERIES             0x70050000U
#define AT32F43_SERIES_4K          0x70084000U
#define AT32F43_SERIES_2K          0x70083000U

#define DBGMCU_IDCODE_MM32L0 0x40013400U
#define DBGMCU_IDCODE_MM32F3 0x40007080U

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

static uint16_t stm32f1_read_idcode(target_s *const target)
{
	if ((target->cpuid & CPUID_PARTNO_MASK) == CORTEX_M0 || (target->cpuid & CPUID_PARTNO_MASK) == CORTEX_M23)
		return target_mem_read32(target, DBGMCU_IDCODE_F0) & 0xfffU;
	/* Is this a Cortex-M33 core with STM32F1-style peripherals? (GD32E50x) */
	if ((target->cpuid & CPUID_PARTNO_MASK) == CORTEX_M33)
		return target_mem_read32(target, DBGMCU_IDCODE_GD32E5) & 0xfffU;

	return target_mem_read32(target, DBGMCU_IDCODE) & 0xfffU;
}

/* Identify GD32F1, GD32F2 and GD32F3 chips */
bool gd32f1_probe(target_s *target)
{
	const uint16_t device_id = stm32f1_read_idcode(target);
	size_t block_size = 0x400;

	switch (device_id) {
	case 0x414U: /* Gigadevice gd32f303 */
	case 0x430U:
		target->driver = "GD32F3";
		break;
	case 0x418U:
		target->driver = "GD32F2";
		break;
	case 0x410U: /* Gigadevice gd32f103, gd32e230 */
		if ((target->cpuid & CPUID_PARTNO_MASK) == CORTEX_M23)
			target->driver = "GD32E230";
		else if ((target->cpuid & CPUID_PARTNO_MASK) == CORTEX_M4)
			target->driver = "GD32F3";
		else
			target->driver = "GD32F1";
		break;
	case 0x444U: /* Gigadevice gd32e50x */
		target->driver = "GD32E5";
		block_size = 0x2000;
		break;
	default:
		return false;
	}

	const uint32_t signature = target_mem_read32(target, GD32Fx_FLASHSIZE);
	const uint16_t flash_size = signature & 0xffffU;
	const uint16_t ram_size = signature >> 16U;

	target->part_id = device_id;
	target->mass_erase = stm32f1_mass_erase;
	target_add_ram(target, 0x20000000, ram_size * 1024U);
	stm32f1_add_flash(target, 0x8000000, (size_t)flash_size * 1024U, block_size);
	target_add_commands(target, stm32f1_cmd_list, target->driver);

	return true;
}

/* Identify RISC-V GD32VF1 chips */
bool gd32vf1_probe(target_s *const target)
{
	/* Make sure the architecture ID matches */
	if (target->cpuid != 0x80000022U)
		return false;
	/* Then read out the device ID */
	const uint16_t device_id = target_mem_read32(target, DBGMCU_IDCODE) & 0xfffU;
	switch (device_id) {
	case 0x410U: /* GD32VF103 */
		target->driver = "GD32VF1";
		break;
	default:
		return false;
	}

	const uint32_t signature = target_mem_read32(target, GD32Fx_FLASHSIZE);
	const uint16_t flash_size = signature & 0xffffU;
	const uint16_t ram_size = signature >> 16U;

	target->part_id = device_id;
	target->mass_erase = stm32f1_mass_erase;
	target_add_ram(target, 0x20000000, ram_size * 1024U);
	stm32f1_add_flash(target, 0x8000000, (size_t)flash_size * 1024U, 0x400U);
	target_add_commands(target, stm32f1_cmd_list, target->driver);

	return true;
}

static bool at32f40_detect(target_s *target, const uint16_t part_id)
{
	// Current driver supports only *default* memory layout (256 KB Flash / 96 KB SRAM)
	// XXX: Support for external Flash for 512KB and 1024KB parts requires specific flash code (not implemented)
	switch (part_id) {
	case 0x0240U: // AT32F403AVCT7 256KB / LQFP100
	case 0x0241U: // AT32F403ARCT7 256KB / LQFP64
	case 0x0242U: // AT32F403ACCT7 256KB / LQFP48
	case 0x0243U: // AT32F403ACCU7 256KB / QFN48
	case 0x0249U: // AT32F407VCT7 256KB / LQFP100
	case 0x024aU: // AT32F407RCT7 256KB / LQFP64
	case 0x0254U: // AT32F407AVCT7 256KB / LQFP100
	case 0x02cdU: // AT32F403AVET7 512KB / LQFP100 (*)
	case 0x02ceU: // AT32F403ARET7 512KB / LQFP64 (*)
	case 0x02cfU: // AT32F403ACET7 512KB / LQFP48 (*)
	case 0x02d0U: // AT32F403ACEU7 512KB / QFN48 (*)
	case 0x02d1U: // AT32F407VET7 512KB / LQFP100 (*)
	case 0x02d2U: // AT32F407RET7 512KB / LQFP64 (*)
	case 0x0344U: // AT32F403AVGT7 1024KB / LQFP100 (*)
	case 0x0345U: // AT32F403ARGT7 1024KB / LQFP64 (*)
	case 0x0346U: // AT32F403ACGT7 1024KB / LQFP48 (*)
	case 0x0347U: // AT32F403ACGU7 1024KB / QFN48 (found on BlackPill+ WeAct Studio) (*)
	case 0x034bU: // AT32F407VGT7 1024KB / LQFP100 (*)
	case 0x034cU: // AT32F407VGT7 1024KB / LQFP64 (*)
	case 0x0353U: // AT32F407AVGT7 1024KB / LQFP100 (*)
		// Flash: 256 KB / 2KB per block
		stm32f1_add_flash(target, 0x08000000, 256U * 1024U, 2U * 1024U);
		break;
	// Unknown/undocumented
	default:
		return false;
	}
	// All parts have 96KB SRAM
	target_add_ram(target, 0x20000000, 96U * 1024U);
	target->driver = "AT32F403A/407";
	target->mass_erase = stm32f1_mass_erase;
	return true;
}

static bool at32f41_detect(target_s *target, const uint16_t part_id)
{
	switch (part_id) {
	case 0x0240U: // LQFP64_10x10
	case 0x0241U: // LQFP48_7x7
	case 0x0242U: // QFN32_4x4
	case 0x0243U: // LQFP64_7x7
	case 0x024cU: // QFN48_6x6
		// Flash: 256 KB / 2KB per block
		stm32f1_add_flash(target, 0x08000000, 256U * 1024U, 2U * 1024U);
		break;
	case 0x01c4U: // LQFP64_10x10
	case 0x01c5U: // LQFP48_7x7
	case 0x01c6U: // QFN32_4x4
	case 0x01c7U: // LQFP64_7x7
	case 0x01cdU: // QFN48_6x6
		// Flash: 128 KB / 2KB per block
		stm32f1_add_flash(target, 0x08000000, 128U * 1024U, 2U * 1024U);
		break;
	case 0x0108U: // LQFP64_10x10
	case 0x0109U: // LQFP48_7x7
	case 0x010aU: // QFN32_4x4
		// Flash: 64 KB / 2KB per block
		stm32f1_add_flash(target, 0x08000000, 64U * 1024U, 2U * 1024U);
		break;
	// Unknown/undocumented
	default:
		return false;
	}
	// All parts have 32KB SRAM
	target_add_ram(target, 0x20000000, 32U * 1024U);
	target->driver = "AT32F415";
	target->mass_erase = stm32f1_mass_erase;
	return true;
}

static bool at32f43_detect(target_s *target, const uint16_t part_id)
{
	/* AT32F435 EOPB0 ZW/NZW split reconfiguration unsupported,
	 * assuming default split ZW=256 SRAM=384.
	 * AT32F437 also have a working "EMAC" (Ethernet MAC)
	 */
	uint32_t flash_size_kb = 0;
	uint32_t sector_size = 0;
	switch (part_id) {
	// 0x70084000U parts with 4KB sectors:
	case 0x0540U: // LQFP144
	case 0x0543U: // LQFP100
	case 0x0546U: // LQFP64
	case 0x0549U: // LQFP48
	case 0x054cU: // QFN48
	case 0x054fU: // LQFP144 w/Eth
	case 0x0552U: // LQFP100 w/Eth
	case 0x0555U: // LQFP64 w/Eth
		// Flash (G): 4032 KB in 2 banks (2048+1984), 4KB per sector.
		flash_size_kb = 4032;
		sector_size = 4096;
		break;
	case 0x0598U: // LQFP144
	case 0x0599U: // LQFP100
	case 0x059aU: // LQFP64
	case 0x059bU: // LQFP48
	case 0x059cU: // QFN48
	case 0x059dU: // LQFP144 w/Eth
	case 0x059eU: // LQFP100 w/Eth
	case 0x059fU: // LQFP64 w/Eth
		// Flash (D): 448 KB, only bank 1, 4KB per sector.
		flash_size_kb = 448;
		sector_size = 4096;
		break;
	// 0x70083000U parts with 2KB sectors:
	case 0x0341U: // LQFP144
	case 0x0344U: // LQFP100
	case 0x0347U: // LQFP64
	case 0x034aU: // LQFP48
	case 0x034dU: // QFN48
	case 0x0350U: // LQFP144 w/Eth
	case 0x0353U: // LQFP100 w/Eth
	case 0x0356U: // LQFP64 w/Eth
		// Flash (M): 1024 KB in 2 banks (equal), 2KB per sector.
		flash_size_kb = 1024;
		sector_size = 2048;
		break;
	case 0x0242U: // LQFP144
	case 0x0245U: // LQFP100
	case 0x0248U: // LQFP64
	case 0x024bU: // LQFP48
	case 0x024eU: // QFN48
	case 0x0251U: // LQFP144 w/Eth
	case 0x0254U: // LQFP100 w/Eth
	case 0x0257U: // LQFP64 w/Eth
		// Flash (C): 256 KB, only bank 1, 2KB per sector.
		flash_size_kb = 256;
		sector_size = 2048;
		break;
	default:
		return false;
	}
	/*
	 * Arterytek F43x Flash controller has BLKERS (1<<3U).
	 * Block erase operates on 64 KB at once for all parts.
	 * Using here only sector erase (page erase) for compatibility.
	 */
	stm32f1_add_flash(target, 0x08000000, flash_size_kb * 1024U, sector_size);
	// SRAM1 (64KB) can be remapped to 0x10000000.
	target_add_ram(target, 0x20000000, 64U * 1024U);
	// SRAM2 (384-64=320 KB default).
	target_add_ram(target, 0x20010000, 320U * 1024U);
	/*
	 * SRAM total is adjustable between 128 KB and 512 KB (max).
	 * Out of 640 KB SRAM present on silicon, at least 128 KB are always
	 * dedicated to "zero-wait-state Flash". ZW region is limited by
	 * specific part flash capacity (for 256, 448 KB) or at 512 KB.
	 * AT32F435ZMT default EOPB0=0xffff05fa,
	 * EOPB[0:2]=0b010 for 384 KB SRAM + 256 KB zero-wait-state flash.
	 */
	target->driver = "AT32F435";
	target->mass_erase = stm32f1_mass_erase;
	return true;
}

/* Identify AT32F4x devices (Cortex-M4) */
bool at32fxx_probe(target_s *target)
{
	// Artery clones use Cortex M4 cores
	if ((target->cpuid & CPUID_PARTNO_MASK) != CORTEX_M4)
		return false;

	// Artery chips use the complete idcode word for identification
	const uint32_t idcode = target_mem_read32(target, DBGMCU_IDCODE);
	const uint32_t series = idcode & AT32F4x_IDCODE_SERIES_MASK;
	const uint16_t part_id = idcode & AT32F4x_IDCODE_PART_MASK;

	if (series == AT32F40_SERIES)
		return at32f40_detect(target, part_id);
	if (series == AT32F41_SERIES)
		return at32f41_detect(target, part_id);
	if (series == AT32F43_SERIES_4K || series == AT32F43_SERIES_2K)
		return at32f43_detect(target, part_id);
	return false;
}

/*
 * On STM32, 16-bit writes use bits 0:15 for even halfwords; bits 16:31 for odd halfwords.
 * On MM32 cortex-m0, 16-bit writes always use bits 0:15.
 * Set both halfwords to the same value, works on both STM32 and NN32.
 */
void mm32l0_mem_write_sized(adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t len, align_e align)
{
	uint32_t odest = dest;

	len >>= align;
	ap_mem_access_setup(ap, dest, align);
	while (len--) {
		uint32_t tmp = 0;
		/* Pack data into correct data lane */
		switch (align) {
		case ALIGN_BYTE: {
			uint8_t value;
			memcpy(&value, src, sizeof(value));
			/* copy byte to be written to all four bytes of the uint32_t */
			tmp = (uint32_t)value;
			tmp = tmp | tmp << 8U;
			tmp = tmp | tmp << 16U;
			break;
		}
		case ALIGN_HALFWORD: {
			uint16_t value;
			memcpy(&value, src, sizeof(value));
			/* copy halfword to be written to both halfwords of the uint32_t */
			tmp = (uint32_t)value;
			tmp = tmp | tmp << 16U;
			break;
		}
		case ALIGN_DWORD:
		case ALIGN_WORD:
			memcpy(&tmp, src, sizeof(tmp));
			break;
		}
		src = (uint8_t *)src + (1 << align);
		dest += (1 << align);
		adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_DRW, tmp);

		/* Check for 10 bit address overflow */
		if ((dest ^ odest) & 0xfffffc00U) {
			odest = dest;
			adiv5_dp_low_access(ap->dp, ADIV5_LOW_WRITE, ADIV5_AP_TAR, dest);
		}
	}
	/* Make sure this write is complete by doing a dummy read */
	adiv5_dp_read(ap->dp, ADIV5_DP_RDBUFF);
}

/* Identify MM32 devices (Cortex-M0) */

bool mm32l0xx_probe(target_s *target)
{
	const char *name;
	size_t flash_kbyte = 0;
	size_t ram_kbyte = 0;
	size_t block_size = 0x400U;

	const uint32_t mm32_id = target_mem_read32(target, DBGMCU_IDCODE_MM32L0);
	if (target_check_error(target)) {
		DEBUG_ERROR("%s: read error at 0x%" PRIx32 "\n", __func__, (uint32_t)DBGMCU_IDCODE_MM32L0);
		return false;
	}
	switch (mm32_id) {
	case 0xcc568091U:
		name = "MM32L07x";
		flash_kbyte = 128;
		ram_kbyte = 8;
		break;
	case 0xcc56a097U:
		name = "MM32SPIN27";
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
	target->driver = name;
	target->mass_erase = stm32f1_mass_erase;
	target_add_ram(target, 0x20000000U, ram_kbyte * 1024U);
	stm32f1_add_flash(target, 0x08000000U, flash_kbyte * 1024U, block_size);
	target_add_commands(target, stm32f1_cmd_list, name);
	cortexm_ap(target)->dp->mem_write = mm32l0_mem_write_sized;
	return true;
}

/* Identify MM32 devices (Cortex-M3, Star-MC1) */
bool mm32f3xx_probe(target_s *target)
{
	const char *name;
	size_t flash_kbyte = 0;
	size_t ram1_kbyte = 0; /* ram at 0x20000000 */
	size_t ram2_kbyte = 0; /* ram at 0x30000000 */
	size_t block_size = 0x400U;

	const uint32_t mm32_id = target_mem_read32(target, DBGMCU_IDCODE_MM32F3);
	if (target_check_error(target)) {
		DEBUG_ERROR("%s: read error at 0x%" PRIx32 "\n", __func__, (uint32_t)DBGMCU_IDCODE_MM32F3);
		return false;
	}
	switch (mm32_id) {
	case 0xcc9aa0e7U:
		name = "MM32F3273";
		flash_kbyte = 512;
		ram1_kbyte = 128;
		break;
	case 0x4d4d0800U:
		name = "MM32F5277";
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
	target->driver = name;
	target->mass_erase = stm32f1_mass_erase;
	if (ram1_kbyte != 0)
		target_add_ram(target, 0x20000000U, ram1_kbyte * 1024U);
	if (ram2_kbyte != 0)
		target_add_ram(target, 0x30000000U, ram2_kbyte * 1024U);
	stm32f1_add_flash(target, 0x08000000U, flash_kbyte * 1024U, block_size);
	target_add_commands(target, stm32f1_cmd_list, name);
	return true;
}

/* Identify real STM32F0/F1/F3 devices */
bool stm32f1_probe(target_s *target)
{
	const uint16_t device_id = stm32f1_read_idcode(target);

	target->mass_erase = stm32f1_mass_erase;
	size_t flash_size = 0;
	size_t block_size = 0x400;

	switch (device_id) {
	case 0x29bU: /* CS clone */
	case 0x410U: /* Medium density */
	case 0x412U: /* Low density */
	case 0x420U: /* Value Line, Low-/Medium density */
		target_add_ram(target, 0x20000000, 0x5000);
		stm32f1_add_flash(target, 0x8000000, 0x20000, 0x400);
		target_add_commands(target, stm32f1_cmd_list, "STM32 LD/MD/VL-LD/VL-MD");
		/* Test for clone parts with Core rev 2*/
		adiv5_access_port_s *ap = cortexm_ap(target);
		if ((ap->idr >> 28U) > 1U) {
			target->driver = "STM32F1 (clone) medium density";
			DEBUG_WARN("Detected clone STM32F1\n");
		} else
			target->driver = "STM32F1 medium density";
		target->part_id = device_id;
		return true;

	case 0x414U: /* High density */
	case 0x418U: /* Connectivity Line */
	case 0x428U: /* Value Line, High Density */
		target->driver = "STM32F1  VL density";
		target->part_id = device_id;
		target_add_ram(target, 0x20000000, 0x10000);
		stm32f1_add_flash(target, 0x8000000, 0x80000, 0x800);
		target_add_commands(target, stm32f1_cmd_list, "STM32 HF/CL/VL-HD");
		return true;

	case 0x430U: /* XL-density */
		target->driver = "STM32F1  XL density";
		target->part_id = device_id;
		target_add_ram(target, 0x20000000, 0x18000);
		stm32f1_add_flash(target, 0x8000000, 0x80000, 0x800);
		stm32f1_add_flash(target, 0x8080000, 0x80000, 0x800);
		target_add_commands(target, stm32f1_cmd_list, "STM32 XL/VL-XL");
		return true;

	case 0x438U: /* STM32F303x6/8 and STM32F328 */
	case 0x422U: /* STM32F30x */
	case 0x446U: /* STM32F303xD/E and STM32F398xE */
		target_add_ram(target, 0x10000000, 0x4000);
		/* fall through */

	case 0x432U: /* STM32F37x */
	case 0x439U: /* STM32F302C8 */
		target->driver = "STM32F3";
		target->part_id = device_id;
		target_add_ram(target, 0x20000000, 0x10000);
		stm32f1_add_flash(target, 0x8000000, 0x80000, 0x800);
		target_add_commands(target, stm32f1_cmd_list, "STM32F3");
		return true;

	case 0x444U: /* STM32F03 RM0091 Rev. 7, STM32F030x[4|6] RM0360 Rev. 4 */
		target->driver = "STM32F03";
		flash_size = 0x8000;
		break;

	case 0x445U: /* STM32F04 RM0091 Rev. 7, STM32F070x6 RM0360 Rev. 4 */
		target->driver = "STM32F04/F070x6";
		flash_size = 0x8000;
		break;

	case 0x440U: /* STM32F05 RM0091 Rev. 7, STM32F030x8 RM0360 Rev. 4 */
		target->driver = "STM32F05/F030x8";
		flash_size = 0x10000;
		break;

	case 0x448U: /* STM32F07 RM0091 Rev. 7, STM32F070xb RM0360 Rev. 4 */
		target->driver = "STM32F07";
		flash_size = 0x20000;
		block_size = 0x800;
		break;

	case 0x442U: /* STM32F09 RM0091 Rev. 7, STM32F030xc RM0360 Rev. 4 */
		target->driver = "STM32F09/F030xc";
		flash_size = 0x40000;
		block_size = 0x800;
		break;

	default: /* NONE */
		return false;
	}

	target->part_id = device_id;
	target_add_ram(target, 0x20000000, 0x5000);
	stm32f1_add_flash(target, 0x8000000, flash_size, block_size);
	target_add_commands(target, stm32f1_cmd_list, "STM32F0");
	return true;
}

static bool stm32f1_flash_unlock(target_s *target, uint32_t bank_offset)
{
	target_mem_write32(target, FLASH_KEYR + bank_offset, KEY1);
	target_mem_write32(target, FLASH_KEYR + bank_offset, KEY2);
	uint32_t ctrl = target_mem_read32(target, FLASH_CR);
	if (ctrl & FLASH_CR_LOCK)
		DEBUG_ERROR("unlock failed, cr: 0x%08" PRIx32 "\n", ctrl);
	return !(ctrl & FLASH_CR_LOCK);
}

static inline void stm32f1_flash_clear_eop(target_s *const target, const uint32_t bank_offset)
{
	const uint32_t status = target_mem_read32(target, FLASH_SR + bank_offset);
	target_mem_write32(target, FLASH_SR + bank_offset, status | SR_EOP); /* EOP is W1C */
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
		status = target_mem_read32(target, FLASH_SR + bank_offset);
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
	if ((target->part_id == 0x430U && end >= FLASH_BANK_SPLIT && !stm32f1_flash_unlock(target, FLASH_BANK2_OFFSET)) ||
		(addr < FLASH_BANK_SPLIT && !stm32f1_flash_unlock(target, 0)))
		return false;

	const uint32_t bank_offset = stm32f1_bank_offset_for(addr);
	stm32f1_flash_clear_eop(target, bank_offset);

	/* Flash page erase instruction */
	target_mem_write32(target, FLASH_CR + bank_offset, FLASH_CR_PER);
	/* write address to FMA */
	target_mem_write32(target, FLASH_AR + bank_offset, addr);
	/* Flash page erase start instruction */
	target_mem_write32(target, FLASH_CR + bank_offset, FLASH_CR_STRT | FLASH_CR_PER);

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

	/* Start by writing any bank 1 data */
	if (offset) {
		stm32f1_flash_clear_eop(target, FLASH_BANK1_OFFSET);

		target_mem_write32(target, FLASH_CR, FLASH_CR_PG);
		/* Use the target API instead of a direct Cortex-M call for GD32VF103 parts */
		if (target->designer_code == JEP106_MANUFACTURER_RV_GIGADEVICE && target->cpuid == 0x80000022U)
			target_mem_write(target, dest, src, offset);
		else
			cortexm_mem_write_sized(target, dest, src, offset, ALIGN_HALFWORD);

		/* Wait for completion or an error */
		if (!stm32f1_flash_busy_wait(target, FLASH_BANK1_OFFSET, NULL))
			return false;
	}

	/* If there's anything to write left over and we're on a part with a second bank, write to bank 2 */
	const size_t remainder = len - offset;
	if (target->part_id == 0x430U && remainder) {
		const uint8_t *data = src;
		stm32f1_flash_clear_eop(target, FLASH_BANK2_OFFSET);

		target_mem_write32(target, FLASH_CR + FLASH_BANK2_OFFSET, FLASH_CR_PG);
		/* Use the target API instead of a direct Cortex-M call for GD32VF103 parts */
		if (target->designer_code == JEP106_MANUFACTURER_RV_GIGADEVICE && target->cpuid == 0x80000022U)
			target_mem_write(target, dest + offset, data + offset, remainder);
		else
			cortexm_mem_write_sized(target, dest + offset, data + offset, remainder, ALIGN_HALFWORD);

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
	target_mem_write32(target, FLASH_CR + bank_offset, FLASH_CR_MER);
	target_mem_write32(target, FLASH_CR + bank_offset, FLASH_CR_STRT | FLASH_CR_MER);

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
	if (target->part_id == 0x430U)
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
	target_mem_write32(target, FLASH_CR, FLASH_CR_OPTER | FLASH_CR_OPTWRE);
	target_mem_write32(target, FLASH_CR, FLASH_CR_STRT | FLASH_CR_OPTER | FLASH_CR_OPTWRE);

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
	target_mem_write32(target, FLASH_CR, FLASH_CR_OPTPG | FLASH_CR_OPTWRE);

	const uint32_t addr = FLASH_OBP_RDP + (offset * 2U);
	if (write16_broken)
		target_mem_write32(target, addr, 0xffff0000U | value);
	else
		target_mem_write16(target, addr, value);

	/* Wait for completion or an error */
	const bool result = stm32f1_flash_busy_wait(target, FLASH_BANK1_OFFSET, NULL);
	if (result || offset != 0U)
		return result;
	/*
	 * In the case that the write failed and we're handling option byte 0 (RDP),
	 * check if we got a status of "Program Error" in FLASH_SR, indicating the target
	 * refused to erase the read protection option bytes (and turn it into a truthy return).
	 */
	const uint8_t status = target_mem_read32(target, FLASH_SR) & SR_ERROR_MASK;
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
		uint32_t val = target_mem_read32(target, FLASH_OBP_RDP + i);
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
	 * GD32E230 is a special case as target_mem_write16 does not work
	 */
	const bool write16_broken = target->part_id == 0x410U && (target->cpuid & CPUID_PARTNO_MASK) == CORTEX_M23;
	for (size_t i = 0U; i < 8U; ++i) {
		if (!stm32f1_option_write_erased(target, i, opt_val[i], write16_broken))
			return false;
	}

	return true;
}

static bool stm32f1_cmd_option(target_s *target, int argc, const char **argv)
{
	const uint32_t read_protected = target_mem_read32(target, FLASH_OBR) & FLASH_OBR_RDPRT;
	const bool erase_requested = argc == 2 && strcmp(argv[1], "erase") == 0;
	/* Fast-exit if the Flash is not readable and the user didn't ask us to erase the option bytes */
	if (read_protected && !erase_requested) {
		tc_printf(target, "Device is Read Protected\nUse `monitor option erase` to unprotect and erase device\n");
		return true;
	}

	/* Unprotect the option bytes so we can modify them */
	if (!stm32f1_flash_unlock(target, FLASH_BANK1_OFFSET))
		return false;
	target_mem_write32(target, FLASH_OPTKEYR, KEY1);
	target_mem_write32(target, FLASH_OPTKEYR, KEY2);

	if (erase_requested) {
		/* When the user asks us to erase the option bytes, kick of an erase */
		if (!stm32f1_option_erase(target))
			return false;
		/*
		 * Write the option bytes Flash readable key, taking into account if we can
		 * use 32- or have to use 16-bit writes.
		 * GD32E230 is a special case as target_mem_write16 does not work
		 */
		const bool write16_broken = target->part_id == 0x410U && (target->cpuid & CPUID_PARTNO_MASK) == CORTEX_M23;
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
		const uint32_t val = target_mem_read32(target, addr);
		tc_printf(target, "0x%08X: 0x%04X\n", addr, val & 0xffffU);
		tc_printf(target, "0x%08X: 0x%04X\n", addr + 2U, val >> 16U);
	}

	return true;
}
