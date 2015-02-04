/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
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

/* This file implements Atmel SAM3X target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 */

#include "general.h"
#include "adiv5.h"
#include "target.h"
#include "command.h"
#include "gdb_packet.h"

static int sam3x_flash_erase(struct target_s *target, uint32_t addr, int len);
static int sam3x_flash_write(struct target_s *target, uint32_t dest,
			const uint8_t *src, int len);

static bool sam3x_cmd_gpnvm_get(target *t);
static bool sam3x_cmd_gpnvm_set(target *t, int argc, char *argv[]);

const struct command_s sam3x_cmd_list[] = {
	{"gpnvm_get", (cmd_handler)sam3x_cmd_gpnvm_get, "Get GPVNM value"},
	{"gpnvm_set", (cmd_handler)sam3x_cmd_gpnvm_set, "Set GPVNM bit"},
	{NULL, NULL, NULL}
};

static const char sam3x_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x80000\" length=\"0x80000\">"
	"    <property name=\"blocksize\">0x100</property>"
	"  </memory>"
	"  <memory type=\"rom\" start=\"0x100000\" length=\"0x200000\"/>"
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x200000\"/>"
	"</memory-map>";

static const char sam3n_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x400000\" length=\"0x400000\">"
	"    <property name=\"blocksize\">0x100</property>"
	"  </memory>"
	"  <memory type=\"rom\" start=\"0x800000\" length=\"0x400000\"/>"
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x200000\"/>"
	"</memory-map>";

static const char sam4s_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x400000\" length=\"0x400000\">"
	"    <property name=\"blocksize\">0x200</property>"
	"  </memory>"
	"  <memory type=\"rom\" start=\"0x800000\" length=\"0x400000\"/>"
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x400000\"/>"
	"</memory-map>";

/* Enhanced Embedded Flash Controller (EEFC) Register Map */
#define SAM3N_EEFC_BASE 	0x400E0A00
#define SAM3X_EEFC_BASE(x)	(0x400E0A00+((x)*0x400))
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
#define SAM3N_CHIPID_CIDR	0x400E0740
#define SAM3S_CHIPID_CIDR	0x400E0740
#define SAM4S_CHIPID_CIDR	0x400E0740

#define CHIPID_CIDR_VERSION_MASK	(0x1F << 0)
#define CHIPID_CIDR_EPROC_CM3		(0x03 << 5)
#define CHIPID_CIDR_EPROC_CM4		(0x07 << 5)
#define CHIPID_CIDR_EPROC_MASK		(0x07 << 5)
#define CHIPID_CIDR_NVPSIZ_MASK		(0x0F << 8)
#define CHIPID_CIDR_NVPSIZ_128K		(0x07 << 8)
#define CHIPID_CIDR_NVPSIZ_256K		(0x09 << 8)
#define CHIPID_CIDR_NVPSIZ_512K		(0x0A << 8)
#define CHIPID_CIDR_NVPSIZ_1024K	(0x0C << 8)
#define CHIPID_CIDR_NVPSIZ_2048K	(0x0E << 8)
#define CHIPID_CIDR_NVPSIZ2_MASK	(0x0F << 12)
#define CHIPID_CIDR_SRAMSIZ_MASK	(0x0F << 16)
#define CHIPID_CIDR_ARCH_MASK		(0xFF << 20)
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

