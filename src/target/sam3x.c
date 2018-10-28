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

/* This file implements Atmel SAM3/4 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * Supported devices: SAM3N, SAM3S, SAM3U, SAM3X, and SAM4S
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"

static int sam4_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static int sam3_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static int sam3x_flash_write(struct target_flash *f, target_addr dest,
                             const void *src, size_t len);

static bool sam3x_cmd_gpnvm_get(target *t);
static bool sam3x_cmd_gpnvm_set(target *t, int argc, char *argv[]);

const struct command_s sam3x_cmd_list[] = {
	{"gpnvm_get", (cmd_handler)sam3x_cmd_gpnvm_get, "Get GPVNM value"},
	{"gpnvm_set", (cmd_handler)sam3x_cmd_gpnvm_set, "Set GPVNM bit"},
	{NULL, NULL, NULL}
};

/* Enhanced Embedded Flash Controller (EEFC) Register Map */
#define SAM3N_EEFC_BASE 	0x400E0A00
#define SAM3X_EEFC_BASE(x)	(0x400E0A00+((x)*0x400))
#define SAM3U_EEFC_BASE(x)	(0x400E0800+((x)*0x200))
#define SAM4S_EEFC_BASE(x)	(0x400E0A00+((x)*0x200))
#define EEFC_FMR(base)		((base)+0x00)
#define EEFC_FCR(base)		((base)+0x04)
#define EEFC_FSR(base)		((base)+0x08)
#define EEFC_FRR(base)		((base)+0x0C)

#define EEFC_FCR_FKEY		(0x5A << 24)
#define EEFC_FCR_FCMD_GETD	0x00
#define EEFC_FCR_FCMD_WP	0x01
#define EEFC_FCR_FCMD_WPL	0x02
#define EEFC_FCR_FCMD_EWP	0x03
#define EEFC_FCR_FCMD_EWPL	0x04
#define EEFC_FCR_FCMD_EA	0x05
#define EEFC_FCR_FCMD_EPA	0x07
#define EEFC_FCR_FCMD_SLB	0x08
#define EEFC_FCR_FCMD_CLB	0x09
#define EEFC_FCR_FCMD_GLB	0x0A
#define EEFC_FCR_FCMD_SGPB	0x0B
#define EEFC_FCR_FCMD_CGPB	0x0C
#define EEFC_FCR_FCMD_GGPB	0x0D
#define EEFC_FCR_FCMD_STUI	0x0E
#define EEFC_FCR_FCMD_SPUI	0x0F

#define EEFC_FSR_FRDY		(1 << 0)
#define EEFC_FSR_FCMDE		(1 << 1)
#define EEFC_FSR_FLOCKE		(1 << 2)
#define EEFC_FSR_ERROR		(EEFC_FSR_FCMDE | EEFC_FSR_FLOCKE)

#define SAM3X_CHIPID_CIDR	0x400E0940
#define SAM34NSU_CHIPID_CIDR	0x400E0740

