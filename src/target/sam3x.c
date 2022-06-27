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
 * Supported devices: SAM3N, SAM3S, SAM3U, SAM3X, SAM4S, SAME70, SAMS70, SAMV71, SAMV70
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"

static int sam_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static int sam3_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static int sam_flash_write(struct target_flash *f, target_addr dest,
                             const void *src, size_t len);

static int sam_gpnvm_get(target *t, uint32_t base, uint32_t *gpnvm);

static bool sam_cmd_gpnvm(target *t, int argc, const char **argv);

const struct command_s sam_cmd_list[] = {
	{"gpnvm", (cmd_handler)sam_cmd_gpnvm, "Set/Get GPVNM bits"},
	{NULL, NULL, NULL}
};

/* Enhanced Embedded Flash Controller (EEFC) Register Map */
#define SAMX7X_EEFC_BASE 	0x400E0C00
#define SAM3N_EEFC_BASE 	0x400E0A00
#define SAM3X_EEFC_BASE(x)	(0x400E0A00+((x)*0x200))
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

#define SAM_SMALL_PAGE_SIZE 256
#define SAM_LARGE_PAGE_SIZE 512

/* CHIPID Register Map */
#define SAM_CHIPID_CIDR	0x400E0940
#define SAM34NSU_CHIPID_CIDR	0x400E0740

#define SAM_CHIPID_EXID	(SAM_CHIPID_CIDR + 0x4)

#define CHIPID_CIDR_VERSION_MASK	(0x1F << 0)

#define CHIPID_CIDR_EPROC_OFFSET	(5)
#define CHIPID_CIDR_EPROC_MASK		(0x7 << CHIPID_CIDR_EPROC_OFFSET)
#define CHIPID_CIDR_EPROC_CM7		(0x0 << CHIPID_CIDR_EPROC_OFFSET)
#define CHIPID_CIDR_EPROC_CM3		(0x3 << CHIPID_CIDR_EPROC_OFFSET)
#define CHIPID_CIDR_EPROC_CM4		(0x7 << CHIPID_CIDR_EPROC_OFFSET)

#define CHIPID_CIDR_NVPSIZ_OFFSET	(8)
#define CHIPID_CIDR_NVPSIZ_MASK		(0xF << CHIPID_CIDR_NVPSIZ_OFFSET)
#define CHIPID_CIDR_NVPSIZ_8K		(0x1 << CHIPID_CIDR_NVPSIZ_OFFSET)
#define CHIPID_CIDR_NVPSIZ_16K		(0x2 << CHIPID_CIDR_NVPSIZ_OFFSET)
#define CHIPID_CIDR_NVPSIZ_32K		(0x3 << CHIPID_CIDR_NVPSIZ_OFFSET)
#define CHIPID_CIDR_NVPSIZ_64K		(0x5 << CHIPID_CIDR_NVPSIZ_OFFSET)
#define CHIPID_CIDR_NVPSIZ_128K		(0x7 << CHIPID_CIDR_NVPSIZ_OFFSET)
#define CHIPID_CIDR_NVPSIZ_256K		(0x9 << CHIPID_CIDR_NVPSIZ_OFFSET)
#define CHIPID_CIDR_NVPSIZ_512K		(0xA << CHIPID_CIDR_NVPSIZ_OFFSET)
#define CHIPID_CIDR_NVPSIZ_1024K	(0xC << CHIPID_CIDR_NVPSIZ_OFFSET)
#define CHIPID_CIDR_NVPSIZ_2048K	(0xE << CHIPID_CIDR_NVPSIZ_OFFSET)

#define CHIPID_CIDR_NVPSIZ2_OFFSET	(12)
#define CHIPID_CIDR_NVPSIZ2_MASK	(0xF << CHIPID_CIDR_NVPSIZ2_OFFSET)

