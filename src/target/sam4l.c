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

/* This file implements Atmel SAM4 target specific functions for detecting
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
#define FLASHCALW_BASE				0x400A0000

/* Flash Control Register */
#define	FLASHCALW_FCR				(FLASHCALW_BASE + 0x00)
#define	FLASHALCW_FCR_WS1OPT		(1 << 7)
#define	FLASHALCW_FCR_FWS			(1 << 6)
#define	FLASHALCW_FCR_ECCE			(1 << 4)
#define	FLASHALCW_FCR_PROGE			(1 << 3)
#define	FLASHALCW_FCR_LOCKE			(1 << 2)
#define	FLASHALCW_FCR_FRDY			(1 << 0)

/* Flash Command Register */
#define	FLASHCALW_FCMD				(FLASHCALW_BASE + 0x04)
#define FLASHCALW_FCMD_KEY_MASK		0xff
#define FLASHCALW_FCMD_KEY_SHIFT	24
#define FLASHCALW_FCMD_PAGEN_MASK	0xffff
#define FLASHCALW_FCMD_PAGEN_SHIFT	8
#define FLASHCALW_FCMD_CMD_MASK		0x3f
#define FLASHCALW_FCMD_CMD_SHIFT	0

#define FLASH_CMD_NOP		0
#define FLASH_CMD_WP		1	/* Write Page */
#define FLASH_CMD_EP		2	/* Erase Page */
#define FLASH_CMD_CPB		3	/* Clear Page Buffer */
#define FLASH_CMD_LP		4	/* Lock page region */
#define FLASH_CMD_UP		5	/* Unlock page region */
#define FLASH_CMD_EA		6	/* Erase All */
#define FLASH_CMD_WGPB		7	/* Write General Purpose Fuse Bit */
#define FLASH_CMD_EGPB		8	/* Erase General Purpose Fuse Bit */
#define FLASH_CMD_SSB		9	/* Set Security Fuses */
#define FLASH_CMD_PGPFB		10	/* Program General Purpose Fuse Byte */
#define FLASH_CMD_EAGPF		11	/* Erase All GP Fuses */
#define FLASH_CMD_QPR		12	/* Quick Page Read (erase check) */
#define FLASH_CMD_WUP		13	/* Write User Page */
#define FLASH_CMD_EUP		14	/* Erase User Page */
#define FLASH_CMD_QPRUP		15	/* Quick Page Read User Page */
#define FLASH_CMD_HSEN		16	/* High Speed Enable */
#define FLASH_CMD_HSDIS		17	/* High Speed Disable */

/* Flash Status Register */
#define FLASHCALW_FSR				(FLASHCALW_BASE + 0x08)
#define FLASHCALW_FSR_LOCK(x)		(1 << (16 + (x)))
#define FLASHCALW_FSR_ECCERR		(1 << 9)
#define FLASHCALW_FSR_ECCERR2		(1 << 8)
#define FLASHCALW_FSR_HSMODE		(1 << 6)
#define FLASHCALW_FSR_QPRR			(1 << 5)
#define FLASHCALW_FSR_SECURITY		(1 << 4)
#define FLASHCALW_FSR_PROGE			(1 << 3)
#define FLASHCALW_FSR_LOCKE			(1 << 2)
#define FLASHCALW_FSR_FRDY			(1 << 0)

/* Flash Parameter Register */
#define FLASHCALW_FPR				(FLASHCALW_BASE + 0x0a)
#define FLASHCALW_FPR_PSZ_MASK		0x7	/* page size */
#define FLASHCALW_FPR_PSZ_SHIFT		8
#define FLASHCALW_FPR_FSZ_MASK		0xf	/* flash size */
#define FLASHCALW_FPR_FSZ_SHIFT		0

/* Flash Version Register */
#define FLASHCALW_FVR				(FLASHCALW_BASE + 0x10)
#define FLASHCALW_FVR_VARIANT_MASK	0xf
#define FLASHCALW_FVR_VARIANT_SHIFT	16
#define FLASHCALW_FVR_VERSION_MASK	0xfff
#define FLASHCALW_FVR_VERSION_SHIFT	0

