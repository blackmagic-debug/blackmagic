/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
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

/* This file implements KL25 target specific functions providing
 * the XML memory map and Flash memory programming.
 *
 * According to Freescale doc KL25P80M48SF0RM:
 *    KL25 Sub-family Reference Manual
 *
 * Extended with support for KL02 family
 *
 * Extended with support for K64 family with info from K22P64M50SF4RM:
 * 		K22 Sub-Family Reference Manual
 *
 * Extended with support for K64 family with info from K64P144M120SF5RM:
 * 		K64 Sub-Family Reference Manual, Rev. 2,
 */

#include "command.h"
#include "general.h"
#include "target.h"
#include "target_internal.h"

#define SIM_SDID   0x40048024
#define SIM_FCFG1  0x4004804C

#define FTFA_BASE  0x40020000
#define FTFA_FSTAT (FTFA_BASE + 0x00)
#define FTFA_FCNFG (FTFA_BASE + 0x01)
#define FTFA_FSEC  (FTFA_BASE + 0x02)
#define FTFA_FOPT  (FTFA_BASE + 0x03)
#define FTFA_FCCOB_0 (FTFA_BASE + 0x04)
#define FTFA_FCCOB_1 (FTFA_BASE + 0x08)
#define FTFA_FCCOB_2 (FTFA_BASE + 0x0C)

#define FTFA_FSTAT_CCIF     (1 << 7)
#define FTFA_FSTAT_RDCOLERR (1 << 6)
#define FTFA_FSTAT_ACCERR   (1 << 5)
#define FTFA_FSTAT_FPVIOL   (1 << 4)
#define FTFA_FSTAT_MGSTAT0  (1 << 0)

#define FTFA_CMD_CHECK_ERASE       0x01
#define FTFA_CMD_PROGRAM_CHECK     0x02
#define FTFA_CMD_READ_RESOURCE     0x03
#define FTFA_CMD_PROGRAM_LONGWORD  0x06
/* Part of the FTFE module for K64 */
#define FTFE_CMD_PROGRAM_PHRASE    0x07
#define FTFA_CMD_ERASE_SECTOR      0x09
#define FTFA_CMD_CHECK_ERASE_ALL   0x40
#define FTFA_CMD_READ_ONCE         0x41
#define FTFA_CMD_PROGRAM_ONCE      0x43
#define FTFA_CMD_ERASE_ALL         0x44
#define FTFA_CMD_BACKDOOR_ACCESS   0x45

#define KL_WRITE_LEN 4
/* 8 byte phrases need to be written to the k64 flash */
#define K64_WRITE_LEN 8

static bool kinetis_cmd_unsafe(target *t, int argc, char *argv[]);

const struct command_s kinetis_cmd_list[] = {
	{"unsafe", (cmd_handler)kinetis_cmd_unsafe, "Allow programming security byte (enable|disable)"},
	{NULL, NULL, NULL}
};

static bool kinetis_cmd_unsafe(target *t, int argc, char *argv[])
{
	if (argc == 1) {
		tc_printf(t, "Allow programming security byte: %s\n",
			  t->unsafe_enabled ? "enabled" : "disabled");
	} else {
		parse_enable_or_disable(argv[1], &t->unsafe_enabled);
	}
	return true;
}

static int kl_gen_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static int kl_gen_flash_write(struct target_flash *f,
                              target_addr dest, const void *src, size_t len);
static int kl_gen_flash_done(struct target_flash *f);

struct kinetis_flash {
	struct target_flash f;
	uint8_t write_len;
};

