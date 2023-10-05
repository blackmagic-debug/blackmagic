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
#include "stm32_flash.h"

/* IDCODE register */
#define STM32_IDCODE_REVISION_ID_OFFSET 16U
#define STM32_IDCODE_REVISION_ID_MASK   (0xffffU << STM32_IDCODE_REVISION_ID_OFFSET)
#define STM32_IDCODE_DEVICE_ID_MASK     0xfffU

/* Noted as reserved by ST, but contains useful information for AT32F43x */
#define STM32_IDCODE_RESERVED_OFFSET 12U
#define STM32_IDCODE_RESERVED_MASK   (0xfU << STM32_IDCODE_RESERVED_OFFSET)

#define STM32F10X_IDCODE 0xe0042000U
#define STM32F0X_IDCODE  0x40015800U
#define GD32E5X_IDCODE   0xe0044000U
#define MM32L0X_IDCODE   0x40013400U
#define MM32FX_IDCODE    0x40007080U

#define AT32F40X_REVISION_ID 0x7005U
#define AT32F41X_REVISION_ID 0x7003U
#define AT32F43X_REVISION_ID 0x7008U

#define AT32F43X_RESERVED_B_2K 3U
#define AT32F43X_RESERVED_B_4K 4U

/* Electronic Signature (ESIG) registers */
#define GD32_ESIG_MEM_DENSITY(esig_base) ((esig_base) + 0x00U) /* Memory density register */
#define GD32_ESIG_MEM_DENSITY_RAM_OFFSET 16U
#define GD32_ESIG_MEM_DENSITY_RAM_MASK   (0xffffU << GD32_ESIG_MEM_DENSITY_RAM_OFFSET) /* KiB units */
#define GD32_ESIG_MEM_DENSITY_FLASH_MASK 0xffffU                                       /* KiB units */
#define GD32_ESIG_UID1(esig_base)        ((esig_base) + 0x08U) /* Unique ID register, bits 0:31 */
#define GD32_ESIG_UID2(esig_base)        ((esig_base) + 0x0cU) /* Unique ID register, bits 32:63 */
#define GD32_ESIG_UID3(esig_base)        ((esig_base) + 0x10U) /* Unique ID register, bits 64:95 */

#define GD32FX_ESIG_BASE  0x1ffff7e0U /* GD32Fx Electronic signature base address */
#define GD32F0X_ESIG_BASE 0x1ffff7ccU /* GD32F0x Electronic signature base address */

/* STM32F10X memory mapping */
#define STM32F10X_FLASH_MEMORY_ADDR     0x08000000U
#define STM32F10X_SRAM_ADDR             0x20000000U
#define STM32F10X_FLASH_BANK_SIZE       512U /* KiB */
#define STM32F10X_FLASH_BANK_SPLIT_ADDR (STM32F10X_FLASH_MEMORY_ADDR + (STM32F10X_FLASH_BANK_SIZE << 10U))

const command_s stm32_cmd_list[] = {
	{"option", stm32_option_bytes_cmd, "Manipulate option bytes"},
	{NULL, NULL, NULL},
};

static uint16_t stm32_read_idcode(target_s *const target)
{
	if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M0 ||
		(target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M23)
		return target_mem_read32(target, STM32F0X_IDCODE) & 0xfffU;
	/* Is this a Cortex-M33 core with STM32F1-style peripherals? (GD32E50x) */
	if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M33)
		return target_mem_read32(target, GD32E5X_IDCODE) & 0xfffU;

	return target_mem_read32(target, STM32F0X_IDCODE) & 0xfffU;
}