/* Flash General Purpose Registers (high) */
#define FLASHCALW_FGPFRHI			(FLASHCALW_BASE + 0x14)
/* Flash General Purpose Registers (low) */
#define FLASHCALW_FGPFRLO			(FLASHCALW_BASE + 0x18)

static void sam4l_extended_reset(target *t);
static int sam4l_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static int sam4l_flash_write_buf(struct target_flash *f, target_addr dest,
									const void *src, size_t len);

/* why Atmel couldn't make it sequential ... */
static const size_t __ram_size[16] = {
	48 * 1024,		/*  0: 48K */
	1 * 1024,		/*  1: 1K */
	2 * 1024,		/*  2: 2K */
	6 * 1024,		/*  3: 6K */
	24 * 1024,		/*  4: 24K */
	4 * 1024,		/*  5: 4K */
	80 * 1024,		/*  6: 80K */
	160 * 1024,		/*  7: 160K */
	8 * 1024,		/*  8: 8K */
	16 * 1024,		/*  9: 16K */
	32 * 1024,		/* 10: 32K */
	64 * 1024,		/* 11: 64K */
	128 * 1024,		/* 12: 128K */
	256 * 1024,		/* 13: 256K */
	96 * 1024,		/* 14: 96K */
	512 * 1024		/* 15: 512K */
};

static const size_t __nvp_size[16] = {
	0,				/*  0: none */
	8 * 1024,		/*  1: 8K */
	16 * 1024,		/*  2: 16K */
	32 * 1024,		/*  3: 32K */
	0,				/*  4: reserved */
	64 * 1024,		/*  5: 64K */
	0,				/*  6: reserved */
	128 * 1024,		/*  7: 128K */
	0,				/*  8: reserved */
	256 * 1024,		/*  9: 256K */
	512 * 1024,		/* 10: 512K */
	0,				/* 11: reserved */
	1024 * 1024,	/* 12: 1024K (1M) */
	0,				/* 13: reserved */
	2048 * 1024,	/* 14: 2048K (2M) */
	0				/* 15: reserved */
};


/* All variants of 4L have a 512 byte page */
#define SAM4L_PAGE_SIZE 512
#define SAM4L_ARCH		0xb0
#define SAM4L_CHIPID_CIDR	0x400E0740
#define CHIPID_CIDR_ARCH_MASK		0xff
#define CHIPID_CIDR_ARCH_SHIFT		20
#define CHIPID_CIDR_SRAMSIZ_MASK	0xf
#define CHIPID_CIDR_SRAMSIZ_SHIFT	16
#define CHIPID_CIDR_NVPSIZ_MASK		0xf
#define CHIPID_CIDR_NVPSIZ_SHIFT	8


/* Arbitrary time to wait for FLASH controller to be ready */
#define FLASH_TIMEOUT	10000

/*
 * Populate a target_flash struct with the necessary function pointers
 * and constants to describe our flash.
 */
static void sam4l_add_flash(target *t, uint32_t addr, size_t length)
{
	struct target_flash *f = calloc(1, sizeof(struct target_flash));
	f->start = addr;
	f->length = length;
	f->blocksize = SAM4L_PAGE_SIZE;
	f->erase = sam4l_flash_erase;
	f->write = sam4l_flash_write_buf;
	f->buf_size = SAM4L_PAGE_SIZE;
	f->erased = 0xff;
	/* add it into the target structures flash chain */
	target_add_flash(t, f);
}

/* Return size of RAM */
static size_t sam_ram_size(uint32_t idcode) {
	return __ram_size[((idcode >> CHIPID_CIDR_SRAMSIZ_SHIFT) & CHIPID_CIDR_SRAMSIZ_MASK)];
}

/* Return size of FLASH */
static size_t sam_nvp_size(uint32_t idcode) {
	return __nvp_size[((idcode >> CHIPID_CIDR_NVPSIZ_SHIFT) & CHIPID_CIDR_NVPSIZ_MASK)];
}