static void kl_gen_add_flash(target *t, uint32_t addr, size_t length,
                             size_t erasesize, size_t write_len)
{
	struct kinetis_flash *kf = calloc(1, sizeof(*kf));
	struct target_flash *f;

	if (!kf) {			/* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	f = &kf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = erasesize;
	f->erase = kl_gen_flash_erase;
	f->write = kl_gen_flash_write;
	f->done = kl_gen_flash_done;
	f->erased = 0xff;
	kf->write_len = write_len;
	target_add_flash(t, f);
}

bool kinetis_probe(target *t)
{
	uint32_t sdid = target_mem_read32(t, SIM_SDID);
	uint32_t fcfg1 = target_mem_read32(t, SIM_FCFG1);

	switch (sdid >> 20) {
	case 0x161:
		/* sram memory size */
		switch((sdid >> 16) & 0x0f) {
			case 0x03:/* 4 KB */
				target_add_ram(t, 0x1ffffc00, 0x0400);
				target_add_ram(t, 0x20000000, 0x0C00);
				break;
			case 0x04:/* 8 KB */
				target_add_ram(t, 0x1ffff800, 0x0800);
				target_add_ram(t, 0x20000000, 0x1800);
				break;
			case 0x05:/* 16 KB */
				target_add_ram(t, 0x1ffff000, 0x1000);
				target_add_ram(t, 0x20000000, 0x3000);
				break;
			case 0x06:/* 32 KB */
				target_add_ram(t, 0x1fffe000, 0x2000);
				target_add_ram(t, 0x20000000, 0x6000);
				break;
			default:
				return false;
				break;
		}

		/* flash memory size */
		switch((fcfg1 >> 24) & 0x0f) {
			case 0x03: /* 32 KB */
				t->driver = "KL16Z32Vxxx";
				kl_gen_add_flash(t, 0x00000000, 0x08000, 0x400, KL_WRITE_LEN);
				break;

			case 0x05: /* 64 KB */
				t->driver = "KL16Z64Vxxx";
				kl_gen_add_flash(t, 0x00000000, 0x10000, 0x400, KL_WRITE_LEN);
				break;

			case 0x07: /* 128 KB */
				t->driver = "KL16Z128Vxxx";
				kl_gen_add_flash(t, 0x00000000, 0x20000, 0x400, KL_WRITE_LEN);
				break;

			case 0x09: /* 256 KB */
				t->driver = "KL16Z256Vxxx";
				kl_gen_add_flash(t, 0x00000000, 0x40000, 0x400, KL_WRITE_LEN);
				break;
			default:
				return false;
				break;
		}

		break;

	case 0x251:
		t->driver = "KL25";
		target_add_ram(t, 0x1ffff000, 0x1000);
		target_add_ram(t, 0x20000000, 0x3000);
		kl_gen_add_flash(t, 0x00000000, 0x20000, 0x400, KL_WRITE_LEN);
		break;
	case 0x231:
		t->driver = "KL27x128"; // MKL27 >=128kb
		target_add_ram(t, 0x1fffe000, 0x2000);
		target_add_ram(t, 0x20000000, 0x6000);
		kl_gen_add_flash(t, 0x00000000, 0x40000, 0x400, KL_WRITE_LEN);
		break;
	case 0x271:
		switch((sdid >> 16) & 0x0f) {
			case 4:
				t->driver = "KL27x32";
				target_add_ram(t, 0x1ffff800, 0x0800);
				target_add_ram(t, 0x20000000, 0x1800);
				kl_gen_add_flash(t, 0x00000000, 0x8000, 0x400, KL_WRITE_LEN);
				break;
			case 5:
				t->driver = "KL27x64";
				target_add_ram(t, 0x1ffff000, 0x1000);
				target_add_ram(t, 0x20000000, 0x3000);
				kl_gen_add_flash(t, 0x00000000, 0x10000, 0x400, KL_WRITE_LEN);
				break;
			default:
				return false;
		}
		break;
	case 0x021: /* KL02 family */
		switch((sdid >> 16) & 0x0f) {
			case 3:
				t->driver = "KL02x32";
				target_add_ram(t, 0x1FFFFC00, 0x400);
				target_add_ram(t, 0x20000000, 0xc00);
				kl_gen_add_flash(t, 0x00000000, 0x7FFF, 0x400, KL_WRITE_LEN);
				break;
			case 2:
				t->driver = "KL02x16";
				target_add_ram(t, 0x1FFFFE00, 0x200);
				target_add_ram(t, 0x20000000, 0x600);
				kl_gen_add_flash(t, 0x00000000, 0x3FFF, 0x400, KL_WRITE_LEN);
				break;
			case 1:
				t->driver = "KL02x8";
				target_add_ram(t, 0x1FFFFF00, 0x100);
				target_add_ram(t, 0x20000000, 0x300);
				kl_gen_add_flash(t, 0x00000000, 0x1FFF, 0x400, KL_WRITE_LEN);
				break;
			default:
				return false;
		}
		break;
	case 0x031: /* KL03 family */
		t->driver = "KL03";
		target_add_ram(t, 0x1ffffe00, 0x200);
		target_add_ram(t, 0x20000000, 0x600);
		kl_gen_add_flash(t, 0, 0x8000, 0x400, KL_WRITE_LEN);
		break;
	case 0x220: /* K22F family */
		t->driver = "K22F";
		target_add_ram(t, 0x1c000000, 0x4000000);
		target_add_ram(t, 0x20000000, 0x100000);
		kl_gen_add_flash(t, 0, 0x40000, 0x800, KL_WRITE_LEN);
		kl_gen_add_flash(t, 0x40000, 0x40000, 0x800, KL_WRITE_LEN);
		break;
	case 0x620: /* K64F family. */
		/* This should be 0x640, but according to the  errata sheet
		 * (KINETIS_1N83J) K64 and K24's will show up with the
		 * subfamily nibble as 2
		 */
		t->driver = "K64";
		target_add_ram(t, 0x1FFF0000,  0x10000);
		target_add_ram(t, 0x20000000,  0x30000);
		kl_gen_add_flash(t, 0, 0x80000, 0x1000, K64_WRITE_LEN);
		kl_gen_add_flash(t, 0x80000, 0x80000, 0x1000, K64_WRITE_LEN);
		break;
	case 0x000: /* Older K-series */
		switch(sdid & 0xff0) {
			case 0x000: /* K10 Family, DIEID=0x0 */
			case 0x080: /* K10 Family, DIEID=0x1 */
			case 0x100: /* K10 Family, DIEID=0x2 */
			case 0x180: /* K10 Family, DIEID=0x3 */
			case 0x220: /* K11 Family, DIEID=0x4 */
				return false;
			case 0x200: /* K12 Family, DIEID=0x4 */
				switch((fcfg1 >> 24) & 0x0f) {
					/* K12 Sub-Family Reference Manual, K12P80M50SF4RM, Rev. 4, February 2013 */
					case 0x7:
						t->driver = "MK12DX128Vxx5";
						target_add_ram(t, 0x1fffc000, 0x00004000); /* SRAM_L, 16 KB */
						target_add_ram(t, 0x20000000, 0x00004000); /* SRAM_H, 16 KB */
						kl_gen_add_flash(t, 0x00000000, 0x00020000, 0x800, KL_WRITE_LEN); /* P-Flash, 128 KB, 2 KB Sectors */
						kl_gen_add_flash(t, 0x10000000, 0x00010000, 0x800, KL_WRITE_LEN); /* FlexNVM, 64 KB, 2 KB Sectors */
						break;
					case 0x9:
						t->driver = "MK12DX256Vxx5";
						target_add_ram(t, 0x1fffc000, 0x00004000); /* SRAM_L, 16 KB */
						target_add_ram(t, 0x20000000, 0x00004000); /* SRAM_H, 16 KB */
						kl_gen_add_flash(t, 0x00000000, 0x00040000, 0x800, KL_WRITE_LEN); /* P-Flash, 256 KB, 2 KB Sectors */
						kl_gen_add_flash(t, 0x10000000, 0x00010000, 0x800, KL_WRITE_LEN); /* FlexNVM, 64 KB, 2 KB Sectors */
						break;
					case 0xb:
						t->driver = "MK12DN512Vxx5";
						target_add_ram(t, 0x1fff8000, 0x00008000); /* SRAM_L, 32 KB */
						target_add_ram(t, 0x20000000, 0x00008000); /* SRAM_H, 32 KB */
						kl_gen_add_flash(t, 0x00000000, 0x00040000, 0x800, KL_WRITE_LEN); /* P-Flash, 256 KB, 2 KB Sectors */
						kl_gen_add_flash(t, 0x00040000, 0x00040000, 0x800, KL_WRITE_LEN); /* FlexNVM, 256 KB, 2 KB Sectors */
						break;
					default:
						return false;
				}
				break;
			case 0x010: /* K20 Family, DIEID=0x0 */
			case 0x090: /* K20 Family, DIEID=0x1 */
			case 0x110: /* K20 Family, DIEID=0x2 */
			case 0x190: /* K20 Family, DIEID=0x3 */
			case 0x230: /* K21 Family, DIEID=0x4 */
			case 0x330: /* K21 Family, DIEID=0x6 */
			case 0x210: /* K22 Family, DIEID=0x4 */
			case 0x310: /* K22 Family, DIEID=0x6 */
			case 0x0a0: /* K30 Family, DIEID=0x1 */
			case 0x120: /* K30 Family, DIEID=0x2 */
			case 0x0b0: /* K40 Family, DIEID=0x1 */
			case 0x130: /* K40 Family, DIEID=0x2 */
			case 0x0e0: /* K50 Family, DIEID=0x1 */
			case 0x0f0: /* K51 Family, DIEID=0x1 */
			case 0x170: /* K53 Family, DIEID=0x2 */
			case 0x140: /* K60 Family, DIEID=0x2 */
			case 0x1c0: /* K60 Family, DIEID=0x3 */
			case 0x1d0: /* K70 Family, DIEID=0x3 */
			default:
				return false;
		}
		break;
	case 0x118: /* S32K118 */
		t->driver = "S32K118";
		target_add_ram(t, 0x1ffffc00, 0x00000400); /* SRAM_L, 1 KB */
		target_add_ram(t, 0x20000000, 0x00005800); /* SRAM_H, 22 KB */
		kl_gen_add_flash(t, 0x00000000, 0x00040000, 0x800, K64_WRITE_LEN); /* P-Flash, 256 KB, 2 KB Sectors */
		kl_gen_add_flash(t, 0x10000000, 0x00008000, 0x800, K64_WRITE_LEN); /* FlexNVM, 32 KB, 2 KB Sectors */
		break;
		/* gen1 s32k14x */
		{
			uint32_t sram_l, sram_h;
			uint32_t flash, flexmem;
	case 0x142: /* s32k142 */
	case 0x143: /* s32k142w */
		sram_l = 0x1FFFC000; /* SRAM_L, 16k */
		sram_h = 0x03000;		 /* SRAM_H, 12k */
		flash = 0x00040000;	 /* flash 256 KB */
		flexmem = 0x10000;	 /* FlexNVM 64 KB */
		goto do_common_s32k14x;
	case 0x144: /* s32k144 */
	case 0x145: /* s32k144w */
		sram_l = 0x1FFF8000; /* SRAM_L, 32k */
		sram_h = 0x07000;		 /* SRAM_H, 28k */
		flash = 0x00080000;	 /* flash 512 KB */
		flexmem = 0x10000;	 /* FlexNVM 64 KB */
		goto do_common_s32k14x;
	case 0x146: /* s32k146 */
		sram_l = 0x1fff0000; /* SRAM_L, 64k */
		sram_h = 0x0f000;		 /* SRAM_H, 60k */
		flash = 0x00100000;	 /* flash 1024 KB */
		flexmem = 0x10000;	 /* FlexNVM 64 KB */
		goto do_common_s32k14x;
	case 0x148: /* S32K148 */
		sram_l = 0x1ffe0000; /* SRAM_L, 128 KB */
		sram_h = 0x1f000;		 /* SRAM_H, 124 KB */
		flash = 0x00180000;	 /* flash 1536 KB */
		flexmem = 0x80000;	 /* FlexNVM 512 KB */
		goto do_common_s32k14x;
do_common_s32k14x:
		t->driver = "S32K14x";
		target_add_ram(t, sram_l, 0x20000000 - sram_l);
		target_add_ram(t, 0x20000000, sram_h);

		kl_gen_add_flash(t, 0x00000000, flash, 0x1000, K64_WRITE_LEN);	 /* P-Flash, 4 KB Sectors */
		kl_gen_add_flash(t, 0x10000000, flexmem, 0x1000, K64_WRITE_LEN); /* FlexNVM, 4 KB Sectors */
		break;
		}
	default:
		return false;
	}
	t->unsafe_enabled = false;
	target_add_commands(t, kinetis_cmd_list, t->driver);
	return true;
}