/* Identify GD32F1, GD32F2 and GD32F3 chips */
bool gd32f1_probe(target_s *const target)
{
	const uint16_t device_id = stm32_read_idcode(target);

	uint16_t block_size = 1U; /* KiB */
	switch (device_id) {
	case 0x414U: /* Gigadevice gd32f303 */
	case 0x430U:
		target->driver = "GD32F3";
		break;
	case 0x418U:
		target->driver = "GD32F2";
		break;
	case 0x410U: /* Gigadevice gd32f103, gd32e230 */
		if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M23)
			target->driver = "GD32E230";
		else if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M4)
			target->driver = "GD32F3";
		else
			target->driver = "GD32F1";
		break;
	case 0x444U: /* Gigadevice gd32e50x */
		target->driver = "GD32E5";
		block_size = 8U;
		break;
	default:
		return false;
	}

	target->part_id = device_id;

	/* Get flash capacity from ESIG register */
	const uint32_t memory_density = target_mem_read32(target, GD32FX_ESIG_BASE);
	const uint16_t flash_size = memory_density & GD32_ESIG_MEM_DENSITY_FLASH_MASK;
	const uint16_t ram_size = (memory_density & GD32_ESIG_MEM_DENSITY_RAM_MASK) >> GD32_ESIG_MEM_DENSITY_RAM_OFFSET;

	target_add_ram(target, 0x20000000, ram_size << 10U);
	stm32_add_flash(target, 0x8000000, flash_size << 10U, STM32F10X_FPEC_BASE, block_size << 10U);

	target_add_commands(target, stm32_cmd_list, target->driver);

	return true;
}

/* Identify RISC-V GD32VF1 chips */
bool gd32vf1_probe(target_s *const target)
{
	/* Make sure the architecture ID matches */
	if (target->cpuid != 0x80000022U)
		return false;

	/* Then read out the device IDCODE */
	const uint16_t device_id = target_mem_read32(target, STM32F10X_IDCODE) & STM32_IDCODE_DEVICE_ID_MASK;
	switch (device_id) {
	case 0x410U: /* GD32VF103 */
		target->driver = "GD32VF1";
		break;
	default:
		return false;
	}

	/* Get flash capacity from ESIG register */
	const uint32_t memory_density = target_mem_read32(target, GD32FX_ESIG_BASE);
	const uint16_t flash_size = memory_density & GD32_ESIG_MEM_DENSITY_FLASH_MASK;
	const uint16_t ram_size = (memory_density & GD32_ESIG_MEM_DENSITY_RAM_MASK) >> GD32_ESIG_MEM_DENSITY_RAM_OFFSET;

	target->part_id = device_id;

	target_add_ram(target, 0x20000000, ram_size << 10U);                                   /* KiB to bytes */
	stm32_add_flash(target, 0x8000000, flash_size << 10U, STM32F10X_FPEC_BASE, 1U << 10U); /* KiB to bytes */

	target_add_commands(target, stm32_cmd_list, target->driver);

	return true;
}

static bool at32f40x_probe(target_s *const target, const uint16_t device_id)
{
	/*
	 * Current driver supports only *default* memory layout (256 KB Flash / 96 KB SRAM)
	 * XXX: Support for external Flash for 512KB and 1024KB parts requires specific flash code (not implemented)
	 */
	switch (device_id) {
	case 0x0240U: /* AT32F403AVCT7 256KB / LQFP100 */
	case 0x0241U: /* AT32F403ARCT7 256KB / LQFP64 */
	case 0x0242U: /* AT32F403ACCT7 256KB / LQFP48 */
	case 0x0243U: /* AT32F403ACCU7 256KB / QFN48 */
		target->driver = "AT32F403x";
		break;
	case 0x0249U: /* AT32F407VCT7 256KB / LQFP100 */
	case 0x024aU: /* AT32F407RCT7 256KB / LQFP64 */
	case 0x0254U: /* AT32F407AVCT7 256KB / LQFP100 */
	case 0x02cdU: /* AT32F403AVET7 512KB / LQFP100 (*) */
	case 0x02ceU: /* AT32F403ARET7 512KB / LQFP64 (*) */
	case 0x02cfU: /* AT32F403ACET7 512KB / LQFP48 (*) */
	case 0x02d0U: /* AT32F403ACEU7 512KB / QFN48 (*) */
	case 0x02d1U: /* AT32F407VET7 512KB / LQFP100 (*) */
	case 0x02d2U: /* AT32F407RET7 512KB / LQFP64 (*) */
	case 0x0344U: /* AT32F403AVGT7 1024KB / LQFP100 (*) */
	case 0x0345U: /* AT32F403ARGT7 1024KB / LQFP64 (*) */
	case 0x0346U: /* AT32F403ACGT7 1024KB / LQFP48 (*) */
	case 0x0347U: /* AT32F403ACGU7 1024KB / QFN48 (found on BlackPill+ WeAct Studio) */
	case 0x034bU: /* AT32F407VGT7 1024KB / LQFP100 (*) */
	case 0x034cU: /* AT32F407VGT7 1024KB / LQFP64 (*) */
	case 0x0353U: /* AT32F407AVGT7 1024KB / LQFP100 (*) */
		target->driver = "AT32F407x";
		break;
	default:
		return false; /* Unknown/undocumented */
	}

	/* 256 KiB Flash with 2 KiB blocks */
	stm32_add_flash(target, 0x08000000, 256U << 10U, STM32F10X_FPEC_BASE, 2U << 10U); /* KiB to bytes */

	/* All parts have 96KB SRAM */
	target_add_ram(target, 0x20000000, 96U << 10U); /* KiB to bytes */

	return true;
}

