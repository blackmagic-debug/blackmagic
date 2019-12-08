/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015, 2017, 2018  Uwe Bonnes
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
 * References:
 * RM0351 STM32L4x5 and STM32L4x6 advanced ARM®-based 32-bit MCUs Rev. 5
 * RM0394 STM32L43xxx STM32L44xxx STM32L45xxx STM32L46xxxx advanced
 *  ARM®-based 32-bit MCUs Rev.3
 * RM0432 STM32L4Rxxx and STM32L4Sxxx advanced Arm®-based 32-bit MCU. Rev 1
 * RM0440 STM32G4 Series advanced Arm®-based 32-bit MCU. Rev 1
 *
 *
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

static bool stm32l4_cmd_erase_mass(target *t, int argc, const char **argv);
static bool stm32l4_cmd_erase_bank1(target *t, int argc, const char **argv);
static bool stm32l4_cmd_erase_bank2(target *t, int argc, const char **argv);
static bool stm32l4_cmd_option(target *t, int argc, char *argv[]);

const struct command_s stm32l4_cmd_list[] = {
	{"erase_mass", (cmd_handler)stm32l4_cmd_erase_mass, "Erase entire flash memory"},
	{"erase_bank1", (cmd_handler)stm32l4_cmd_erase_bank1, "Erase entire bank1 flash memory"},
	{"erase_bank2", (cmd_handler)stm32l4_cmd_erase_bank2, "Erase entire bank2 flash memory"},
	{"option", (cmd_handler)stm32l4_cmd_option, "Manipulate option bytes"},
	{NULL, NULL, NULL}
};


static int stm32l4_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static int stm32l4_flash_write(struct target_flash *f,
                               target_addr dest, const void *src, size_t len);

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
#define FLASH_CR_OBL_LAUNCH	(1 << 27)
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

/* Used in STM32L47*/
#define OR_DUALBANK		(1 << 21)
/* Used in STM32L47R*/
#define OR_DB1M 		(1 << 21)
/* Used in STM32L47R and STM32G47 */
#define OR_DBANK 		(1 << 22)

#define DBGMCU_CR(dbgmcureg)	(dbgmcureg + 0x04)
#define DBGMCU_CR_DBG_SLEEP		(0x1U << 0U)
#define DBGMCU_CR_DBG_STOP		(0x1U << 1U)
#define DBGMCU_CR_DBG_STANDBY	(0x1U << 2U)

enum {
        STM32G0_DBGMCU_IDCODE_PHYS = 0x40015800,
        STM32L4_DBGMCU_IDCODE_PHYS = 0xe0042000,
};
#define FLASH_SIZE_REG  0x1FFF75E0

struct stm32l4_flash {
	struct target_flash f;
	uint32_t bank1_start;
};

enum ID_STM32L4 {
	ID_STM32L41  = 0x464u, /* RM0394, Rev.4 */
	ID_STM32L43  = 0x435u, /* RM0394, Rev.4 */
	ID_STM32L45  = 0x462u, /* RM0394, Rev.4 */
	ID_STM32L47  = 0x415u, /* RM0351, Rev.5 */
	ID_STM32L49  = 0x461u, /* RM0351, Rev.5 */
	ID_STM32L4R  = 0x470u, /* RM0432, Rev.5 */
	ID_STM32G03  = 0x466u, /* RM0444/454, Rev.2 */
	ID_STM32G07  = 0x460u, /* RM0444/454, Rev.2 */
	ID_STM32G43  = 0x468u, /* RM0440, Rev.1 */
	ID_STM32G47  = 0x469u, /* RM0440, Rev.1 */
};

enum FAM_STM32L4 {
	FAM_STM32L4xx = 1,
	FAM_STM32L4Rx = 2,
	FAM_STM32G0x = 3,
	FAM_STM32WBxx = 4,
	FAM_STM32G4xx = 5,
};

#define DUAL_BANK	0x80u
#define RAM_COUNT_MSK	0x07u

struct stm32l4_info {
	char designator[10];
	uint16_t sram1; /* Normal SRAM mapped at 0x20000000*/
	uint16_t sram2; /* SRAM at 0x10000000, mapped after sram1 (not L47) */
	uint16_t sram3; /* SRAM mapped after SRAM1 and SRAM2 */
	enum ID_STM32L4 idcode;
	enum FAM_STM32L4 family;
	uint8_t flags;          /* Only DUAL_BANK is evaluated for now.*/
};

