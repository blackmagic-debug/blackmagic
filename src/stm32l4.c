/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Uwe Bonnes
 * Written by Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
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

/* This file implements STM32L4 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * On L4, flash and options are written in DWORDs (8-Byte) only.
 *
 * Refereces:
 * ST doc - RM0351
 *    Reference manual
 *    STM32L4x6 advanced ARM®-based 32-bit MCUs
 *
 */

#include "general.h"
#include "adiv5.h"
#include "target.h"
#include "cortexm.h"
#include "command.h"
#include "gdb_packet.h"

static bool stm32l4_cmd_erase_mass(target *t);
static bool stm32l4_cmd_erase_bank1(target *t);
static bool stm32l4_cmd_erase_bank2(target *t);
static bool stm32l4_cmd_option(target *t, int argc, char *argv[]);

const struct command_s stm32l4_cmd_list[] = {
	{"erase_mass", (cmd_handler)stm32l4_cmd_erase_mass, "Erase entire flash memory"},
	{"erase_bank1", (cmd_handler)stm32l4_cmd_erase_bank1, "Erase entire bank1 flash memory"},
	{"erase_bank2", (cmd_handler)stm32l4_cmd_erase_bank2, "Erase entire bank2 flash memory"},
	{"option", (cmd_handler)stm32l4_cmd_option, "Manipulate option bytes"},
	{NULL, NULL, NULL}
};


static int stm32l4_flash_erase(struct target_flash *f, uint32_t addr, size_t len);
static int stm32l4_flash_write(struct target_flash *f,
                               uint32_t dest, const void *src, size_t len);

static const char stm32l4_driver_str[] = "STM32L4xx";

#define PAGE_SIZE   0x800
/* Flash Program ad Erase Controller Register Map */
#define FPEC_BASE			0x40022000
#define FLASH_ACR			(FPEC_BASE+0x00)
#define FLASH_KEYR			(FPEC_BASE+0x08)
#define FLASH_OPTKEYR		(FPEC_BASE+0x0c)
#define FLASH_SR			(FPEC_BASE+0x10)
#define FLASH_CR			(FPEC_BASE+0x14)
#define FLASH_OPTR			(FPEC_BASE+0x20)
//#define FLASH_OPTCR		(FPEC_BASE+0x14)

#define FLASH_CR_PG			(1 << 0)
#define FLASH_CR_PER		(1 << 1)
#define FLASH_CR_MER1		(1 << 2)
#define FLASH_CR_PAGE_SHIFT	3
#define FLASH_CR_BKER		(1 << 11)
#define FLASH_CR_MER2		(1 << 15)
#define FLASH_CR_STRT		(1 << 16)
#define FLASH_CR_OPTSTRT	(1 << 17)
#define FLASH_CR_FSTPG	 	(1 << 18)
#define FLASH_CR_EOPIE		(1 << 24)
#define FLASH_CR_ERRIE		(1 << 25)
#define FLASH_CR_OPTLOCK	(1 << 30)
#define FLASH_CR_LOCK		(1 << 31)

#define FLASH_SR_EOP		(1 << 0)
#define FLASH_SR_OPERR		(1 << 1)
#define FLASH_SR_PROGERR	(1 << 3)
#define FLASH_SR_WRPERR		(1 << 4)
#define FLASH_SR_PGAERR		(1 << 5)
#define FLASH_SR_SIZERR		(1 << 6)
#define FLASH_SR_PGSERR		(1 << 7)
#define FLASH_SR_MSERR		(1 << 8)
#define FLASH_SR_FASTERR	(1 << 9)
#define FLASH_SR_RDERR		(1 << 14)
#define FLASH_SR_OPTVERR	(1 << 15)
#define FLASH_SR_ERROR_MASK	0xC3FA
#define FLASH_SR_BSY		(1 << 16)

#define KEY1 0x45670123
#define KEY2 0xCDEF89AB

#define OPTKEY1 0x08192A3B
#define OPTKEY2 0x4C5D6E7F

#define SR_ERROR_MASK	0xF2

#define OR_DUALBANK		(1 << 21)

#define DBGMCU_IDCODE	0xE0042000
#define FLASH_SIZE_REG  0x1FFF75E0
#define FLASH_SIZE_REG  0x1FFF75E0

/* This routine is uses double word access.*/
static const uint16_t stm32l4_flash_write_stub[] = {
#include "../flashstub/stm32l4.stub"
};

#define SRAM_BASE 0x20000000
#define STUB_BUFFER_BASE ALIGN(SRAM_BASE + sizeof(stm32l4_flash_write_stub), 8)

struct stm32l4_flash {
	struct target_flash f;
	uint32_t bank1_start;
};