static bool at32f41x_probe(target_s *const target, const uint16_t device_id)
{
	size_t flash_size = 0U; /* KiB */
	switch (device_id) {
	case 0x0240U:          /* LQFP64_10x10 */
	case 0x0241U:          /* LQFP48_7x7 */
	case 0x0242U:          /* QFN32_4x4 */
	case 0x0243U:          /* LQFP64_7x7 */
	case 0x024cU:          /* QFN48_6x6 */
		flash_size = 256U; /* KiB */
		break;
	case 0x01c4U:          /* LQFP64_10x10 */
	case 0x01c5U:          /* LQFP48_7x7 */
	case 0x01c6U:          /* QFN32_4x4 */
	case 0x01c7U:          /* LQFP64_7x7 */
	case 0x01cdU:          /* QFN48_6x6 */
		flash_size = 128U; /* KiB */
		break;
	case 0x0108U:         /* LQFP64_10x10 */
	case 0x0109U:         /* LQFP48_7x7 */
	case 0x010aU:         /* QFN32_4x4 */
		flash_size = 64U; /* KiB */
		break;
	default:
		return false; /* Unknown/undocumented */
	}

	target->driver = "AT32F415";

	/* Flash with 2 KiB blocks */
	stm32_add_flash(target, 0x08000000, flash_size << 10U, STM32F10X_FPEC_BASE, 2U << 10U); /* KiB to bytes */

	/* All parts have 32KB SRAM */
	target_add_ram(target, 0x20000000, 32U << 10U);

	return true;
}

