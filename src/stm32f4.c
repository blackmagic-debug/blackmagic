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


static int stm32f4_flash_erase(struct target_flash *f, uint32_t addr, size_t len);
static int stm32f4_flash_write(struct target_flash *f,
                               uint32_t dest, const void *src, size_t len);

static const char stm32f4_driver_str[] = "STM32F4xx";
static const char stm32f7_driver_str[] = "STM32F7xx";

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

#define DBGMCU_CR		0xE0042004
#define DBG_STANDBY		(1 << 0)
#define DBG_STOP		(1 << 1)
#define DBG_SLEEP		(1 << 2)

#define DBGMCU_APB1_FZ	0xE0042008
#define DBG_WWDG_STOP	(1 << 11)
#define DBG_IWDG_STOP	(1 << 12)

/* This routine uses word access.  Only usable on target voltage >2.7V */
static const uint16_t stm32f4_flash_write_stub[] = {
#include "../flashstub/stm32f4.stub"
};

#define SRAM_BASE 0x20000000
#define STUB_BUFFER_BASE ALIGN(SRAM_BASE + sizeof(stm32f4_flash_write_stub), 4)

struct stm32f4_flash {
	struct target_flash f;
	uint8_t base_sector;
};

static void stm32f4_add_flash(target *t,
                              uint32_t addr, size_t length, size_t blocksize,
                              uint8_t base_sector)
{
	struct stm32f4_flash *sf = calloc(1, sizeof(*sf));
	struct target_flash *f = &sf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = blocksize;
	f->erase = stm32f4_flash_erase;
	f->write = stm32f4_flash_write;
	f->align = 4;
	f->erased = 0xff;
	sf->base_sector = base_sector;
	target_add_flash(t, f);
}

bool stm32f4_probe(target *t)
{
	uint32_t idcode;

	idcode = target_mem_read32(t, DBGMCU_IDCODE);
	idcode &= 0xFFF;
	switch(idcode) {
	case 0x419: /* 427/437 */
		/* Second bank for 2M parts. */
		stm32f4_add_flash(t, 0x8100000, 0x10000, 0x4000, 12);
		stm32f4_add_flash(t, 0x8110000, 0x10000, 0x10000, 16);
		stm32f4_add_flash(t, 0x8120000, 0xE0000, 0x20000, 17);
		/* Fall through for stuff common to F40x/F41x */
	case 0x411: /* Documented to be 0x413! This is what I read... */
	case 0x413: /* F407VGT6 */
	case 0x421: /* F446 */
	case 0x423: /* F401 B/C RM0368 Rev.3 */
	case 0x431: /* F411     RM0383 Rev.4 */
	case 0x433: /* F401 D/E RM0368 Rev.3 */
		t->driver = stm32f4_driver_str;
		target_add_ram(t, 0x10000000, 0x10000);
		target_add_ram(t, 0x20000000, 0x30000);
		stm32f4_add_flash(t, 0x8000000, 0x10000, 0x4000, 0);
		stm32f4_add_flash(t, 0x8010000, 0x10000, 0x10000, 4);
		stm32f4_add_flash(t, 0x8020000, 0xE0000, 0x20000, 5);
		target_add_commands(t, stm32f4_cmd_list, "STM32F4");
		break;
	case 0x449: /* F7x6 RM0385 Rev.2 */
		t->driver = stm32f7_driver_str;
		target_add_ram(t, 0x00000000, 0x4000);
		target_add_ram(t, 0x20000000, 0x50000);
		stm32f4_add_flash(t, 0x8000000, 0x20000, 0x8000, 0);
		stm32f4_add_flash(t, 0x8020000, 0x20000, 0x20000, 4);
		stm32f4_add_flash(t, 0x8040000, 0xC0000, 0x40000, 5);
		target_add_commands(t, stm32f4_cmd_list, "STM32F7");
		break;
	default:
		return false;
	}
	t->idcode = idcode;
	return true;
}

static void stm32f4_flash_unlock(target *t)
{
	if (target_mem_read32(t, FLASH_CR) & FLASH_CR_LOCK) {
		/* Enable FPEC controller access */
		target_mem_write32(t, FLASH_KEYR, KEY1);
		target_mem_write32(t, FLASH_KEYR, KEY2);
	}
}

static int stm32f4_flash_erase(struct target_flash *f, uint32_t addr, size_t len)
{
	target *t = f->t;
	uint16_t sr;
	uint8_t sector = ((struct stm32f4_flash *)f)->base_sector +
	                  (addr - f->start)/f->blocksize;

	stm32f4_flash_unlock(t);

	while(len) {
		uint32_t cr = FLASH_CR_EOPIE | FLASH_CR_ERRIE | FLASH_CR_SER |
		              (sector << 3);
		/* Flash page erase instruction */
		target_mem_write32(t, FLASH_CR, cr);
		/* write address to FMA */
		target_mem_write32(t, FLASH_CR, cr | FLASH_CR_STRT);

		/* Read FLASH_SR to poll for BSY bit */
		while(target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
			if(target_check_error(t))
				return -1;

		len -= f->blocksize;
		sector++;
	}

	/* Check for error */
	sr = target_mem_read32(t, FLASH_SR);
	if(sr & SR_ERROR_MASK)
		return -1;

	return 0;
}

static int stm32f4_flash_write(struct target_flash *f,
                               uint32_t dest, const void *src, size_t len)
{
	/* Write buffer to target ram call stub */
	target_mem_write(f->t, SRAM_BASE, stm32f4_flash_write_stub,
	                 sizeof(stm32f4_flash_write_stub));
	target_mem_write(f->t, STUB_BUFFER_BASE, src, len);
	return cortexm_run_stub(f->t, SRAM_BASE, dest,
	                        STUB_BUFFER_BASE, len, 0);
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
	uint32_t start, val;
	int len;

	if (t->idcode == 0x449) {
		start = 0x1FFF0000;
		len = 0x20;
	}
	else {
		start = 0x1FFFC000;
		len = 0x10;
	}

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

	for (int i = 0; i < len; i += 8) {
		uint32_t addr =  start + i;
		val = target_mem_read32(t, addr);
		gdb_outf("0x%08X: 0x%04X\n", addr, val & 0xFFFF);
	}
	return true;
}