#define CHIPID_CIDR_VERSION_MASK	(0x1F << 0)
#define CHIPID_CIDR_EPROC_CM3		(0x03 << 5)
#define CHIPID_CIDR_EPROC_CM4		(0x07 << 5)
#define CHIPID_CIDR_EPROC_MASK		(0x07 << 5)
#define CHIPID_CIDR_NVPSIZ_MASK		(0x0F << 8)
#define CHIPID_CIDR_NVPSIZ_8K		(0x01 << 8)
#define CHIPID_CIDR_NVPSIZ_16K		(0x02 << 8)
#define CHIPID_CIDR_NVPSIZ_32K		(0x03 << 8)
#define CHIPID_CIDR_NVPSIZ_64K		(0x05 << 8)
#define CHIPID_CIDR_NVPSIZ_128K		(0x07 << 8)
#define CHIPID_CIDR_NVPSIZ_256K		(0x09 << 8)
#define CHIPID_CIDR_NVPSIZ_512K		(0x0A << 8)
#define CHIPID_CIDR_NVPSIZ_1024K	(0x0C << 8)
#define CHIPID_CIDR_NVPSIZ_2048K	(0x0E << 8)
#define CHIPID_CIDR_NVPSIZ2_MASK	(0x0F << 12)
#define CHIPID_CIDR_SRAMSIZ_MASK	(0x0F << 16)
#define CHIPID_CIDR_ARCH_MASK		(0xFF << 20)
#define CHIPID_CIDR_ARCH_SAM3UxC	(0x80 << 20)
#define CHIPID_CIDR_ARCH_SAM3UxE	(0x81 << 20)
#define CHIPID_CIDR_ARCH_SAM3XxC	(0x84 << 20)
#define CHIPID_CIDR_ARCH_SAM3XxE	(0x85 << 20)
#define CHIPID_CIDR_ARCH_SAM3XxG	(0x86 << 20)
#define CHIPID_CIDR_ARCH_SAM3NxA	(0x93 << 20)
#define CHIPID_CIDR_ARCH_SAM3NxB	(0x94 << 20)
#define CHIPID_CIDR_ARCH_SAM3NxC	(0x95 << 20)
#define CHIPID_CIDR_ARCH_SAM3SxA	(0x88 << 20)
#define CHIPID_CIDR_ARCH_SAM3SxB	(0x89 << 20)
#define CHIPID_CIDR_ARCH_SAM3SxC	(0x8A << 20)
#define CHIPID_CIDR_ARCH_SAM4SxA	(0x88 << 20)
#define CHIPID_CIDR_ARCH_SAM4SxB	(0x89 << 20)
#define CHIPID_CIDR_ARCH_SAM4SxC	(0x8A << 20)
#define CHIPID_CIDR_NVPTYP_MASK		(0x07 << 28)
#define CHIPID_CIDR_NVPTYP_FLASH	(0x02 << 28)
#define CHIPID_CIDR_NVPTYP_ROM_FLASH	(0x03 << 28)
#define CHIPID_CIDR_EXT			(0x01 << 31)

#define SAM3_PAGE_SIZE 256
#define SAM4_PAGE_SIZE 512

struct sam_flash {
	struct target_flash f;
	uint32_t eefc_base;
	uint8_t write_cmd;
};

static void sam3_add_flash(target *t,
                           uint32_t eefc_base, uint32_t addr, size_t length)
{
	struct sam_flash *sf = calloc(1, sizeof(*sf));
	struct target_flash *f = &sf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = SAM3_PAGE_SIZE;
	f->erase = sam3_flash_erase;
	f->write = sam3x_flash_write;
	f->buf_size = SAM3_PAGE_SIZE;
	sf->eefc_base = eefc_base;
	sf->write_cmd = EEFC_FCR_FCMD_EWP;
	target_add_flash(t, f);
}

static void sam4_add_flash(target *t,
                           uint32_t eefc_base, uint32_t addr, size_t length)
{
	struct sam_flash *sf = calloc(1, sizeof(*sf));
	struct target_flash *f = &sf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = SAM4_PAGE_SIZE * 8;
	f->erase = sam4_flash_erase;
	f->write = sam3x_flash_write;
	f->buf_size = SAM4_PAGE_SIZE;
	sf->eefc_base = eefc_base;
	sf->write_cmd = EEFC_FCR_FCMD_WP;
	target_add_flash(t, f);
}