static bool at32f43x_probe(target_s *const target, const uint16_t device_id)
{
	/*
	 * AT32F435 EOPB0 ZW/NZW split reconfiguration unsupported,
	 * assuming default split ZW=256 SRAM=384.
	 * AT32F437 also have a working "EMAC" (Ethernet MAC)
	 */
	size_t flash_size = 0; /* KiB */
	target_addr_t bank_split = 0;
	size_t block_size = 0; /* KiB */
	switch (device_id) {
	/* Parts with 4 KiB sectors */
	case 0x0540U: // LQFP144
	case 0x0543U: // LQFP100
	case 0x0546U: // LQFP64
	case 0x0549U: // LQFP48
	case 0x054cU: // QFN48
	case 0x054fU: // LQFP144 w/Eth
	case 0x0552U: // LQFP100 w/Eth
	case 0x0555U: // LQFP64 w/Eth
		/* Flash (G): 4032 KB in 2 banks (2048 + 1984) */
		flash_size = 4032U; /* KiB */
		bank_split = 0x08000000 + (2048U << 10U);
		block_size = 4U; /* KiB */
		break;
	case 0x0598U: // LQFP144
	case 0x0599U: // LQFP100
	case 0x059aU: // LQFP64
	case 0x059bU: // LQFP48
	case 0x059cU: // QFN48
	case 0x059dU: // LQFP144 w/Eth
	case 0x059eU: // LQFP100 w/Eth
	case 0x059fU: // LQFP64 w/Eth
		/* Flash (D): 448 KiB, only 1 bank */
		flash_size = 448U; /* KiB */
		block_size = 4U;   /* KiB */
		break;
	/* Parts with 2 KiB sectors */
	case 0x0341U: // LQFP144
	case 0x0344U: // LQFP100
	case 0x0347U: // LQFP64
	case 0x034aU: // LQFP48
	case 0x034dU: // QFN48
	case 0x0350U: // LQFP144 w/Eth
	case 0x0353U: // LQFP100 w/Eth
	case 0x0356U: // LQFP64 w/Eth
		/* Flash (M): 1024 KB in 2 banks (equal sized) */
		flash_size = 1024U; /* KiB */
		bank_split = 0x08000000 + (512U << 10U);
		block_size = 2U; /* KiB */
		break;
	case 0x0242U: // LQFP144
	case 0x0245U: // LQFP100
	case 0x0248U: // LQFP64
	case 0x024bU: // LQFP48
	case 0x024eU: // QFN48
	case 0x0251U: // LQFP144 w/Eth
	case 0x0254U: // LQFP100 w/Eth
	case 0x0257U: // LQFP64 w/Eth
		/* Flash (C): 256 KB, only 1 bank */
		flash_size = 256U; /* KiB */
		block_size = 2U;   /* KiB */
		break;
	default:
		return false;
	}

	target->driver = "AT32F435";

	/*
	 * FIXME: This sounds like it can be handled
	 * Arterytek F43x Flash controller has BLKERS (1U << 3U).
	 * Block erase operates on 64 KB at once for all parts.
	 * Using here only sector erase (page erase) for compatibility.
	 */
	if (bank_split)
		stm32_add_banked_flash(target, 0x08000000, flash_size << 10U, bank_split, STM32F10X_FPEC_BASE,
			block_size << 10U); /* KiB to bytes */
	else
		stm32_add_flash(target, 0x08000000, flash_size, STM32F10X_FPEC_BASE, block_size << 10U); /* KiB to bytes */

	/*
	 * FIXME: handle dynamic SRAM mapping (see SAMx7x)
	 * SRAM total is adjustable between 128 KB and 512 KB (max).
	 * Out of 640 KB SRAM present on silicon, at least 128 KB are always
	 * dedicated to "zero-wait-state Flash". ZW region is limited by
	 * specific part flash capacity (for 256, 448 KiB) or at 512 KiB.
	 * AT32F435ZMT default EOPB0=0xffff05fa,
	 * EOPB[0:2]=0b010 for 384 KiB SRAM + 256 KiB zero-wait-state flash.
	 */

	/* SRAM1 (64 KiB) can be remapped to 0x10000000 */
	target_add_ram(target, 0x20000000 + 7U, 64U << 10U);
	/* SRAM2 (384 - 64 = 320 KiB default) */
	target_add_ram(target, 0x20010000, 320U << 10U);

	return true;
}

/* Identify AT32F4x devices (Cortex-M4) */
bool at32fxx_probe(target_s *const target)
{
	// Artery clones use Cortex M4 cores
	if ((target->cpuid & CORTEX_CPUID_PARTNO_MASK) != CORTEX_M4)
		return false;

	// Artery chips use the complete idcode word for identification
	const uint32_t idcode = target_mem_read32(target, STM32F10X_IDCODE);
	const uint16_t revision_id = (idcode & STM32_IDCODE_REVISION_ID_MASK) >> STM32_IDCODE_REVISION_ID_OFFSET;
	const uint16_t device_id = idcode & STM32_IDCODE_DEVICE_ID_MASK;

	if (revision_id == AT32F40X_REVISION_ID)
		return at32f40x_probe(target, device_id);
	if (revision_id == AT32F41X_REVISION_ID)
		return at32f41x_probe(target, device_id);
	if (revision_id == AT32F43X_REVISION_ID)
		return at32f43x_probe(target, device_id);
	return false;
}

