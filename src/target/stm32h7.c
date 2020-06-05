/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2017-2020 Uwe Bonnes bon@elektron.ikp.physik.tu-darmstadt.de
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

/* This file implements STM32H7 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * Refereces:
 * ST doc - RM0433
 *   Reference manual - STM32H7x3 advanced ARM®-based 32-bit MCUs Rev.3
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

static bool stm32h7_cmd_erase_mass(target *t, int argc, const char **argv);
/* static bool stm32h7_cmd_option(target *t, int argc, char *argv[]); */
static bool stm32h7_uid(target *t, int argc, const char **argv);
static bool stm32h7_crc(target *t, int argc, const char **argv);
static bool stm32h7_cmd_psize(target *t, int argc, char *argv[]);
static bool stm32h7_cmd_rev(target *t, int argc, const char **argv);

const struct command_s stm32h7_cmd_list[] = {
	{"erase_mass", (cmd_handler)stm32h7_cmd_erase_mass,
	 "Erase entire flash memory"},
/*	{"option", (cmd_handler)stm32h7_cmd_option,
	"Manipulate option bytes"},*/
	{"psize", (cmd_handler)stm32h7_cmd_psize,
	 "Configure flash write parallelism: (x8|x16|x32|x64(default))"},
	{"uid", (cmd_handler)stm32h7_uid, "Print unique device ID"},
	{"crc", (cmd_handler)stm32h7_crc, "Print CRC of both banks"},
	{"revision", (cmd_handler)stm32h7_cmd_rev,
	 "Returns the Device ID and Revision"},
	{NULL, NULL, NULL}
};


static int stm32h7_flash_erase(struct target_flash *f, target_addr addr,
							   size_t len);
static int stm32h7_flash_write(struct target_flash *f,
                               target_addr dest, const void *src, size_t len);

static const char stm32h74_driver_str[] = "STM32H74x";

enum stm32h7_regs
{
	FLASH_ACR		= 0x00,
	FLASH_KEYR		= 0x04,
	FLASH_OPTKEYR	= 0x08,
	FLASH_CR		= 0x0c,
	FLASH_SR		= 0x10,
	FLASH_CCR		= 0x14,
	FLASH_OPTCR		= 0x18,
	FLASH_OPTSR_CUR = 0x1C,
	FLASH_OPTSR     = 0x20,
	FLASH_CRCCR		= 0x50,
	FLASH_CRCDATA	= 0x5C,
};

/* Flash Program and Erase Controller Register Map */
#define H7_IWDG_BASE        0x58004c00
#define FPEC1_BASE			0x52002000
#define FPEC2_BASE			0x52002100
#define FLASH_SR_BSY		(1 <<  0)
#define FLASH_SR_WBNE		(1 <<  1)
#define FLASH_SR_QW			(1 <<  2)
#define FLASH_SR_CRC_BUSY	(1 <<  3)
#define FLASH_SR_EOP		(1 << 16)
#define FLASH_SR_WRPERR		(1 << 17)
#define FLASH_SR_PGSERR		(1 << 18)
#define FLASH_SR_STRBERR	(1 << 19)
#define FLASH_SR_INCERR		(1 << 21)
#define FLASH_SR_OPERR		(1 << 22)
#define FLASH_SR_OPERR		(1 << 22)
#define FLASH_SR_RDPERR		(1 << 23)
#define FLASH_SR_RDSERR		(1 << 24)
#define FLASH_SR_SNECCERR	(1 << 25)
#define FLASH_SR_DBERRERR	(1 << 26)
#define FLASH_SR_ERROR_READ	(FLASH_SR_RDPERR   | FLASH_SR_RDSERR  |	\
							 FLASH_SR_SNECCERR |FLASH_SR_DBERRERR)
#define FLASH_SR_ERROR_MASK	(										\
		FLASH_SR_WRPERR  | FLASH_SR_PGSERR  | FLASH_SR_STRBERR |	\
		FLASH_SR_INCERR  | FLASH_SR_OPERR    | FLASH_SR_ERROR_READ)
#define FLASH_CR_LOCK		(1 << 0)
#define FLASH_CR_PG			(1 << 1)
#define FLASH_CR_SER		(1 << 2)
#define FLASH_CR_BER		(1 << 3)
#define FLASH_CR_PSIZE8		(0 << 4)
#define FLASH_CR_PSIZE16	(1 << 4)
#define FLASH_CR_PSIZE32	(2 << 4)
#define FLASH_CR_PSIZE64	(3 << 4)
#define FLASH_CR_FW			(1 << 6)
#define FLASH_CR_START		(1 << 7)
#define FLASH_CR_SNB_1		(1 << 8)
#define FLASH_CR_SNB		(3 << 8)
#define FLASH_CR_CRC_EN		(1 << 15)