#define SMAP_BASE	0x400a3000
#define SMAP_CR		(SMAP_BASE + 0x00)
#define SMAP_SR		(SMAP_BASE + 0x04)
#define SMAP_SR_DONE	(1 << 0)
#define SMAP_SR_HCR		(1 << 1)
#define SMAP_SR_BERR	(1 << 2)
#define SMAP_SR_FAIL	(1 << 3)
#define SMAP_SR_LCK		(1 << 4)
#define SMAP_SR_EN		(1 << 8)
#define SMAP_SR_PROT	(1 << 9)
#define SMAP_SR_DBGP	(1 << 10)


#define SMAP_SCR	(SMAP_BASE + 0x08)
#define SMAP_ADDR	(SMAP_BASE + 0x0c)
#define SMAP_LEN	(SMAP_BASE + 0x10)
#define SMAP_DATA	(SMAP_BASE + 0x14)
#define SMAP_VERS	(SMAP_BASE + 0x28)
#define SMAP_CHIPID	(SMAP_BASE + 0xf0)
#define SMAP_EXTID	(SMAP_BASE + 0xf4)
#define SMAP_IDR	(SMAP_BASE + 0xfc)


/*
 * The probe function, look where the CIDR register should be, see if
 * it matches the SAM4L architecture code.
 *
 * Figure out from the register how much RAM and FLASH this variant has.
 */
bool sam4l_probe(target *t)
{
	size_t	ram_size, flash_size;

	DEBUG("\nSAM4L: Probe function called\n");
	t->idcode = target_mem_read32(t, SAM4L_CHIPID_CIDR);
	if (((t->idcode >> CHIPID_CIDR_ARCH_SHIFT) & CHIPID_CIDR_ARCH_MASK) == SAM4L_ARCH) {
		t->driver = "Atmel SAM4L";
		/* this function says we need to do "extra" stuff after reset */
		t->extended_reset = sam4l_extended_reset;
		ram_size = sam_ram_size(t->idcode);
		target_add_ram(t, 0x20000000, ram_size);
		flash_size = sam_nvp_size(t->idcode);
		sam4l_add_flash(t, 0x0, flash_size);
		DEBUG("\nSAM4L: RAM = 0x%x (%dK), FLASH = 0x%x (%dK)\n",
			(unsigned int) ram_size, (unsigned int) (ram_size / 1024),
					(unsigned int) flash_size, (unsigned int)(flash_size / 1024));

		/* enable SMAP if not, check for HCR and reset if set */
		sam4l_extended_reset(t);
		DEBUG("\nSAM4L: SAM4L Selected.\n");
		if (target_check_error(t)) {
			DEBUG("SAM4L: target_check_error returned true\n");
		}
		return true;
	}
	return false;
}

/*
 * We've been reset, make sure we take the core out of reset
 */
static void
sam4l_extended_reset(target *t)
{
	uint32_t	reg;
	int i;

	DEBUG("SAM4L: Extended Reset\n");
	/* enable SMAP in case we're dealing with a non-TCK SRST */
	target_mem_write32(t, SMAP_CR, 0x1); /* enable SMAP */
	reg = target_mem_read32(t, SMAP_SR);
	DEBUG("\nSAM4L: SMAP_SR has 0x%08lx\n", (long unsigned int) reg);
	if ((reg & SMAP_SR_HCR) != 0) {
		/* write '1' bit to the status clear register */
		target_mem_write32(t, SMAP_SCR, SMAP_SR_HCR);
		/* waiting 250 loops for it to reset is arbitrary, it should happen right away */
		for (i = 0; i < 250; i++) {
			reg = target_mem_read32(t, SMAP_SR);
		}
		/* not sure what to do if we can't reset that bit */
		if (i > 249) {
			DEBUG("\nSAM4L: Reset failed. SMAP_SR has 0x%08lx\n", (long unsigned int) reg);
		}
	}
	/* reset bus error if for some reason SMAP was disabled */
	target_check_error(t);
}

/*
 * sam4l_flash_command
 *
 * Helper function, wait for the flash controller to be ready to receive a
 * command. Then send it the command, page number, and the authorization
 * key (always 0xA5) in the command register.
 *
 * Need the target struct to call the mem_read32 and mem_write32 function
 * pointers.
 */