/*
 * On STM32, 16-bit writes use bits 0:15 for even halfwords; bits 16:31 for odd halfwords.
 * On MM32 cortex-m0, 16-bit writes always use bits 0:15.
 * Set both halfwords to the same value, works on both STM32 and NN32.
 */
void mm32l0_mem_write_sized(
	adiv5_access_port_s *const ap, uint32_t dest, const void *src, size_t len, const align_e align)
{
	uint32_t odest = dest;

	len >>= align;
	ap_mem_access_setup(ap, dest, align);
	while (len--) {
		uint32_t tmp = 0;
		/* Pack data into correct data lane */
		switch (align) {
		case ALIGN_8BIT: {
			uint8_t value;
			memcpy(&value, src, sizeof(value));
			/* copy byte to be written to all four bytes of the uint32_t */
			tmp = (uint32_t)value;
			tmp = tmp | tmp << 8U;
			tmp = tmp | tmp << 16U;
			break;
		}
		case ALIGN_16BIT: {
			uint16_t value;
			memcpy(&value, src, sizeof(value));
			/* copy halfword to be written to both halfwords of the uint32_t */
			tmp = (uint32_t)value;
			tmp = tmp | tmp << 16U;
			break;
		}
		case ALIGN_64BIT:
		case ALIGN_32BIT:
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
bool mm32l0xx_probe(target_s *const target)
{
	const uint32_t idcode = target_mem_read32(target, MM32L0X_IDCODE);

	size_t flash_size = 0;
	size_t ram_size = 0;
	switch (idcode) {
	case 0xcc568091U:
		target->driver = "MM32L07x";
		flash_size = 128U; /* KiB */
		ram_size = 8U;     /* KiB */
		break;
	case 0xcc56a097U:
		target->driver = "MM32SPIN27";
		flash_size = 128U; /* KiB */
		ram_size = 12U;    /* KiB */
		break;
	default:
		DEBUG_WARN("%s: unknown MM32 IDCODE 0x%" PRIx32 "\n", __func__, idcode);
		return false;
	}

	target->part_id = idcode & STM32_IDCODE_DEVICE_ID_MASK;

	cortex_ap(target)->dp->mem_write = mm32l0_mem_write_sized;

	target_add_ram(target, 0x20000000U, ram_size << 10U);                                    /* KiB to bytes */
	stm32_add_flash(target, 0x08000000U, flash_size << 10U, STM32F10X_FPEC_BASE, 1U << 10U); /* KiB to bytes */

	target_add_commands(target, stm32_cmd_list, target->driver);

	return true;
}

/* Identify MM32 devices (Cortex-M3, Star-MC1) */
bool mm32f3xx_probe(target_s *target)
{
	const uint32_t idcode = target_mem_read32(target, MM32FX_IDCODE);

	size_t flash_size = 0; /* KiB */
	size_t ram1_size = 0;  /* RAM at 0x20000000, KiB */
	size_t ram2_size = 0;  /* RAM at 0x30000000, KiB */
	switch (idcode) {
	case 0xcc9aa0e7U:
		target->driver = "MM32F3273";
		flash_size = 512U; /* KiB */
		ram1_size = 128U;  /* KiB */
		break;
	case 0x4d4d0800U:
		target->driver = "MM32F5277";
		flash_size = 256U; /* KiB */
		ram1_size = 32U;   /* KiB */
		ram2_size = 128U;  /* KiB */
		break;
	default:
		DEBUG_WARN("%s: unknown MM32 IDCODE 0x%" PRIx32 "\n", __func__, idcode);
		return false;
	}

	target->part_id = idcode & STM32_IDCODE_DEVICE_ID_MASK;

	stm32_add_flash(target, 0x08000000U, flash_size << 10U, STM32F10X_FPEC_BASE, 1U << 10U); /* KiB to bytes */
	if (ram1_size)
		target_add_ram(target, 0x20000000U, ram1_size << 10U); /* KiB to bytes */
	if (ram2_size)
		target_add_ram(target, 0x30000000U, ram2_size << 10U); /* KiB to bytes */

	target_add_commands(target, stm32_cmd_list, target->driver);

	return true;
}

/* Identify real STM32F0/F1/F3 devices */
bool stm32f1_probe(target_s *target)
{
	const uint16_t device_id = stm32_read_idcode(target);

	size_t ram_size = 20U;  /* KiB */
	size_t flash_size = 0;  /* KiB */
	size_t block_size = 1U; /* KiB */
	switch (device_id) {
	case 0x29bU: /* CS clone */
	case 0x410U: /* Medium density */
	case 0x412U: /* Low density */
	case 0x420U: /* Value Line, Low-/Medium density */
		/* Test for clone parts with Core rev 2*/
		if ((cortex_ap(target)->idr >> 28U) > 1U) {
			target->driver = "STM32F1 (clone) medium density";
			DEBUG_WARN("Detected clone STM32F1\n");
		} else
			target->driver = "STM32F1 medium density";
		flash_size = 128U; /* KiB */
		break;

	case 0x414U: /* High density */
	case 0x418U: /* Connectivity Line */
	case 0x428U: /* Value Line, High Density */
		target->driver = "STM32F1 VL density";
		ram_size = 64U;    /* KiB */
		flash_size = 512U; /* KiB */
		block_size = 2U;   /* KiB */
		break;

	case 0x430U: /* XL-density */
		target->driver = "STM32F1 XL density";
		ram_size = 96U;     /* KiB */
		flash_size = 1024U; /* KiB */
		block_size = 2U;    /* KiB */
		break;

	case 0x438U: /* STM32F303x6/8 and STM32F328 */
	case 0x422U: /* STM32F30x */
	case 0x446U: /* STM32F303xD/E and STM32F398xE */
		/* Additional RAM */
		target_add_ram(target, 0x10000000, 16U << 10U); /* KiB to bytes */

		/* fall through */
	case 0x432U: /* STM32F37x */
	case 0x439U: /* STM32F302C8 */
		target->driver = "STM32F3";
		ram_size = 64U;    /* KiB */
		flash_size = 512U; /* KiB */
		block_size = 2U;   /* KiB */
		break;

	case 0x444U: /* STM32F03 RM0091 Rev. 7, STM32F030x[4|6] RM0360 Rev. 4 */
		target->driver = "STM32F03";
		flash_size = 32U; /* KiB */
		break;

	case 0x445U: /* STM32F04 RM0091 Rev. 7, STM32F070x6 RM0360 Rev. 4 */
		target->driver = "STM32F04/F070x6";
		flash_size = 32U; /* KiB */
		break;

	case 0x440U: /* STM32F05 RM0091 Rev. 7, STM32F030x8 RM0360 Rev. 4 */
		target->driver = "STM32F05/F030x8";
		flash_size = 64U; /* KiB */
		break;

	case 0x448U: /* STM32F07 RM0091 Rev. 7, STM32F070xb RM0360 Rev. 4 */
		target->driver = "STM32F07";
		flash_size = 128U; /* KiB */
		block_size = 2U;   /* KiB */
		break;

	case 0x442U: /* STM32F09 RM0091 Rev. 7, STM32F030xc RM0360 Rev. 4 */
		target->driver = "STM32F09/F030xc";
		flash_size = 256U; /* KiB */
		block_size = 2U;   /* KiB */
		break;

	default:
		return false; /* Unknown/undocumented */
	}

	target->part_id = device_id;

	target_add_ram(target, STM32F10X_SRAM_ADDR, ram_size << 10U); /* KiB to bytes */

	if (flash_size > STM32F10X_FLASH_BANK_SIZE)
		stm32_add_banked_flash(target, STM32F10X_FLASH_MEMORY_ADDR, flash_size << 10U, STM32F10X_FLASH_BANK_SPLIT_ADDR,
			STM32F10X_FPEC_BASE, block_size << 10U);
	else
		stm32_add_flash(target, STM32F10X_FLASH_MEMORY_ADDR, flash_size << 10U, STM32F10X_FPEC_BASE,
			block_size << 10U); /* KiB to bytes */

	target_add_commands(target, stm32_cmd_list, target->driver);

	return true;
}