#define CHIPID_CIDR_SRAMSIZ_OFFSET	(16)
#define CHIPID_CIDR_SRAMSIZ_MASK	(0xF << CHIPID_CIDR_SRAMSIZ_OFFSET)
#define CHIPID_CIDR_SRAMSIZ_384K	(0x2 << CHIPID_CIDR_SRAMSIZ_OFFSET)
#define CHIPID_CIDR_SRAMSIZ_256K	(0xD << CHIPID_CIDR_SRAMSIZ_OFFSET)

#define CHIPID_CIDR_ARCH_OFFSET		(20)
#define CHIPID_CIDR_ARCH_MASK		(0xFF << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAME70		(0x10 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAMS70		(0x11 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAMV71		(0x12 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAMV70		(0x13 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM3UxC	(0x80 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM3UxE	(0x81 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM3XxC	(0x84 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM3XxE	(0x85 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM3XxG	(0x86 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM3NxA	(0x93 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM3NxB	(0x94 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM3NxC	(0x95 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM3SxA	(0x88 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM3SxB	(0x89 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM3SxC	(0x8A << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM4SxA	(0x88 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM4SxB	(0x89 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM4SxC	(0x8A << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM4SDB	(0x99 << CHIPID_CIDR_ARCH_OFFSET)
#define CHIPID_CIDR_ARCH_SAM4SDC	(0x9A << CHIPID_CIDR_ARCH_OFFSET)

#define CHIPID_CIDR_NVPTYP_OFFSET	(28)
#define CHIPID_CIDR_NVPTYP_MASK		(0x7 << CHIPID_CIDR_NVPTYP_OFFSET)
#define CHIPID_CIDR_NVPTYP_FLASH	(0x2 << CHIPID_CIDR_NVPTYP_OFFSET)
#define CHIPID_CIDR_NVPTYP_ROM_FLASH	(0x3 << CHIPID_CIDR_NVPTYP_OFFSET)

#define CHIPID_CIDR_EXT			(0x01 << 31)

#define CHIPID_EXID_SAMX7X_PINS_OFFSET	(0)
#define CHIPID_EXID_SAMX7X_PINS_MASK	(0x3 << CHIPID_EXID_SAMX7X_PINS_OFFSET)
#define CHIPID_EXID_SAMX7X_PINS_Q		(0x2 << CHIPID_EXID_SAMX7X_PINS_OFFSET)
#define CHIPID_EXID_SAMX7X_PINS_N		(0x1 << CHIPID_EXID_SAMX7X_PINS_OFFSET)
#define CHIPID_EXID_SAMX7X_PINS_J		(0x0 << CHIPID_EXID_SAMX7X_PINS_OFFSET)

/* GPNVM */
#define GPNVM_SAMX7X_SECURITY_BIT_OFFSET	(0)
#define GPNVM_SAMX7X_SECURITY_BIT_MASK		(0x1 << GPNVM_SAMX7X_SECURITY_BIT_OFFSET)

#define GPNVM_SAMX7X_BOOT_BIT_OFFSET		(1)
#define GPNVM_SAMX7X_BOOT_BIT_MASK			(0x1 << GPNVM_SAMX7X_BOOT_BIT_OFFSET)
#define GPNVM_SAMX7X_BOOT_ROM				(0x0 << GPNVM_SAMX7X_BOOT_BIT_OFFSET)
#define GPNVM_SAMX7X_BOOT_FLASH				(0x1 << GPNVM_SAMX7X_BOOT_BIT_OFFSET)

#define GPNVM_SAMX7X_TCM_BIT_OFFSET			(7)
#define GPNVM_SAMX7X_TCM_BIT_MASK			(0x3 << GPNVM_SAMX7X_TCM_BIT_OFFSET)
#define GPNVM_SAMX7X_TCM_0K					(0x0 << GPNVM_SAMX7X_TCM_BIT_OFFSET)
#define GPNVM_SAMX7X_TCM_32K				(0x1 << GPNVM_SAMX7X_TCM_BIT_OFFSET)
#define GPNVM_SAMX7X_TCM_64K				(0x2 << GPNVM_SAMX7X_TCM_BIT_OFFSET)
#define GPNVM_SAMX7X_TCM_128K				(0x3 << GPNVM_SAMX7X_TCM_BIT_OFFSET)

