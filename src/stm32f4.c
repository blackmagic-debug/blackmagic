/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
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

/* This file implements STM32F4 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * Refereces:
 * ST doc - RM0090
 *   Reference manual - STM32F405xx, STM32F407xx, STM32F415xx and STM32F417xx
 *   advanced ARM-based 32-bit MCUs
 * ST doc - PM0081
 *   Programming manual - STM32F40xxx and STM32F41xxx Flash programming
 *    manual
 */

#include "general.h"
#include "adiv5.h"
#include "target.h"
#include "cortexm.h"
#include "command.h"
#include "gdb_packet.h"

static bool stm32f4_cmd_erase_mass(target *t);
static bool stm32f4_cmd_option(target *t, int argc, char *argv[]);

const struct command_s stm32f4_cmd_list[] = {
	{"erase_mass", (cmd_handler)stm32f4_cmd_erase_mass, "Erase entire flash memory"},
	{"option", (cmd_handler)stm32f4_cmd_option, "Manipulate option bytes"},
	{NULL, NULL, NULL}
};


static int stm32f4_flash_erase(target *t, uint32_t addr, size_t len);
static int stm32f4_flash_write(target *t, uint32_t dest,
                               const uint8_t *src, size_t len);

static const char stm32f4_driver_str[] = "STM32F4xx";

static const char stm32f4_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x8000000\" length=\"0x10000\">"
	"    <property name=\"blocksize\">0x4000</property>"
	"  </memory>"
	"  <memory type=\"flash\" start=\"0x8010000\" length=\"0x10000\">"
	"    <property name=\"blocksize\">0x10000</property>"
	"  </memory>"
	"  <memory type=\"flash\" start=\"0x8020000\" length=\"0xE0000\">"
	"    <property name=\"blocksize\">0x20000</property>"
	"  </memory>"
	"  <memory type=\"flash\" start=\"0x8100000\" length=\"0x10000\">"
	"    <property name=\"blocksize\">0x4000</property>"
	"  </memory>"
	"  <memory type=\"flash\" start=\"0x8110000\" length=\"0x10000\">"
	"    <property name=\"blocksize\">0x10000</property>"
	"  </memory>"
	"  <memory type=\"flash\" start=\"0x8120000\" length=\"0xE0000\">"
	"    <property name=\"blocksize\">0x20000</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x30000\"/>"
	"  <memory type=\"ram\" start=\"0x10000000\" length=\"0x10000\"/>"
	"</memory-map>";

/* Flash Program ad Erase Controller Register Map */
#define FPEC_BASE	0x40023C00
#define FLASH_ACR	(FPEC_BASE+0x00)
#define FLASH_KEYR	(FPEC_BASE+0x04)
#define FLASH_OPTKEYR	(FPEC_BASE+0x08)
#define FLASH_SR	(FPEC_BASE+0x0C)
#define FLASH_CR	(FPEC_BASE+0x10)
#define FLASH_OPTCR	(FPEC_BASE+0x14)

#define FLASH_CR_PG		(1 << 0)
#define FLASH_CR_SER		(1 << 1)
#define FLASH_CR_MER		(1 << 2)
#define FLASH_CR_PSIZE8		(0 << 8)
#define FLASH_CR_PSIZE16	(1 << 8)
#define FLASH_CR_PSIZE32	(2 << 8)
#define FLASH_CR_PSIZE64	(3 << 8)
#define FLASH_CR_STRT		(1 << 16)
#define FLASH_CR_EOPIE		(1 << 24)
#define FLASH_CR_ERRIE		(1 << 25)
#define FLASH_CR_STRT		(1 << 16)
#define FLASH_CR_LOCK		(1 << 31)

#define FLASH_SR_BSY		(1 << 16)

#define FLASH_OPTCR_OPTLOCK	(1 << 0)
#define FLASH_OPTCR_OPTSTRT	(1 << 1)
#define FLASH_OPTCR_RESERVED	0xf0000013

#define KEY1 0x45670123
#define KEY2 0xCDEF89AB

#define OPTKEY1 0x08192A3B
#define OPTKEY2 0x4C5D6E7F

#define SR_ERROR_MASK	0xF2
#define SR_EOP		0x01

#define DBGMCU_IDCODE	0xE0042000

/* This routine is uses word access.  Only usable on target voltage >2.7V */
static const uint16_t stm32f4_flash_write_stub[] = {
#include "../flashstub/stm32f4.stub"
};

#define SRAM_BASE 0x20000000
#define STUB_BUFFER_BASE ALIGN(SRAM_BASE + sizeof(stm32f4_flash_write_stub), 4)

bool stm32f4_probe(target *t)
{
	uint32_t idcode;

	idcode = target_mem_read32(t, DBGMCU_IDCODE);
	switch(idcode & 0xFFF) {
	case 0x411: /* Documented to be 0x413! This is what I read... */
	case 0x413: /* F407VGT6 */
	case 0x419: /* 427/437 */
	case 0x423: /* F401 B/C RM0368 Rev.3 */
	case 0x431: /* F411     RM0383 Rev.4 */
	case 0x433: /* F401 D/E RM0368 Rev.3 */
		t->xml_mem_map = stm32f4_xml_memory_map;
		t->driver = stm32f4_driver_str;
		t->flash_erase = stm32f4_flash_erase;
		t->flash_write = stm32f4_flash_write;
		target_add_commands(t, stm32f4_cmd_list, "STM32F4");
		return true;
	}
	return false;
}

