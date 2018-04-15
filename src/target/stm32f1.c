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

/* This file implements STM32 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * Refereces:
 * ST doc - RM0008
 *   Reference manual - STM32F101xx, STM32F102xx, STM32F103xx, STM32F105xx
 *   and STM32F107xx advanced ARM-based 32-bit MCUs
 * ST doc - RM0091
 *   Reference manual - STM32F0x1/STM32F0x2/STM32F0x8
 *   advanced ARM®-based 32-bit MCUs
 * ST doc - RM0360
 *   Reference manual - STM32F030x4/x6/x8/xC and STM32F070x6/xB
 * ST doc - PM0075
 *   Programming manual - STM32F10xxx Flash memory microcontrollers
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

static bool stm32f1_cmd_erase_mass(target *t);
static bool stm32f1_cmd_option(target *t, int argc, char *argv[]);

const struct command_s stm32f1_cmd_list[] = {
	{"erase_mass", (cmd_handler)stm32f1_cmd_erase_mass, "Erase entire flash memory"},
	{"option", (cmd_handler)stm32f1_cmd_option, "Manipulate option bytes"},
	{NULL, NULL, NULL}
};


static int stm32f1_flash_erase(struct target_flash *f,
                               target_addr addr, size_t len);
static int stm32f1_flash_write(struct target_flash *f,
                               target_addr dest, const void *src, size_t len);

/* Flash Program ad Erase Controller Register Map */
#define FPEC_BASE	0x40022000
#define FLASH_ACR	(FPEC_BASE+0x00)
#define FLASH_KEYR	(FPEC_BASE+0x04)
#define FLASH_OPTKEYR	(FPEC_BASE+0x08)
#define FLASH_SR	(FPEC_BASE+0x0C)
#define FLASH_CR	(FPEC_BASE+0x10)
#define FLASH_AR	(FPEC_BASE+0x14)
#define FLASH_OBR	(FPEC_BASE+0x1C)
#define FLASH_WRPR	(FPEC_BASE+0x20)

#define FLASH_CR_OBL_LAUNCH (1<<13)
#define FLASH_CR_OPTWRE	(1 << 9)
#define FLASH_CR_STRT	(1 << 6)
#define FLASH_CR_OPTER	(1 << 5)
#define FLASH_CR_OPTPG	(1 << 4)
#define FLASH_CR_MER	(1 << 2)
#define FLASH_CR_PER	(1 << 1)

#define FLASH_OBR_RDPRT (1 << 1)

#define FLASH_SR_BSY	(1 << 0)

#define FLASH_OBP_RDP 0x1FFFF800
#define FLASH_OBP_RDP_KEY 0x5aa5
#define FLASH_OBP_RDP_KEY_F3 0x55AA

#define KEY1 0x45670123
#define KEY2 0xCDEF89AB

#define SR_ERROR_MASK	0x14
#define SR_EOP		0x20

#define DBGMCU_IDCODE	0xE0042000
#define DBGMCU_IDCODE_F0	0x40015800

#define FLASHSIZE     0x1FFFF7E0
#define FLASHSIZE_F0  0x1FFFF7CC

static const uint16_t stm32f1_flash_write_stub[] = {
#include "flashstub/stm32f1.stub"
};

#define SRAM_BASE 0x20000000
#define STUB_BUFFER_BASE ALIGN(SRAM_BASE + sizeof(stm32f1_flash_write_stub), 4)

static void stm32f1_add_flash(target *t,
                              uint32_t addr, size_t length, size_t erasesize)
{
	struct target_flash *f = calloc(1, sizeof(*f));
	f->start = addr;
	f->length = length;
	f->blocksize = erasesize;
	f->erase = stm32f1_flash_erase;
	f->write = stm32f1_flash_write;
	f->erased = 0xff;
	target_add_flash(t, f);
}

