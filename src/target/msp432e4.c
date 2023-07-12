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
#define MSP432E4_SYS_CTRL_DID0          (MSP432E4_SYS_CTRL_BASE + 0x0000U)
#define MSP432E4_SYS_CTRL_DID0_MASK     0xffff0000U
#define MSP432E4_SYS_CTRL_DID0_MSP432E4 0x180c0000U

#define MSP432E4_SYS_CTRL_DID1          (MSP432E4_SYS_CTRL_BASE + 0x0004U)
#define MSP432E4_SYS_CTRL_DID1_MASK     0xff000000U
#define MSP432E4_SYS_CTRL_DID1_MSP432E4 0x10000000U

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

/*
 * XXX: The flash block size might be nice to use, but the part used in
 * testing has a 16k sector which is too big w/ the current build setup.
 */
#define BUFFERED_WRITE_SIZE 0x100

typedef struct msp432e4_flash {
	target_flash_s target_flash;
	uint16_t flash_key;
} msp432e4_flash_s;

static bool msp432e4_cmd_erase(target_s *t, int argc, const char **argv);

/* Optional commands structure*/
static const command_s msp432e4_cmd_list[] = {
	{"erase", msp432e4_cmd_erase, "Erase flash: all | <sector> <n sectors>?"},
	{NULL, NULL, NULL},
};

static bool msp432e4_flash_erase_sectors(target_flash_s *flash, target_addr_t addr, size_t len);
static bool msp432e4_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);