enum sam_driver {
	DRIVER_SAM3X,
	DRIVER_SAM3U,
	DRIVER_SAM4S,
	DRIVER_SAM3NS,
	DRIVER_SAMX7X,
};

struct sam_flash {
	struct target_flash f;
	uint32_t eefc_base;
	uint8_t write_cmd;
};

struct samx7x_descr {
	char product_code;
	uint8_t product_id;
	char pins;
	uint32_t ram_size;
	uint32_t flash_size;
	uint8_t density;
	char revision;
};
struct sam_priv_s {
	struct samx7x_descr descr;
	char sam_variant_string[16];
};

static void sam3_add_flash(target *t,
                           uint32_t eefc_base, uint32_t addr, size_t length)
{
	struct sam_flash *sf = calloc(1, sizeof(*sf));
	struct target_flash *f;

	if (!sf) {			/* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	f = &sf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = SAM_SMALL_PAGE_SIZE;
	f->erase = sam3_flash_erase;
	f->write = sam_flash_write;
	f->buf_size = SAM_SMALL_PAGE_SIZE;
	sf->eefc_base = eefc_base;
	sf->write_cmd = EEFC_FCR_FCMD_EWP;
	target_add_flash(t, f);
}

static void sam_add_flash(target *t,
                           uint32_t eefc_base, uint32_t addr, size_t length)
{
	struct sam_flash *sf = calloc(1, sizeof(*sf));
	struct target_flash *f;

	if (!sf) {			/* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	f = &sf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = SAM_LARGE_PAGE_SIZE * 8;
	f->erase = sam_flash_erase;
	f->write = sam_flash_write;
	f->buf_size = SAM_LARGE_PAGE_SIZE;
	sf->eefc_base = eefc_base;
	sf->write_cmd = EEFC_FCR_FCMD_WP;
	target_add_flash(t, f);
}

static void samx7x_add_ram(target* t, uint32_t tcm_config, uint32_t ram_size) {
	uint32_t itcm_size = 0, dtcm_size = 0;

	switch (tcm_config)
	{
	case GPNVM_SAMX7X_TCM_32K:
		itcm_size = dtcm_size = 0x8000;
		break;
	case GPNVM_SAMX7X_TCM_64K:
		itcm_size = dtcm_size = 0x10000;
		break;
	case GPNVM_SAMX7X_TCM_128K:
		itcm_size = dtcm_size = 0x20000;
		break;
	}

	if (dtcm_size > 0) {
		target_add_ram(t, 0x20000000, dtcm_size);
	}
	if (itcm_size > 0) {
		target_add_ram(t, 0x00000000, itcm_size);
	}

	uint32_t sram_size = ram_size - (itcm_size + dtcm_size);
	if (sram_size > 0) {
		target_add_ram(t, 0x20400000, sram_size);
	}
}

static size_t sam_flash_size(uint32_t cidr)
{
	switch (cidr & CHIPID_CIDR_NVPSIZ_MASK) {
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

static size_t sam_sram_size(uint32_t cidr)
{
	switch (cidr & CHIPID_CIDR_SRAMSIZ_MASK) {
	case CHIPID_CIDR_SRAMSIZ_256K:
		return 0x40000;
	case CHIPID_CIDR_SRAMSIZ_384K:
		return 0x60000;
	default:
		return 0;
	}
}

struct samx7x_descr samx7x_parse_id(uint32_t cidr, uint32_t exid) {

	struct samx7x_descr descr = {0};

	switch (cidr & CHIPID_CIDR_ARCH_MASK) {
	case CHIPID_CIDR_ARCH_SAME70:
		descr.product_code = 'E';
		descr.product_id = 70;
		break;
	case CHIPID_CIDR_ARCH_SAMS70:
		descr.product_code = 'S';
		descr.product_id = 70;
		break;
	case CHIPID_CIDR_ARCH_SAMV71:
		descr.product_code = 'V';
		descr.product_id = 71;
		break;
	case CHIPID_CIDR_ARCH_SAMV70:
		descr.product_code = 'V';
		descr.product_id = 70;
		break;
	}

	// A = Revision A, legacy version
	// B = Revision B, current variant
	switch (exid & CHIPID_CIDR_VERSION_MASK) {
	case 0:
		descr.revision = 'A';
		break;
	case 1:
		descr.revision = 'B';
		break;
	default:
		descr.revision = '_';
		break;
	}

	// Q = 144 pins
	// N = 100 pins
	// J = 64 pins
	switch (exid & CHIPID_EXID_SAMX7X_PINS_MASK) {
	case CHIPID_EXID_SAMX7X_PINS_Q:
		descr.pins = 'Q';
		break;
	case CHIPID_EXID_SAMX7X_PINS_N:
		descr.pins = 'N';
		break;
	case CHIPID_EXID_SAMX7X_PINS_J:
		descr.pins = 'J';
		break;
	}

	descr.ram_size = sam_sram_size(cidr);
	descr.flash_size = sam_flash_size(cidr);

	// 21 = 2048 KB
	// 20 = 1024 KB
	// 19 = 512 KB
	switch (descr.flash_size)
	{
	case 0x200000:
		descr.density = 21;
		break;
	case 0x100000:
		descr.density = 20;
		break;
	case 0x80000:
		descr.density = 19;
		break;
	default:
		descr.density = 0;
		break;
	}

	return descr;
}

bool samx7x_probe(target *t)
{
	uint32_t cidr = target_mem_read32(t, SAM_CHIPID_CIDR);
	uint32_t exid = 0;
	if (cidr & CHIPID_CIDR_EXT) {
		exid = target_mem_read32(t, SAM_CHIPID_EXID);
	}

	switch (cidr & CHIPID_CIDR_ARCH_MASK) {
	case CHIPID_CIDR_ARCH_SAME70:
	case CHIPID_CIDR_ARCH_SAMS70:
	case CHIPID_CIDR_ARCH_SAMV71:
	case CHIPID_CIDR_ARCH_SAMV70:
		break;
	default:
		return false;
	}

	struct sam_priv_s *priv_storage = calloc(1, sizeof(*priv_storage));
	if (!priv_storage) { /* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return false;
	}
	t->target_storage = (void*)priv_storage;

	priv_storage->descr = samx7x_parse_id(cidr, exid);

	uint32_t tcm_config = 0;
	sam_gpnvm_get(t, SAMX7X_EEFC_BASE, &tcm_config);
	tcm_config &= GPNVM_SAMX7X_TCM_BIT_MASK;

	samx7x_add_ram(t, tcm_config, priv_storage->descr.ram_size);

	sam_add_flash(t, SAMX7X_EEFC_BASE, 0x00400000, priv_storage->descr.flash_size);

	target_add_commands(t, sam_cmd_list, "SAMX7X");

	sprintf(priv_storage->sam_variant_string,
			"SAM%c%02d%c%d%c",
			priv_storage->descr.product_code,
			priv_storage->descr.product_id,
			priv_storage->descr.pins,
			priv_storage->descr.density,
			priv_storage->descr.revision);

	t->driver = priv_storage->sam_variant_string;

	return true;
}

bool sam3x_probe(target *t)
{
	uint32_t cidr = target_mem_read32(t, SAM_CHIPID_CIDR);
	size_t size = sam_flash_size(cidr);
	switch (cidr & (CHIPID_CIDR_ARCH_MASK | CHIPID_CIDR_EPROC_MASK)) {
	case CHIPID_CIDR_ARCH_SAM3XxC | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3XxE | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3XxG | CHIPID_CIDR_EPROC_CM3:
		t->driver = "Atmel SAM3X";
		target_add_ram(t, 0x20000000, 0x200000);
		/* 2 Flash memories back-to-back starting at 0x80000 */
		sam3_add_flash(t, SAM3X_EEFC_BASE(0), 0x80000, size/2);
		sam3_add_flash(t, SAM3X_EEFC_BASE(1), 0x80000 + size/2, size/2);
		target_add_commands(t, sam_cmd_list, "SAM3X");
		return true;
	}

	cidr = target_mem_read32(t, SAM34NSU_CHIPID_CIDR);
	size = sam_flash_size(cidr);
	switch (cidr & (CHIPID_CIDR_ARCH_MASK | CHIPID_CIDR_EPROC_MASK)) {
	case CHIPID_CIDR_ARCH_SAM3NxA | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3NxB | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3NxC | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3SxA | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3SxB | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3SxC | CHIPID_CIDR_EPROC_CM3:
		t->driver = "Atmel SAM3N/S";
		target_add_ram(t, 0x20000000, 0x200000);
		/* These devices only have a single bank */
		size = sam_flash_size(cidr);
		sam3_add_flash(t, SAM3N_EEFC_BASE, 0x400000, size);
		target_add_commands(t, sam_cmd_list, "SAM3N/S");
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
		target_add_commands(t, sam_cmd_list, "SAM3U");
		return true;
	case CHIPID_CIDR_ARCH_SAM4SxA | CHIPID_CIDR_EPROC_CM4:
	case CHIPID_CIDR_ARCH_SAM4SxB | CHIPID_CIDR_EPROC_CM4:
	case CHIPID_CIDR_ARCH_SAM4SxC | CHIPID_CIDR_EPROC_CM4:
	case CHIPID_CIDR_ARCH_SAM4SDB | CHIPID_CIDR_EPROC_CM4:
	case CHIPID_CIDR_ARCH_SAM4SDC | CHIPID_CIDR_EPROC_CM4:
		t->driver = "Atmel SAM4S";
		target_add_ram(t, 0x20000000, 0x400000);
		size = sam_flash_size(cidr);
		if (size <= 0x80000) {
			/* Smaller devices have a single bank */
			sam_add_flash(t, SAM4S_EEFC_BASE(0), 0x400000, size);
		} else {
			/* Larger devices are split evenly between 2 */
			sam_add_flash(t, SAM4S_EEFC_BASE(0), 0x400000, size/2);
			sam_add_flash(t, SAM4S_EEFC_BASE(1),
			               0x400000 + size/2, size/2);
		}
		target_add_commands(t, sam_cmd_list, "SAM4S");
		return true;
	}

	return false;
}

