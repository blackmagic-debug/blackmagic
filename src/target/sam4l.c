/*
 *
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2016  Chuck McManis <cmcmanis@mcmanis.com>
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
 * This file implements Atmel SAM4L target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * Supported devices: SAM4L2, SAM4L4, SAM4L8
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

/*
 * Flash Controller defines
 */
#define FLASHCALW_BASE UINT32_C(0x400a0000)

/* Flash Control Register */
#define FLASHCALW_FCR        FLASHCALW_BASE
#define FLASHALCW_FCR_WS1OPT (1U << 7U)
#define FLASHALCW_FCR_FWS    (1U << 6U)
#define FLASHALCW_FCR_ECCE   (1U << 4U)
#define FLASHALCW_FCR_PROGE  (1U << 3U)
#define FLASHALCW_FCR_LOCKE  (1U << 2U)
#define FLASHALCW_FCR_FRDY   (1U << 0U)

/* Flash Command Register */
#define FLASHCALW_FCMD             (FLASHCALW_BASE + 0x04U)
#define FLASHCALW_FCMD_KEY_MASK    0xffU
#define FLASHCALW_FCMD_KEY_SHIFT   24U
#define FLASHCALW_FCMD_PAGEN_MASK  0xffffU
#define FLASHCALW_FCMD_PAGEN_SHIFT 8U
#define FLASHCALW_FCMD_CMD_MASK    0x3fU
#define FLASHCALW_FCMD_CMD_SHIFT   0U

#define FLASH_CMD_NOP   0U
#define FLASH_CMD_WP    1U  /* Write Page */
#define FLASH_CMD_EP    2U  /* Erase Page */
#define FLASH_CMD_CPB   3U  /* Clear Page Buffer */
#define FLASH_CMD_LP    4U  /* Lock page region */
#define FLASH_CMD_UP    5U  /* Unlock page region */
#define FLASH_CMD_EA    6U  /* Erase All */
#define FLASH_CMD_WGPB  7U  /* Write General Purpose Fuse Bit */
#define FLASH_CMD_EGPB  8U  /* Erase General Purpose Fuse Bit */
#define FLASH_CMD_SSB   9U  /* Set Security Fuses */
#define FLASH_CMD_PGPFB 10U /* Program General Purpose Fuse Byte */
#define FLASH_CMD_EAGPF 11U /* Erase All GP Fuses */
#define FLASH_CMD_QPR   12U /* Quick Page Read (erase check) */
#define FLASH_CMD_WUP   13U /* Write User Page */
#define FLASH_CMD_EUP   14U /* Erase User Page */
#define FLASH_CMD_QPRUP 15U /* Quick Page Read User Page */
#define FLASH_CMD_HSEN  16U /* High Speed Enable */
#define FLASH_CMD_HSDIS 17U /* High Speed Disable */

/* Flash Status Register */
#define FLASHCALW_FSR          (FLASHCALW_BASE + 0x08U)
#define FLASHCALW_FSR_LOCK(x)  (1U << (16U + (x)))
#define FLASHCALW_FSR_ECCERR   (1U << 9U)
#define FLASHCALW_FSR_ECCERR2  (1U << 8U)
#define FLASHCALW_FSR_HSMODE   (1U << 6U)
#define FLASHCALW_FSR_QPRR     (1U << 5U)
#define FLASHCALW_FSR_SECURITY (1U << 4U)
#define FLASHCALW_FSR_PROGE    (1U << 3U)
#define FLASHCALW_FSR_LOCKE    (1U << 2U)
#define FLASHCALW_FSR_FRDY     (1U << 0U)

/* Flash Parameter Register */
#define FLASHCALW_FPR           (FLASHCALW_BASE + 0x0aU)
#define FLASHCALW_FPR_PSZ_MASK  0x7U /* page size */
#define FLASHCALW_FPR_PSZ_SHIFT 8U
#define FLASHCALW_FPR_FSZ_MASK  0xfU /* flash size */
#define FLASHCALW_FPR_FSZ_SHIFT 0U

/* Flash Version Register */
#define FLASHCALW_FVR               (FLASHCALW_BASE + 0x10U)
#define FLASHCALW_FVR_VARIANT_MASK  0xfU
#define FLASHCALW_FVR_VARIANT_SHIFT 16U
#define FLASHCALW_FVR_VERSION_MASK  0xfffU
#define FLASHCALW_FVR_VERSION_SHIFT 0U

/* Flash General Purpose Registers (high) */
#define FLASHCALW_FGPFRHI (FLASHCALW_BASE + 0x14U)
/* Flash General Purpose Registers (low) */
#define FLASHCALW_FGPFRLO (FLASHCALW_BASE + 0x18U)