bool stm32f1_probe(target *t)
{
	size_t flash_size;
	size_t block_size = 0x400;
	t->idcode = target_mem_read32(t, DBGMCU_IDCODE) & 0xfff;
	switch(t->idcode) {
	case 0x410:  /* Medium density */
	case 0x412:  /* Low denisty */
	case 0x420:  /* Value Line, Low-/Medium density */
		t->driver = "STM32F1 medium density";
		target_add_ram(t, 0x20000000, 0x5000);
		stm32f1_add_flash(t, 0x8000000, 0x20000, 0x400);
		target_add_commands(t, stm32f1_cmd_list, "STM32 LD/MD");
		return true;
	case 0x414:	 /* High density */
	case 0x418:  /* Connectivity Line */
	case 0x428:	 /* Value Line, High Density */
		t->driver = "STM32F1 high density";
		target_add_ram(t, 0x20000000, 0x10000);
		stm32f1_add_flash(t, 0x8000000, 0x80000, 0x800);
		target_add_commands(t, stm32f1_cmd_list, "STM32 HD/CL");
		return true;
	case 0x438:  /* STM32F303x6/8 and STM32F328 */
	case 0x422:  /* STM32F30x */
	case 0x446:  /* STM32F303xD/E and STM32F398xE */
		target_add_ram(t, 0x10000000, 0x4000);
		/* fall through */
	case 0x432:  /* STM32F37x */
	case 0x439:  /* STM32F302C8 */
		t->driver = "STM32F3";
		target_add_ram(t, 0x20000000, 0x10000);
		stm32f1_add_flash(t, 0x8000000, 0x80000, 0x800);
		target_add_commands(t, stm32f1_cmd_list, "STM32F3");
		return true;
	}

	t->idcode = target_mem_read32(t, DBGMCU_IDCODE_F0) & 0xfff;
	switch(t->idcode) {
	case 0x444:  /* STM32F03 RM0091 Rev.7, STM32F030x[4|6] RM0360 Rev. 4*/
		t->driver = "STM32F03";
		flash_size = 0x8000;
		break;
	case 0x445:  /* STM32F04 RM0091 Rev.7, STM32F070x6 RM0360 Rev. 4*/
		t->driver = "STM32F04/F070x6";
		flash_size = 0x8000;
		break;
	case 0x440:  /* STM32F05 RM0091 Rev.7, STM32F030x8 RM0360 Rev. 4*/
		t->driver = "STM32F05/F030x8";
		flash_size = 0x10000;
		break;
	case 0x448:  /* STM32F07 RM0091 Rev.7, STM32F070xB RM0360 Rev. 4*/
		t->driver = "STM32F07";
		flash_size = 0x20000;
		block_size = 0x800;
		break;
	case 0x442:  /* STM32F09 RM0091 Rev.7, STM32F030xC RM0360 Rev. 4*/
		t->driver = "STM32F09/F030xC";
		flash_size = 0x40000;
		block_size = 0x800;
		break;
	default:     /* NONE */
		return false;
	}

	target_add_ram(t, 0x20000000, 0x5000);
	stm32f1_add_flash(t, 0x8000000, flash_size, block_size);
	target_add_commands(t, stm32f1_cmd_list, "STM32F0");
	return true;
}

static void stm32f1_flash_unlock(target *t)
{
	target_mem_write32(t, FLASH_KEYR, KEY1);
	target_mem_write32(t, FLASH_KEYR, KEY2);
}

static int stm32f1_flash_erase(struct target_flash *f,
                               target_addr addr, size_t len)
{
	target *t = f->t;
	uint16_t sr;

	stm32f1_flash_unlock(t);

	while(len) {
		/* Flash page erase instruction */
		target_mem_write32(t, FLASH_CR, FLASH_CR_PER);
		/* write address to FMA */
		target_mem_write32(t, FLASH_AR, addr);
		/* Flash page erase start instruction */
		target_mem_write32(t, FLASH_CR, FLASH_CR_STRT | FLASH_CR_PER);

		/* Read FLASH_SR to poll for BSY bit */
		while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
			if(target_check_error(t))
				return -1;

		len -= f->blocksize;
		addr += f->blocksize;
	}

	/* Check for error */
	sr = target_mem_read32(t, FLASH_SR);
	if ((sr & SR_ERROR_MASK) || !(sr & SR_EOP))
		return -1;

	return 0;
}

static int stm32f1_flash_write(struct target_flash *f,
                               target_addr dest, const void *src, size_t len)
{
	target *t = f->t;
	/* Write stub and data to target ram and set PC */
	target_mem_write(t, SRAM_BASE, stm32f1_flash_write_stub,
	                 sizeof(stm32f1_flash_write_stub));
	target_mem_write(t, STUB_BUFFER_BASE, src, len);
	return cortexm_run_stub(t, SRAM_BASE, dest, STUB_BUFFER_BASE, len, 0);
}

static bool stm32f1_cmd_erase_mass(target *t)
{
	stm32f1_flash_unlock(t);

	/* Flash mass erase start instruction */
	target_mem_write32(t, FLASH_CR, FLASH_CR_MER);
	target_mem_write32(t, FLASH_CR, FLASH_CR_STRT | FLASH_CR_MER);

	/* Read FLASH_SR to poll for BSY bit */
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
		if(target_check_error(t))
			return false;

	/* Check for error */
	uint16_t sr = target_mem_read32(t, FLASH_SR);
	if ((sr & SR_ERROR_MASK) || !(sr & SR_EOP))
		return false;

	return true;
}

