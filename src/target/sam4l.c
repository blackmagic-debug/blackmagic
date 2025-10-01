/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2016 Chuck McManis <cmcmanis@mcmanis.com>
 * Copyright (C) 2022-2025 1BitSquared <info@1bitsquared.com>
 * Written by Chuck McManis <cmcmanis@mcmanis.com>
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
 * This file implements support for Atmel SAM4L series devices, providing
 * memory maps and Flash programming routines.
 *
 * Supported devices: SAM4L2, SAM4L4, SAM4L8
 *
 * References:
 * 42023 - ATSAM ARM-based Flash MCU SAM4L Series, Rev. H (11/2016)
 *   https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/Atmel-42023-ARM-Microcontroller-ATSAM4L-Low-Power-LCD_Datasheet.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

/* Flash Controller defines (§14 FLASHCALW, pg263) */
#define SAM4L_FLASHCTRL_BASE UINT32_C(0x400a0000)
#define SAM4L_FLASHCTRL_FCR  (SAM4L_FLASHCTRL_BASE + 0x00U)
#define SAM4L_FLASHCTRL_FCMD (SAM4L_FLASHCTRL_BASE + 0x04U)
#define SAM4L_FLASHCTRL_FSR  (SAM4L_FLASHCTRL_BASE + 0x08U)
#define SAM4L_FLASHCTRL_FPR  (SAM4L_FLASHCTRL_BASE + 0x0aU)
#define SAM4L_FLASHCTRL_FVR  (SAM4L_FLASHCTRL_BASE + 0x10U)

/* Flash Control Register */
#define SAM4L_FLASHCTRL_FCR_WS1OPT (1U << 7U)
#define SAM4L_FLASHCTRL_FCR_FWS    (1U << 6U)
#define SAM4L_FLASHCTRL_FCR_ECCE   (1U << 4U)
#define SAM4L_FLASHCTRL_FCR_PROGE  (1U << 3U)
#define SAM4L_FLASHCTRL_FCR_LOCKE  (1U << 2U)
#define SAM4L_FLASHCTRL_FCR_FRDY   (1U << 0U)

/* Flash Command Register */
#define SAM4L_FLASHCTRL_FCMD_KEY_MASK    0xffU
#define SAM4L_FLASHCTRL_FCMD_KEY_SHIFT   24U
#define SAM4L_FLASHCTRL_FCMD_PAGEN_MASK  0xffffU
#define SAM4L_FLASHCTRL_FCMD_PAGEN_SHIFT 8U
#define SAM4L_FLASHCTRL_FCMD_CMD_MASK    0x3fU
#define SAM4L_FLASHCTRL_FCMD_CMD_SHIFT   0U

#define SAM4L_FLASHCTRL_FLASH_CMD_NOP   0U
#define SAM4L_FLASHCTRL_FLASH_CMD_WP    1U  /* Write Page */
#define SAM4L_FLASHCTRL_FLASH_CMD_EP    2U  /* Erase Page */
#define SAM4L_FLASHCTRL_FLASH_CMD_CPB   3U  /* Clear Page Buffer */
#define SAM4L_FLASHCTRL_FLASH_CMD_LP    4U  /* Lock page region */
#define SAM4L_FLASHCTRL_FLASH_CMD_UP    5U  /* Unlock page region */
#define SAM4L_FLASHCTRL_FLASH_CMD_EA    6U  /* Erase All */
#define SAM4L_FLASHCTRL_FLASH_CMD_WGPB  7U  /* Write General Purpose Fuse Bit */
#define SAM4L_FLASHCTRL_FLASH_CMD_EGPB  8U  /* Erase General Purpose Fuse Bit */
#define SAM4L_FLASHCTRL_FLASH_CMD_SSB   9U  /* Set Security Fuses */
#define SAM4L_FLASHCTRL_FLASH_CMD_PGPFB 10U /* Program General Purpose Fuse Byte */
#define SAM4L_FLASHCTRL_FLASH_CMD_EAGPF 11U /* Erase All GP Fuses */
#define SAM4L_FLASHCTRL_FLASH_CMD_QPR   12U /* Quick Page Read (erase check) */
#define SAM4L_FLASHCTRL_FLASH_CMD_WUP   13U /* Write User Page */
#define SAM4L_FLASHCTRL_FLASH_CMD_EUP   14U /* Erase User Page */
#define SAM4L_FLASHCTRL_FLASH_CMD_QPRUP 15U /* Quick Page Read User Page */
#define SAM4L_FLASHCTRL_FLASH_CMD_HSEN  16U /* High Speed Enable */
#define SAM4L_FLASHCTRL_FLASH_CMD_HSDIS 17U /* High Speed Disable */

