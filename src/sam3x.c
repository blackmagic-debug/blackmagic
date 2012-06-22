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

#include <stdlib.h>
#include <string.h>

#include "general.h"
#include "adiv5.h"
#include "target.h"

static int sam3x_flash_erase(struct target_s *target, uint32_t addr, int len);
static int sam3x_flash_write(struct target_s *target, uint32_t dest, 
			const uint8_t *src, int len);

static const char sam3x_driver_str[] = "Atmel SAM3X";

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


/* Enhanced Embedded Flash Controller (EEFC) Register Map */
#define EEFC_BASE(x)	(0x400E0A00+((x)*0x400))
#define EEFC_FMR(x)	(EEFC_BASE(x)+0x00)
#define EEFC_FCR(x)	(EEFC_BASE(x)+0x04)
#define EEFC_FSR(x)	(EEFC_BASE(x)+0x08)
#define EEFC_FRR(x)	(EEFC_BASE(x)+0x0C)

#define EEFC_FCR_FKEY		(0x5A << 24)
#define EEFC_FCR_FCMD_GETD	0x00
#define EEFC_FCR_FCMD_WP	0x01
#define EEFC_FCR_FCMD_WPL	0x02
#define EEFC_FCR_FCMD_EWP	0x03
#define EEFC_FCR_FCMD_EWPL	0x04
#define EEFC_FCR_FCMD_EA	0x05
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

#define CHIPID_CIDR	0x400E0940

#define CHIPID_CIDR_VERSION_MASK	(0x1F << 0)
#define CHIPID_CIDR_EPROC_CM3		(0x03 << 5)
#define CHIPID_CIDR_EPROC_MASK		(0x07 << 5)
#define CHIPID_CIDR_NVPSIZ_MASK		(0x0F << 8)
#define CHIPID_CIDR_NVPSIZ_128K		(0x07 << 8)
#define CHIPID_CIDR_NVPSIZ_256K		(0x09 << 8)
#define CHIPID_CIDR_NVPSIZ_512K		(0x0A << 8)
#define CHIPID_CIDR_NVPSIZ2_MASK	(0x0F << 12)
#define CHIPID_CIDR_SRAMSIZ_MASK	(0x0F << 16)
#define CHIPID_CIDR_ARCH_MASK		(0xFF << 20)
#define CHIPID_CIDR_ARCH_SAM3XxC	(0x84 << 20)
#define CHIPID_CIDR_ARCH_SAM3XxE	(0x85 << 20)
#define CHIPID_CIDR_ARCH_SAM3XxG	(0x86 << 20)
#define CHIPID_CIDR_NVPTYP_MASK		(0x07 << 28)
#define CHIPID_CIDR_NVPTYP_FLASH	(0x02 << 28)
#define CHIPID_CIDR_NVPTYP_ROM_FLASH	(0x03 << 28)
#define CHIPID_CIDR_EXT			(0x01 << 31)

#define PAGE_SIZE 256

int sam3x_probe(struct target_s *target)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	target->idcode = adiv5_ap_mem_read(ap, CHIPID_CIDR);

	/* FIXME: Check for all variants with similar flash interface */
	switch (target->idcode & (CHIPID_CIDR_ARCH_MASK | CHIPID_CIDR_EPROC_MASK)) {
	case CHIPID_CIDR_ARCH_SAM3XxC | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3XxE | CHIPID_CIDR_EPROC_CM3:
	case CHIPID_CIDR_ARCH_SAM3XxG | CHIPID_CIDR_EPROC_CM3:
		target->driver = sam3x_driver_str;
		target->xml_mem_map = sam3x_xml_memory_map;
		target->flash_erase = sam3x_flash_erase;
		target->flash_write = sam3x_flash_write;
		return 0;
	}
	return -1;
}

static int
sam3x_flash_cmd(struct target_s *target, int bank, uint8_t cmd, uint16_t arg)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);

	adiv5_ap_mem_write(ap, EEFC_FCR(bank), 
			EEFC_FCR_FKEY | cmd | ((uint32_t)arg << 8));

	while(!(adiv5_ap_mem_read(ap, EEFC_FSR(bank)) & EEFC_FSR_FRDY))
		if(target_check_error(target)) 
			return -1;

	uint32_t sr = adiv5_ap_mem_read(ap, EEFC_FSR(bank));
	return sr & EEFC_FSR_ERROR;
}

static int
sam3x_flash_bank(struct target_s *target, uint32_t addr, uint32_t *offset)
{
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
		return 1;
	}

	if (offset)
		*offset = addr - 0x80000;
	return 0;
}

static int sam3x_flash_erase(struct target_s *target, uint32_t addr, int len)
{
	/* FIXME: This device can't do sector erase.  What do we do here?
	 * Sector erase is done as part of write cycle in sam3x_flash_write()
	 */
	(void)target;
	(void)addr;
	(void)len;

	return 0;
}

static int sam3x_flash_write(struct target_s *target, uint32_t dest, 
			  const uint8_t *src, int len)
{
	uint32_t offset;
	uint8_t bank = sam3x_flash_bank(target, dest, &offset);
	uint32_t buf[PAGE_SIZE];
	unsigned first_chunk = offset / PAGE_SIZE;
	unsigned last_chunk = (offset + len - 1) / PAGE_SIZE;
	offset %= PAGE_SIZE;
	dest -= offset;

	for (unsigned chunk = first_chunk; chunk <= last_chunk; chunk++) {
		
		DEBUG("chunk %u len %d\n", chunk, len);
		/* first and last chunk may require special handling */
		if ((chunk == first_chunk) || (chunk == last_chunk)) {

			/* fill with all ff to avoid sector rewrite corrupting other writes */
			memset(buf, 0xff, sizeof(buf));
		
			/* copy as much as fits */				
			int copylen = PAGE_SIZE - offset;
			if (copylen > len)
				copylen = len;
			memcpy(&buf[offset], src, copylen);

			/* update to suit */
			len -= copylen;
			src += copylen;
			offset = 0;
		} else {

			/* interior chunk, must be aligned and full-sized */
			memcpy(buf, src, PAGE_SIZE);
			len -= PAGE_SIZE;
			src += PAGE_SIZE;
		}

		target_mem_write_words(target, dest, buf, PAGE_SIZE); 
		if(sam3x_flash_cmd(target, bank, EEFC_FCR_FCMD_EWP, chunk))
			return -1;
	}

	return 0;
}