static bool
kl_gen_command(target *t, uint8_t cmd, uint32_t addr, const uint8_t data[8])
{
	uint8_t fstat;

	/* clear errors unconditionally, so we can start a new operation */
	target_mem_write8(t,FTFA_FSTAT,(FTFA_FSTAT_ACCERR | FTFA_FSTAT_FPVIOL));

	/* Wait for CCIF to be high */
	do {
		fstat = target_mem_read8(t, FTFA_FSTAT);
	} while (!(fstat & FTFA_FSTAT_CCIF));

	/* Write command to FCCOB */
	addr &= 0xffffff;
	addr |= (uint32_t)cmd << 24;
	target_mem_write32(t, FTFA_FCCOB_0, addr);
	if (data) {
		target_mem_write32(t, FTFA_FCCOB_1, *(uint32_t*)&data[0]);
		target_mem_write32(t, FTFA_FCCOB_2, *(uint32_t*)&data[4]);
	}

	/* Enable execution by clearing CCIF */
	target_mem_write8(t, FTFA_FSTAT, FTFA_FSTAT_CCIF);

	/* Wait for execution to complete */
	do {
		fstat = target_mem_read8(t, FTFA_FSTAT);
		/* Check ACCERR and FPVIOL are zero in FSTAT */
		if (fstat & (FTFA_FSTAT_ACCERR | FTFA_FSTAT_FPVIOL))
			return false;
	} while (!(fstat & FTFA_FSTAT_CCIF));

	return true;
}