/* Flash Status Register */
#define SAM4L_FLASHCTRL_FSR_LOCK(x)  (1U << (16U + (x)))
#define SAM4L_FLASHCTRL_FSR_ECCERR   (1U << 9U)
#define SAM4L_FLASHCTRL_FSR_ECCERR2  (1U << 8U)
#define SAM4L_FLASHCTRL_FSR_HSMODE   (1U << 6U)
#define SAM4L_FLASHCTRL_FSR_QPRR     (1U << 5U)
#define SAM4L_FLASHCTRL_FSR_SECURITY (1U << 4U)
#define SAM4L_FLASHCTRL_FSR_PROGE    (1U << 3U)
#define SAM4L_FLASHCTRL_FSR_LOCKE    (1U << 2U)
#define SAM4L_FLASHCTRL_FSR_FRDY     (1U << 0U)

/* Flash Parameter Register */
#define SAM4L_FLASHCTRL_FPR_PSZ_MASK  0x7U /* page size */
#define SAM4L_FLASHCTRL_FPR_PSZ_SHIFT 8U
#define SAM4L_FLASHCTRL_FPR_FSZ_MASK  0xfU /* flash size */
#define SAM4L_FLASHCTRL_FPR_FSZ_SHIFT 0U

/* Flash Version Register */
#define SAM4L_FLASHCTRL_FVR_VARIANT_MASK  0xfU
#define SAM4L_FLASHCTRL_FVR_VARIANT_SHIFT 16U
#define SAM4L_FLASHCTRL_FVR_VERSION_MASK  0xfffU
#define SAM4L_FLASHCTRL_FVR_VERSION_SHIFT 0U

/* All variants of 4L have a 512 byte page */
#define SAM4L_PAGE_SIZE 512U

/* Chip Identifier (§9 CHIPID, pg99) */
#define SAM4L_CHIPID_BASE                 0x400e0740U
#define SAM4L_CHIPID_CIDR                 (SAM4L_CHIPID_BASE + 0x0U)
#define SAM4L_CHIPID_CIDR_ARCH_MASK       0x0ff00000U
#define SAM4L_CHIPID_CIDR_ARCH_SHIFT      20U
#define SAM4L_CHIPID_CIDR_ARCH_SAM4L      0xb0U
#define SAM4L_CHIPID_CIDR_SRAM_SIZE_MASK  0x000f0000U
#define SAM4L_CHIPID_CIDR_SRAM_SIZE_SHIFT 16U
#define SAM4L_CHIPID_CIDR_NVP_SIZE_MASK   0x00000f00U
#define SAM4L_CHIPID_CIDR_NVP_SIZE_SHIFT  8U

/* Arbitrary time to wait for FLASH controller to be ready */
#define FLASH_TIMEOUT 1000U /* ms */

/* System Manager Access Port (§8.8, pg77) */
#define SAM4L_SMAP_BASE   UINT32_C(0x400a3000)
#define SAM4L_SMAP_CR     (SAM4L_SMAP_BASE + 0x00U)
#define SAM4L_SMAP_SR     (SAM4L_SMAP_BASE + 0x04U)
#define SAM4L_SMAP_SCR    (SAM4L_SMAP_BASE + 0x08U)
#define SAM4L_SMAP_ADDR   (SAM4L_SMAP_BASE + 0x0cU)
#define SAM4L_SMAP_LEN    (SAM4L_SMAP_BASE + 0x10U)
#define SAM4L_SMAP_DATA   (SAM4L_SMAP_BASE + 0x14U)
#define SAM4L_SMAP_VERS   (SAM4L_SMAP_BASE + 0x28U)
#define SAM4L_SMAP_CHIPID (SAM4L_SMAP_BASE + 0xf0U)
#define SAM4L_SMAP_EXTID  (SAM4L_SMAP_BASE + 0xf4U)
#define SAM4L_SMAP_IDR    (SAM4L_SMAP_BASE + 0xfcU)

#define SAM4L_SMAP_SR_DONE (1U << 0U)
#define SAM4L_SMAP_SR_HCR  (1U << 1U)
#define SAM4L_SMAP_SR_BERR (1U << 2U)
#define SAM4L_SMAP_SR_FAIL (1U << 3U)
#define SAM4L_SMAP_SR_LCK  (1U << 4U)
#define SAM4L_SMAP_SR_EN   (1U << 8U)
#define SAM4L_SMAP_SR_PROT (1U << 9U)
#define SAM4L_SMAP_SR_DBGP (1U << 10U)

static void sam4l_extended_reset(target_s *target);
static bool sam4l_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool sam4l_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);