struct stm32l4_info const L4info[] = {
	{
		.idcode = ID_STM32L41,
		.family = FAM_STM32L4xx,
		.designator = "STM32L41x",
		.sram1 = 32,
		.sram2 = 8,
		.flags = 2,
	},
	{
		.idcode = ID_STM32L43,
		.family = FAM_STM32L4xx,
		.designator = "STM32L43x",
		.sram1 = 48,
		.sram2 = 16,
		.flags = 2,
	},
	{
		.idcode = ID_STM32L45,
		.family = FAM_STM32L4xx,
		.designator = "STM32L45x",
		.sram1 = 128,
		.sram2 = 32,
		.flags = 2,
	},
	{
		.idcode = ID_STM32L47,
		.family = FAM_STM32L4xx,
		.designator = "STM32L47x",
		.sram1 = 96,
		.sram2 = 32,
		.flags = 2 | DUAL_BANK,
	},
	{
		.idcode = ID_STM32L49,
		.family = FAM_STM32L4xx,
		.designator = "STM32L49x",
		.sram1 = 256,
		.sram2 = 64,
		.flags = 2 | DUAL_BANK,
	},
	{
		.idcode = ID_STM32L4R,
		.family = FAM_STM32L4Rx,
		.designator = "STM32L4Rx",
		.sram1 = 192,
		.sram2 = 64,
		.sram3 = 384,
		.flags = 3 | DUAL_BANK,
	},
	{
		.idcode = ID_STM32G07,
		.family = FAM_STM32G0x,
		.designator = "STM32G07",
		.sram1 = 36,
		.flags = 1,
	},
	{
		.idcode = ID_STM32G03,
		.family = FAM_STM32G0x,
		.designator = "STM32G03",
		.sram1 = 8,
		.flags = 1,
	},
	{
		.idcode = ID_STM32G43,
		.family = FAM_STM32G4xx,
		.designator = "STM32G43",
		.sram1 = 22,
		.sram2 = 10,
	},
	{
		.idcode = ID_STM32G47,
		.family = FAM_STM32G4xx,
		.designator = "STM32G47",
		.sram1 = 96, /* SRAM1 and SRAM2 are mapped contigiously */
		.sram2 = 32, /* CCM SRAM is mapped as per SRAM2 on G4 */
		.flags = 2,
	},
	{
		/* Terminator */
		.idcode = 0,
	},
};


/* Retrieve chip basic information, just add to the vector to extend */
static struct stm32l4_info const * stm32l4_get_chip_info(uint32_t idcode) {
	struct stm32l4_info const *p = L4info;
	while (p->idcode && (p->idcode != idcode))
		p++;
	return p;
}

static void stm32l4_add_flash(target *t,
                              uint32_t addr, size_t length, size_t blocksize,
                              uint32_t bank1_start)
{
	struct stm32l4_flash *sf = calloc(1, sizeof(*sf));
	struct target_flash *f;

	if (!sf) {			/* calloc failed: heap exhaustion */
		DEBUG("calloc: failed in %s\n", __func__);
		return;
	}

	f = &sf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = blocksize;
	f->erase = stm32l4_flash_erase;
	f->write = stm32l4_flash_write;
	f->buf_size = 2048;
	f->erased = 0xff;
	sf->bank1_start = bank1_start;
	target_add_flash(t, f);
}