static int kl_gen_flash_erase(struct target_flash *f, target_addr addr, size_t len)
{
	while (len) {
		if (kl_gen_command(f->t, FTFA_CMD_ERASE_SECTOR, addr, NULL)) {
			/* Different targets have different flash erase sizes */
			if (len > f->blocksize)
				len -= f->blocksize;
			else
				len = 0;
			addr += f->blocksize;
		} else {
			return 1;
		}
	}
	return 0;
}

#define FLASH_SECURITY_BYTE_ADDRESS 0x40C
#define FLASH_SECURITY_BYTE_UNSECURED 0xFE

static int kl_gen_flash_write(struct target_flash *f,
                              target_addr dest, const void *src, size_t len)
{
	struct kinetis_flash *kf = (struct kinetis_flash *)f;

	/* Ensure we don't write something horrible over the security byte */
	if (!f->t->unsafe_enabled &&
	    (dest <= FLASH_SECURITY_BYTE_ADDRESS) &&
	    ((dest + len) > FLASH_SECURITY_BYTE_ADDRESS)) {
		((uint8_t*)src)[FLASH_SECURITY_BYTE_ADDRESS - dest] =
		    FLASH_SECURITY_BYTE_UNSECURED;
	}

	/* Determine write command based on the alignment. */
	uint8_t write_cmd;
	if (kf->write_len == K64_WRITE_LEN) {
		write_cmd = FTFE_CMD_PROGRAM_PHRASE;
	} else {
		write_cmd = FTFA_CMD_PROGRAM_LONGWORD;
	}

	while (len) {
		if (kl_gen_command(f->t, write_cmd, dest, src)) {
			if (len > kf->write_len)
				len -= kf->write_len;
			else
				len = 0;
			dest += kf->write_len;
			src += kf->write_len;
		} else {
			return 1;
		}
	}
	return 0;
}