/* Why couldn't Atmel make it sequential ... */
static const uint32_t sam4l_ram_size[16] = {
	48U * 1024U,  /*  0: 48K */
	1U * 1024U,   /*  1: 1K */
	2U * 1024U,   /*  2: 2K */
	6U * 1024U,   /*  3: 6K */
	24U * 1024U,  /*  4: 24K */
	4U * 1024U,   /*  5: 4K */
	80U * 1024U,  /*  6: 80K */
	160U * 1024U, /*  7: 160K */
	8U * 1024U,   /*  8: 8K */
	16U * 1024U,  /*  9: 16K */
	32U * 1024U,  /* 10: 32K */
	64U * 1024U,  /* 11: 64K */
	128U * 1024U, /* 12: 128K */
	256U * 1024U, /* 13: 256K */
	96U * 1024U,  /* 14: 96K */
	512U * 1024U  /* 15: 512K */
};

static const uint32_t sam4l_nvp_size[16] = {
	0,             /*  0: none */
	8U * 1024U,    /*  1: 8K */
	16U * 1024U,   /*  2: 16K */
	32U * 1024U,   /*  3: 32K */
	0,             /*  4: reserved */
	64U * 1024U,   /*  5: 64K */
	0,             /*  6: reserved */
	128U * 1024U,  /*  7: 128K */
	0,             /*  8: reserved */
	256U * 1024U,  /*  9: 256K */
	512U * 1024U,  /* 10: 512K */
	0,             /* 11: reserved */
	1024U * 1024U, /* 12: 1024K (1M) */
	0,             /* 13: reserved */
	2048U * 1024U, /* 14: 2048K (2M) */
	0              /* 15: reserved */
};

/*
 * Populate a target_flash struct with the necessary function pointers
 * and constants to describe our flash.
 */
static void sam4l_add_flash(target_s *const target, const uint32_t addr, const size_t length)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = addr;
	flash->length = length;
	flash->blocksize = SAM4L_PAGE_SIZE;
	flash->erase = sam4l_flash_erase;
	flash->write = sam4l_flash_write;
	flash->writesize = SAM4L_PAGE_SIZE;
	flash->erased = 0xff;
	/* Add it into the target structures flash chain */
	target_add_flash(target, flash);
}

/*
 * The probe function, look where the CIDR register should be, see if
 * it matches the SAM4L architecture code.
 *
 * Figure out from the register how much RAM and FLASH this variant has.
 */
bool sam4l_probe(target_s *const target)
{
	const uint32_t cidr = target_mem32_read32(target, SAM4L_CHIPID_CIDR);
	if (((cidr & SAM4L_CHIPID_CIDR_ARCH_MASK) >> SAM4L_CHIPID_CIDR_ARCH_SHIFT) != SAM4L_CHIPID_CIDR_ARCH_SAM4L)
		return false;

	/* Look up the RAM and Flash size of the device */
	const uint32_t ram_size =
		sam4l_ram_size[(cidr & SAM4L_CHIPID_CIDR_SRAM_SIZE_MASK) >> SAM4L_CHIPID_CIDR_SRAM_SIZE_SHIFT];
	const uint32_t flash_size =
		sam4l_nvp_size[(cidr & SAM4L_CHIPID_CIDR_NVP_SIZE_MASK) >> SAM4L_CHIPID_CIDR_NVP_SIZE_SHIFT];

	target->driver = "Atmel SAM4L";
	/* This function says we need to do "extra" stuff after reset */
	target->extended_reset = sam4l_extended_reset;

	target_add_ram32(target, 0x20000000, ram_size);
	sam4l_add_flash(target, 0x0, flash_size);

	DEBUG_INFO("SAM4L - RAM: 0x%" PRIx32 " (%" PRIu32 "kiB), FLASH: 0x%" PRIx32 " (%" PRIu32 "kiB)\n", ram_size,
		ram_size / 1024U, flash_size, flash_size / 1024U);

	/* Enable SMAP if not, check for HCR and reset if set */
	sam4l_extended_reset(target);
	if (target_check_error(target))
		DEBUG_ERROR("SAM4L: target_check_error returned true\n");
	return true;
}