#define FLASH_OPTCR_OPTLOCK	(1 << 0)
#define FLASH_OPTCR_OPTSTRT	(1 << 1)

#define FLASH_OPTSR_IWDG1_SW	(1 <<  4)

#define FLASH_CRCCR_ALL_BANK	(1 <<  7)
#define FLASH_CRCCR_START_CRC	(1 << 16)
#define FLASH_CRCCR_CLEAN_CRC	(1 << 17)
#define FLASH_CRCCR_CRC_BURST_3	(3 << 20)

#define KEY1 0x45670123
#define KEY2 0xCDEF89AB

#define OPTKEY1 0x08192A3B
#define OPTKEY2 0x4C5D6E7F

#define DBGMCU_IDCODE	0x5c001000
/* Access from processor address space.
 * Access via the APB-D is at 0xe00e1000 */
#define DBGMCU_IDC		(DBGMCU_IDCODE + 0)
#define DBGMCU_CR		(DBGMCU_IDCODE + 4)
#define DBGSLEEP_D1		(1 << 0)
#define DBGSTOP_D1		(1 << 1)
#define DBGSTBY_D1		(1 << 2)
#define DBGSTOP_D3		(1 << 7)
#define DBGSTBY_D3		(1 << 8)
#define D1DBGCKEN		(1 << 21)
#define D3DBGCKEN		(1 << 22)


#define FLASH_SIZE_REG 	0x1ff1e880

#define BANK1_START 		0x08000000
#define NUM_SECTOR_PER_BANK 8
#define FLASH_SECTOR_SIZE 	0x20000
#define BANK2_START         0x08100000
enum ID_STM32H7 {
	ID_STM32H74x  = 0x450,
};

struct stm32h7_flash {
	struct target_flash f;
	enum align psize;
	uint32_t regbase;
};

static void stm32h7_add_flash(target *t,
                              uint32_t addr, size_t length, size_t blocksize)
{
	struct stm32h7_flash *sf = calloc(1, sizeof(*sf));
	struct target_flash *f;

	if (!sf) {			/* calloc failed: heap exhaustion */
		DEBUG_WARN("calloc: failed in %s\n", __func__);
		return;
	}

	f = &sf->f;
	f->start = addr;
	f->length = length;
	f->blocksize = blocksize;
	f->erase = stm32h7_flash_erase;
	f->write = stm32h7_flash_write;
	f->buf_size = 2048;
	f->erased = 0xff;
	sf->regbase = FPEC1_BASE;
	if (addr >= BANK2_START)
		sf->regbase = FPEC2_BASE;
	sf->psize = ALIGN_DWORD;
	target_add_flash(t, f);
}

static bool stm32h7_attach(target *t)
{
	if (!cortexm_attach(t))
		return false;
	/* RM0433 Rev 4 is not really clear, what bits are needed.
	 * Set all possible relevant bits for now. */
	uint32_t dbgmcu_cr = target_mem_read32(t, DBGMCU_CR);
	t->target_storage = dbgmcu_cr;
	target_mem_write32(t, DBGMCU_CR, DBGSLEEP_D1 | D1DBGCKEN);
	/* If IWDG runs as HARDWARE watchdog (44.3.4) erase
	 * will be aborted by the Watchdog and erase fails!
	 * Setting IWDG_KR to 0xaaaa does not seem to help!*/
	uint32_t optsr = target_mem_read32(t, FPEC1_BASE + FLASH_OPTSR);
	if (!(optsr & FLASH_OPTSR_IWDG1_SW))
		tc_printf(t, "Hardware IWDG running. Expect failure. Set IWDG1_SW!");

	/* Free previously loaded memory map */
	target_mem_map_free(t);

	/* Add RAM to memory map */
	target_add_ram(t, 0x00000000, 0x10000); /* ITCM Ram,  64 k */
	target_add_ram(t, 0x20000000, 0x20000); /* DTCM Ram, 128 k */
	target_add_ram(t, 0x24000000, 0x80000); /* AXI Ram,  512 k */
	target_add_ram(t, 0x30000000, 0x20000); /* AHB SRAM1, 128 k */
	target_add_ram(t, 0x32000000, 0x20000); /* AHB SRAM2, 128 k */
	target_add_ram(t, 0x34000000, 0x08000); /* AHB SRAM3,  32 k */
	target_add_ram(t, 0x38000000, 0x01000); /* AHB SRAM4,  32 k */

	/* Add the flash to memory map. */
	stm32h7_add_flash(t, 0x8000000, 0x100000, FLASH_SECTOR_SIZE);
	stm32h7_add_flash(t, 0x8100000, 0x100000, FLASH_SECTOR_SIZE);
	return true;
}

