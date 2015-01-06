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
 */

#include <stdlib.h>
#include <string.h>

#include "general.h"
#include "adiv5.h"
#include "target.h"

#define SIM_SDID   0x40048024

#define FTFA_BASE  0x40020000
#define FTFA_FSTAT (FTFA_BASE + 0x00)
#define FTFA_FCNFG (FTFA_BASE + 0x01)
#define FTFA_FSEC  (FTFA_BASE + 0x02)
#define FTFA_FOPT  (FTFA_BASE + 0x03)
#define FTFA_FCCOB(x) (FTFA_BASE + 0x04 + ((x) ^ 3))

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

#define KL25_PAGESIZE 0x400

static int kl25_flash_erase(struct target_s *target, uint32_t addr, int len);
static int kl25_flash_write(struct target_s *target, uint32_t dest,
			  const uint8_t *src, int len);

static const char kl25_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0\" length=\"0x20000\">"
	"    <property name=\"blocksize\">0x400</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x1ffff000\" length=\"0x1000\"/>"
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x3000\"/>"
	"</memory-map>";

bool kinetis_probe(struct target_s *t)
{
	uint32_t sdid = adiv5_ap_mem_read(adiv5_target_ap(t), SIM_SDID);
	switch (sdid >> 20) {
	case 0x251:
		t->driver = "KL25";
		t->xml_mem_map = kl25_xml_memory_map;
		t->flash_erase = kl25_flash_erase;
		t->flash_write = kl25_flash_write;
		return true;
	}
	return false;
}

static bool
kl25_command(struct target_s *t, uint8_t cmd, uint32_t addr, const uint8_t data[8])
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);
	uint8_t fstat;

        /* Wait for CCIF to be high */
	do {
		fstat = adiv5_ap_mem_read_byte(ap, FTFA_FSTAT);
		/* Check ACCERR and FPVIOL are zero in FSTAT */
            	if (fstat & (FTFA_FSTAT_ACCERR | FTFA_FSTAT_FPVIOL))
			return false;
	} while (!(fstat & FTFA_FSTAT_CCIF));

	/* Write command to FCCOB */
	addr &= 0xffffff;
	addr |= (uint32_t)cmd << 24;
	adiv5_ap_mem_write(ap, FTFA_FCCOB(0), addr);
	if (data) {
		adiv5_ap_mem_write(ap, FTFA_FCCOB(4), *(uint32_t*)&data[0]);
		adiv5_ap_mem_write(ap, FTFA_FCCOB(8), *(uint32_t*)&data[4]);
	}

	/* Enable execution by clearing CCIF */
	adiv5_ap_mem_write_byte(ap, FTFA_FSTAT, FTFA_FSTAT_CCIF);

	/* Wait for execution to complete */
	do {
		fstat = adiv5_ap_mem_read_byte(ap, FTFA_FSTAT);
		/* Check ACCERR and FPVIOL are zero in FSTAT */
            	if (fstat & (FTFA_FSTAT_ACCERR | FTFA_FSTAT_FPVIOL))
			return false;
	} while (!(fstat & FTFA_FSTAT_CCIF));

	return true;
}

static int kl25_flash_erase(struct target_s *t, uint32_t addr, int len)
{
	addr &= ~(KL25_PAGESIZE - 1);
	len = (len + KL25_PAGESIZE - 1) & ~(KL25_PAGESIZE - 1);

	while (len) {
		kl25_command(t, FTFA_CMD_ERASE_SECTOR, addr, NULL);
		len -= KL25_PAGESIZE;
		addr += KL25_PAGESIZE;
	}
	return 0;
}

static int kl25_flash_write(struct target_s *t, uint32_t dest,
			  const uint8_t *src, int len)
{
	/* FIXME handle misaligned start and end of sections */
	if ((dest & 3) || (len & 3))
		return -1;

	while (len) {
		kl25_command(t, FTFA_CMD_PROGRAM_LONGWORD, dest, src);
		len -= 4;
		dest += 4;
		src += 4;
	}
	return 0;
}