/* We've been reset, make sure we take the core out of reset */
static void sam4l_extended_reset(target_s *const target)
{
	DEBUG_INFO("SAM4L: Extended Reset\n");

	/* Enable SMAP in case we're dealing with a non-JTAG reset */
	target_mem32_write32(target, SAM4L_SMAP_CR, 0x1); /* enable SMAP */
	uint32_t reg = target_mem32_read32(target, SAM4L_SMAP_SR);
	DEBUG_INFO("SMAP_SR has 0x%08" PRIx32 "\n", reg);
	if ((reg & SAM4L_SMAP_SR_HCR) != 0) {
		/* Write '1' bit to the status clear register */
		target_mem32_write32(target, SAM4L_SMAP_SCR, SAM4L_SMAP_SR_HCR);
		/* Waiting 250 loops for it to reset is arbitrary, it should happen right away */
		for (size_t i = 0; i < 250U; i++) {
			reg = target_mem32_read32(target, SAM4L_SMAP_SR);
			if (!(reg & SAM4L_SMAP_SR_HCR))
				break;
			/* Not sure what to do if we can't reset that bit */
			if (i == 249)
				DEBUG_INFO("Reset failed. SMAP_SR has 0x%08" PRIx32 "\n", reg);
		}
	}
	/* reset bus error if for some reason SMAP was disabled */
	target_check_error(target);
}

/*
 * Helper function, wait for the flash controller to be ready to receive a
 * command. Then send it the command, page number, and the authorization
 * key (always 0xa5) in the command register.
 *
 * Need the target struct to call the mem_read32 and mem_write32 function
 * pointers.
 */
static bool sam4l_flash_command(target_s *const target, const uint32_t page, const uint32_t cmd)
{
	DEBUG_INFO("%s: FSR: 0x%08" PRIx32 ", page = %" PRIu32 ", command = %" PRIu32 "\n", __func__, SAM4L_FLASHCTRL_FSR,
		page, cmd);

	/* Wait for Flash controller ready */
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, FLASH_TIMEOUT);
	while (!(target_mem32_read32(target, SAM4L_FLASHCTRL_FSR) & SAM4L_FLASHCTRL_FSR_FRDY)) {
		if (platform_timeout_is_expired(&timeout)) {
			DEBUG_WARN("%s: Not ready!\n", __func__);
			return false;
		}
	}

	/* Load up the new command */
	const uint32_t cmd_reg = (cmd & SAM4L_FLASHCTRL_FCMD_CMD_MASK) |
		((page & SAM4L_FLASHCTRL_FCMD_PAGEN_MASK) << SAM4L_FLASHCTRL_FCMD_PAGEN_SHIFT) |
		(0xa5U << SAM4L_FLASHCTRL_FCMD_KEY_SHIFT);
	DEBUG_INFO("%s: Writing command word 0x%08" PRIx32 "\n", __func__, cmd_reg);

	/* And kick it off */
	target_mem32_write32(target, SAM4L_FLASHCTRL_FCMD, cmd_reg);
	/* Don't actually wait for it to finish, the next command will stall if it is not done */
	return true;
}

/* Write data from 'src' into flash using the algorithm provided by Atmel in their data sheet. */
static bool sam4l_flash_write(
	target_flash_s *const flash, const target_addr_t dest, const void *const src, const size_t len)
{
	(void)len;
	target_s *target = flash->t;
	/* This will fail with unaligned writes, however the target Flash API guarantees we're called aligned */
	const uint16_t page = dest / SAM4L_PAGE_SIZE;

	/* Clear the page buffer */
	if (!sam4l_flash_command(target, 0, SAM4L_FLASHCTRL_FLASH_CMD_CPB))
		return false;

	/* Now fill page buffer with our 512 bytes of data */
	const uint32_t *const data = src;
	/*
	 * `target_mem32_write` use has been attempted, however that resulted in the
	 * last 64 bits (8 bytes) to be incorrect on even pages (0, 2, 4, ...).
	 * Since it works this way, it has not been investigated further.
	 */
	for (size_t offset = 0; offset < SAM4L_PAGE_SIZE; offset += 4U) {
		/*
 		 * The page buffer overlaps flash, its only 512 bytes long
		 * and no matter where you write it from it goes to the page
		 * you point it to. So we don't need the specific address here
		 * instead we just write 0 -> pagelen (512) and that fills our
		 * buffer correctly.
		 */
		target_mem32_write32(target, dest + offset, data[offset / 4U]);
	}

	/* write the page */
	return sam4l_flash_command(target, page, SAM4L_FLASHCTRL_FLASH_CMD_WP);
}

/* Erase flash across the addresses specified by addr and len */
static bool sam4l_flash_erase(target_flash_s *const flash, const target_addr_t addr, const size_t len)
{
	/*
	 * NB: if addr isn't aligned to a page boundary, or length
	 * is not an even multiple of page sizes, we may end up
	 * erasing data we didn't intend to.
	 * This issue is however mitigated by the target Flash API layer somewhat.
	 */

	target_s *target = flash->t;

	for (size_t offset = 0; offset < len; offset += SAM4L_PAGE_SIZE) {
		const size_t page = (addr + offset) / SAM4L_PAGE_SIZE;
		if (!sam4l_flash_command(target, page, SAM4L_FLASHCTRL_FLASH_CMD_EP))
			return false;
	}
	return true;
}