static void stm32l4_add_flash(target *t,
                              uint32_t addr, size_t length, size_t blocksize,
                              uint32_t bank1_start)
{
	struct stm32l4_flash *sf = calloc(1, sizeof(*sf));
	struct target_flash *f = &sf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = blocksize;
	f->erase = stm32l4_flash_erase;
	f->write = stm32l4_flash_write;
	f->align = 8;
	f->erased = 0xff;
	sf->bank1_start = bank1_start;
	target_add_flash(t, f);
}

bool stm32l4_probe(target *t)
{
	uint32_t idcode;
	uint32_t size;
	uint32_t options;
	uint32_t bank1_start = 0x08040000;

	idcode = target_mem_read32(t, DBGMCU_IDCODE);
	switch(idcode & 0xFFF) {
	case 0x415: /*  */
		t->driver = stm32l4_driver_str;
		target_add_ram(t, 0x10000000, 1 << 15);
		target_add_ram(t, 0x20000000, 3 << 15);
		size    = (target_mem_read32(t, FLASH_SIZE_REG) & 0xffff);
		options =  target_mem_read32(t, FLASH_OPTR);
		if ((size < 0x400) && (options & OR_DUALBANK))
			bank1_start =  0x08000000 + (size << 9);
		stm32l4_add_flash(t, 0x08000000, size << 10, PAGE_SIZE, bank1_start);
		target_add_commands(t, stm32l4_cmd_list, "STM32L4");
		return true;
	}
	return false;
}

static void stm32l4_flash_unlock(target *t)
{
	if (target_mem_read32(t, FLASH_CR) & FLASH_CR_LOCK) {
		/* Enable FPEC controller access */
		target_mem_write32(t, FLASH_KEYR, KEY1);
		target_mem_write32(t, FLASH_KEYR, KEY2);
	}
}

static int stm32l4_flash_erase(struct target_flash *f, uint32_t addr, size_t len)
{
	target *t = f->t;
	uint16_t sr;
	uint32_t bank1_start = ((struct stm32l4_flash *)f)->bank1_start;
	uint32_t page;

	stm32l4_flash_unlock(t);

	page = (addr - 0x08000000) / PAGE_SIZE;
	while(len) {
		uint32_t cr;

		cr = FLASH_CR_PER | (page << FLASH_CR_PAGE_SHIFT );
		if (addr >= bank1_start)
			cr |= FLASH_CR_BKER;
		/* Flash page erase instruction */
		target_mem_write32(t, FLASH_CR, cr);
		/* write address to FMA */
		cr |= FLASH_CR_STRT;
		target_mem_write32(t, FLASH_CR, cr);

		/* Read FLASH_SR to poll for BSY bit */
		while(target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
			if(target_check_error(t))
				return -1;

		len  -= PAGE_SIZE;
		addr += PAGE_SIZE;
		page++;
	}

	/* Check for error */
	sr = target_mem_read32(t, FLASH_SR);
	if(sr & FLASH_SR_ERROR_MASK)
		return -1;

	return 0;
}

static int stm32l4_flash_write(struct target_flash *f,
                               uint32_t dest, const void *src, size_t len)
{
	/* Write buffer to target ram call stub */
	target_mem_write(f->t, SRAM_BASE, stm32l4_flash_write_stub,
	                 sizeof(stm32l4_flash_write_stub));
	target_mem_write(f->t, STUB_BUFFER_BASE, src, len);
	return cortexm_run_stub(f->t, SRAM_BASE, dest,
	                        STUB_BUFFER_BASE, len, 0);
}

static bool stm32l4_cmd_erase(target *t, uint32_t action)
{
	const char spinner[] = "|/-\\";
	int spinindex = 0;

	gdb_out("Erasing flash... This may take a few seconds.  ");
	stm32l4_flash_unlock(t);

	/* Flash erase action start instruction */
	target_mem_write32(t, FLASH_CR, action);
	target_mem_write32(t, FLASH_CR, action | FLASH_CR_STRT);

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
	if (sr & FLASH_SR_ERROR_MASK)
		return false;
	return true;
}

static bool stm32l4_cmd_erase_mass(target *t)
{
	return stm32l4_cmd_erase(t, FLASH_CR_MER1 | FLASH_CR_MER2);
}

static bool stm32l4_cmd_erase_bank1(target *t)
{
	return stm32l4_cmd_erase(t, FLASH_CR_MER1);
}

static bool stm32l4_cmd_erase_bank2(target *t)
{
	return stm32l4_cmd_erase(t, FLASH_CR_MER2);
}

static bool stm32l4_cmd_option(target *t, int argc, char *argv[])
{
	uint32_t addr, val;

	(void) argc;
	(void) argv;
	for (int i = 0; i < 0x23; i += 8) {
		addr = 0x1fff7800 + i;
		val = target_mem_read32(t, addr);
		gdb_outf("0x%08X: 0x%08x\n", addr, val);
	}
	for (int i = 8; i < 0x23; i += 8) {
		addr = 0x1ffff800 + i;
		val = target_mem_read32(t, addr);
		gdb_outf("0x%08X: 0x%08X\n", addr, val);
	}
	return true;
}