static int
sam4l_flash_command(target *t, uint32_t page, uint32_t cmd)
{
	uint32_t cmd_reg;
	uint32_t status;
	int	timeout;
	DEBUG("\nSAM4L: sam4l_flash_command: FSR: 0x%08x, page = %d, command = %d\n",
		(unsigned int)(FLASHCALW_FSR), (int) page, (int) cmd);
	/* wait for Flash controller ready */
	for (timeout = 0; timeout < FLASH_TIMEOUT; timeout++) {
		status = target_mem_read32(t, FLASHCALW_FSR);
		if (status & FLASHCALW_FSR_FRDY) {
			break;
		}
	}
	if (timeout == FLASH_TIMEOUT) {
		DEBUG("\nSAM4L: sam4l_flash_command: Not ready! Status = 0x%08x\n", (unsigned int) status);
		return -1; /* Failed */
	}
	/* load up the new command */
	cmd_reg = (cmd & FLASHCALW_FCMD_CMD_MASK) |
			  ((page & FLASHCALW_FCMD_PAGEN_MASK) << FLASHCALW_FCMD_PAGEN_SHIFT) |
		  	  (0xA5 << FLASHCALW_FCMD_KEY_SHIFT);
	DEBUG("\nSAM4L: sam4l_flash_command: Wrting command word 0x%08x\n", (unsigned int) cmd_reg);
	/* and kick it off */
	target_mem_write32(t, FLASHCALW_FCMD, cmd_reg);
	/* don't actually wait for it to finish, the next command will stall if it is not done */
	return 0;
}

/*
 * Write data from 'src' into flash using the algorithim provided by
 * Atmel in their data sheet.
 */
static int
sam4l_flash_write_buf(struct target_flash *f, target_addr addr, const void *src, size_t len)
{
	target *t = f->t;
	uint32_t *src_data = (uint32_t *)src;
	uint32_t ndx;
	uint16_t page;

	DEBUG("\nSAM4L: sam4l_flash_write_buf: addr = 0x%08lx, len %d\n", (long unsigned int) addr, (int) len);
	/* This will fail with unaligned writes, the write_buf version */
	page = addr / SAM4L_PAGE_SIZE;

	if (len != SAM4L_PAGE_SIZE) {
		return -1;
	}

	/* clear the page buffer */
	if (sam4l_flash_command(t, 0, FLASH_CMD_CPB)) {
		return -1;
	}

	/* Now fill page buffer with our 512 bytes of data */

	/* I did try to use target_mem_write however that resulted in the
	 * last 64 bits (8 bytes) to be incorrect on even pages (0, 2, 4, ...)
	 * since it works this way I've not investigated further.
	 */
	for (ndx = 0; ndx < SAM4L_PAGE_SIZE; ndx += 4) {
		/*
 		 * the page buffer overlaps flash, its only 512 bytes long
		 * and no matter where you write it from it goes to the page
		 * you point it to. So we don't need the specific address here
		 * instead we just write 0 - pagelen (512) and that fills our
		 * buffer correctly.
		 */
		target_mem_write32(t, addr+ndx, *src_data);
		src_data++;
	}
	/* write the page */
	if (sam4l_flash_command(t, page, FLASH_CMD_WP)) {
		return -1;
	}
	return 0;
}

/*
 * Erase flash across the addresses specified by addr and len
 */
static int
sam4l_flash_erase(struct target_flash *f, target_addr addr, size_t len)
{
	target *t = f->t;
	uint16_t page;

	DEBUG("SAM4L: flash erase address 0x%08x for %d bytes\n",
		(unsigned int) addr, (unsigned int) len);
	/*
	 *  NB: if addr isn't aligned to a page boundary, or length
	 * is not an even multiple of page sizes, we may end up
	 * erasing data we didn't intend to.
	 */

	while (len) {
		page = addr / SAM4L_PAGE_SIZE;
		if (sam4l_flash_command(t, page, FLASH_CMD_EP)) {
			return -1;
		}
		len -= SAM4L_PAGE_SIZE;
		addr += SAM4L_PAGE_SIZE;
	}
	return 0;
}
