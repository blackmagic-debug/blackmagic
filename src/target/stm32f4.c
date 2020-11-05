/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2017, 2018  Uwe Bonnes
 *                           <bon@elektron.ikp.physik.tu-darmstadt.de>
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
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

static bool stm32f4_cmd_erase_mass(target *t, int argc, const char **argv);
static bool stm32f4_cmd_option(target *t, int argc, char *argv[]);
static bool stm32f4_cmd_psize(target *t, int argc, char *argv[]);

const struct command_s stm32f4_cmd_list[] = {
	{"erase_mass", (cmd_handler)stm32f4_cmd_erase_mass,
	 "Erase entire flash memory"},
	{"option", (cmd_handler)stm32f4_cmd_option, "Manipulate option bytes"},
	{"psize", (cmd_handler)stm32f4_cmd_psize,
	 "Configure flash write parallelism: (x8|x16|x32(default)|x64)"},
	{NULL, NULL, NULL}
};

static bool stm32f4_attach(target *t);
static int stm32f4_flash_erase(struct target_flash *f, target_addr addr,
							   size_t len);
static int stm32f4_flash_write(struct target_flash *f,
                               target_addr dest, const void *src, size_t len);

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
#define FLASH_CR_MER1		(1 << 15)
#define FLASH_CR_STRT		(1 << 16)
#define FLASH_CR_EOPIE		(1 << 24)
#define FLASH_CR_ERRIE		(1 << 25)
#define FLASH_CR_STRT		(1 << 16)
#define FLASH_CR_LOCK		(1 << 31)

#define FLASH_SR_BSY		(1 << 16)

#define FLASH_OPTCR_OPTLOCK	(1 << 0)
#define FLASH_OPTCR_OPTSTRT	(1 << 1)
#define FLASH_OPTCR_WDG_SW	(1 << 5)
#define FLASH_OPTCR_nDBANK	(1 << 29)
#define FLASH_OPTCR_DB1M	(1 << 30)

#define FLASH_OPTCR_PROT_MASK	0xff00
#define FLASH_OPTCR_PROT_L0  	0xaa00
#define FLASH_OPTCR_PROT_L1  	0xbb00

#define KEY1 0x45670123
#define KEY2 0xCDEF89AB

#define OPTKEY1 0x08192A3B
#define OPTKEY2 0x4C5D6E7F

#define SR_ERROR_MASK	0xF2
#define SR_EOP		0x01

#define F4_FLASHSIZE	0x1FFF7A22
#define F7_FLASHSIZE	0x1FF0F442
#define F72X_FLASHSIZE	0x1FF07A22
#define DBGMCU_IDCODE	0xE0042000
#define DBGMCU_CR		0xE0042004
#define DBG_SLEEP		(1 <<  0)

#define AXIM_BASE 0x8000000
#define ITCM_BASE 0x0200000

struct stm32f4_flash {
	struct target_flash f;
	enum align psize;
	uint8_t base_sector;
	uint8_t bank_split;
};

enum IDS_STM32F247 {
	ID_STM32F20X  = 0x411,
	ID_STM32F40X  = 0x413,
	ID_STM32F42X  = 0x419,
	ID_STM32F446  = 0x421,
	ID_STM32F401C = 0x423,
	ID_STM32F411  = 0x431,
	ID_STM32F401E = 0x433,
	ID_STM32F46X  = 0x434,
	ID_STM32F412  = 0x441,
	ID_STM32F74X  = 0x449,
	ID_STM32F76X  = 0x451,
	ID_STM32F72X  = 0x452,
	ID_STM32F410  = 0x458,
	ID_STM32F413  = 0x463
};