/* All variants of 4L have a 512 byte page */
#define SAM4L_PAGE_SIZE           512U
#define SAM4L_ARCH                0xb0U
#define SAM4L_CHIPID_CIDR         0x400e0740U
#define CHIPID_CIDR_ARCH_MASK     0xffU
#define CHIPID_CIDR_ARCH_SHIFT    20U
#define CHIPID_CIDR_SRAMSIZ_MASK  0xfU
#define CHIPID_CIDR_SRAMSIZ_SHIFT 16U
#define CHIPID_CIDR_NVPSIZ_MASK   0xfU
#define CHIPID_CIDR_NVPSIZ_SHIFT  8U

/* Arbitrary time to wait for FLASH controller to be ready */
#define FLASH_TIMEOUT 1000U /* ms */

#define SMAP_BASE    UINT32_C(0x400a3000)
#define SMAP_CR      (SMAP_BASE + 0x00U)
#define SMAP_SR      (SMAP_BASE + 0x04U)
#define SMAP_SR_DONE (1U << 0U)
#define SMAP_SR_HCR  (1U << 1U)
#define SMAP_SR_BERR (1U << 2U)
#define SMAP_SR_FAIL (1U << 3U)
#define SMAP_SR_LCK  (1U << 4U)
#define SMAP_SR_EN   (1U << 8U)
#define SMAP_SR_PROT (1U << 9U)
#define SMAP_SR_DBGP (1U << 10U)

#define SMAP_SCR    (SMAP_BASE + 0x08U)
#define SMAP_ADDR   (SMAP_BASE + 0x0cU)
#define SMAP_LEN    (SMAP_BASE + 0x10U)
#define SMAP_DATA   (SMAP_BASE + 0x14U)
#define SMAP_VERS   (SMAP_BASE + 0x28U)
#define SMAP_CHIPID (SMAP_BASE + 0xf0U)
#define SMAP_EXTID  (SMAP_BASE + 0xf4U)
#define SMAP_IDR    (SMAP_BASE + 0xfcU)

static void sam4l_extended_reset(target_s *t);
static bool sam4l_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool sam4l_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len);

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
static void sam4l_add_flash(target_s *t, uint32_t addr, size_t length)
{
	target_flash_s *f = calloc(1, sizeof(*f));
	if (!f) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = addr;
	f->length = length;
	f->blocksize = SAM4L_PAGE_SIZE;
	f->erase = sam4l_flash_erase;
	f->write = sam4l_flash_write;
	f->writesize = SAM4L_PAGE_SIZE;
	f->erased = 0xff;
	/* Add it into the target structures flash chain */
	target_add_flash(t, f);
}

/*
 * The probe function, look where the CIDR register should be, see if
 * it matches the SAM4L architecture code.
 *
 * Figure out from the register how much RAM and FLASH this variant has.
 */
bool sam4l_probe(target_s *t)
{
	const uint32_t cidr = target_mem_read32(t, SAM4L_CHIPID_CIDR);
	if (((cidr >> CHIPID_CIDR_ARCH_SHIFT) & CHIPID_CIDR_ARCH_MASK) != SAM4L_ARCH)
		return false;

	/* Look up the RAM and Flash size of the device */
	const uint32_t ram_size = sam4l_ram_size[(cidr >> CHIPID_CIDR_SRAMSIZ_SHIFT) & CHIPID_CIDR_SRAMSIZ_MASK];
	const uint32_t flash_size = sam4l_nvp_size[(cidr >> CHIPID_CIDR_NVPSIZ_SHIFT) & CHIPID_CIDR_NVPSIZ_MASK];

	t->driver = "Atmel SAM4L";
	/* This function says we need to do "extra" stuff after reset */
	t->extended_reset = sam4l_extended_reset;

	target_add_ram(t, 0x20000000, ram_size);
	sam4l_add_flash(t, 0x0, flash_size);

	DEBUG_INFO("SAM4L - RAM: 0x%" PRIx32 " (%" PRIu32 "kiB), FLASH: 0x%" PRIx32 " (%" PRIu32 "kiB)\n", ram_size,
		ram_size / 1024U, flash_size, flash_size / 1024U);

	/* Enable SMAP if not, check for HCR and reset if set */
	sam4l_extended_reset(t);
	if (target_check_error(t))
		DEBUG_ERROR("SAM4L: target_check_error returned true\n");
	return true;
}

/*
 * We've been reset, make sure we take the core out of reset
 */