static size_t sam_flash_size(uint32_t idcode)
{
	switch (idcode & CHIPID_CIDR_NVPSIZ_MASK) {
	case CHIPID_CIDR_NVPSIZ_8K:
		return 0x2000;
	case CHIPID_CIDR_NVPSIZ_16K:
		return 0x4000;
	case CHIPID_CIDR_NVPSIZ_32K:
		return 0x8000;
	case CHIPID_CIDR_NVPSIZ_64K:
		return 0x10000;
	case CHIPID_CIDR_NVPSIZ_128K:
		return 0x20000;
	case CHIPID_CIDR_NVPSIZ_256K:
		return 0x40000;
	case CHIPID_CIDR_NVPSIZ_512K:
		return 0x80000;
	case CHIPID_CIDR_NVPSIZ_1024K:
		return 0x100000;
	case CHIPID_CIDR_NVPSIZ_2048K:
		return 0x200000;
	}
	return 0;
}

bool sam3x_probe(target *t)
{
	t->idcode = target_mem_read32(t, SAM3X_CHIPID_CIDR);
	size_t size = sam_flash_size(t->idcode);
	switch (t->idcode & (CHIPID_CIDR_ARCH_MASK | CHIPID_CIDR_EPROC_MASK)) {
	case CHIPID_CIDR_ARCH_SAM3XxC | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3XxE | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3XxG | CHIPID_CIDR_EPROC_CM3:
		t->driver = "Atmel SAM3X";
		target_add_ram(t, 0x20000000, 0x200000);
		/* 2 Flash memories back-to-back starting at 0x80000 */
		sam3_add_flash(t, SAM3X_EEFC_BASE(0), 0x80000, size/2);
		sam3_add_flash(t, SAM3X_EEFC_BASE(1), 0x80000 + size/2, size/2);
		target_add_commands(t, sam3x_cmd_list, "SAM3X");
		return true;
	}

	t->idcode = target_mem_read32(t, SAM34NSU_CHIPID_CIDR);
	size = sam_flash_size(t->idcode);
	switch (t->idcode & (CHIPID_CIDR_ARCH_MASK | CHIPID_CIDR_EPROC_MASK)) {
	case CHIPID_CIDR_ARCH_SAM3NxA | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3NxB | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3NxC | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3SxA | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3SxB | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3SxC | CHIPID_CIDR_EPROC_CM3:
		t->driver = "Atmel SAM3N/S";
		target_add_ram(t, 0x20000000, 0x200000);
		/* These devices only have a single bank */
		size = sam_flash_size(t->idcode);
		sam3_add_flash(t, SAM3N_EEFC_BASE, 0x400000, size);
		target_add_commands(t, sam3x_cmd_list, "SAM3N/S");
		return true;
	case CHIPID_CIDR_ARCH_SAM3UxC | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3UxE | CHIPID_CIDR_EPROC_CM3:
		t->driver = "Atmel SAM3U";
		target_add_ram(t, 0x20000000, 0x200000);
		/* One flash up to 512K at 0x80000 */
		sam3_add_flash(t, SAM3U_EEFC_BASE(0), 0x80000, MIN(size, 0x80000));
		if (size >= 0x80000) {
			/* Larger devices have a second bank at 0x100000 */
			sam3_add_flash(t, SAM3U_EEFC_BASE(1),
			               0x100000, 0x80000);
		}
		target_add_commands(t, sam3x_cmd_list, "SAM3U");
		return true;
	case CHIPID_CIDR_ARCH_SAM4SxA | CHIPID_CIDR_EPROC_CM4:
	case CHIPID_CIDR_ARCH_SAM4SxB | CHIPID_CIDR_EPROC_CM4:
	case CHIPID_CIDR_ARCH_SAM4SxC | CHIPID_CIDR_EPROC_CM4:
		t->driver = "Atmel SAM4S";
		target_add_ram(t, 0x20000000, 0x400000);
		size_t size = sam_flash_size(t->idcode);
		if (size <= 0x80000) {
			/* Smaller devices have a single bank */
			sam4_add_flash(t, SAM4S_EEFC_BASE(0), 0x400000, size);
		} else {
			/* Larger devices are split evenly between 2 */
			sam4_add_flash(t, SAM4S_EEFC_BASE(0), 0x400000, size/2);
			sam4_add_flash(t, SAM4S_EEFC_BASE(1),
			               0x400000 + size/2, size/2);
		}
		target_add_commands(t, sam3x_cmd_list, "SAM4S");
		return true;
	}

	return false;
}