static void stm32h7_detach(target *t)
{
	target_mem_write32(t, DBGMCU_CR, t->target_storage);
	cortexm_detach(t);
}

bool stm32h7_probe(target *t)
{
	ADIv5_AP_t *ap = cortexm_ap(t);
	uint32_t idcode = (ap->dp->targetid >> 16) & 0xfff;
	if (idcode == ID_STM32H74x) {
		t->idcode = idcode;
		t->driver = stm32h74_driver_str;
		t->attach = stm32h7_attach;
		t->detach = stm32h7_detach;
		target_add_commands(t, stm32h7_cmd_list, stm32h74_driver_str);
		return true;
	}
	return false;
}

static bool stm32h7_flash_unlock(target *t, uint32_t addr)
{
	uint32_t regbase = FPEC1_BASE;
	if (addr >= BANK2_START) {
		regbase = FPEC2_BASE;
	}

	while(target_mem_read32(t, regbase + FLASH_SR) & FLASH_SR_BSY) {
		if(target_check_error(t))
			return false;
	}
	uint32_t sr = target_mem_read32(t, regbase + FLASH_SR);
	if (sr & FLASH_SR_ERROR_MASK) {
		tc_printf(t, "Error 0x%08lx", sr & FLASH_SR_ERROR_MASK);
		target_mem_write32(t, regbase + FLASH_CCR, sr & FLASH_SR_ERROR_MASK);
		return false;
	}
	if (target_mem_read32(t, regbase + FLASH_CR) & FLASH_CR_LOCK) {
		/* Enable FLASH controller access */
		target_mem_write32(t, regbase + FLASH_KEYR, KEY1);
		target_mem_write32(t, regbase + FLASH_KEYR, KEY2);
	}
	if (target_mem_read32(t, regbase + FLASH_CR) & FLASH_CR_LOCK)
		return false;
	else
		return true;
}

static int stm32h7_flash_erase(struct target_flash *f, target_addr addr,
							   size_t len)
{
	target *t = f->t;
	struct stm32h7_flash *sf = (struct stm32h7_flash *)f;
	if (stm32h7_flash_unlock(t, addr) == false)
		return -1;
	/* We come out of reset with HSI 64 MHz. Adapt FLASH_ACR.*/
	target_mem_write32(t, sf->regbase + FLASH_ACR, 0);
	addr &= (NUM_SECTOR_PER_BANK * FLASH_SECTOR_SIZE) - 1;
	int start_sector =  addr / FLASH_SECTOR_SIZE;
	int end_sector   = (addr + len - 1) / FLASH_SECTOR_SIZE;

	enum align psize = ((struct stm32h7_flash *)f)->psize;
	uint32_t sr;
	while (start_sector <= end_sector) {
		uint32_t cr = (psize * FLASH_CR_PSIZE16) | FLASH_CR_SER |
			(start_sector * FLASH_CR_SNB_1);
		target_mem_write32(t, sf->regbase + FLASH_CR, cr);
		cr |= FLASH_CR_START;
		target_mem_write32(t, sf->regbase + FLASH_CR, cr);
		DEBUG_INFO(" started cr %08" PRIx32 " sr %08" PRIx32 "\n",
				   target_mem_read32(t, sf->regbase + FLASH_CR),
				   target_mem_read32(t, sf->regbase + FLASH_SR));
		do {
			sr = target_mem_read32(t, sf->regbase + FLASH_SR);
			if (target_check_error(t)) {
				DEBUG_WARN("stm32h7_flash_erase: comm failed\n");
				return -1;
			}
//			target_mem_write32(t, H7_IWDG_BASE, 0x0000aaaa);
		}while (sr & (FLASH_SR_QW | FLASH_SR_BSY));
		if (sr & FLASH_SR_ERROR_MASK) {
			DEBUG_WARN("stm32h7_flash_erase: error, sr: %08" PRIx32 "\n", sr);
			return -1;
		}
		start_sector++;
	}
	return 0;
}

