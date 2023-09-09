/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2017, 2018  Uwe Bonnes
 *                           <bon@elektron.ikp.physik.tu-darmstadt.de>
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
 * This file implements STM32F4 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * References:
 * ST doc - RM0090
 *   Reference manual - STM32F405xx, STM32F407xx, STM32F415xx and STM32F417xx
 *   advanced ARM-based 32-bit MCUs
 * ST doc - PM0081
 *   Programming manual - STM32F40xxx and STM32F41xxx Flash programming
 *    manual
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"
#include "stm32_common.h"

static bool stm32f4_cmd_option(target_s *t, int argc, const char **argv);
static bool stm32f4_cmd_psize(target_s *t, int argc, const char **argv);

const command_s stm32f4_cmd_list[] = {
	{"option", stm32f4_cmd_option, "Manipulate option bytes"},
	{"psize", stm32f4_cmd_psize, "Configure flash write parallelism: (x8|x16|x32(default)|x64)"},
	{NULL, NULL, NULL},
};

static bool stm32f4_attach(target_s *t);
static void stm32f4_detach(target_s *t);
static bool stm32f4_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool stm32f4_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len);
static bool stm32f4_mass_erase(target_s *t);

/* Flash Program and Erase Controller Register Map */
#define FPEC_BASE     0x40023c00U
#define FLASH_ACR     (FPEC_BASE + 0x00U)
#define FLASH_KEYR    (FPEC_BASE + 0x04U)
#define FLASH_OPTKEYR (FPEC_BASE + 0x08U)
#define FLASH_SR      (FPEC_BASE + 0x0cU)
#define FLASH_CR      (FPEC_BASE + 0x10U)
#define FLASH_OPTCR   (FPEC_BASE + 0x14U)

#define FLASH_CR_PG      (1U << 0U)
#define FLASH_CR_SER     (1U << 1U)
#define FLASH_CR_MER     (1U << 2U)
#define FLASH_CR_PSIZE8  (0U << 8U)
#define FLASH_CR_PSIZE16 (1U << 8U)
#define FLASH_CR_PSIZE32 (2U << 8U)
#define FLASH_CR_PSIZE64 (3U << 8U)
#define FLASH_CR_MER1    (1U << 15U)
#define FLASH_CR_STRT    (1U << 16U)
#define FLASH_CR_EOPIE   (1U << 24U)
#define FLASH_CR_ERRIE   (1U << 25U)
#define FLASH_CR_STRT    (1U << 16U)
#define FLASH_CR_LOCK    (1U << 31U)

#define FLASH_SR_BSY (1U << 16U)

#define FLASH_OPTCR_OPTLOCK (1U << 0U)
#define FLASH_OPTCR_OPTSTRT (1U << 1U)
#define FLASH_OPTCR_WDG_SW  (1U << 5U)
#define FLASH_OPTCR_nDBANK  (1U << 29U)
#define FLASH_OPTCR_DB1M    (1U << 30U)

#define FLASH_OPTCR_PROT_MASK 0xff00U
#define FLASH_OPTCR_PROT_L0   0xaa00U
#define FLASH_OPTCR_PROT_L1   0xbb00U

#define KEY1 0x45670123U
#define KEY2 0xcdef89abU

#define OPTKEY1 0x08192a3bU
#define OPTKEY2 0x4c5d6e7fU

#define SR_ERROR_MASK 0xf2U
#define SR_EOP        0x01U

#define F4_FLASHSIZE   0x1fff7a22U
#define F7_FLASHSIZE   0x1ff0f442U
#define F72X_FLASHSIZE 0x1ff07a22U
#define DBGMCU_IDCODE  0xe0042000U
#define DBGMCU_CR      0xe0042004U
#define DBG_SLEEP      (1U << 0U)

#define AXIM_BASE 0x8000000U
#define ITCM_BASE 0x0200000U

#define DBGMCU_CR_DBG_SLEEP   (0x1U << 0U)
#define DBGMCU_CR_DBG_STOP    (0x1U << 1U)
#define DBGMCU_CR_DBG_STANDBY (0x1U << 2U)

typedef struct stm32f4_flash {
	target_flash_s f;
	align_e psize;
	uint8_t base_sector;
	uint8_t bank_split;
} stm32f4_flash_s;