static void sam4l_extended_reset(target_s *t)
{
	DEBUG_INFO("SAM4L: Extended Reset\n");

	/* Enable SMAP in case we're dealing with a non-JTAG reset */
	target_mem_write32(t, SMAP_CR, 0x1); /* enable SMAP */
	uint32_t reg = target_mem_read32(t, SMAP_SR);
	DEBUG_INFO("SMAP_SR has 0x%08" PRIx32 "\n", reg);
	if ((reg & SMAP_SR_HCR) != 0) {
		/* Write '1' bit to the status clear register */
		target_mem_write32(t, SMAP_SCR, SMAP_SR_HCR);
		/* Waiting 250 loops for it to reset is arbitrary, it should happen right away */
		for (size_t i = 0; i < 250U; i++) {
			reg = target_mem_read32(t, SMAP_SR);
			if (!(reg & SMAP_SR_HCR))
				break;
			/* Not sure what to do if we can't reset that bit */
			if (i == 249)
				DEBUG_INFO("Reset failed. SMAP_SR has 0x%08" PRIx32 "\n", reg);
		}
	}
	/* reset bus error if for some reason SMAP was disabled */
	target_check_error(t);
}

/*
 * Helper function, wait for the flash controller to be ready to receive a
 * command. Then send it the command, page number, and the authorization
 * key (always 0xa5) in the command register.
 *
 * Need the target struct to call the mem_read32 and mem_write32 function
 * pointers.
 */
static bool sam4l_flash_command(target_s *t, uint32_t page, uint32_t cmd)
{
	DEBUG_INFO(
		"%s: FSR: 0x%08" PRIx32 ", page = %" PRIu32 ", command = %" PRIu32 "\n", __func__, FLASHCALW_FSR, page, cmd);

	/* Wait for Flash controller ready */
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, FLASH_TIMEOUT);
	while (!(target_mem_read32(t, FLASHCALW_FSR) & FLASHCALW_FSR_FRDY)) {
		if (platform_timeout_is_expired(&timeout)) {
			DEBUG_WARN("%s: Not ready!\n", __func__);
			return false;
		}
	}

	/* Load up the new command */
	const uint32_t cmd_reg = (cmd & FLASHCALW_FCMD_CMD_MASK) |
		((page & FLASHCALW_FCMD_PAGEN_MASK) << FLASHCALW_FCMD_PAGEN_SHIFT) | (0xa5U << FLASHCALW_FCMD_KEY_SHIFT);
	DEBUG_INFO("%s: Writing command word 0x%08" PRIx32 "\n", __func__, cmd_reg);

	/* And kick it off */
	target_mem_write32(t, FLASHCALW_FCMD, cmd_reg);
	/* Don't actually wait for it to finish, the next command will stall if it is not done */
	return true;
}

/* Write data from 'src' into flash using the algorithm provided by Atmel in their data sheet. */
static bool sam4l_flash_write(
	target_flash_s *const f, const target_addr_t dest, const void *const src, const size_t len)
{
	DEBUG_INFO("%s: dest = 0x%08" PRIx32 ", len %" PRIx32 "\n", __func__, dest, (uint32_t)len);
	/* Writing any more or less than 1 page size is not supported by this for now */
	if (len != SAM4L_PAGE_SIZE)
		return false;

	target_s *t = f->t;
	/* This will fail with unaligned writes, however the target Flash API guarantees we're called aligned */
	const uint16_t page = dest / SAM4L_PAGE_SIZE;

	/* Clear the page buffer */
	if (!sam4l_flash_command(t, 0, FLASH_CMD_CPB))
		return false;

	/* Now fill page buffer with our 512 bytes of data */

	const uint32_t *const data = src;
	/* I did try to use target_mem_write however that resulted in the
	 * last 64 bits (8 bytes) to be incorrect on even pages (0, 2, 4, ...)
	 * since it works this way I've not investigated further.
	 */
	for (size_t offset = 0; offset < SAM4L_PAGE_SIZE; offset += 4U) {
		/*
 		 * The page buffer overlaps flash, its only 512 bytes long
		 * and no matter where you write it from it goes to the page
		 * you point it to. So we don't need the specific address here
		 * instead we just write 0 -> pagelen (512) and that fills our
		 * buffer correctly.
		 */
		target_mem_write32(t, dest + offset, data[offset / 4U]);
	}

	/* write the page */
	return sam4l_flash_command(t, page, FLASH_CMD_WP);
}

/* Erase flash across the addresses specified by addr and len */
static bool sam4l_flash_erase(target_flash_s *f, target_addr_t addr, size_t len)
{
	DEBUG_INFO("SAM4L: flash erase address 0x%08" PRIx32 " for %" PRIu32 " bytes\n", addr, (uint32_t)len);
	/*
	 * NB: if addr isn't aligned to a page boundary, or length
	 * is not an even multiple of page sizes, we may end up
	 * erasing data we didn't intend to.
	 * This issue is however mitigated by the target Flash API layer somewhat.
	 */

	target_s *t = f->t;

	for (size_t offset = 0; offset < len; offset += SAM4L_PAGE_SIZE) {
		const size_t page = (addr + offset) / SAM4L_PAGE_SIZE;
		if (!sam4l_flash_command(t, page, FLASH_CMD_EP))
			return false;
	}
	return true;
}