static bool stm32l4_attach(target *t)
{
	if (!cortexm_attach(t))
		return false;

	/* Retrive chip information, no need to check return */
	struct stm32l4_info const *chip = stm32l4_get_chip_info(t->idcode);


	uint32_t idcodereg = (chip->family == FAM_STM32G0x)
				     ? STM32G0_DBGMCU_IDCODE_PHYS
				     : STM32L4_DBGMCU_IDCODE_PHYS;


	/* Save DBGMCU_CR to restore it when detaching*/
	uint32_t dbgmcu_cr = target_mem_read32(t, DBGMCU_CR(idcodereg));
	t->target_storage = dbgmcu_cr;

	/* Enable debugging during all low power modes*/
	target_mem_write32(t, DBGMCU_CR(idcodereg), DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STANDBY | DBGMCU_CR_DBG_STOP);


	/* Free previously loaded memory map */
	target_mem_map_free(t);

	/* Add RAM to memory map */
	if (chip->family == FAM_STM32G0x) {
		target_add_ram(t, 0x20000000, chip->sram1 << 10);
	} else {
		target_add_ram(t, 0x10000000, chip->sram2 << 10);
		/* All L4 beside L47 alias SRAM2 after SRAM1.*/
		uint32_t ramsize = (t->idcode == ID_STM32L47)?
			chip->sram1 : (chip->sram1 + chip->sram2 + chip->sram3);
		target_add_ram(t, 0x20000000, ramsize << 10);
	}

	/* Add the flash to memory map. */
	uint32_t size = target_mem_read16(t, FLASH_SIZE_REG);
	uint32_t options =  target_mem_read32(t, FLASH_OPTR);

	if (chip->family == FAM_STM32L4Rx) {
		/* rm0432 Rev. 2 does not mention 1 MB devices or explain DB1M.*/
		if (options & OR_DBANK) {
			stm32l4_add_flash(t, 0x08000000, 0x00100000, 0x1000, 0x08100000);
			stm32l4_add_flash(t, 0x08100000, 0x00100000, 0x1000, 0x08100000);
		} else
			stm32l4_add_flash(t, 0x08000000, 0x00200000, 0x2000, -1);
	} else if (chip->family == FAM_STM32G4xx) {
		if (options & OR_DBANK) {
			uint32_t banksize = size << 9;
			stm32l4_add_flash(t, 0x08000000           , banksize, 0x0800, 0x08000000 + banksize);
			stm32l4_add_flash(t, 0x08000000 + banksize, banksize, 0x0800, 0x08000000 + banksize);
		} else {
			uint32_t banksize = size << 10;
			stm32l4_add_flash(t, 0x08000000           , banksize, 0x1000, -1);
		}
	} else if (chip->flags & DUAL_BANK) {
		if (options & OR_DUALBANK) {
			uint32_t banksize = size << 9;
			stm32l4_add_flash(t, 0x08000000           , banksize, 0x0800, 0x08000000 + banksize);
			stm32l4_add_flash(t, 0x08000000 + banksize, banksize, 0x0800, 0x08000000 + banksize);
		} else {
			uint32_t banksize = size << 10;
			stm32l4_add_flash(t, 0x08000000           , banksize, 0x0800, -1);
		}
	} else
		stm32l4_add_flash(t, 0x08000000, size << 10, 0x800, -1);

	/* Clear all errors in the status register. */
	target_mem_write32(t, FLASH_SR, target_mem_read32(t, FLASH_SR));

	return true;
}

static void stm32l4_detach(target *t)
{
	/*reverse all changes to DBGMCU_CR*/
	uint32_t idcodereg = STM32L4_DBGMCU_IDCODE_PHYS;
	if (t->idcode == ID_STM32G07)
		idcodereg = STM32G0_DBGMCU_IDCODE_PHYS;
	target_mem_write32(t, DBGMCU_CR(idcodereg), t->target_storage);
	cortexm_detach(t);
}

bool stm32l4_probe(target *t)
{
	uint32_t idcode_reg = STM32L4_DBGMCU_IDCODE_PHYS;
	ADIv5_AP_t *ap = cortexm_ap(t);
	if (ap->dp->idcode == 0x0BC11477)
		idcode_reg = STM32G0_DBGMCU_IDCODE_PHYS;
	uint32_t idcode = target_mem_read32(t, idcode_reg) & 0xfff;

	struct stm32l4_info const *chip = stm32l4_get_chip_info(idcode);

	if( !chip->idcode )	/* Not found */
		return false;

	t->idcode = idcode;
	t->driver = chip->designator;
	t->attach = stm32l4_attach;
	t->detach = stm32l4_detach;
	target_add_commands(t, stm32l4_cmd_list, chip->designator);
	return true;
}

static void stm32l4_flash_unlock(target *t)
{
	if (target_mem_read32(t, FLASH_CR) & FLASH_CR_LOCK) {
		/* Enable FPEC controller access */
		target_mem_write32(t, FLASH_KEYR, KEY1);
		target_mem_write32(t, FLASH_KEYR, KEY2);
	}
}

