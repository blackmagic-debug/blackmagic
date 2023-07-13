/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 dpf ("neutered") <weasel@cs.stanford.edu>
 * Written by dpf ("neutered") <weasel@cs.stanford.edu>
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
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
 * This file implements MSP432 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * Refereces:
 * TI doc - SLAU723a
 *   MSP423e4xx Technical Reference Manual (https://www.ti.com/lit/ug/slau723a/slau723a.pdf)
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "buffer_utils.h"

#define MSP432E4_EEPROM_BASE     0x400af000U
#define MSP432E4_FLASH_CTRL_BASE 0x400fd000U
#define MSP432E4_SYS_CTRL_BASE   0x400fe000U

/*
 * DEVID0
 *  [31]    - 0 - reserved
 *  [30:28] - 001 - version
 *  [27:24] - 1000 - reserved
 *  [23:16] - 0c - device class == msp432e4
 *  [15:8]  - xx - major
 *  [7:0]   - xx - minor
 * DEVID1
 *  [31:28] - 0001 - version
 *  [27:24] - 0000 - device family == msp432e4
 *  [23:16] - xx - part number w/in family
 *  [15:13] - bbb - pin count
 *  [12:8]  - 00000 - reserved
 *  [7:5]   - bbb - temperature range
 *  [4:3]   - bb - package type
 *  [2]     - b - rohs
 *  [1:0]   - bb - qualification status
 */
#define MSP432E4_SYS_CTRL_DID0                   (MSP432E4_SYS_CTRL_BASE + 0x0000U)
#define MSP432E4_SYS_CTRL_DID0_CLASS_MASK        0xffff0000U
#define MSP432E4_SYS_CTRL_DID0_MSP432E4          0x180c0000U
#define MSP432E4_SYS_CTRL_DID0_VERSION_MAJ_SHIFT 8U
#define MSP432E4_SYS_CTRL_DID0_VERSION_MAJ_MASK  0xffU
#define MSP432E4_SYS_CTRL_DID0_VERSION_MIN_MASK  0xffU

#define MSP432E4_SYS_CTRL_DID1                    (MSP432E4_SYS_CTRL_BASE + 0x0004U)
#define MSP432E4_SYS_CTRL_DID1_FAMILY_MASK        0xff000000U
#define MSP432E4_SYS_CTRL_DID1_MSP432E4           0x10000000U
#define MSP432E4_SYS_CTRL_DID1_PART_NUM_SHIFT     16U
#define MSP432E4_SYS_CTRL_DID1_PART_NUM_MASK      0xffU
#define MSP432E4_SYS_CTRL_DID1_PIN_COUNT_SHIFT    13U
#define MSP432E4_SYS_CTRL_DID1_PIN_COUNT_MASK     0x7U
#define MSP432E4_SYS_CTRL_DID1_TEMP_RANGE_SHIFT   5U
#define MSP432E4_SYS_CTRL_DID1_TEMP_RANGE_MASK    0x7U
#define MSP432E4_SYS_CTRL_DID1_PACKAGE_TYPE_SHIFT 3U
#define MSP432E4_SYS_CTRL_DID1_PACKAGE_TYPE_MASK  0x3U

/*
 * Boot Config
 *  [31] - b - lock register
 *  [30:16] - 7fff - reserved
 *  [15:13] - x - gpio port
 *  [12:10] - x - gpio pin
 *  [9] - b - gpio polarity
 *  [8] - b - gpio enable
 *  [7:5] - 111 - reserved
 *  [4] - b - key select
 *  [3:2] - 11 - reserved
 *  [1:0] - bb - debug control
 */
#define MSP432E4_SYS_CTRL_BOOTCFG     (MSP432E4_SYS_CTRL_BASE + 0x01d0U)
#define MSP432E4_SYS_CTRL_BOOTCFG_KEY (1U << 4U)

/*
 * Flash Peripheral Properties
 *  [31] - 0 - reserved
 *  [30] - b - prefetch buffer mode
 *  [29] - b - flash mirror mode
 *  [28] - b - dma flash access
 *  [27:23] - 00000 - reserved
 *  [22:19] - bbbb - eeprom sector size
 *  [18:16] - bbb - flash sector size
 *  [15:0] - xxxx - flash size
 */
#define MSP432E4_FLASH_PERIPH_PROP (MSP432E4_FLASH_CTRL_BASE + 0x0fc0U)

/*
 * SRAM Size
 *  [31:16] - 0000 - reserved
 *  [15:0] - xxxx - sram size
 */
#define MSP432E4_FLASH_SRAM_SIZE (MSP432E4_FLASH_CTRL_BASE + 0x0fc4U)

/*
 * Control1
 *  [31:16] - xxxx - write key
 *  [15:4] - 000 - reserved
 *  [3] - b - commit
 *  [2] - b - mass erase
 *  [1] - b - erase sector
 *  [0] - b - write
 * Control2
 *  [31:16] - xxxx - write key
 *  [15:1] - 0000 - reserved
 *  [0] - b - buffered flash memory write
 */
#define MSP432E4_FLASH_CTRL            (MSP432E4_FLASH_CTRL_BASE + 0x0008U)
#define MSP432E4_FLASH_CTRL2           (MSP432E4_FLASH_CTRL_BASE + 0x0020U)
#define MSP432E4_FLASH_CTRL_WRITE      (1U << 0U)
#define MSP432E4_FLASH_CTRL_ERASE      (1U << 1U)
#define MSP432E4_FLASH_CTRL_MASS_ERASE (1U << 2U)
#define MSP432E4_FLASH_CTRL_COMMIT     (1U << 3U)

/*
 * Raw Interrupt Status
 *  [31:14] - 00000 - reserved
 *  [13] - b - program verify
 *  [12] - 0 - reserved
 *  [11] - b - erase verify
 *  [10] - b - invalid data
 *  [9] - b - pump voltage
 *  [8:3] - 00 - reserved
 *  [2] - b - eeprom status
 *  [1] - b - program status
 *  [0] - b - access status
 */
#define MSP432E4_FLASH_FCRIS (MSP432E4_FLASH_CTRL_BASE + 0x000cU)

/*
 * Flash Write Key
 *  [31:16] - 0000 - reserved
 *  [15:0] - xxxx - key
 */
#define MSP432E4_FLASH_FLPEKEY (MSP432E4_FLASH_CTRL_BASE + 0x003cU)

/*
 * Flash Access Address
 *  [31:20] - 000 - reserved
 *  [19:0] - xxxxx - operation aligned address
 */
#define MSP432E4_FLASH_ADDR (MSP432E4_FLASH_CTRL_BASE + 0x0000U)

/* Flash Data */
#define MSP432E4_FLASH_DATA (MSP432E4_FLASH_CTRL_BASE + 0x0004U)

/*
 * EEPROM Size
 *  [31:16] - xxxx - # 16bit words
 *  [15:0] - xxxx - # 32bit words
 */
#define MSP432E4_EEPROM_SIZE (MSP432E4_EEPROM_BASE + 0x0000U)

#define MSP432E4_SRAM_BASE  0x20000000U
#define MSP432E4_FLASH_BASE 0x00000000U

/* The Flash routines can write only 4 bytes at a time, so let the target Flash layer take care of the rest */
#define MSP432E4_FLASH_WRITE_SIZE 4U

typedef struct msp432e4_flash {
	target_flash_s target_flash;
	uint16_t flash_key;
} msp432e4_flash_s;

static bool msp432e4_flash_erase(target_flash_s *flash, target_addr_t addr, size_t length);
static bool msp432e4_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t length);
static bool msp432e4_mass_erase(target_s *target);