typedef struct stm32f4_priv {
	uint32_t dbgmcu_cr;
} stm32f4_priv_s;

#define ID_STM32F20X  0x411U
#define ID_STM32F40X  0x413U
#define ID_STM32F42X  0x419U
#define ID_STM32F446  0x421U
#define ID_STM32F401C 0x423U
#define ID_STM32F411  0x431U
#define ID_STM32F401E 0x433U
#define ID_STM32F46X  0x434U
#define ID_STM32F412  0x441U
#define ID_STM32F74X  0x449U
#define ID_STM32F76X  0x451U
#define ID_STM32F72X  0x452U
#define ID_STM32F410  0x458U
#define ID_STM32F413  0x463U
#define ID_GD32F450   0x2b3U
#define ID_GD32F470   0xa2eU

static void stm32f4_add_flash(target_s *const t, const uint32_t addr, const size_t length, const size_t blocksize,
	const uint8_t base_sector, const uint8_t split)
{
	if (length == 0)
		return;

	stm32f4_flash_s *sf = calloc(1, sizeof(*sf));
	if (!sf) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	target_flash_s *f = &sf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = blocksize;
	f->erase = stm32f4_flash_erase;
	f->write = stm32f4_flash_write;
	f->writesize = 1024;
	f->erased = 0xffU;
	sf->base_sector = base_sector;
	sf->bank_split = split;
	sf->psize = ALIGN_32BIT;
	target_add_flash(t, f);
}

static char *stm32f4_get_chip_name(const uint32_t device_id)
{
	switch (device_id) {
	case ID_STM32F40X: /* F40XxE/G */
		return "STM32F40x";
	case ID_STM32F42X: /* F42XxG/I */
		return "STM32F42x";
	case ID_STM32F46X: /* 469/479 xG/I*/
		return "STM32F47x";
	case ID_STM32F20X: /* F205 xB/C/E/G*/
		return "STM32F2";
	case ID_STM32F446: /* F446 xC/E*/
		return "STM32F446";
	case ID_STM32F401C: /* F401 B/C RM0368 Rev.3 */
		return "STM32F401C";
	case ID_STM32F411: /* F411 xC/E  RM0383 Rev.4 */
		return "STM32F411";
	case ID_STM32F412: /* F412 xG/I  RM0402 Rev.4, 256 kB Ram */
		return "STM32F412";
	case ID_STM32F401E: /* F401 D/E RM0368 Rev.3 */
		return "STM32F401E";
	case ID_STM32F413: /* F413xG/H  RM0430 Rev.2, 320 kB Ram, 1.5 MB flash. */
		return "STM32F413";
	case ID_STM32F74X: /* F74XxG/I RM0385 Rev.4 */
		return "STM32F74x";
	case ID_STM32F76X: /* F76XxE/G F77x RM0410 */
		return "STM32F76x";
	case ID_STM32F72X: /* F72/3xC/E RM0431 */
		return "STM32F72x";
	case ID_GD32F450: /* GigaDevice F450 */
		return "GD32F450";
	case ID_GD32F470: /* GigaDevice F470 */
		return "GD32F470";
	default:
		return NULL;
	}
}

static uint16_t stm32f4_read_idcode(target_s *const t)
{
	const uint16_t idcode = target_mem_read32(t, DBGMCU_IDCODE) & 0xfffU;
	/*
	 * F405 revision A has the wrong IDCODE, use ARM_CPUID to make the
	 * distinction with F205. Revision is also wrong (0x2000 instead
	 * of 0x1000). See F40x/F41x errata.
	 */
	if (idcode == ID_STM32F20X && (t->cpuid & CORTEX_CPUID_PARTNO_MASK) == CORTEX_M4)
		return ID_STM32F40X;
	return idcode;
}