static int kl_gen_flash_done(struct target_flash *f)
{
	struct kinetis_flash *kf = (struct kinetis_flash *)f;

	if (f->t->unsafe_enabled)
		return 0;

	if (target_mem_read8(f->t, FLASH_SECURITY_BYTE_ADDRESS) ==
	    FLASH_SECURITY_BYTE_UNSECURED)
		return 0;

	/* Load the security byte based on the alignment (determine 8 byte phrases
	 * vs 4 byte phrases).
	 */
	if (kf->write_len == 8) {
		uint32_t vals[2];
		vals[0] = target_mem_read32(f->t, FLASH_SECURITY_BYTE_ADDRESS-4);
		vals[1] = target_mem_read32(f->t, FLASH_SECURITY_BYTE_ADDRESS);
		vals[1] = (vals[1] & 0xffffff00) | FLASH_SECURITY_BYTE_UNSECURED;
		kl_gen_command(f->t, FTFE_CMD_PROGRAM_PHRASE,
					   FLASH_SECURITY_BYTE_ADDRESS - 4, (uint8_t*)vals);
	} else {
		uint32_t vals[2] = {target_mem_read32(f->t, FLASH_SECURITY_BYTE_ADDRESS), 0};
		vals[0] = (vals[0] & 0xffffff00) | FLASH_SECURITY_BYTE_UNSECURED;
		kl_gen_command(f->t, FTFA_CMD_PROGRAM_LONGWORD,
					   FLASH_SECURITY_BYTE_ADDRESS, (uint8_t*)&vals);
	}

	return 0;
}

/*** Kinetis recovery mode using the MDM-AP ***/