static int stm32h7_flash_write(struct target_flash *f, target_addr dest,
                               const void *src, size_t len)
{
	target *t = f->t;
	struct stm32h7_flash *sf = (struct stm32h7_flash *)f;
	enum align psize = sf->psize;
	if (stm32h7_flash_unlock(t, dest) == false)
		return -1;
	uint32_t cr = psize * FLASH_CR_PSIZE16;
	target_mem_write32(t, sf->regbase + FLASH_CR, cr);
	cr |= FLASH_CR_PG;
	target_mem_write32(t, sf->regbase + FLASH_CR, cr);
	/* does H7 stall?*/
	uint32_t sr_reg = sf->regbase + FLASH_SR;
	uint32_t sr;
	target_mem_write(t, dest, src, len);
	while ((sr = target_mem_read32(t, sr_reg)) & FLASH_SR_BSY) {
		if(target_check_error(t)) {
			DEBUG_WARN("stm32h7_flash_write: BSY comm failed\n");
			return -1;
		}
	}
	if (sr & FLASH_SR_ERROR_MASK) {
		DEBUG_WARN("stm32h7_flash_write: error sr %08" PRIx32 "\n", sr);
		return -1;
	}
	/* Close write windows.*/
	target_mem_write32(t, sf->regbase + FLASH_CR, 0);
	return 0;
}

/* Both banks are erased in parallel.*/
static bool stm32h7_cmd_erase(target *t, int bank_mask)
{
	const char spinner[] = "|/-\\";
	int spinindex = 0;
	bool do_bank1 = bank_mask & 1, do_bank2 = bank_mask & 2;
	uint32_t cr;
	bool result = false;
	enum align psize = ALIGN_DWORD;
	for (struct target_flash *f = t->flash; f; f = f->next) {
		if (f->write == stm32h7_flash_write) {
			psize = ((struct stm32h7_flash *)f)->psize;
		}
	}
	cr = (psize * FLASH_CR_PSIZE16) | FLASH_CR_BER | FLASH_CR_START;
	/* Flash mass erase start instruction */
	if (do_bank1) {
		if (stm32h7_flash_unlock(t, BANK1_START) == false) {
			DEBUG_WARN("ME: Unlock bank1 failed\n");
			goto done;
		}
		uint32_t regbase = FPEC1_BASE;
		/* BER and start can be merged (3.3.10).*/
		target_mem_write32(t, regbase + FLASH_CR, cr);
		DEBUG_INFO("ME bank1 started\n");
	}
	if (do_bank2) {
		if (stm32h7_flash_unlock(t, BANK2_START) == false) {
			DEBUG_WARN("ME: Unlock bank2 failed\n");
			goto done;
		}
		uint32_t regbase = FPEC2_BASE;
		/* BER and start can be merged (3.3.10).*/
		target_mem_write32(t, regbase + FLASH_CR, cr);
		DEBUG_INFO("ME bank2 started\n");
	}

	/* Read FLASH_SR to poll for QW bit */
	if (do_bank1) {
		uint32_t regbase = FPEC1_BASE;
		while (target_mem_read32(t, regbase + FLASH_SR) & FLASH_SR_QW) {
//			target_mem_write32(t, H7_IWDG_BASE, 0x0000aaaa);
			tc_printf(t, "\b%c", spinner[spinindex++ % 4]);
			if(target_check_error(t)) {
				DEBUG_WARN("ME bank1: comm failed\n");
				goto done;
			}
		}
	}
	if (do_bank2) {
		uint32_t regbase = FPEC2_BASE;
		while (target_mem_read32(t, regbase + FLASH_SR) & FLASH_SR_QW) {
//			target_mem_write32(t, H7_IWDG_BASE 0x0000aaaa);
			tc_printf(t, "\b%c", spinner[spinindex++ % 4]);
			if(target_check_error(t)) {
				DEBUG_WARN("ME bank2: comm failed\n");
				goto done;
			}
		}
	}

	if (do_bank1) {
		/* Check for error */
		uint32_t regbase = FPEC1_BASE;
		uint32_t sr = target_mem_read32(t, regbase + FLASH_SR);
		if (sr & FLASH_SR_ERROR_MASK) {
			DEBUG_WARN("ME bank1, error sr %" PRIx32 "\n", sr);
			goto done;
		}
	}
	if (do_bank2) {
		/* Check for error */
		uint32_t regbase = FPEC2_BASE;
		uint32_t sr = target_mem_read32(t, regbase + FLASH_SR);
		if (sr & FLASH_SR_ERROR_MASK) {
			DEBUG_WARN("ME bank2, error: sr %" PRIx32 "\n", sr);
			goto done;
		}
	}
	result = true;
  done:
	tc_printf(t, "\n");
	return result;
}

static bool stm32h7_cmd_erase_mass(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	tc_printf(t, "Erasing flash... This may take a few seconds.  ");
	return stm32h7_cmd_erase(t, 3);
}

/* Print the Unique device ID.
 * Can be reused for other STM32 devices With uid as parameter.
 */