bool stm32f4_probe(target_s *t)
{
	const uint16_t device_id = stm32f4_read_idcode(t);
	switch (device_id) {
	case ID_STM32F74X: /* F74x RM0385 Rev.4 */
	case ID_STM32F76X: /* F76x F77x RM0410 */
	case ID_STM32F72X: /* F72x F73x RM0431 */
	case ID_STM32F42X: /* 427/437 */
	case ID_STM32F46X: /* 469/479 */
	case ID_STM32F20X: /* F205 */
	case ID_STM32F40X:
	case ID_STM32F446:  /* F446 */
	case ID_STM32F401C: /* F401 B/C RM0368 Rev.3 */
	case ID_STM32F411:  /* F411     RM0383 Rev.4 */
	case ID_STM32F412:  /* F412     RM0402 Rev.4, 256 kB Ram */
	case ID_STM32F401E: /* F401 D/E RM0368 Rev.3 */
	case ID_STM32F413:  /* F413     RM0430 Rev.2, 320 kB Ram, 1.5 MB flash. */
		t->attach = stm32f4_attach;
		t->detach = stm32f4_detach;
		t->mass_erase = stm32f4_mass_erase;
		t->driver = stm32f4_get_chip_name(device_id);
		t->part_id = device_id;
		target_add_commands(t, stm32f4_cmd_list, t->driver);
		return true;
	}
	return false;
}

bool gd32f4_probe(target_s *t)
{
	if (t->part_id != ID_GD32F450 && t->part_id != ID_GD32F470)
		return false;

	t->attach = cortexm_attach;
	t->detach = cortexm_detach;
	t->mass_erase = stm32f4_mass_erase;
	t->driver = stm32f4_get_chip_name(t->part_id);
	target_add_commands(t, stm32f4_cmd_list, t->driver);

	target_mem_map_free(t);
	target_add_ram(t, 0x10000000, 0x10000); /* 64 k CCM Ram*/
	target_add_ram(t, 0x20000000, 0x50000); /* 320 k RAM */

	/* TODO implement DBS mode */
	const uint8_t split = 12;
	/* Bank 1*/
	stm32f4_add_flash(t, 0x8000000, 0x10000, 0x4000, 0, split);  /* 4 16K */
	stm32f4_add_flash(t, 0x8010000, 0x10000, 0x10000, 4, split); /* 1 64K */
	stm32f4_add_flash(t, 0x8020000, 0xe0000, 0x20000, 5, split); /* 7 128K */

	/* Bank 2 */
	stm32f4_add_flash(t, 0x8100000, 0x10000, 0x4000, 16, split);  /* 4 16K */
	stm32f4_add_flash(t, 0x8110000, 0x10000, 0x10000, 20, split); /* 1 64K */
	stm32f4_add_flash(t, 0x8120000, 0xe0000, 0x20000, 21, split); /* 7 128K */

	/* Third MB composed of 4 256 KB sectors, and uses sector values 12-15 */
	stm32f4_add_flash(t, 0x8200000, 0x100000, 0x40000, 12, split);

	return true;
}

static inline bool stm32f4_device_is_f7(const uint16_t part_id)
{
	return part_id == ID_STM32F74X || part_id == ID_STM32F76X || part_id == ID_STM32F72X;
}

static inline bool stm32f4_device_is_dual_bank(const uint16_t part_id)
{
	return part_id == ID_STM32F42X || part_id == ID_STM32F46X || part_id == ID_STM32F76X;
}

static inline bool stm32f4_device_has_ccm_ram(const uint16_t part_id)
{
	return part_id == ID_STM32F40X || part_id == ID_STM32F42X || part_id == ID_STM32F46X;
}

static inline bool stm32f4_device_has_large_sectors(const uint16_t part_id)
{
	return part_id == ID_STM32F74X || part_id == ID_STM32F76X;
}

static uint32_t stm32f4_remaining_bank_length(const uint32_t bank_length, const uint32_t small_sector_bytes)
{
	if (bank_length > small_sector_bytes)
		return bank_length - small_sector_bytes;
	return 0;
}