/* Kinetis security bits are stored in regular flash, so it is possible
 * to enable protection by accident when flashing a bad binary.
 * a backdoor AP is provided which may allow a mass erase to recover the
 * device.  This provides a fake target to allow a monitor command interface
 */
#include "adiv5.h"

#define KINETIS_MDM_IDR_K22F 0x1c0000
#define KINETIS_MDM_IDR_KZ03 0x1c0020

static bool kinetis_mdm_cmd_erase_mass(target *t, int argc, const char **argv);
static bool kinetis_mdm_cmd_ke04_mode(target *t, int argc, const char **argv);

const struct command_s kinetis_mdm_cmd_list[] = {
	{"erase_mass", (cmd_handler)kinetis_mdm_cmd_erase_mass, "Erase entire flash memory"},
	{"ke04_mode", (cmd_handler)kinetis_mdm_cmd_ke04_mode, "Allow erase for KE04"},
	{NULL, NULL, NULL}
};

enum target_halt_reason mdm_halt_poll(target *t, target_addr *watch)
{
	(void)t; (void)watch;
	return TARGET_HALT_REQUEST;
}

void kinetis_mdm_probe(ADIv5_AP_t *ap)
{
	switch(ap->idr) {
	case KINETIS_MDM_IDR_KZ03: /* Also valid for KE04, no way to check! */
	case KINETIS_MDM_IDR_K22F:
		break;
	default:
		return;
	}

	target *t = target_new();
	if (!t) {
		return;
	}

	adiv5_ap_ref(ap);
	t->priv = ap;
	t->priv_free = (void*)adiv5_ap_unref;

	t->driver = "Kinetis Recovery (MDM-AP)";
	t->regs_size = 4;
	target_add_commands(t, kinetis_mdm_cmd_list, t->driver);
}

#define MDM_STATUS  ADIV5_AP_REG(0x00)
#define MDM_CONTROL ADIV5_AP_REG(0x04)

#define MDM_STATUS_MASS_ERASE_ACK (1 << 0)
#define MDM_STATUS_FLASH_READY (1 << 1)
#define MDM_STATUS_MASS_ERASE_ENABLED (1 << 5)

#define MDM_CONTROL_MASS_ERASE (1 << 0)
#define MDM_CONTROL_SYS_RESET  (1 << 3)

/* This is needed as a separate command, as there's no way to  *
 * tell a KE04 from other kinetis in kinetis_mdm_probe()       */
static bool kinetis_mdm_cmd_ke04_mode(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* Set a flag to ignore part of the status and assert reset */
	t->ke04_mode = true;
	tc_printf(t, "Mass erase for KE04 now allowed\n");
	return true;
}
static bool kinetis_mdm_cmd_erase_mass(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	ADIv5_AP_t *ap = t->priv;

	/* Keep the MCU in reset as stated in KL25PxxM48SF0RM */
	if(t->ke04_mode)
		adiv5_ap_write(ap, MDM_CONTROL, MDM_CONTROL_SYS_RESET);

	uint32_t status, control;
	status = adiv5_ap_read(ap, MDM_STATUS);
	control = adiv5_ap_read(ap, MDM_CONTROL);
	tc_printf(t, "Requesting mass erase (status = 0x%"PRIx32")\n", status);

	/* This flag does not exist on KE04 */
	if (!(status & MDM_STATUS_MASS_ERASE_ENABLED) && !t->ke04_mode) {
		tc_printf(t, "ERROR: Mass erase disabled!\n");
		return false;
	}

	/* Flag is not persistent */
	t->ke04_mode = false;

	if (!(status & MDM_STATUS_FLASH_READY)) {
		tc_printf(t, "ERROR: Flash not ready!\n");
		return false;
	}

	if (status & MDM_STATUS_MASS_ERASE_ACK) {
		tc_printf(t, "ERROR: Mass erase already in progress!\n");
		return false;
	}

	adiv5_ap_write(ap, MDM_CONTROL, MDM_CONTROL_MASS_ERASE);

	do {
		status = adiv5_ap_read(ap, MDM_STATUS);
	} while (!(status & MDM_STATUS_MASS_ERASE_ACK));
	tc_printf(t, "Mass erase acknowledged\n");

	do {
		control = adiv5_ap_read(ap, MDM_CONTROL);
	} while (!(control & MDM_CONTROL_MASS_ERASE));
	tc_printf(t, "Mass erase complete\n");

	return true;
}
