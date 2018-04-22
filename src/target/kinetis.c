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

#include "general.h"
#include "target.h"
#include "target_internal.h"

#define SIM_SDID   0x40048024

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
static bool unsafe_enabled;

const struct command_s kinetis_cmd_list[] = {
	{"unsafe", (cmd_handler)kinetis_cmd_unsafe, "Allow programming security byte (enable|disable)"},
	{NULL, NULL, NULL}
};

static bool kinetis_cmd_unsafe(target *t, int argc, char *argv[])
{
	if (argc == 1)
		tc_printf(t, "Allow programming security byte: %s\n",
			  unsafe_enabled ? "enabled" : "disabled");
	else
		unsafe_enabled = argv[1][0] == 'e';
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
	struct target_flash *f = &kf->f;
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
	switch (sdid >> 20) {
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
		switch((sdid>>16)&0x0f){
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
		switch((sdid>>16) & 0x0f){
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
	default:
		return false;
	}
	unsafe_enabled = false;
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
			len -= f->blocksize;
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
	if (!unsafe_enabled &&
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
			len -= kf->write_len;
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

	if (unsafe_enabled)
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
		uint32_t val = target_mem_read32(f->t, FLASH_SECURITY_BYTE_ADDRESS);
		val = (val & 0xffffff00) | FLASH_SECURITY_BYTE_UNSECURED;
		kl_gen_command(f->t, FTFA_CMD_PROGRAM_LONGWORD,
					   FLASH_SECURITY_BYTE_ADDRESS, (uint8_t*)&val);
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

static bool kinetis_mdm_cmd_erase_mass(target *t);

const struct command_s kinetis_mdm_cmd_list[] = {
	{"erase_mass", (cmd_handler)kinetis_mdm_cmd_erase_mass, "Erase entire flash memory"},
	{NULL, NULL, NULL}
};

bool nop_function(void)
{
	return true;
}

enum target_halt_reason mdm_halt_poll(target *t, target_addr *watch)
{
	(void)t; (void)watch;
	return TARGET_HALT_REQUEST;
}

void kinetis_mdm_probe(ADIv5_AP_t *ap)
{
	switch(ap->idr) {
	case KINETIS_MDM_IDR_KZ03:
	case KINETIS_MDM_IDR_K22F:
		break;
	default:
		return;
	}

	target *t = target_new();
	adiv5_ap_ref(ap);
	t->priv = ap;
	t->priv_free = (void*)adiv5_ap_unref;

	t->driver = "Kinetis Recovery (MDM-AP)";
	t->attach = (void*)nop_function;
	t->detach = (void*)nop_function;
	t->check_error = (void*)nop_function;
	t->mem_read = (void*)nop_function;
	t->mem_write = (void*)nop_function;
	t->regs_size = 4;
	t->regs_read = (void*)nop_function;
	t->regs_write = (void*)nop_function;
	t->reset = (void*)nop_function;
	t->halt_request = (void*)nop_function;
	t->halt_poll = mdm_halt_poll;
	t->halt_resume = (void*)nop_function;

	target_add_commands(t, kinetis_mdm_cmd_list, t->driver);
}

#define MDM_STATUS  ADIV5_AP_REG(0x00)
#define MDM_CONTROL ADIV5_AP_REG(0x04)

#define MDM_STATUS_MASS_ERASE_ACK (1 << 0)
#define MDM_STATUS_FLASH_READY (1 << 1)
#define MDM_STATUS_MASS_ERASE_ENABLED (1 << 5)

#define MDM_CONTROL_MASS_ERASE (1 << 0)

static bool kinetis_mdm_cmd_erase_mass(target *t)
{
	ADIv5_AP_t *ap = t->priv;

	uint32_t status, control;
	status = adiv5_ap_read(ap, MDM_STATUS);
	control = adiv5_ap_read(ap, MDM_CONTROL);
	tc_printf(t, "Requesting mass erase (status = 0x%"PRIx32")\n", status);

	if (!(status & MDM_STATUS_MASS_ERASE_ENABLED)) {
		tc_printf(t, "ERROR: Mass erase disabled!\n");
		return false;
	}

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