bool sam3x_probe(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	target->idcode = adiv5_ap_mem_read(ap, SAM3X_CHIPID_CIDR);

	/* FIXME: Check for all variants with similar flash interface */
	switch (target->idcode & (CHIPID_CIDR_ARCH_MASK | CHIPID_CIDR_EPROC_MASK)) {
	case CHIPID_CIDR_ARCH_SAM3XxC | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3XxE | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3XxG | CHIPID_CIDR_EPROC_CM3:
		target->driver = "Atmel SAM3X";
		target->xml_mem_map = sam3x_xml_memory_map;
		target->flash_erase = sam3x_flash_erase;
		target->flash_write = sam3x_flash_write;
		target_add_commands(target, sam3x_cmd_list, "SAM3X");
		return true;
	}

	target->idcode = adiv5_ap_mem_read(ap, SAM3N_CHIPID_CIDR);
	switch (target->idcode & (CHIPID_CIDR_ARCH_MASK | CHIPID_CIDR_EPROC_MASK)) {
	case CHIPID_CIDR_ARCH_SAM3NxA | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3NxB | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3NxC | CHIPID_CIDR_EPROC_CM3:
		target->driver = "Atmel SAM3N";
		target->xml_mem_map = sam3n_xml_memory_map;
		target->flash_erase = sam3x_flash_erase;
		target->flash_write = sam3x_flash_write;
		target_add_commands(target, sam3x_cmd_list, "SAM3N");
		return true;
	}
    
    target->idcode = adiv5_ap_mem_read(ap, SAM3S_CHIPID_CIDR);
    switch (target->idcode & (CHIPID_CIDR_ARCH_MASK | CHIPID_CIDR_EPROC_MASK)) {
        case CHIPID_CIDR_ARCH_SAM3SxA | CHIPID_CIDR_EPROC_CM3:
        case CHIPID_CIDR_ARCH_SAM3SxB | CHIPID_CIDR_EPROC_CM3:
        case CHIPID_CIDR_ARCH_SAM3SxC | CHIPID_CIDR_EPROC_CM3:
            target->driver = "Atmel SAM3S";
            target->xml_mem_map = sam3n_xml_memory_map;
            target->flash_erase = sam3x_flash_erase;
            target->flash_write = sam3x_flash_write;
            target_add_commands(target, sam3x_cmd_list, "SAM3S");
            return true;
    }

	target->idcode = adiv5_ap_mem_read(ap, SAM4S_CHIPID_CIDR);
	switch (target->idcode & (CHIPID_CIDR_ARCH_MASK | CHIPID_CIDR_EPROC_MASK)) {
	case CHIPID_CIDR_ARCH_SAM4SxA | CHIPID_CIDR_EPROC_CM4:
	case CHIPID_CIDR_ARCH_SAM4SxB | CHIPID_CIDR_EPROC_CM4:
	case CHIPID_CIDR_ARCH_SAM4SxC | CHIPID_CIDR_EPROC_CM4:
		target->driver = "Atmel SAM4S";
		target->xml_mem_map = sam4s_xml_memory_map;
		target->flash_erase = sam3x_flash_erase;
		target->flash_write = sam3x_flash_write;
		target_add_commands(target, sam3x_cmd_list, "SAM4S");
		return true;
	}

	return false;
}

static int
sam3x_flash_cmd(struct target_s *target, uint32_t base, uint8_t cmd, uint16_t arg)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	DEBUG("%s: base = 0x%08x cmd = 0x%02X, arg = 0x%06X\n",
		__func__, base, cmd, arg);
	adiv5_ap_mem_write(ap, EEFC_FCR(base),
			EEFC_FCR_FKEY | cmd | ((uint32_t)arg << 8));

	while(!(adiv5_ap_mem_read(ap, EEFC_FSR(base)) & EEFC_FSR_FRDY))
		if(target_check_error(target))
			return -1;

	uint32_t sr = adiv5_ap_mem_read(ap, EEFC_FSR(base));
	return sr & EEFC_FSR_ERROR;
}

static uint32_t
sam3x_flash_base(struct target_s *target, uint32_t addr, uint32_t *offset)
{
	if (strcmp(target->driver, "Atmel SAM3X") == 0) {
		uint32_t half = -1;
		switch (target->idcode & CHIPID_CIDR_NVPSIZ_MASK) {
		case CHIPID_CIDR_NVPSIZ_128K:
			half = 0x00090000;
			break;
		case CHIPID_CIDR_NVPSIZ_256K:
			half = 0x000A0000;
			break;
		case CHIPID_CIDR_NVPSIZ_512K:
			half = 0x000C0000;
			break;
		}
		if (addr > half) {
			if (offset)
				*offset = addr - half;
			return SAM3X_EEFC_BASE(1);
		} else {
			if (offset)
				*offset = addr - 0x80000;
			return SAM3X_EEFC_BASE(0);
		}
	}

	if (strcmp(target->driver, "Atmel SAM4S") == 0) {
		uint32_t half = -1;
		switch (target->idcode & CHIPID_CIDR_NVPSIZ_MASK) {
		case CHIPID_CIDR_NVPSIZ_128K:
		case CHIPID_CIDR_NVPSIZ_256K:
		case CHIPID_CIDR_NVPSIZ_512K:
			if (offset)
				*offset = addr - 0x400000;
			return SAM4S_EEFC_BASE(0);
		case CHIPID_CIDR_NVPSIZ_1024K:
			half = 0x480000;
			break;
		case CHIPID_CIDR_NVPSIZ_2048K:
			half = 0x500000;
			break;
		}
		if (addr >= half) {
			if (offset)
				*offset = addr - half;
			return SAM4S_EEFC_BASE(1);
		} else {
			if (offset)
				*offset = addr - 0x400000;
			return SAM4S_EEFC_BASE(0);
		}
	}

	/* SAM3N device */
	if (offset)
		*offset = addr - 0x400000;
	return SAM3N_EEFC_BASE;
}