static void msp432e4_add_flash(target_s *const target, const uint32_t sector, const uint32_t base, const size_t length)
{
	msp432e4_flash_s *const flash = calloc(1, sizeof(*flash));
	if (flash == NULL) {
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *target_flash = &flash->target_flash;
	target_flash->start = base;
	target_flash->length = length;
	target_flash->blocksize = sector;
	target_flash->erase = msp432e4_flash_erase_sectors;
	target_flash->write = msp432e4_flash_write;
	target_flash->writesize = BUFFERED_WRITE_SIZE;
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
	DEBUG_INFO("%s: devid %" PRIx32 ":%" PRIx32 "\n", __func__, devid0, devid1);

	/* Does it look like an msp432e4 variant? */
	if ((devid0 & MSP432E4_SYS_CTRL_DID0_MASK) != MSP432E4_SYS_CTRL_DID0_MSP432E4 ||
		(devid1 & MSP432E4_SYS_CTRL_DID1_MASK) != MSP432E4_SYS_CTRL_DID1_MSP432E4)
		return false;

	DEBUG_INFO("%s: ver %x:%x part %x pin %x temp %x package %x\n", __func__, (devid0 >> 8) & 0xff,
		(devid0 >> 0) & 0xff, (devid1 >> 16) & 0xff, (devid1 >> 13) & 0x7, (devid1 >> 5) & 0x7, (devid1 >> 3) & 0x3);

	/* EEPROM size as # of 32bit words but not directly accessible */
	const uint32_t eeprom_size = (target_mem_read32(target, MSP432E4_EEPROM_SIZE) & 0xffffU) * 4U;

	/* SRAM is banked but interleaved into one logical bank */
	const uint32_t sram_size = ((target_mem_read32(target, MSP432E4_FLASH_SRAM_SIZE) & 0xffffU) + 1U) * 256U;

	/* Flash is in four banks but two-way interleaved */
	const uint32_t flash_props = target_mem_read32(target, MSP432E4_FLASH_PERIPH_PROP);
	const uint32_t flash_size = ((flash_props & 0xffffU) + 1U) * 2048U;
	const uint32_t flash_sector = (1U << ((flash_props >> 16U) & 0x07U)) * 1024U;

#define FORMAT "MSP432E4 %" PRIu32 "k eeprom / %" PRIu32 "k sram / %" PRIu32 "k flash"
	size_t nb = snprintf(NULL, 0, FORMAT, eeprom_size / 1024U, sram_size / 1024U, flash_size / 1024U);
	char *p = (char *)malloc(nb + 1);
	if (p == NULL)
		return false;
	snprintf(p, nb + 1, FORMAT, eeprom_size / 1024U, sram_size / 1024U, flash_size / 1024U);
	target->driver = p;
	target->target_storage = p;
#undef FORMAT

	target_add_ram(target, MSP432E4_SRAM_BASE, sram_size);

	/* the flash is physically 4x but is 2x banked and 2x interleaved. */
	msp432e4_add_flash(target, flash_sector, MSP432E4_FLASH_BASE, flash_size / 2U);
	msp432e4_add_flash(target, flash_sector, MSP432E4_FLASH_BASE + flash_size / 2U, flash_size / 2U);

	/* Connect the optional commands */
	target_add_commands(target, msp432e4_cmd_list, "MSP432E4");

	/* All done */
	return true;
}

/* Erase from addr for len bytes */
static bool msp432e4_flash_erase_sectors(target_flash_s *const target_flash, const target_addr_t addr, const size_t len)
{
	target_s *target = target_flash->t;
	const msp432e4_flash_s *const flash = (msp432e4_flash_s *)target_flash;
	const uint32_t fmc = (flash->flash_key << 16U) | MSP432E4_FLASH_CTRL_ERASE;

	for (size_t offset = 0; offset < len; offset += target_flash->blocksize) {
		target_mem_write32(target, MSP432E4_FLASH_ADDR, addr + offset);
		target_mem_write32(target, MSP432E4_FLASH_CTRL, fmc);
		/* FixMe - verify/timeout bit? */
		while (target_mem_read32(target, MSP432E4_FLASH_CTRL) & MSP432E4_FLASH_CTRL_ERASE)
			continue;
	}
	return true;
}

/* Program flash */
static bool msp432e4_flash_write(
	target_flash_s *const target_flash, target_addr_t dest, const void *const src, const size_t len)
{
	target_s *const target = target_flash->t;
	const msp432e4_flash_s *const flash = (msp432e4_flash_s *)target_flash;
	const uint32_t *const buffer = (const uint32_t *)src;

	const uint32_t fmc = (flash->flash_key << 16U) | MSP432E4_FLASH_CTRL_WRITE;

	/* Transfer the aligned part, 1 uint32_t at a time */
	const size_t aligned_len = len & ~3U;
	for (size_t offset = 0; offset < aligned_len; offset += 4U) {
		target_mem_write32(target, MSP432E4_FLASH_ADDR, dest + offset);
		target_mem_write32(target, MSP432E4_FLASH_DATA, buffer[offset >> 2U]);
		target_mem_write32(target, MSP432E4_FLASH_CTRL, fmc);
		while (target_mem_read32(target, MSP432E4_FLASH_CTRL) & MSP432E4_FLASH_CTRL_WRITE)
			continue;
	}

	/* Pack and transfer the remainder */
	if (len != aligned_len) {
		uint8_t data[4] = {0U};
		memcpy(data, buffer + (aligned_len >> 2U), len - aligned_len);
		target_mem_write32(target, MSP432E4_FLASH_ADDR, dest + aligned_len);
		target_mem_write32(target, MSP432E4_FLASH_DATA, read_le4(data, 0));
		target_mem_write32(target, MSP432E4_FLASH_CTRL, fmc);
		while (target_mem_read32(target, MSP432E4_FLASH_CTRL) & MSP432E4_FLASH_CTRL_WRITE)
			continue;
	}

	return true;
}

/* Special case command for erase all flash */
static bool msp432e4_flash_erase_all(target_s *const target)
{
	const msp432e4_flash_s *const flash = (msp432e4_flash_s *)target->flash;
	uint32_t fmc = (flash->flash_key << 16U) | MSP432E4_FLASH_CTRL_MASS_ERASE;
	target_mem_write32(target, MSP432E4_FLASH_CTRL, fmc);
	/* FixMe - timeout/verify bit? */
	while (target_mem_read32(target, MSP432E4_FLASH_CTRL) & MSP432E4_FLASH_CTRL_MASS_ERASE)
		continue;
	return true;
}

/* Optional commands handlers */
static bool msp432e4_cmd_erase(target_s *const target, const int argc, const char **const argv)
{
	if (argc != 2 && argc != 3)
		goto err_usage;

	if (strcmp(argv[1], "all") == 0) {
		if (argc != 2)
			goto err_usage;
		return msp432e4_flash_erase_all(target);
	}

	char *end = NULL;
	const uint32_t addr = strtoul(argv[1], &end, 0);
	if (end == argv[1])
		goto err_usage;
	target_flash_s *flash = target->flash;
	while (flash != NULL) {
		if ((flash->start <= addr) && (addr < flash->start + flash->length))
			break;
		flash = flash->next;
	}
	if (flash == NULL)
		goto err_usage;

	uint32_t n = 1;
	if (argc == 3) {
		n = strtoul(argv[2], &end, 0);
		if (end == argv[2])
			goto err_usage;
	}
	n *= flash->blocksize;
	if (n == 0 || addr + n > flash->start + flash->length)
		goto err_usage;

	return msp432e4_flash_erase_sectors(flash, addr, n);

err_usage:
	tc_printf(target, "usage: monitor erase (all | <addr> <n>?)\n");
	return false;
}