static bool stm32f1_option_erase(target *t)
{
	/* Erase option bytes instruction */
	target_mem_write32(t, FLASH_CR, FLASH_CR_OPTER | FLASH_CR_OPTWRE);
	target_mem_write32(t, FLASH_CR,
			   FLASH_CR_STRT | FLASH_CR_OPTER | FLASH_CR_OPTWRE);
	/* Read FLASH_SR to poll for BSY bit */
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
		if(target_check_error(t))
			return false;
	return true;
}

static bool stm32f1_option_write_erased(target *t, uint32_t addr, uint16_t value)
{
	if (value == 0xffff)
		return true;
	/* Erase option bytes instruction */
	target_mem_write32(t, FLASH_CR, FLASH_CR_OPTPG | FLASH_CR_OPTWRE);
	target_mem_write16(t, addr, value);
	/* Read FLASH_SR to poll for BSY bit */
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
		if(target_check_error(t))
			return false;
	return true;
}

static bool stm32f1_option_write(target *t, uint32_t addr, uint16_t value)
{
	uint16_t opt_val[8];
	int i, index;

	index = (addr - FLASH_OBP_RDP) / 2;
	if ((index < 0) || (index > 7))
		 return false;
	/* Retrieve old values */
	for (i = 0; i < 16; i = i +4) {
		 uint32_t val = target_mem_read32(t, FLASH_OBP_RDP + i);
		 opt_val[i/2] = val & 0xffff;
		 opt_val[i/2 +1] = val >> 16;
	}
	if (opt_val[index] == value)
		return true;
	/* Check for erased value */
	if (opt_val[index] != 0xffff)
		if (!(stm32f1_option_erase(t)))
			return false;
	opt_val[index] = value;
	/* Write changed values*/
	for (i = 0; i < 8; i++)
		if (!(stm32f1_option_write_erased
			(t, FLASH_OBP_RDP + i*2,opt_val[i])))
			return false;
	return true;
}

static bool stm32f1_cmd_option(target *t, int argc, char *argv[])
{
	uint32_t addr, val;
	uint32_t flash_obp_rdp_key;
	uint32_t rdprt;

	switch(t->idcode) {
	case 0x422:  /* STM32F30x */
	case 0x432:  /* STM32F37x */
	case 0x438:  /* STM32F303x6/8 and STM32F328 */
	case 0x440:  /* STM32F0 */
	case 0x446:  /* STM32F303xD/E and STM32F398xE */
		flash_obp_rdp_key = FLASH_OBP_RDP_KEY_F3;
		break;
	default: flash_obp_rdp_key = FLASH_OBP_RDP_KEY;
	}
	rdprt = target_mem_read32(t, FLASH_OBR) & FLASH_OBR_RDPRT;
	stm32f1_flash_unlock(t);
	target_mem_write32(t, FLASH_OPTKEYR, KEY1);
	target_mem_write32(t, FLASH_OPTKEYR, KEY2);

	if ((argc == 2) && !strcmp(argv[1], "erase")) {
		stm32f1_option_erase(t);
		stm32f1_option_write_erased(t, FLASH_OBP_RDP, flash_obp_rdp_key);
	} else if (rdprt) {
		tc_printf(t, "Device is Read Protected\n");
		tc_printf(t, "Use \"monitor option erase\" to unprotect, erasing device\n");
		return true;
	} else if (argc == 3) {
		addr = strtol(argv[1], NULL, 0);
		val = strtol(argv[2], NULL, 0);
		stm32f1_option_write(t, addr, val);
	} else {
		tc_printf(t, "usage: monitor option erase\n");
		tc_printf(t, "usage: monitor option <addr> <value>\n");
	}

	if (0 && flash_obp_rdp_key == FLASH_OBP_RDP_KEY_F3) {
		/* Reload option bytes on F0 and F3*/
		val = target_mem_read32(t, FLASH_CR);
		val |= FLASH_CR_OBL_LAUNCH;
		stm32f1_option_write(t, FLASH_CR, val);
		val &= ~FLASH_CR_OBL_LAUNCH;
		stm32f1_option_write(t, FLASH_CR, val);
	}

	for (int i = 0; i < 0xf; i += 4) {
		addr = 0x1ffff800 + i;
		val = target_mem_read32(t, addr);
		tc_printf(t, "0x%08X: 0x%04X\n", addr, val & 0xFFFF);
		tc_printf(t, "0x%08X: 0x%04X\n", addr + 2, val >> 16);
	}
	return true;
}