static void msp432e4_add_flash(
	target_s *const target, const uint32_t sector_size, const uint32_t base, const size_t length)
{
	msp432e4_flash_s *const flash = calloc(1, sizeof(*flash));
	if (flash == NULL) {
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *target_flash = &flash->target_flash;
	target_flash->start = base;
	target_flash->length = length;
	target_flash->blocksize = sector_size;
	target_flash->writesize = MSP432E4_FLASH_WRITE_SIZE;
	target_flash->erase = msp432e4_flash_erase;
	target_flash->write = msp432e4_flash_write;
	target_flash->erased = 0xff;
	target_add_flash(target, target_flash);

	/* If the boot config KEY bit is set, use the fixed key value, otherwise read out the configured key */
	if (target_mem_read32(target, MSP432E4_SYS_CTRL_BOOTCFG) & MSP432E4_SYS_CTRL_BOOTCFG_KEY)
		flash->flash_key = 0xa442U;
	else
		flash->flash_key = (uint16_t)target_mem_read32(target, MSP432E4_FLASH_FLPEKEY);
}

bool msp432e4_probe(target_s *const target)
{
	const uint32_t devid0 = target_mem_read32(target, MSP432E4_SYS_CTRL_DID0);
	const uint32_t devid1 = target_mem_read32(target, MSP432E4_SYS_CTRL_DID1);
	DEBUG_INFO("%s: Device ID %" PRIx32 ":%" PRIx32 "\n", __func__, devid0, devid1);

	/* Does it look like an msp432e4 variant? */
	if ((devid0 & MSP432E4_SYS_CTRL_DID0_CLASS_MASK) != MSP432E4_SYS_CTRL_DID0_MSP432E4 ||
		(devid1 & MSP432E4_SYS_CTRL_DID1_FAMILY_MASK) != MSP432E4_SYS_CTRL_DID1_MSP432E4)
		return false;

	DEBUG_TARGET("%s: Device version %x:%x, part ID %x, pin count %u, temperature grade %x, package type %x\n",
		__func__, (devid0 >> MSP432E4_SYS_CTRL_DID0_VERSION_MAJ_SHIFT) & MSP432E4_SYS_CTRL_DID0_VERSION_MAJ_MASK,
		devid0 & MSP432E4_SYS_CTRL_DID0_VERSION_MIN_MASK,
		(devid1 >> MSP432E4_SYS_CTRL_DID1_PART_NUM_SHIFT) & MSP432E4_SYS_CTRL_DID1_PART_NUM_MASK,
		(devid1 >> MSP432E4_SYS_CTRL_DID1_PIN_COUNT_SHIFT) & MSP432E4_SYS_CTRL_DID1_PIN_COUNT_MASK,
		(devid1 >> MSP432E4_SYS_CTRL_DID1_TEMP_RANGE_SHIFT) & MSP432E4_SYS_CTRL_DID1_TEMP_RANGE_MASK,
		(devid1 >> MSP432E4_SYS_CTRL_DID1_PACKAGE_TYPE_SHIFT) & MSP432E4_SYS_CTRL_DID1_PACKAGE_TYPE_MASK);

	target->driver = "MSP432E4";
	target->mass_erase = msp432e4_mass_erase;

	/* SRAM is banked but interleaved into one logical bank */
	const uint32_t sram_size = ((target_mem_read32(target, MSP432E4_FLASH_SRAM_SIZE) & 0xffffU) + 1U) * 256U;
	target_add_ram(target, MSP432E4_SRAM_BASE, sram_size);

	/* Flash is in four banks but two-way interleaved */
	const uint32_t flash_props = target_mem_read32(target, MSP432E4_FLASH_PERIPH_PROP);
	const uint32_t flash_size = ((flash_props & 0xffffU) + 1U) * 2048U;
	/*
	 * Convert the Flash sector size from a value between 1 (2kiB) and 4 (16kiB) to a value of
	 * 2, 4, 8 or 16. Then multiply by a kibibyte to land on the final size.
	 */
	const uint32_t flash_sector_size = (UINT32_C(1) << ((flash_props >> 16U) & 7U)) * 1024U;

	/*
	 * While the Flash is in a banked 2x2 arrangement, this doesn't matter in practical terms
	 * because the controller hides this for us behind a cohearent interface.
	 * Register just the one big linear region.
	 */
	msp432e4_add_flash(target, flash_sector_size, MSP432E4_FLASH_BASE, flash_size);

	/* All done */
	return true;
}

/* Erase from addr for length bytes */
static bool msp432e4_flash_erase(target_flash_s *const target_flash, const target_addr_t addr, const size_t length)
{
	(void)length;
	target_s *const target = target_flash->t;
	const msp432e4_flash_s *const flash = (msp432e4_flash_s *)target_flash;
	/*
	 * The target Flash layer guarantees we're called at the start of each target_flash->blocksize
	 * so we only need to trigger the erase of the Flash sector pair and that logic will take care of the rest.
	 */
	target_mem_write32(target, MSP432E4_FLASH_ADDR, addr);
	target_mem_write32(target, MSP432E4_FLASH_CTRL, (flash->flash_key << 16U) | MSP432E4_FLASH_CTRL_ERASE);
	while (target_mem_read32(target, MSP432E4_FLASH_CTRL) & MSP432E4_FLASH_CTRL_ERASE)
		continue;
	return true;
}

/* Program flash */
static bool msp432e4_flash_write(
	target_flash_s *const target_flash, target_addr_t dest, const void *const src, const size_t length)
{
	(void)length;
	target_s *const target = target_flash->t;
	const msp432e4_flash_s *const flash = (msp432e4_flash_s *)target_flash;
	/*
	 * The target Flash layer guarantees that we're called with a length that's a complete write size
	 * and that the source data buffer is filled with the erase byte value so we don't disturb unwritten
	 * Flash. With the write size set to 4 to match how many bytes we can write in one go, that
	 * allows this routine to go 32-bit block at a time efficiently, passing the complexity up a layer.
	 */
	target_mem_write32(target, MSP432E4_FLASH_ADDR, dest);
	target_mem_write32(target, MSP432E4_FLASH_DATA, read_le4((const uint8_t *)src, 0));
	target_mem_write32(target, MSP432E4_FLASH_CTRL, (flash->flash_key << 16U) | MSP432E4_FLASH_CTRL_WRITE);
	while (target_mem_read32(target, MSP432E4_FLASH_CTRL) & MSP432E4_FLASH_CTRL_WRITE)
		continue;
	return true;
}

/* Mass erases the Flash */
static bool msp432e4_mass_erase(target_s *const target)
{
	const msp432e4_flash_s *const flash = (msp432e4_flash_s *)target->flash;
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	/* Kick off the mass erase */
	target_mem_write32(target, MSP432E4_FLASH_CTRL, (flash->flash_key << 16U) | MSP432E4_FLASH_CTRL_MASS_ERASE);
	/* Wait for the erase to complete, printing a '.' every so often to keep GDB happy */
	while (target_mem_read32(target, MSP432E4_FLASH_CTRL) & MSP432E4_FLASH_CTRL_MASS_ERASE)
		target_print_progress(&timeout);
	return true;
}