static int
sam_flash_cmd(target *t, uint32_t base, uint8_t cmd, uint16_t arg)
{
	DEBUG_INFO("%s: base = 0x%08"PRIx32" cmd = 0x%02X, arg = 0x%06X\n",
		__func__, base, cmd, arg);

	if(base == 0)
		return -1;

	target_mem_write32(t, EEFC_FCR(base),
	                   EEFC_FCR_FKEY | cmd | ((uint32_t)arg << 8));

	while (!(target_mem_read32(t, EEFC_FSR(base)) & EEFC_FSR_FRDY))
		if(target_check_error(t))
			return -1;

	uint32_t sr = target_mem_read32(t, EEFC_FSR(base));
	return sr & EEFC_FSR_ERROR;
}

static enum sam_driver sam_driver(target *t)
{
	if (strcmp(t->driver, "Atmel SAM3X") == 0) {
		return DRIVER_SAM3X;
	}
	if (strcmp(t->driver, "Atmel SAM3U") == 0) {
		return DRIVER_SAM3U;
	}
	if (strcmp(t->driver, "Atmel SAM4S") == 0) {
		return DRIVER_SAM4S;
	}
	if (strcmp(t->driver, "Atmel SAM3N/S") == 0) {
		return DRIVER_SAM3NS;
	}
	return DRIVER_SAMX7X;
}