static void stm32f4_add_flash(target *t,
                              uint32_t addr, size_t length, size_t blocksize,
                              unsigned int base_sector, int split)
{
	if (length == 0) return;
	struct stm32f4_flash *sf = calloc(1, sizeof(*sf));
	struct target_flash *f;
	if (!sf) {			/* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	f = &sf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = blocksize;
	f->erase = stm32f4_flash_erase;
	f->write = stm32f4_flash_write;
	f->buf_size = 1024;
	f->erased = 0xff;
	sf->base_sector = base_sector;
	sf->bank_split = split;
	sf->psize = ALIGN_WORD;
	target_add_flash(t, f);
}

char *stm32f4_get_chip_name(uint32_t idcode)
{
	switch(idcode){
	case ID_STM32F40X: /* F40XxE/G */
		return "STM32F40x";
	case ID_STM32F42X: /* F42XxG/I */
		return "STM32F42x";
	case ID_STM32F46X: /* 469/479 xG/I*/
		return "STM32F47x";
	case ID_STM32F20X: /* F205 xB/C/E/G*/
		return "STM32F2";
	case ID_STM32F446: /* F446 xC/E*/
		return "STM32F446";
	case ID_STM32F401C: /* F401 B/C RM0368 Rev.3 */
		return "STM32F401C";
	case ID_STM32F411: /* F411 xC/E  RM0383 Rev.4 */
		return "STM32F411";
	case ID_STM32F412: /* F412 xG/I  RM0402 Rev.4, 256 kB Ram */
		return "STM32F412";
	case ID_STM32F401E: /* F401 D/E RM0368 Rev.3 */
		return "STM32F401E";
	case ID_STM32F413: /* F413xG/H  RM0430 Rev.2, 320 kB Ram, 1.5 MB flash. */
		return "STM32F413";
	case ID_STM32F74X: /* F74XxG/I RM0385 Rev.4 */
		return "STM32F74x";
	case ID_STM32F76X: /* F76XxE/G F77x RM0410 */
		return "STM32F76x";
	case ID_STM32F72X: /* F72/3xC/E RM0431 */
		return "STM32F72x";
	default:
		return NULL;
	}
}

static void stm32f7_detach(target *t)
{
	ADIv5_AP_t *ap = cortexm_ap(t);
	target_mem_write32(t, DBGMCU_CR, ap->ap_storage);
	cortexm_detach(t);
}

bool stm32f4_probe(target *t)
{
	if (t->idcode == ID_STM32F20X) {
		/* F405 revision A have a wrong IDCODE, use ARM_CPUID to make the
		 * distinction with F205. Revision is also wrong (0x2000 instead
		 * of 0x1000). See F40x/F41x errata. */
		if ((t->cpuid & 0xFFF0) == CORTEX_M4)
			t->idcode = ID_STM32F40X;
	}
	switch(t->idcode) {
	case ID_STM32F74X: /* F74x RM0385 Rev.4 */
	case ID_STM32F76X: /* F76x F77x RM0410 */
	case ID_STM32F72X: /* F72x F73x RM0431 */
		t->detach = stm32f7_detach;
		/* fall through */
	case ID_STM32F40X:
	case ID_STM32F42X: /* 427/437 */
	case ID_STM32F46X: /* 469/479 */
	case ID_STM32F20X: /* F205 */
	case ID_STM32F446: /* F446 */
	case ID_STM32F401C: /* F401 B/C RM0368 Rev.3 */
	case ID_STM32F411: /* F411     RM0383 Rev.4 */
	case ID_STM32F412: /* F412     RM0402 Rev.4, 256 kB Ram */
	case ID_STM32F401E: /* F401 D/E RM0368 Rev.3 */
	case ID_STM32F413: /* F413     RM0430 Rev.2, 320 kB Ram, 1.5 MB flash. */
		t->driver = stm32f4_get_chip_name(t->idcode);
		t->attach = stm32f4_attach;
		target_add_commands(t, stm32f4_cmd_list, t->driver);
		return true;
	default:
		return false;
	}
}