static bool stm32f4_attach(target_s *t)
{
	/* First try and figure out the Flash size (if we don't know the part ID, warn and return false) */
	uint16_t max_flashsize = 0;
	switch (t->part_id) {
	case ID_STM32F401E: /* F401D/E RM0368 Rev.3 */
	case ID_STM32F411:  /* F411 RM0383 Rev.4 */
	case ID_STM32F446:  /* F446 */
	case ID_STM32F46X:  /* F469/F479 */
	case ID_STM32F72X:  /* F72x/F73x RM0431 */
		max_flashsize = 512;
		break;
	case ID_STM32F401C: /* F401B/C RM0368 Rev.3 */
		max_flashsize = 256;
		break;
	case ID_STM32F40X:
	case ID_STM32F20X: /* F205xB/C/E/G */
	case ID_STM32F412: /* F412xE/G RM0402 Rev.4, 256kiB RAM */
	case ID_STM32F74X: /* F74x RM0385 Rev.4 */
		max_flashsize = 1024;
		break;
	case ID_STM32F413: /* F413 RM0430 Rev.2, 320kiB RAM, 1.5MiB Flash. */
		max_flashsize = 1536;
		break;
	case ID_STM32F42X: /* F427/F437 */
	case ID_STM32F76X: /* F76x/F77x RM0410 */
		max_flashsize = 2048;
		break;
	default:
		DEBUG_WARN("Unsupported part id: %u\n", t->part_id);
		return false;
	}

	/* Try to attach now we've determined it's a part we can work with */
	if (!cortexm_attach(t))
		return false;

	/* And then grab back all the part properties used to configure the memory map */
	const bool dual_bank = stm32f4_device_is_dual_bank(t->part_id);
	const bool has_ccm_ram = stm32f4_device_has_ccm_ram(t->part_id);
	const bool is_f7 = stm32f4_device_is_f7(t->part_id);
	const bool large_sectors = stm32f4_device_has_large_sectors(t->part_id);

	/* Allocate target-specific storage */
	stm32f4_priv_s *priv_storage = calloc(1, sizeof(*priv_storage));
	if (!priv_storage) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}
	t->target_storage = priv_storage;

	/* Get the current value of the debug control register (and store it for later) */
	priv_storage->dbgmcu_cr = target_mem_read32(t, DBGMCU_CR);
	/* Enable debugging during all low power modes*/
	target_mem_write32(
		t, DBGMCU_CR, priv_storage->dbgmcu_cr | DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STANDBY | DBGMCU_CR_DBG_STOP);

	/* Free any previously built memory map */
	target_mem_map_free(t);
	/* And rebuild the RAM map */
	bool use_dual_bank = !is_f7 && dual_bank;
	if (is_f7) {
		target_add_ram(t, 0x00000000, 0x4000);  /* 16kiB ITCM RAM */
		target_add_ram(t, 0x20000000, 0x20000); /* 128kiB DTCM RAM */
		target_add_ram(t, 0x20020000, 0x60000); /* 384kiB RAM */
		if (dual_bank) {
			const uint32_t option_ctrl = target_mem_read32(t, FLASH_OPTCR);
			use_dual_bank = !(option_ctrl & FLASH_OPTCR_nDBANK);
		}
	} else {
		if (has_ccm_ram)
			target_add_ram(t, 0x10000000, 0x10000); /* 64kiB CCM RAM */
		target_add_ram(t, 0x20000000, 0x50000);     /* 320kiB RAM */
		if (dual_bank && max_flashsize < 2048U) {
			/* Check the dual-bank status on 1MiB Flash devices */
			const uint32_t option_ctrl = target_mem_read32(t, FLASH_OPTCR);
			use_dual_bank = !(option_ctrl & FLASH_OPTCR_DB1M);
		}
	}

	/* Now we have a base RAM map, rebuild the Flash map */
	uint8_t split = 0;
	uint32_t bank_length;
	/* If we're targeting a dual-bank part and the second bank is enabled */
	if (use_dual_bank) {
		bank_length = max_flashsize << 9U;
		split = max_flashsize == 1024U ? 8U : 12U;
	} else
		bank_length = max_flashsize << 10U;

	/* For parts that have large sectors, the Flash map is built with different sector sizes */
	if (large_sectors) {
		/*
		 * The first 0x40000 bytes of the Flash bank use smaller sector sizes.
		 * 0x8000 for the first 0x20000 bytes
		 * 0x20000 for the next 0x20000 bytes
		 * Subtract these off the total bank length for the final chunk
		 */
		const uint32_t remaining_bank_length = stm32f4_remaining_bank_length(bank_length, 0x40000);
		/* 256kiB in small sectors */
		stm32f4_add_flash(t, ITCM_BASE, 0x20000, 0x8000, 0, split);
		stm32f4_add_flash(t, 0x0220000, 0x20000, 0x20000, 4, split);
		stm32f4_add_flash(t, 0x0240000, remaining_bank_length, 0x40000, 5, split);
		stm32f4_add_flash(t, AXIM_BASE, 0x20000, 0x8000, 0, split);
		stm32f4_add_flash(t, 0x8020000, 0x20000, 0x20000, 4, split);
		stm32f4_add_flash(t, 0x8040000, remaining_bank_length, 0x40000, 5, split);
	} else {
		/*
		 * The first 0x20000 bytes of the Flash bank use smaller sector sizes.
		 * 0x4000 for the first 0x10000 bytes
		 * 0x10000 for the next 0x10000 bytes
		 * Subtract these off the total bank length for the final chunk
		 */
		const uint32_t remaining_bank_length = stm32f4_remaining_bank_length(bank_length, 0x20000);
		/* 128kiB in small sectors */
		if (is_f7)
			stm32f4_add_flash(t, ITCM_BASE, 0x10000, 0x4000, 0, split);
		stm32f4_add_flash(t, AXIM_BASE, 0x10000, 0x4000, 0, split);
		if (bank_length > 0x10000U) {
			stm32f4_add_flash(t, 0x8010000, 0x10000, 0x10000, 4, split);
			if (remaining_bank_length)
				stm32f4_add_flash(t, 0x8020000, remaining_bank_length, 0x20000, 5, split);
		}
		/* If the device has an enabled second bank, we better deal with that too. */
		if (use_dual_bank) {
			if (is_f7) {
				const uint32_t bank1_base = ITCM_BASE + bank_length;
				stm32f4_add_flash(t, bank1_base, 0x10000, 0x4000, 0, split);
				stm32f4_add_flash(t, bank1_base + 0x10000U, 0x10000, 0x10000, 4, split);
				stm32f4_add_flash(t, bank1_base + 0x20000U, remaining_bank_length, 0x20000, 5, split);
			}
			const uint32_t bank2_base = AXIM_BASE + bank_length;
			stm32f4_add_flash(t, bank2_base, 0x10000, 0x4000, 16, split);
			stm32f4_add_flash(t, bank2_base + 0x10000U, 0x10000, 0x10000, 20, split);
			stm32f4_add_flash(t, bank2_base + 0x20000U, remaining_bank_length, 0x20000, 21, split);
		}
	}
	return true;
}