static int
sam3x_flash_cmd(target *t, uint32_t base, uint8_t cmd, uint16_t arg)
{
	DEBUG("%s: base = 0x%08"PRIx32" cmd = 0x%02X, arg = 0x%06X\n",
		__func__, base, cmd, arg);
	target_mem_write32(t, EEFC_FCR(base),
	                   EEFC_FCR_FKEY | cmd | ((uint32_t)arg << 8));

	while (!(target_mem_read32(t, EEFC_FSR(base)) & EEFC_FSR_FRDY))
		if(target_check_error(t))
			return -1;

	uint32_t sr = target_mem_read32(t, EEFC_FSR(base));
	return sr & EEFC_FSR_ERROR;
}

static uint32_t sam3x_flash_base(target *t)
{
	if (strcmp(t->driver, "Atmel SAM3X") == 0) {
		return SAM3X_EEFC_BASE(0);
	}
	if (strcmp(t->driver, "Atmel SAM3U") == 0) {
		return SAM3U_EEFC_BASE(0);
	}
	if (strcmp(t->driver, "Atmel SAM4S") == 0) {
		return SAM4S_EEFC_BASE(0);
	}
	return SAM3N_EEFC_BASE;
}

static int sam4_flash_erase(struct target_flash *f, target_addr addr, size_t len)
{
	target *t = f->t;
	uint32_t base = ((struct sam_flash *)f)->eefc_base;
	uint32_t offset = addr - f->start;

	/* The SAM4S is the only supported device with a page erase command.
	 * Erasing is done in 8-page chunks. arg[15:2] contains the page
	 * number and arg[1:0] contains 0x1, indicating 8-page chunks.
	 */
	unsigned chunk = offset / SAM4_PAGE_SIZE;

	while (len) {
		int16_t arg = chunk | 0x1;
		if(sam3x_flash_cmd(t, base, EEFC_FCR_FCMD_EPA, arg))
			return -1;

		len -= f->blocksize;
		chunk += 8;
	}
	return 0;
}

static int sam3_flash_erase(struct target_flash *f, target_addr addr, size_t len)
{
	/* The SAM3X/SAM3N don't really have a page erase function.
	 * We do nothing here and use Erase/Write page in flash_write.
	 */
	(void)f; (void)addr; (void)len;
	return 0;
}

static int sam3x_flash_write(struct target_flash *f, target_addr dest,
                             const void *src, size_t len)
{
	target *t = f->t;
	struct sam_flash *sf = (struct sam_flash *)f;
	uint32_t base = sf->eefc_base;
	unsigned chunk = (dest - f->start) / f->buf_size;

	target_mem_write(t, dest, src, len);
	if(sam3x_flash_cmd(t, base, sf->write_cmd, chunk))
		return -1;

	return 0;
}

static bool sam3x_cmd_gpnvm_get(target *t)
{
	uint32_t base = sam3x_flash_base(t);

	sam3x_flash_cmd(t, base, EEFC_FCR_FCMD_GGPB, 0);
	tc_printf(t, "GPNVM: 0x%08X\n", target_mem_read32(t, EEFC_FRR(base)));

	return true;
}

static bool sam3x_cmd_gpnvm_set(target *t, int argc, char *argv[])
{
	uint32_t bit, cmd;
	uint32_t base = sam3x_flash_base(t);

	if (argc != 3) {
		tc_printf(t, "usage: monitor gpnvm_set <bit> <val>\n");
		return false;
	}
	bit = atol(argv[1]);
	cmd = atol(argv[2]) ? EEFC_FCR_FCMD_SGPB : EEFC_FCR_FCMD_CGPB;

	sam3x_flash_cmd(t, base, cmd, bit);
	sam3x_cmd_gpnvm_get(t);

	return true;
}