static int stm32l4_flash_erase(struct target_flash *f, target_addr addr, size_t len)
{
	target *t = f->t;
	uint16_t sr;
	uint32_t bank1_start = ((struct stm32l4_flash *)f)->bank1_start;
	uint32_t page;
	uint32_t blocksize = f->blocksize;

	stm32l4_flash_unlock(t);

	/* Read FLASH_SR to poll for BSY bit */
	while(target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
		if(target_check_error(t))
			return -1;
	/* Fixme: OPTVER always set after reset! Wrong option defaults?*/
	target_mem_write32(t, FLASH_SR, target_mem_read32(t, FLASH_SR));
	page = (addr - 0x08000000) / blocksize;
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
		if (len > blocksize)
			len  -= blocksize;
		else
			len = 0;
		addr += blocksize;
		page++;
	}

	/* Check for error */
	sr = target_mem_read32(t, FLASH_SR);
	if(sr & FLASH_SR_ERROR_MASK)
		return -1;

	return 0;
}

static int stm32l4_flash_write(struct target_flash *f,
                               target_addr dest, const void *src, size_t len)
{
	target *t = f->t;
	target_mem_write32(t, FLASH_CR, FLASH_CR_PG);
	target_mem_write(t, dest, src, len);
	/* Wait for completion or an error */
	uint32_t sr;
	do {
		sr = target_mem_read32(t, FLASH_SR);
		if (target_check_error(t)) {
			DEBUG("stm32l4 flash write: comm error\n");
			return -1;
		}
	} while (sr & FLASH_SR_BSY);

	if(sr & FLASH_SR_ERROR_MASK) {
		DEBUG("stm32l4 flash write error: sr 0x%" PRIu32 "\n", sr);
		return -1;
	}
	return 0;
}

static bool stm32l4_cmd_erase(target *t, uint32_t action)
{
	stm32l4_flash_unlock(t);
	/* Erase time is 25 ms. No need for a spinner.*/
	/* Flash erase action start instruction */
	target_mem_write32(t, FLASH_CR, action);
	target_mem_write32(t, FLASH_CR, action | FLASH_CR_STRT);

	/* Read FLASH_SR to poll for BSY bit */
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY) {
		if(target_check_error(t)) {
			return false;
		}
	}

	/* Check for error */
	uint16_t sr = target_mem_read32(t, FLASH_SR);
	if (sr & FLASH_SR_ERROR_MASK)
		return false;
	return true;
}

static bool stm32l4_cmd_erase_mass(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	return stm32l4_cmd_erase(t, FLASH_CR_MER1 | FLASH_CR_MER2);
}

static bool stm32l4_cmd_erase_bank1(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	return stm32l4_cmd_erase(t, FLASH_CR_MER1);
}

static bool stm32l4_cmd_erase_bank2(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	return stm32l4_cmd_erase(t, FLASH_CR_MER2);
}

static const uint8_t l4_i2offset[9] = {
	0x20, 0x24, 0x28, 0x2c, 0x30, 0x44, 0x48, 0x4c, 0x50
};

static const uint8_t g0_i2offset[7] = {
	0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38
};

static const uint8_t g4_i2offset[11] = {
	0x20, 0x24, 0x28, 0x2c, 0x30, 0x70, 0x44, 0x48, 0x4c, 0x50, 0x74
};

static bool stm32l4_option_write(
	target *t,const uint32_t *values, int len, const uint8_t *i2offset)
{
	tc_printf(t, "Device will lose connection. Rescan!\n");
	stm32l4_flash_unlock(t);
	target_mem_write32(t, FLASH_OPTKEYR, OPTKEY1);
	target_mem_write32(t, FLASH_OPTKEYR, OPTKEY2);
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
		if(target_check_error(t))
			return true;
	for (int i = 0; i < len; i++)
		target_mem_write32(t, FPEC_BASE + i2offset[i], values[i]);
	target_mem_write32(t, FLASH_CR, FLASH_CR_OPTSTRT);
	while (target_mem_read32(t, FLASH_SR) & FLASH_SR_BSY)
		if(target_check_error(t))
			return true;
	target_mem_write32(t, FLASH_CR, FLASH_CR_OBL_LAUNCH);
	while (target_mem_read32(t, FLASH_CR) & FLASH_CR_OBL_LAUNCH)
		if(target_check_error(t))
			return true;
	target_mem_write32(t, FLASH_CR, FLASH_CR_LOCK);
	return false;
}