static void stm32f4_detach(target_s *t)
{
	stm32f4_priv_s *ps = t->target_storage;
	/*reverse all changes to DBGMCU_CR*/
	target_mem_write32(t, DBGMCU_CR, ps->dbgmcu_cr);
	cortexm_detach(t);
}

static void stm32f4_flash_unlock(target_s *t)
{
	if (target_mem_read32(t, FLASH_CR) & FLASH_CR_LOCK) {
		/* Enable FPEC controller access */
		target_mem_write32(t, FLASH_KEYR, KEY1);
		target_mem_write32(t, FLASH_KEYR, KEY2);
	}
}

static bool stm32f4_flash_busy_wait(target_s *const t, platform_timeout_s *const timeout)
{
	/* Read FLASH_SR to poll for BSY bit */
	uint32_t status = FLASH_SR_BSY;
	while (status & FLASH_SR_BSY) {
		status = target_mem_read32(t, FLASH_SR);
		if ((status & SR_ERROR_MASK) || target_check_error(t)) {
			DEBUG_ERROR("stm32f4 flash error 0x%" PRIx32 "\n", status);
			return false;
		}
		if (timeout)
			target_print_progress(timeout);
	}
	return true;
}

static bool stm32f4_flash_erase(target_flash_s *f, target_addr_t addr, size_t len)
{
	target_s *t = f->t;
	stm32f4_flash_s *sf = (stm32f4_flash_s *)f;
	stm32f4_flash_unlock(t);

	align_e psize = ALIGN_32BIT;
	/*
	 * XXX: What is this and why does it exist?
	 * A dry-run walk-through says it'll pull out the psize for the Flash region added first by stm32f4_attach()
	 * because all Flash regions added by stm32f4_add_flash match the if condition. This looks redundant and wrong.
	 */
	for (target_flash_s *currf = t->flash; currf; currf = currf->next) {
		if (currf->write == stm32f4_flash_write)
			psize = ((stm32f4_flash_s *)currf)->psize;
	}

	/* No address translation is needed here, as we erase by sector number */
	uint8_t sector = sf->base_sector + ((addr - f->start) / f->blocksize);

	/* Erase the requested chunk of flash, one sector at a time. */
	for (size_t offset = 0; offset < len; offset += f->blocksize) {
		uint32_t cr = FLASH_CR_EOPIE | FLASH_CR_ERRIE | FLASH_CR_SER | (psize * FLASH_CR_PSIZE16) | (sector << 3U);
		/* Flash page erase instruction */
		target_mem_write32(t, FLASH_CR, cr);
		/* write address to FMA */
		target_mem_write32(t, FLASH_CR, cr | FLASH_CR_STRT);

		/* Wait for completion or an error */
		if (!stm32f4_flash_busy_wait(t, NULL))
			return false;

		++sector;
		if (sf->bank_split && sector == sf->bank_split)
			sector = 16;
	}
	return true;
}

