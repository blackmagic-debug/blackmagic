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
#define FTFA_CMD_ERASE_SECTOR      0x09
#define FTFA_CMD_CHECK_ERASE_ALL   0x40
#define FTFA_CMD_READ_ONCE         0x41
#define FTFA_CMD_PROGRAM_ONCE      0x43
#define FTFA_CMD_ERASE_ALL         0x44
#define FTFA_CMD_BACKDOOR_ACCESS   0x45

#define KL_GEN_PAGESIZE 0x400

static int kl_gen_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static int kl_gen_flash_write(struct target_flash *f,
                              target_addr dest, const void *src, size_t len);

static void kl_gen_add_flash(target *t,
                           uint32_t addr, size_t length, size_t erasesize)
{
	struct target_flash *f = calloc(1, sizeof(*f));
	f->start = addr;
	f->length = length;
	f->blocksize = erasesize;
	f->erase = kl_gen_flash_erase;
	f->write = kl_gen_flash_write;
	f->align = 4;
	f->erased = 0xff;
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
		kl_gen_add_flash(t, 0x00000000, 0x20000, 0x400);
		return true;
	case 0x231:
		t->driver = "KL27";
		target_add_ram(t, 0x1fffe000, 0x2000);
		target_add_ram(t, 0x20000000, 0x6000);
		kl_gen_add_flash(t, 0x00000000, 0x40000, 0x400);
		return true;
	case 0x021: /* KL02 family */
		switch((sdid>>16) & 0x0f){
			case 3:
				t->driver = "KL02x32";
				target_add_ram(t, 0x1FFFFC00, 0x400);
				target_add_ram(t, 0x20000000, 0xc00);
				kl_gen_add_flash(t, 0x00000000, 0x7FFF, 0x400);
				break;
			case 2:
				t->driver = "KL02x16";
				target_add_ram(t, 0x1FFFFE00, 0x200);
				target_add_ram(t, 0x20000000, 0x600);
				kl_gen_add_flash(t, 0x00000000, 0x3FFF, 0x400);
				break;
			case 1:
				t->driver = "KL02x8";
				target_add_ram(t, 0x1FFFFF00, 0x100);
				target_add_ram(t, 0x20000000, 0x300);
				kl_gen_add_flash(t, 0x00000000, 0x1FFF, 0x400);
				break;
			default:
				return false;
			}
		return true;
	}
	return false;
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
			len -= KL_GEN_PAGESIZE;
			addr += KL_GEN_PAGESIZE;
		} else {
			return 1;
		}
	}
	return 0;
}

static int kl_gen_flash_write(struct target_flash *f,
                              target_addr dest, const void *src, size_t len)
{
	while (len) {
		if (kl_gen_command(f->t, FTFA_CMD_PROGRAM_LONGWORD, dest, src)) {
			len -= 4;
			dest += 4;
			src += 4;
		} else {
			return 1;
		}
	}
	return 0;
}