static int sam3x_flash_erase(struct target_s *target, uint32_t addr, int len)
{
	uint32_t offset;
	uint32_t base = sam3x_flash_base(target, addr, &offset);

	/* The SAM4S is the only supported device with a page erase command.
	 * Erasing is done in 8-page chunks. arg[15:2] contains the page
	 * number and arg[1:0] contains 0x1, indicating 8-page chunks.
	 */
	if (strcmp(target->driver, "Atmel SAM4S") == 0) {
		unsigned chunk = offset / SAM4_PAGE_SIZE;

		/* Fail if the start address is not 8-page-aligned. */
		if (chunk % 8 != 0)
			return -1;

		/* Note that the length might not be a multiple of 8 pages.
		 * In this case, we will erase a few extra pages at the end.
		 */
		while (len > 0) {
			int16_t arg = chunk | 0x1;
			if(sam3x_flash_cmd(target, base, EEFC_FCR_FCMD_EPA, arg))
				return -1;

			len -= SAM4_PAGE_SIZE * 8;
			addr += SAM4_PAGE_SIZE * 8;
			chunk += 8;
		}

		return 0;
	}

	/* The SAM3X/SAM3N don't really have a page erase function.
	 * This Erase/Write page is the best we have, so we write with all
	 * ones.  This does waste time, but what can we do?
	 */
	unsigned chunk = offset / SAM3_PAGE_SIZE;
	uint8_t buf[SAM3_PAGE_SIZE];

	memset(buf, 0xff, sizeof(buf));
	/* Only do this once, since it doesn't change. */
	target_mem_write_words(target, addr, (void*)buf, SAM3_PAGE_SIZE);

	while (len) {
		if(sam3x_flash_cmd(target, base, EEFC_FCR_FCMD_EWP, chunk))
			return -1;

		len -= SAM3_PAGE_SIZE;
		addr += SAM3_PAGE_SIZE;
		chunk++;
	}

	return 0;
}

static int sam3x_flash_write(struct target_s *target, uint32_t dest,
			  const uint8_t *src, int len)
{
	unsigned page_size;
	if (strcmp(target->driver, "Atmel SAM4S") == 0) {
	        page_size = SAM4_PAGE_SIZE;
	} else {
		page_size = SAM3_PAGE_SIZE;
	}
	uint32_t offset;
	uint32_t base = sam3x_flash_base(target, dest, &offset);
	uint8_t buf[page_size];
	unsigned first_chunk = offset / page_size;
	unsigned last_chunk = (offset + len - 1) / page_size;
	offset %= page_size;
	dest -= offset;

	for (unsigned chunk = first_chunk; chunk <= last_chunk; chunk++) {

		DEBUG("chunk %u len %d\n", chunk, len);
		/* first and last chunk may require special handling */
		if ((chunk == first_chunk) || (chunk == last_chunk)) {

			/* fill with all ff to avoid sector rewrite corrupting other writes */
			memset(buf, 0xff, sizeof(buf));

			/* copy as much as fits */
			int copylen = page_size - offset;
			if (copylen > len)
				copylen = len;
			memcpy(&buf[offset], src, copylen);

			/* update to suit */
			len -= copylen;
			src += copylen;
			offset = 0;
		} else {

			/* interior chunk, must be aligned and full-sized */
			memcpy(buf, src, page_size);
			len -= page_size;
			src += page_size;
		}

		target_mem_write_words(target, dest, (void*)buf, page_size);
		if(sam3x_flash_cmd(target, base, EEFC_FCR_FCMD_WP, chunk))
			return -1;
	}

	return 0;
}

static bool sam3x_cmd_gpnvm_get(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);
	uint32_t base = sam3x_flash_base(t, 0, NULL);

	sam3x_flash_cmd(t, base, EEFC_FCR_FCMD_GGPB, 0);
	gdb_outf("GPNVM: 0x%08X\n", adiv5_ap_mem_read(ap, EEFC_FRR(base)));

	return true;
}

static bool sam3x_cmd_gpnvm_set(target *t, int argc, char *argv[])
{
	uint32_t bit, cmd;
	uint32_t base = sam3x_flash_base(t, 0, NULL);

	if (argc != 3) {
		gdb_out("usage: monitor gpnvm_set <bit> <val>\n");
		return false;
	}
	bit = atol(argv[1]);
	cmd = atol(argv[2]) ? EEFC_FCR_FCMD_SGPB : EEFC_FCR_FCMD_CGPB;

	sam3x_flash_cmd(t, base, cmd, bit);
	sam3x_cmd_gpnvm_get(t);

	return true;
}