static bool stm32f4_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	/* Translate ITCM addresses to AXIM */
	if (dest >= ITCM_BASE && dest < AXIM_BASE)
		dest += AXIM_BASE - ITCM_BASE;
	target_s *t = f->t;

	align_e psize = ((stm32f4_flash_s *)f)->psize;
	target_mem_write32(t, FLASH_CR, (psize * FLASH_CR_PSIZE16) | FLASH_CR_PG);
	cortexm_mem_write_sized(t, dest, src, len, psize);

	/* Wait for completion or an error */
	return stm32f4_flash_busy_wait(t, NULL);
}

static bool stm32f4_mass_erase(target_s *t)
{
	/* XXX: Is it correct to grab the most recently added Flash region here? What is this really trying to do? */
	stm32f4_flash_s *sf = (stm32f4_flash_s *)t->flash;
	stm32f4_flash_unlock(t);

	/* Flash mass erase start instruction */
	const uint32_t ctrl = FLASH_CR_MER | (sf->bank_split ? FLASH_CR_MER1 : 0);
	target_mem_write32(t, FLASH_CR, ctrl);
	target_mem_write32(t, FLASH_CR, ctrl | FLASH_CR_STRT);

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);
	/* Wait for completion or an error */
	return stm32f4_flash_busy_wait(t, &timeout);
}

/*
 * Dev   |Manual|Rev|ID |OPTCR    |OPTCR    |OPTCR1   |OPTCR1   |OPTCR2   |OPTCR2
 *                  |hex|default  |reserved |default  |reserved |default  |reserved
 * F20x  |PM0059|5.1|411|0FFFAAED |F0000010 |
 * F40x  |RM0090| 11|413|0FFFAAED |F0000010 |
 * F42x  |RM0090| 11|419|0FFFAAED |30000000 |0FFF0000 |F000FFFF
 * F446  |RM0390|  2|421|0FFFAAED |7F000010 |
 * F401BC|RM0368|  3|423|0FFFAAED |7FC00010 |
 * F411  |RM0383|  2|431|0FFFAAED |7F000010 |
 * F401DE|RM0368|  3|433|0FFFAAED |7F000010 |
 * F46x  |RM0386|  2|434|0FFFAAED |30000000 |0FFF0000 |F000FFFF
 * F412  |RM0402|  4|441|0FFFAAED*|70000010 |
 * F74x  |RM0385|  4|449|C0FFAAFD |3F000000 |00400080*|00000000
 * F76x  |RM0410|  2|451|FFFFAAFD*|00000000 |00400080*|00000000
 * F72x  |RM0431|  1|452|C0FFAAFD |3F000000 |00400080*|00000000 |00000000 |800000FF
 * F410  |RM0401|  2|458|0FFFAAED*|7FE00010 |
 * F413  |RM0430|  2|463|7FFFAAED*|00000010 |
 *
 * * Documentation for F7 with OPTCR1 default = 0fff7f0080 seems wrong!
 * * Documentation for F412 with OPTCR default = 0ffffffed seems wrong!
 * * Documentation for F413 with OPTCR default = 0ffffffed seems wrong!
 */