static bool stm32f4_attach(target *t)
{
	bool dual_bank = false;
	bool has_ccmram = false;
	bool is_f7  = false;
	bool large_sectors = false;
	uint16_t max_flashsize;

	if (!cortexm_attach(t))
		return false;

	switch(t->idcode) {
	case ID_STM32F40X:
		has_ccmram = true;
		max_flashsize = 1024;
		break;
	case ID_STM32F42X: /* 427/437 */
		has_ccmram = true;
		dual_bank  = true;
		max_flashsize = 2048;
		break;
	case ID_STM32F46X: /* 469/479 */
		has_ccmram = true;
		dual_bank  = true;
		max_flashsize = 512;
		break;
	case ID_STM32F401C: /* F401 B/C RM0368 Rev.3 */
		max_flashsize = 256;
		break;
	case ID_STM32F401E: /* F401 D/E RM0368 Rev.3 */
	case ID_STM32F411:  /* F411      RM0383 Rev.4 */
	case ID_STM32F446:  /* F446 */
		max_flashsize = 512;
		break;
	case ID_STM32F20X:  /* F205xB/C/E/G */
	case ID_STM32F412:  /* F412xE/G  RM0402 Rev.4, 256 kB Ram */
		max_flashsize = 1024;
		break;
	case ID_STM32F413: /* F413     RM0430 Rev.2, 320 kB Ram, 1.5 MB flash. */
		max_flashsize = 1536;
		break;
	case ID_STM32F74X: /* F74x RM0385 Rev.4 */
		is_f7 = true;
		large_sectors = true;
		max_flashsize = 1024;
		break;
	case ID_STM32F76X: /* F76x F77x RM0410 */
		is_f7 = true;
		dual_bank = true;
		large_sectors = true;
		max_flashsize = 2048;
		break;
	case ID_STM32F72X: /* F72x F73x RM0431 */
		is_f7 = true;
		max_flashsize = 512;
		break;
	default:
		return false;
	}
	bool use_dual_bank = false;
	target_mem_map_free(t);
	if (is_f7) {
		target_add_ram(t, 0x00000000, 0x4000);  /* 16 k ITCM Ram */
		target_add_ram(t, 0x20000000, 0x20000); /* 128 k DTCM Ram */
		target_add_ram(t, 0x20020000, 0x60000); /* 384 k Ram */
		if (dual_bank) {
			uint32_t optcr;
			optcr = target_mem_read32(t, FLASH_OPTCR);
			use_dual_bank =  !(optcr & FLASH_OPTCR_nDBANK);
		}
	} else {
		if (has_ccmram)
			target_add_ram(t, 0x10000000, 0x10000); /* 64 k CCM Ram*/
		target_add_ram(t, 0x20000000, 0x50000);     /* 320 k RAM */
		if (dual_bank) {
			use_dual_bank = true;
			if (max_flashsize < 0x800) {
				/* Check Dual-bank on 1 Mbyte Flash memory devices*/
				uint32_t optcr;
				optcr = target_mem_read32(t, FLASH_OPTCR);
				use_dual_bank = !(optcr & FLASH_OPTCR_DB1M);
			}
		}
	}
	int split = 0;
	uint32_t banksize;
	if (use_dual_bank) {
		banksize = max_flashsize << 9; /* flash split on two sectors. */
		split = (max_flashsize == 0x400) ? 8 : 12;
	}
	else
		banksize = max_flashsize << 10;
	if (large_sectors) {
		uint32_t remains = banksize - 0x40000;
		/* 256 k in small sectors.*/
		stm32f4_add_flash(t, ITCM_BASE, 0x20000,  0x8000, 0, split);
		stm32f4_add_flash(t, 0x0220000, 0x20000, 0x20000, 4, split);
		stm32f4_add_flash(t, 0x0240000, remains, 0x40000, 5, split);
		stm32f4_add_flash(t, AXIM_BASE, 0x20000,  0x8000, 0, split);
		stm32f4_add_flash(t, 0x8020000, 0x20000, 0x20000, 4, split);
		stm32f4_add_flash(t, 0x8040000, remains, 0x40000, 5, split);
	} else {
		uint32_t remains = 0;
		if (banksize > 0x20000)
			remains = banksize - 0x20000; /* 128 k in small sectors.*/
		if (is_f7) {
			stm32f4_add_flash(t, ITCM_BASE, 0x10000,  0x4000,  0, split);
		}
		stm32f4_add_flash(t, 0x8000000, 0x10000,  0x4000,  0, split);
		if (banksize > 0x10000) {
			stm32f4_add_flash(t, 0x8010000, 0x10000, 0x10000,  4, split);
			stm32f4_add_flash(t, 0x8020000, remains, 0x20000,  5, split);
		}
		if (use_dual_bank) {
			if (is_f7) {
				uint32_t bk1 = ITCM_BASE + banksize;
				stm32f4_add_flash(t, bk1          , 0x10000,  0x4000, 0, split);
				stm32f4_add_flash(t, bk1 + 0x10000, 0x10000, 0x10000, 4, split);
				stm32f4_add_flash(t, bk1 + 0x20000, remains, 0x20000, 5, split);
			}
			uint32_t bk2 = 0x8000000 + banksize;
			stm32f4_add_flash(t, bk2          , 0x10000,  0x4000, 16, split);
			stm32f4_add_flash(t, bk2 + 0x10000, 0x10000, 0x10000, 20, split);
			stm32f4_add_flash(t, bk2 + 0x20000, remains, 0x20000, 21, split);
		}
	}
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

static int stm32f4_flash_erase(struct target_flash *f, target_addr addr,
							   size_t len)
{
	target *t = f->t;
	struct stm32f4_flash *sf = (struct stm32f4_flash *)f;
	uint32_t sr;
	/* No address translation is needed here, as we erase by sector number */
	uint8_t sector = sf->base_sector + (addr - f->start)/f->blocksize;
	stm32f4_flash_unlock(t);

	enum align psize = ALIGN_WORD;
	for (struct target_flash *f = t->flash; f; f = f->next) {
		if (f->write == stm32f4_flash_write) {
			psize = ((struct stm32f4_flash *)f)->psize;
		}
	}
	while(len) {
		uint32_t cr = FLASH_CR_EOPIE | FLASH_CR_ERRIE | FLASH_CR_SER |
			(psize * FLASH_CR_PSIZE16) | (sector << 3);
		/* Flash page erase instruction */
		target_mem_write32(t, FLASH_CR, cr);
		/* write address to FMA */
		target_mem_write32(t, FLASH_CR, cr | FLASH_CR_STRT);

		/* Read FLASH_SR to poll for BSY bit */
		while(target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
			if(target_check_error(t)) {
				DEBUG_WARN("stm32f4 flash erase: comm error\n");
				return -1;
			}
		if (len > f->blocksize)
			len -= f->blocksize;
		else
			len = 0;
		sector++;
		if ((sf->bank_split) && (sector == sf->bank_split))
			sector = 16;
	}

	/* Check for error */
	sr = target_mem_read32(t, FLASH_SR);
	if(sr & SR_ERROR_MASK) {
		DEBUG_WARN("stm32f4 flash erase: sr error: 0x%" PRIu32 "\n", sr);
		return -1;
	}
	return 0;
}

static int stm32f4_flash_write(struct target_flash *f,
                               target_addr dest, const void *src, size_t len)
{
	/* Translate ITCM addresses to AXIM */
	if ((dest >= ITCM_BASE) && (dest < AXIM_BASE)) {
		dest = AXIM_BASE + (dest - ITCM_BASE);
	}
	target *t = f->t;
	uint32_t sr;
	enum align psize = ((struct stm32f4_flash *)f)->psize;
	target_mem_write32(t, FLASH_CR,
					   (psize * FLASH_CR_PSIZE16) | FLASH_CR_PG);
	cortexm_mem_write_sized(t, dest, src, len, psize);
	/* Read FLASH_SR to poll for BSY bit */
	/* Wait for completion or an error */
	do {
		sr = target_mem_read32(t, FLASH_SR);
		if(target_check_error(t)) {
			DEBUG_WARN("stm32f4 flash write: comm error\n");
			return -1;
		}
	} while (sr & FLASH_SR_BSY);

	if (sr & SR_ERROR_MASK) {
		DEBUG_WARN("stm32f4 flash write error 0x%" PRIx32 "\n", sr);
			return -1;
	}
	return 0;
}

static bool stm32f4_cmd_erase_mass(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	const char spinner[] = "|/-\\";
	int spinindex = 0;
	struct target_flash *f = t->flash;
	struct stm32f4_flash *sf = (struct stm32f4_flash *)f;

	tc_printf(t, "Erasing flash... This may take a few seconds.  ");
	stm32f4_flash_unlock(t);

	/* Flash mass erase start instruction */
	uint32_t cr =  FLASH_CR_MER;
	if (sf->bank_split)
		cr |=  FLASH_CR_MER1;
	target_mem_write32(t, FLASH_CR, cr);
	target_mem_write32(t, FLASH_CR, cr | FLASH_CR_STRT);

	/* Read FLASH_SR to poll for BSY bit */
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY) {
		tc_printf(t, "\b%c", spinner[spinindex++ % 4]);
		if(target_check_error(t)) {
			tc_printf(t, "\n");
			return false;
		}
	}
	tc_printf(t, "\n");

	/* Check for error */
	uint32_t sr = target_mem_read32(t, FLASH_SR);
	if ((sr & SR_ERROR_MASK) || !(sr & SR_EOP))
		return false;

	return true;
}

/* Dev   | DOC  |Rev|ID |OPTCR    |OPTCR   |OPTCR1   |OPTCR1 | OPTCR2
                    |hex|default  |reserved|default  |resvd  | default|resvd
 * F20x  |pm0059|5.1|411|0FFFAAED |F0000010|
 * F40x  |rm0090|11 |413|0FFFAAED |F0000010|
 * F42x  |rm0090|11 |419|0FFFAAED |30000000|0FFF0000 |F000FFFF
 * F446  |rm0390| 2 |421|0FFFAAED |7F000010|
 * F401BC|rm0368| 3 |423|0FFFAAED |7FC00010|
 * F411  |rm0383| 2 |431|0FFFAAED |7F000010|
 * F401DE|rm0368| 3 |433|0FFFAAED |7F000010|
 * F46x  |rm0386| 2 |434|0FFFAAED |30000000|0FFF0000 |F000FFFF
 * F412  |rm0402| 4 |441|0FFFAAED*|70000010|
 * F74x  |rm0385| 4 |449|C0FFAAFD |3F000000|00400080*|00000000
 * F76x  |rm0410| 2 |451|FFFFAAFD*|00000000|00400080*|00000000
 * F72x  |rm0431| 1 |452|C0FFAAFD |3F000000|00400080*|00000000|00000000|800000FF
 * F410  |rm0401| 2 |458|0FFFAAED*|7FE00010|
 * F413  |rm0430| 2 |463|7FFFAAED*|00000010|
 *
 * * Documentation for F7 with OPTCR1 default = 0fff7f0080 seems wrong!
 * * Documentation for F412 with OPTCR default = 0ffffffed seems wrong!
 * * Documentation for F413 with OPTCR default = 0ffffffed seems wrong!
 */

static bool optcr_mask(target *t, uint32_t *val)
{
	switch (t->idcode) {
	case ID_STM32F20X:
	case ID_STM32F40X:
		val[0] &= ~0xF0000010;
		break;
	case ID_STM32F46X:
	case ID_STM32F42X:
		val[0] &= ~0x30000000;
		val[1] &=  0x0fff0000;
		break;
	case ID_STM32F401C:
		val[0] &= ~0x7FC00010;
		break;
	case ID_STM32F446:
	case ID_STM32F411:
	case ID_STM32F401E:
		val[0] &= ~0x7F000010;
		break;
	case ID_STM32F410:
		val[0] &= ~0x7FE00010;
		break;
	case ID_STM32F412:
		val[0] &= ~0x70000010;
		break;
	case ID_STM32F413:
		val[0] &= ~0x00000010;
		break;
	case ID_STM32F72X:
		val[2] &=  ~0x800000ff;
		/* Fall through*/
	case ID_STM32F74X:
		val[0] &= ~0x3F000000;
		break;
	case ID_STM32F76X:
		break;
	default:
		return false;
	}
	return true;
}

static bool stm32f4_option_write(target *t, uint32_t *val, int count)
{
	val[0] &= ~(FLASH_OPTCR_OPTSTRT | FLASH_OPTCR_OPTLOCK);
	uint32_t optcr = target_mem_read32(t, FLASH_OPTCR);
	/* Check if watchdog and read protection is active.
	 * When both are active, watchdog will trigger when erasing
	 * to get back to level 0 protection and operation aborts!
	 */
	if (!(optcr & FLASH_OPTCR_WDG_SW) &&
		((optcr & FLASH_OPTCR_PROT_MASK) != FLASH_OPTCR_PROT_L0) &&
		((val[0] & FLASH_OPTCR_PROT_MASK) != FLASH_OPTCR_PROT_L1)) {
		val[0] &= ~FLASH_OPTCR_PROT_MASK;
		val[0] |=  FLASH_OPTCR_PROT_L1;
		tc_printf(t, "Keeping L1 protection while HW Watchdog fuse is set!\n");
	}
	target_mem_write32(t, FLASH_OPTKEYR, OPTKEY1);
	target_mem_write32(t, FLASH_OPTKEYR, OPTKEY2);
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
		if(target_check_error(t))
			return -1;

	/* WRITE option bytes instruction */
	if (((t->idcode == ID_STM32F42X) || (t->idcode == ID_STM32F46X) ||
		 (t->idcode == ID_STM32F72X) || (t->idcode == ID_STM32F74X) ||
		 (t->idcode == ID_STM32F76X)) && (count > 1))
	    /* Checkme: Do we need to read old value and then set it? */
		target_mem_write32(t, FLASH_OPTCR + 4, val[1]);
	if ((t->idcode == ID_STM32F72X) && (count > 2))
			target_mem_write32(t, FLASH_OPTCR + 8, val[2]);

	target_mem_write32(t, FLASH_OPTCR, val[0]);
	target_mem_write32(t, FLASH_OPTCR, val[0] | FLASH_OPTCR_OPTSTRT);
	const char spinner[] = "|/-\\";
	int spinindex = 0;
	tc_printf(t, "Erasing flash... This may take a few seconds.  ");
	/* Read FLASH_SR to poll for BSY bit */
	while(target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY) {
		platform_delay(100);
		tc_printf(t, "\b%c", spinner[spinindex++ % 4]);
		if(target_check_error(t)) {
			tc_printf(t, " failed\n");
			return false;
		}
	}
	tc_printf(t, "\n");
	target_mem_write32(t, FLASH_OPTCR, FLASH_OPTCR_OPTLOCK);
	/* Reset target to reload option bits.*/
	target_reset(t);
	return true;
}

static bool stm32f4_option_write_default(target *t)
{
	uint32_t val[3];
	switch (t->idcode) {
	case ID_STM32F42X:
	case ID_STM32F46X:
		val[0] = 0x0FFFAAED;
		val[1] = 0x0FFF0000;
		return stm32f4_option_write(t, val, 2);
	case ID_STM32F72X:
		val[0] = 0xC0FFAAFD;
		val[1] = 0x00400080;
		val[2] = 0;
		return stm32f4_option_write(t, val, 3);
	case ID_STM32F74X:
		val[0] = 0xC0FFAAFD;
		val[1] = 0x00400080;
		return stm32f4_option_write(t, val, 2);
	case ID_STM32F76X:
		val[0] = 0xFFFFAAFD;
		val[1] = 0x00400080;
		return stm32f4_option_write(t, val, 2);
	case ID_STM32F413:
		val[0] = 0x7FFFAAFD;
		return stm32f4_option_write(t, val, 1);
	default:
		val[0] = 0x0FFFAAED;
		return stm32f4_option_write(t, val, 1);
	}
}

static bool stm32f4_cmd_option(target *t, int argc, char *argv[])
{
	uint32_t val[3];
	int count = 0, readcount = 1;

	switch (t->idcode) {
	case ID_STM32F72X: /* STM32F72|3 */
		readcount++;
		/* fall through.*/
	case ID_STM32F74X:
	case ID_STM32F76X:
		readcount++;
		break;
	case ID_STM32F42X:
	case ID_STM32F46X:
		readcount++;
	}

	if ((argc == 2) && !strncasecmp(argv[1], "erase", 1)) {
		stm32f4_option_write_default(t);
	}
	else if ((argc > 2) && !strncasecmp(argv[1], "write", 1)) {
		val[0] = strtoul(argv[2], NULL, 0);
		count++;
		if (argc > 3) {
			val[1] = strtoul(argv[3], NULL, 0);
			count ++;
		}
		if (argc > 4) {
			val[2] = strtoul(argv[4], NULL, 0);
			count ++;
		}
		if (optcr_mask(t, val))
			stm32f4_option_write(t, val, count);
		else
			tc_printf(t, "error\n");
	} else {
		tc_printf(t, "usage: monitor option erase\n");
		tc_printf(t, "usage: monitor option write <OPTCR>");
		if (readcount > 1)
			tc_printf(t, " <OPTCR1>");
		if (readcount > 2)
			tc_printf(t, " <OPTCR2>");
		tc_printf(t, "\n");
	}

	val[0]  = target_mem_read32(t, FLASH_OPTCR);
	if (readcount > 1)
		val[1] = target_mem_read32(t, FLASH_OPTCR + 4);
	if (readcount > 2)
		val[2] = target_mem_read32(t, FLASH_OPTCR + 8);
	optcr_mask(t, val);
	tc_printf(t, "OPTCR: 0x%08X ", val[0]);
	if (readcount > 1)
		tc_printf(t, "OPTCR1: 0x%08lx ", val[1]);
	if (readcount > 2)
		tc_printf(t, "OPTCR2: 0x%08lx" , val[2]);
	tc_printf(t, "\n");
	return true;
}

static bool stm32f4_cmd_psize(target *t, int argc, char *argv[])
{
	if (argc == 1) {
		enum align psize = ALIGN_WORD;
		for (struct target_flash *f = t->flash; f; f = f->next) {
			if (f->write == stm32f4_flash_write) {
				psize = ((struct stm32f4_flash *)f)->psize;
			}
		}
		tc_printf(t, "Flash write parallelism: %s\n",
		          psize == ALIGN_DWORD ? "x64" :
		          psize == ALIGN_WORD ? "x32" :
				  psize == ALIGN_HALFWORD ? "x16" : "x8");
	} else {
		enum align psize;
		if (!strcmp(argv[1], "x8")) {
			psize = ALIGN_BYTE;
		} else if (!strcmp(argv[1], "x16")) {
			psize = ALIGN_HALFWORD;
		} else if (!strcmp(argv[1], "x32")) {
			psize = ALIGN_WORD;
		} else if (!strcmp(argv[1], "x64")) {
			psize = ALIGN_DWORD;
		} else {
			tc_printf(t, "usage: monitor psize (x8|x16|x32|x32)\n");
			return false;
		}
		for (struct target_flash *f = t->flash; f; f = f->next) {
			if (f->write == stm32f4_flash_write) {
				((struct stm32f4_flash *)f)->psize = psize;
			}
		}
	}
	return true;
}