static bool stm32h7_uid(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	uint32_t uid = 0x1ff1e800;
	int i;
	tc_printf(t, "0x");
	for (i = 0; i < 12; i = i + 4) {
		uint32_t val = target_mem_read32(t, uid + i);
		tc_printf(t, "%02X", (val >> 24) & 0xff);
		tc_printf(t, "%02X", (val >> 16) & 0xff);
		tc_printf(t, "%02X", (val >>  8) & 0xff);
		tc_printf(t, "%02X", (val >>  0) & 0xff);
	}
	tc_printf(t, "\n");
	return true;
}
static int stm32h7_crc_bank(target *t, uint32_t bank)
{
	uint32_t regbase = FPEC1_BASE;
	if (bank >= BANK2_START)
		regbase = FPEC2_BASE;

	if (stm32h7_flash_unlock(t, bank) == false)
			return -1;
	uint32_t cr = FLASH_CR_CRC_EN;
	target_mem_write32(t, regbase + FLASH_CR, cr);
	uint32_t crccr= FLASH_CRCCR_CRC_BURST_3 |
		FLASH_CRCCR_CLEAN_CRC | FLASH_CRCCR_ALL_BANK;
	target_mem_write32(t, regbase + FLASH_CRCCR, crccr);
	target_mem_write32(t, regbase + FLASH_CRCCR, crccr | FLASH_CRCCR_START_CRC);
	uint32_t sr;
	while ((sr = target_mem_read32(t, regbase + FLASH_SR)) &
		   FLASH_SR_CRC_BUSY) {
		if(target_check_error(t)) {
			DEBUG_WARN("CRC bank %d: comm failed\n",
					   (bank < BANK2_START) ? 1 : 2);
			return -1;
		}
		if (sr & FLASH_SR_ERROR_READ) {
			DEBUG_WARN("CRC bank %d: error sr %08" PRIx32 "\n",
				  (bank < BANK2_START) ? 1 : 2, sr);
			return -1;
		}
	}
	return 0;
}

static bool stm32h7_crc(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	if (stm32h7_crc_bank(t, BANK1_START) ) return false;
	uint32_t crc1 = target_mem_read32(t, FPEC1_BASE + FLASH_CRCDATA);
	if (stm32h7_crc_bank(t, BANK2_START) ) return false;
	uint32_t crc2 = target_mem_read32(t, FPEC1_BASE + FLASH_CRCDATA);
	tc_printf(t, "CRC: bank1 0x%08lx, bank2 0x%08lx\n", crc1, crc2);
	return true;
}
static bool stm32h7_cmd_psize(target *t, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	if (argc == 1) {
		enum align psize = ALIGN_DWORD;
		for (struct target_flash *f = t->flash; f; f = f->next) {
			if (f->write == stm32h7_flash_write) {
				psize = ((struct stm32h7_flash *)f)->psize;
			}
		}
		tc_printf(t, "Flash write parallelism: %s\n",
		          psize == ALIGN_DWORD ? "x64" :
		          psize == ALIGN_WORD  ? "x32" :
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
			tc_printf(t, "usage: monitor psize (x8|x16|x32|x64)\n");
			return false;
		}
		for (struct target_flash *f = t->flash; f; f = f->next) {
			if (f->write == stm32h7_flash_write) {
				((struct stm32h7_flash *)f)->psize = psize;
			}
		}
	}
	return true;
}

static const struct stm32h7xx_rev {
	uint32_t rev_id;
	char revision;
} stm32h7xx_revisions[] = {
	{ 0x1000, 'A' },
	{ 0x1001, 'Z' },
	{ 0x1003, 'Y' },
	{ 0x2001, 'X' },
	{ 0x2003, 'V' }
};
static bool stm32h7_cmd_rev(target *t, int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	/* DBGMCU identity code register */
	uint32_t dbgmcu_idc = target_mem_read32(t, DBGMCU_IDC);
	uint16_t rev_id = (dbgmcu_idc >> 16) & 0xFFFF;
	uint16_t dev_id = dbgmcu_idc & 0xFFF;

	/* Print device */
	switch (dev_id) {
	case 0x450:
		tc_printf(t, "STM32H742/743/753/750\n");
		break;
	default:
		tc_printf(t, "Unknown STM32H7. This driver may not support it!\n");
	}

	/* Print revision */
	char rev = '?';
	for (size_t i = 0;
		 i < sizeof(stm32h7xx_revisions)/sizeof(struct stm32h7xx_rev); i++) {
		/* Check for matching revision */
		if (stm32h7xx_revisions[i].rev_id == rev_id) {
			rev = stm32h7xx_revisions[i].revision;
		}
	}
	tc_printf(t, "Revision %c\n", rev);

	return true;
}