static bool optcr_mask(target_s *const t, uint32_t *const val)
{
	switch (t->part_id) {
	case ID_STM32F20X:
	case ID_STM32F40X:
		val[0] &= ~0xf0000010U;
		break;
	case ID_STM32F46X:
	case ID_STM32F42X:
	case ID_GD32F450:
	case ID_GD32F470:
		val[0] &= ~0x30000000U;
		val[1] &= 0x0fff0000U;
		break;
	case ID_STM32F401C:
		val[0] &= ~0x7fc00010U;
		break;
	case ID_STM32F446:
	case ID_STM32F411:
	case ID_STM32F401E:
		val[0] &= ~0x7f000010U;
		break;
	case ID_STM32F410:
		val[0] &= ~0x7fe00010U;
		break;
	case ID_STM32F412:
		val[0] &= ~0x70000010U;
		break;
	case ID_STM32F413:
		val[0] &= ~0x00000010U;
		break;
	case ID_STM32F72X:
		val[2] &= ~0x800000ffU;
		/* Fall through*/
	case ID_STM32F74X:
		val[0] &= ~0x3f000000U;
		break;
	case ID_STM32F76X:
		break;
	default:
		return false;
	}
	return true;
}

static size_t stm32f4_opt_bytes_for(const uint16_t part_id)
{
	if (part_id == ID_STM32F72X)
		return 3;
	if (part_id == ID_STM32F42X || part_id == ID_STM32F46X || part_id == ID_STM32F74X || part_id == ID_STM32F76X)
		return 2;
	if (part_id == ID_GD32F450 || part_id == ID_GD32F470)
		return 2;
	return 1;
}

static bool stm32f4_option_write(target_s *t, uint32_t *const val, size_t count)
{
	val[0] &= ~(FLASH_OPTCR_OPTSTRT | FLASH_OPTCR_OPTLOCK);
	uint32_t optcr = target_mem_read32(t, FLASH_OPTCR);
	/* Check if watchdog and read protection is active.
	 * When both are active, watchdog will trigger when erasing
	 * to get back to level 0 protection and operation aborts!
	 */
	if (!(optcr & FLASH_OPTCR_WDG_SW) && (optcr & FLASH_OPTCR_PROT_MASK) != FLASH_OPTCR_PROT_L0 &&
		(val[0] & FLASH_OPTCR_PROT_MASK) != FLASH_OPTCR_PROT_L1) {
		val[0] &= ~FLASH_OPTCR_PROT_MASK;
		val[0] |= FLASH_OPTCR_PROT_L1;
		tc_printf(t, "Keeping L1 protection while HW Watchdog fuse is set!\n");
	}
	target_mem_write32(t, FLASH_OPTKEYR, OPTKEY1);
	target_mem_write32(t, FLASH_OPTKEYR, OPTKEY2);
	if (!stm32f4_flash_busy_wait(t, NULL))
		return false;

	const uint16_t part_id = t->part_id;
	/* Write option bytes instruction */
	if (stm32f4_opt_bytes_for(part_id) > 1U && count > 1U) {
		/* XXX: Do we need to read old value and then set it? */
		target_mem_write32(t, FLASH_OPTCR + 4U, val[1]);
		if (part_id == ID_STM32F72X && count > 2U)
			target_mem_write32(t, FLASH_OPTCR + 8U, val[2]);
	}

	target_mem_write32(t, FLASH_OPTCR, val[0]);
	target_mem_write32(t, FLASH_OPTCR, val[0] | FLASH_OPTCR_OPTSTRT);

	tc_printf(t, "Erasing flash\nThis may take a few seconds...\n");

	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 100);
	/* Wait for completion or an error */
	if (!stm32f4_flash_busy_wait(t, &timeout))
		return false;
	tc_printf(t, "\n");

	target_mem_write32(t, FLASH_OPTCR, FLASH_OPTCR_OPTLOCK);
	/* Reset target to reload option bits.*/
	target_reset(t);
	return true;
}

static bool stm32f4_option_write_default(target_s *t)
{
	uint32_t val[3] = {0};
	switch (t->part_id) {
	case ID_STM32F42X:
	case ID_STM32F46X:
	case ID_GD32F450:
	case ID_GD32F470:
		val[0] = 0x0fffaaedU;
		val[1] = 0x0fff0000U;
		return stm32f4_option_write(t, val, 2);
	case ID_STM32F72X:
		val[0] = 0xc0ffaafdU;
		val[1] = 0x00400080U;
		return stm32f4_option_write(t, val, 3);
	case ID_STM32F74X:
		val[0] = 0xc0ffaafdU;
		val[1] = 0x00400080U;
		return stm32f4_option_write(t, val, 2);
	case ID_STM32F76X:
		val[0] = 0xffffaafdU;
		val[1] = 0x00400080U;
		return stm32f4_option_write(t, val, 2);
	case ID_STM32F413:
		val[0] = 0x7fffaafdU;
		return stm32f4_option_write(t, val, 1);
	default:
		val[0] = 0x0fffaaedU;
		return stm32f4_option_write(t, val, 1);
	}
}