static void stm32f4_flash_unlock(target *t)
{
	if (target_mem_read32(t, FLASH_CR) & FLASH_CR_LOCK) {
		/* Enable FPEC controller access */
		target_mem_write32(t, FLASH_KEYR, KEY1);
		target_mem_write32(t, FLASH_KEYR, KEY2);
	}
}

static int stm32f4_flash_erase(target *t, uint32_t addr, size_t len)
{
	uint16_t sr;
	uint32_t cr;
	uint32_t pagesize;

	addr &= 0x07FFC000;

	stm32f4_flash_unlock(t);

	while(len) {
		if (addr < 0x10000) { /* Sector 0..3 */
			cr = (addr >> 11);
			pagesize = 0x4000;
		} else if (addr < 0x20000) { /* Sector 4 */
			cr = (4 << 3);
			pagesize = 0x10000;
		} else if (addr < 0x100000) { /* Sector 5..11 */
			cr = (((addr - 0x20000) >> 14) + 0x28);
			pagesize = 0x20000;
		} else { /* Sector > 11 ?? */
			return -1;
		}
		cr |= FLASH_CR_EOPIE | FLASH_CR_ERRIE | FLASH_CR_SER;
		/* Flash page erase instruction */
		target_mem_write32(t, FLASH_CR, cr);
		/* write address to FMA */
		target_mem_write32(t, FLASH_CR, cr | FLASH_CR_STRT);

		/* Read FLASH_SR to poll for BSY bit */
		while(target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
			if(target_check_error(t))
				return -1;

		len -= pagesize;
		addr += pagesize;
	}

	/* Check for error */
	sr = target_mem_read32(t, FLASH_SR);
	if(sr & SR_ERROR_MASK)
		return -1;

	return 0;
}

static int stm32f4_flash_write(target *t, uint32_t dest,
                               const uint8_t *src, size_t len)
{
	uint32_t offset = dest % 4;
	uint8_t data[ALIGN(offset + len, 4)];

	/* Construct data buffer used by stub */
	/* pad partial words with all 1s to avoid damaging overlapping areas */
	memset(data, 0xff, sizeof(data));
	memcpy((uint8_t *)data + offset, src, len);

	/* Write buffer to target ram call stub */
	target_mem_write(t, SRAM_BASE, stm32f4_flash_write_stub,
	                 sizeof(stm32f4_flash_write_stub));
	target_mem_write(t, STUB_BUFFER_BASE, data, sizeof(data));
	return cortexm_run_stub(t, SRAM_BASE, dest - offset,
	                        STUB_BUFFER_BASE, sizeof(data), 0);
}

static bool stm32f4_cmd_erase_mass(target *t)
{
	const char spinner[] = "|/-\\";
	int spinindex = 0;

	gdb_out("Erasing flash... This may take a few seconds.  ");
	stm32f4_flash_unlock(t);

	/* Flash mass erase start instruction */
	target_mem_write32(t, FLASH_CR, FLASH_CR_MER);
	target_mem_write32(t, FLASH_CR, FLASH_CR_STRT | FLASH_CR_MER);

	/* Read FLASH_SR to poll for BSY bit */
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY) {
		gdb_outf("\b%c", spinner[spinindex++ % 4]);
		if(target_check_error(t)) {
			gdb_out("\n");
			return false;
		}
	}
	gdb_out("\n");

	/* Check for error */
	uint16_t sr = target_mem_read32(t, FLASH_SR);
	if ((sr & SR_ERROR_MASK) || !(sr & SR_EOP))
		return false;

	return true;
}

static bool stm32f4_option_write(target *t, uint32_t value)
{
	target_mem_write32(t, FLASH_OPTKEYR, OPTKEY1);
	target_mem_write32(t, FLASH_OPTKEYR, OPTKEY2);
	value &= ~FLASH_OPTCR_RESERVED;
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
		if(target_check_error(t))
			return -1;

	/* WRITE option bytes instruction */
	target_mem_write32(t, FLASH_OPTCR, value);
	target_mem_write32(t, FLASH_OPTCR, value | FLASH_OPTCR_OPTSTRT);
	/* Read FLASH_SR to poll for BSY bit */
	while(target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
		if(target_check_error(t))
			return false;
	target_mem_write32(t, FLASH_OPTCR, value | FLASH_OPTCR_OPTLOCK);
	return true;
}

static bool stm32f4_cmd_option(target *t, int argc, char *argv[])
{
	uint32_t addr, val;

	if ((argc == 2) && !strcmp(argv[1], "erase")) {
		stm32f4_option_write(t, 0x0fffaaed);
	}
	else if ((argc == 3) && !strcmp(argv[1], "write")) {
		val = strtoul(argv[2], NULL, 0);
		stm32f4_option_write(t, val);
	} else {
		gdb_out("usage: monitor option erase\n");
		gdb_out("usage: monitor option write <value>\n");
	}

	for (int i = 0; i < 0xf; i += 8) {
		addr = 0x1fffC000 + i;
		val = target_mem_read32(t, addr);
		gdb_outf("0x%08X: 0x%04X\n", addr, val & 0xFFFF);
	}
	return true;
}