static int sam_flash_erase(struct target_flash *f, target_addr addr, size_t len)
{
	target *t = f->t;
	uint32_t base = ((struct sam_flash *)f)->eefc_base;
	uint32_t offset = addr - f->start;

	/* The SAM4S is the only supported device with a page erase command.
	 * Erasing is done in 8-page chunks. arg[15:2] contains the page
	 * number and arg[1:0] contains 0x1, indicating 8-page chunks.
	 */
	unsigned chunk = offset / SAM_LARGE_PAGE_SIZE;

	while (len) {
		int16_t arg = chunk | 0x1;
		if(sam_flash_cmd(t, base, EEFC_FCR_FCMD_EPA, arg))
			return -1;

		if (len > f->blocksize)
			len -= f->blocksize;
		else
			len = 0;
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

static int sam_flash_write(struct target_flash *f, target_addr dest,
                             const void *src, size_t len)
{
	target *t = f->t;
	struct sam_flash *sf = (struct sam_flash *)f;
	uint32_t base = sf->eefc_base;
	unsigned chunk = (dest - f->start) / f->buf_size;

	target_mem_write(t, dest, src, len);
	if(sam_flash_cmd(t, base, sf->write_cmd, chunk))
		return -1;

	return 0;
}

static int sam_gpnvm_get(target *t, uint32_t base, uint32_t *gpnvm)
{
	if(!gpnvm || sam_flash_cmd(t, base, EEFC_FCR_FCMD_GGPB, 0))
		return -1;

	*gpnvm = target_mem_read32(t, EEFC_FRR(base));

	return 0;
}

static bool sam_cmd_gpnvm(target *t, int argc, const char **argv)
{
	if (argc != 2 && argc != 4) {
		goto bad_usage;
	}

	uint8_t arglen = strlen(argv[1]);
	if (arglen == 0) {
		goto bad_usage;
	}

	uint32_t base = 0, gpnvm_mask = 0;
	enum sam_driver drv = sam_driver(t);
	switch(drv) {
	case DRIVER_SAM3X:
		gpnvm_mask = 0x7;
		base = SAM3X_EEFC_BASE(0);
		break;
	case DRIVER_SAM3U:
		gpnvm_mask = 0x7;
		base = SAM3U_EEFC_BASE(0);
		break;
	case DRIVER_SAM4S:
		gpnvm_mask = 0x7;
		base = SAM4S_EEFC_BASE(0);
		break;
	case DRIVER_SAM3NS:
		gpnvm_mask = 0x3;
		base = SAM3N_EEFC_BASE;
		break;
	case DRIVER_SAMX7X:
		gpnvm_mask = 0x1BF;
		base = SAMX7X_EEFC_BASE;
		break;
	default:
		/* unknown / invalid driver*/
		return false;
	}

	uint32_t mask = 0, values = 0;
	if (strncmp(argv[1], "get", arglen) == 0) {
		/* nothing to do */
	} else if (strncmp(argv[1], "set", arglen) == 0) {
		char *eos;
		mask = strtoul(argv[2], &eos, 0);
		values = strtoul(argv[3], &eos, 0);

		if (mask == 0 || mask & ~gpnvm_mask) {
			/* trying to write invalid bits */
			goto bad_usage;
		}

		uint32_t work_mask = mask;
		uint32_t work_values = values;
		for (uint16_t bit = 0; work_mask > 0; bit++)
		{
			if (work_mask & 1) {
				uint8_t cmd = (work_values & 1) ? EEFC_FCR_FCMD_SGPB : EEFC_FCR_FCMD_CGPB;
				if(sam_flash_cmd(t, base, cmd, bit))
					return false;
			}
			work_mask >>= 1;
			work_values >>= 1;
		}
	} else {
		goto bad_usage;
	}

	uint32_t gpnvm = 0;
	if (sam_gpnvm_get(t, base, &gpnvm))
		return false;
	tc_printf(t, "GPNVM: 0x%08X\n", gpnvm);

	if ((drv == DRIVER_SAMX7X) && (mask & GPNVM_SAMX7X_TCM_BIT_MASK)) {
		struct sam_priv_s * storage = (struct sam_priv_s *)t->target_storage;

		target_ram_map_free(t);
		samx7x_add_ram(t, gpnvm & GPNVM_SAMX7X_TCM_BIT_MASK, storage->descr.ram_size);
		target_reset(t);
	}

	return true;

bad_usage:
	tc_printf(t, "usage: monitor gpnvm get\n");
	tc_printf(t, "usage: monitor gpnvm set <mask> <val>\n");
	return false;
}