static const char option_cmd_erase[] = "erase";
static const char option_cmd_write[] = "write";
#define OPTION_CMD_LEN(cmd) ARRAY_LENGTH(cmd) - 1U

static bool partial_match(const char *const str, const char *const what, const size_t what_len)
{
	const size_t str_len = strlen(str);
	if (str_len > what_len)
		return false;
	return strncasecmp(str, what, str_len) == 0;
}

static bool stm32f4_cmd_option(target_s *t, int argc, const char **argv)
{
	const size_t opt_bytes = stm32f4_opt_bytes_for(t->part_id);
	if (argc == 2 && partial_match(argv[1], option_cmd_erase, OPTION_CMD_LEN(option_cmd_erase)))
		stm32f4_option_write_default(t);
	else if (argc > 2 && partial_match(argv[1], option_cmd_write, OPTION_CMD_LEN(option_cmd_write))) {
		uint32_t val[3] = {0};
		size_t count = argc > 4 ? 3 : argc - 1;
		val[0] = strtoul(argv[2], NULL, 0);
		if (argc > 3) {
			val[1] = strtoul(argv[3], NULL, 0);
			if (argc > 4)
				val[2] = strtoul(argv[4], NULL, 0);
		}

		if (optcr_mask(t, val))
			stm32f4_option_write(t, val, count);
		else
			tc_printf(t, "error\n");
	} else
		tc_printf(t, "usage: monitor option erase\nusage: monitor option write <OPTCR>%s%s\n",
			opt_bytes > 1U ? " <OPTCR1>" : "", opt_bytes == 3U ? " <OPTCR2>" : "");

	uint32_t val[3] = {0};
	val[0] = target_mem_read32(t, FLASH_OPTCR);
	if (opt_bytes > 1U) {
		val[1] = target_mem_read32(t, FLASH_OPTCR + 4U);
		if (opt_bytes == 3U)
			val[2] = target_mem_read32(t, FLASH_OPTCR + 8U);
	}
	optcr_mask(t, val);
	tc_printf(t, "OPTCR: 0x%08" PRIx32, val[0]);
	if (opt_bytes > 1U) {
		tc_printf(t, " OPTCR1: 0x%08" PRIx32, val[1]);
		if (opt_bytes > 2U)
			tc_printf(t, " OPTCR2: 0x%08" PRIx32, val[2]);
	}
	tc_printf(t, "\n");
	return true;
}

static bool stm32f4_cmd_psize(target_s *t, int argc, const char **argv)
{
	if (argc == 1) {
		align_e psize = ALIGN_32BIT;
		/*
		 * XXX: What is this and why does it exist?
		 * A dry-run walk-through says it'll pull out the psize for the Flash region added first by stm32f4_attach()
		 * because all Flash regions added by stm32f4_add_flash match the if condition. This looks redundant and wrong.
		 */
		for (target_flash_s *f = t->flash; f; f = f->next) {
			if (f->write == stm32f4_flash_write)
				psize = ((stm32f4_flash_s *)f)->psize;
		}
		tc_printf(t, "Flash write parallelism: %s\n", stm32_psize_to_string(psize));
	} else {
		align_e psize;
		if (!stm32_psize_from_string(t, argv[1], &psize))
			return false;

		/*
		 * XXX: What is this and why does it exist?
		 * A dry-run walk-through says it'll overwrite psize for every Flash region added by stm32f4_attach()
		 * because all Flash regions added by stm32f4_add_flash match the if condition. This looks redundant and wrong.
		 */
		for (target_flash_s *f = t->flash; f; f = f->next) {
			if (f->write == stm32f4_flash_write)
				((stm32f4_flash_s *)f)->psize = psize;
		}
	}
	return true;
}