/* Chip       L43X/mask  L43x/def   L47x/mask  L47x/def   G47x/mask  G47x/def
 *                                  L49x/mask  L49x/def   G48x/mask  G48x/def
 * Option
 * 0X1FFF7800 0x0f8f77ff 0xFFEFF8AA 0x0FDF77FF 0xFFEFF8AA 0x0FDF77FF 0xFFEFF8AA
 * 0X1FFF7808 0x0000FFFF 0xFFFFFFFF 0x0000FFFF 0xFFFFFFFF 0x00007FFF 0xFFFFFFFF
 * 0X1FFF7810 0x8000FFFF 0          0x8000FFFF 0          0x80007FFF 0x00FF0000
 * 0X1FFF7818 0x00FF00FF 0x000000ff 0x00FF00FF 0x000000ff 0x007F007F 0xFF00FFFF
 * 0X1FFF7820 0x00FF00FF 0x000000ff 0x00FF00FF 0x000000ff 0x007F007F 0xFF00FFFF
 * 0X1FFF7828 0          0          0          0          0x000100FF 0xFF00FF00
 * 0X1FFFF808 0          0          0x8000FFFF 0xffffffff 0x00007FFF 0xFFFFFFFF
 * 0X1FFFF810 0          0          0x8000FFFF 0          0x00007FFF 0xFFFFFFFF
 * 0X1FFFF818 0          0          0x00FF00FF 0          0x00FF00FF 0xFF00FFFF
 * 0X1FFFF820 0          0          0x00FF00FF 0x000000ff 0x00FF00FF 0xFF00FFFF
 * 0X1FFFF828 0          0          0          0          0x000000FF 0xFF00FF00
 */

static bool stm32l4_cmd_option(target *t, int argc, char *argv[])
{
	static const uint32_t g4_values[11] = {
		0xFFEFF8AA, 0xFFFFFFFF, 0x00FF0000, 0xFF00FFFF, 0xFF00FFFF, 0xFF00FF00,
		0xFFFFFFFF, 0xFFFFFFFF, 0xFF00FFFF, 0xFF00FFFF, 0xFF00FF00
	};

	uint32_t val;
	uint32_t values[11] = { 0xFFEFF8AA, 0xFFFFFFFF, 0, 0x000000ff,
							0x000000ff, 0xffffffff, 0, 0x000000ff, 0x000000ff };
	int len;
	bool res = false;

	const uint8_t *i2offset = l4_i2offset;
	if (t->idcode == ID_STM32L43) {/* L43x */
		len = 5;
	} else if (t->idcode == ID_STM32G07) {/* G07x */
		i2offset = g0_i2offset;
		len = 7;
	} else if (t->idcode == ID_STM32G47) {/* G47 */
		i2offset = g4_i2offset;
		len = 11;
		for (int i = 0; i < len; i++)
			values[i] = g4_values[i];
	} else {
		len = 9;
	}
	if ((argc == 2) && !strcmp(argv[1], "erase")) {
		res = stm32l4_option_write(t, values, len, i2offset);
	} else if ((argc >  2) && !strcmp(argv[1], "write")) {
		int i;
		for (i = 2; i < argc; i++)
			values[i - 2] = strtoul(argv[i], NULL, 0);
		for (i = i - 2; i < len; i++) {
			uint32_t addr = FPEC_BASE + i2offset[i];
			values[i] = target_mem_read32(t, addr);
		}
		if ((values[0] & 0xff) == 0xCC) {
			values[0]++;
			tc_printf(t, "Changing Level 2 request to Level 1!");
		}
		res = stm32l4_option_write(t, values, len, i2offset);
	} else {
		tc_printf(t, "usage: monitor option erase\n");
		tc_printf(t, "usage: monitor option write <value> ...\n");
	}
	if (res) {
		tc_printf(t, "Writing options failed!\n");
		return false;
	}
	for (int i = 0; i < len; i ++) {
		uint32_t addr = FPEC_BASE + i2offset[i];
		val = target_mem_read32(t, FPEC_BASE + i2offset[i]);
		tc_printf(t, "0x%08X: 0x%08X\n", addr, val);
	}
	return true;
}
