/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012 Gareth McMullin <gareth@blacksphere.co.nz>
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

#include <stddef.h>
#include "command.h"
#include "general.h"
#include "adiv5.h"
#include "target.h"

#define LPC43XX_CHIPID	0x40043200
#define ARM_CPUID	0xE000ED00
#define ARM_THUMB_BREAKPOINT 0xBE00

#define R_MSP				17	// Main stack pointer register number
#define R_PC				15	// Program counter register number
#define R_LR				14	// Link register number

#define IAP_ENTRYPOINT_LOCATION	0x10400100

#define LPC43XX_ETBAHB_SRAM_BASE 0x2000C000
#define LPC43XX_ETBAHB_SRAM_SIZE (16*1024)

#define IAP_RAM_SIZE	LPC43XX_ETBAHB_SRAM_SIZE
#define IAP_RAM_BASE	LPC43XX_ETBAHB_SRAM_BASE

#define IAP_PGM_CHUNKSIZE	4096

#define IAP_CMD_INIT		49
#define IAP_CMD_PREPARE		50
#define IAP_CMD_PROGRAM		51
#define IAP_CMD_ERASE		52
#define IAP_CMD_BLANKCHECK	53
#define IAP_CMD_SET_ACTIVE_BANK	60

#define IAP_STATUS_CMD_SUCCESS		0
#define IAP_STATUS_INVALID_COMMAND	1
#define IAP_STATUS_SRC_ADDR_ERROR	2
#define IAP_STATUS_DST_ADDR_ERROR	3
#define IAP_STATUS_SRC_ADDR_NOT_MAPPED	4
#define IAP_STATUS_DST_ADDR_NOT_MAPPED	5
#define IAP_STATUS_COUNT_ERROR		6
#define IAP_STATUS_INVALID_SECTOR	7
#define IAP_STATUS_SECTOR_NOT_BLANK	8
#define IAP_STATUS_SECTOR_NOT_PREPARED	9
#define IAP_STATUS_COMPARE_ERROR	10
#define IAP_STATUS_BUSY			11

#define FLASH_BANK_A_BASE 0x1A000000
#define FLASH_BANK_A_SIZE 0x80000
#define FLASH_BANK_B_BASE 0x1B000000
#define FLASH_BANK_B_SIZE 0x80000
#define FLASH_NUM_BANK		2
#define FLASH_NUM_SECTOR	15
#define FLASH_LARGE_SECTOR_OFFSET 0x00010000

/* CPU Frequency */
#define CPU_CLK_KHZ 12000

struct flash_param {
	uint16_t	opcode;			/* opcode to return to after calling the ROM */
	uint16_t	pad0;
	uint32_t	command;		/* IAP command */
	union {
		uint32_t	words[5];	/* command parameters */
		struct {
			uint32_t start_sector;
			uint32_t end_sector;
			uint32_t flash_bank;
		} prepare;
		struct {
			uint32_t start_sector;
			uint32_t end_sector;
			uint32_t cpu_clk_khz;
			uint32_t flash_bank;
		} erase;
		struct {
			uint32_t dest;
			uint32_t source;
			uint32_t byte_count;
			uint32_t cpu_clk_khz;
		} program;
		struct {
			uint32_t start_sector;
			uint32_t end_sector;
			uint32_t flash_bank;
		} blank_check;
		struct {
			uint32_t flash_bank;
			uint32_t cpu_clk_khz;
		} make_active;
	} params;
	uint32_t	result[5];	/* result data */
} __attribute__((aligned(4)));

struct flash_program {
	struct	flash_param	p;
	uint8_t	data[IAP_PGM_CHUNKSIZE];
};

static bool lpc43xx_cmd_erase(target *target, int argc, const char *argv[]);
static bool lpc43xx_cmd_reset(target *target, int argc, const char *argv[]);
static bool lpc43xx_cmd_mkboot(target *target, int argc, const char *argv[]);
static int lpc43xx_flash_init(struct target_s *target);
static void lpc43xx_iap_call(struct target_s *target, struct flash_param *param, unsigned param_len);
static int lpc43xx_flash_prepare(struct target_s *target, uint32_t addr, int len);
static int lpc43xx_flash_erase(struct target_s *target, uint32_t addr, int len);
static int lpc43xx_flash_write(struct target_s *target, uint32_t dest, const uint8_t *src, int len);
static void lpc43xx_set_internal_clock(struct target_s *target);

const struct command_s lpc43xx_cmd_list[] = {
	{"erase_mass", lpc43xx_cmd_erase, "Erase entire flash memory"},
	{"reset", lpc43xx_cmd_reset, "Reset target"},
	{"mkboot", lpc43xx_cmd_mkboot, "Make flash bank bootable"},
	{NULL, NULL, NULL}
};

/* blocksize is the erasure block size */
static const char lpc4337_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*
	"<!DOCTYPE memory-map "
	" PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"\"http://sourceware.org/gdb/gdb-memory-map.dtd\">"
*/
"<memory-map>"
"  <memory type=\"ram\" start=\"0x0\" length=\"0x1A000000\"/>"
"  <memory type=\"flash\" start=\"0x1A000000\" length=\"0x10000\">"
"    <property name=\"blocksize\">0x2000</property>"
"  </memory>"
"  <memory type=\"flash\" start=\"0x1A010000\" length=\"0x70000\">"
"    <property name=\"blocksize\">0x10000</property>"
"  </memory>"
"  <memory type=\"ram\" start=\"0x1A080000\" length=\"0x00F80000\"/>"
"  <memory type=\"flash\" start=\"0x1B000000\" length=\"0x10000\">"
"    <property name=\"blocksize\">0x2000</property>"
"  </memory>"
"  <memory type=\"flash\" start=\"0x1B010000\" length=\"0x70000\">"
"    <property name=\"blocksize\">0x10000</property>"
"  </memory>"
"  <memory type=\"ram\" start=\"0x1B080000\" length=\"0xE4F80000\"/>"
"</memory-map>";

bool lpc43xx_probe(struct target_s *target)
{
	uint32_t chipid, cpuid;

	chipid = adiv5_ap_mem_read(adiv5_target_ap(target), LPC43XX_CHIPID);
	cpuid = adiv5_ap_mem_read(adiv5_target_ap(target), ARM_CPUID);

	switch(chipid) {
		case 0x4906002B:	/* Parts with on-chip flash */
			switch (cpuid & 0xFF00FFF0) {
				case 0x4100C240:
					target->driver = "LPC43xx Cortex-M4";
					if (cpuid == 0x410FC241)
					{
						/* LPC4337 */
						target->xml_mem_map = lpc4337_xml_memory_map;
						target->flash_erase = lpc43xx_flash_erase;
						target->flash_write = lpc43xx_flash_write;
						target_add_commands(target, lpc43xx_cmd_list, "LPC43xx");
					}
					break;
				case 0x4100C200:
					target->driver = "LPC43xx Cortex-M0";
					break;
				default:
					target->driver = "LPC43xx <Unknown>";
			}
			return true;
		case 0x5906002B:	/* Flashless parts */
		case 0x6906002B:
			switch (cpuid & 0xFF00FFF0) {
				case 0x4100C240:
					target->driver = "LPC43xx Cortex-M4";
					break;
				case 0x4100C200:
					target->driver = "LPC43xx Cortex-M0";
					break;
				default:
					target->driver = "LPC43xx <Unknown>";
			}
			return true;
	}

	return false;
}

/* Reset all major systems _except_ debug */
static bool lpc43xx_cmd_reset(target *target, int __attribute__((unused)) argc, const char __attribute__((unused)) *argv[])
{
	/* Cortex-M4 Application Interrupt and Reset Control Register */
	static const uint32_t AIRCR = 0xE000ED0C;
	/* Magic value key */
	static const uint32_t reset_val = 0x05FA0004;

	/* System reset on target */
	target_mem_write_words(target, AIRCR, &reset_val, sizeof(reset_val));

	return true;
}

static bool lpc43xx_cmd_erase(target *target, int __attribute__((unused)) argc, const char __attribute__((unused)) *argv[])
{
	uint32_t bank = 0;
	struct flash_program flash_pgm;

	lpc43xx_flash_init(target);

	for (bank = 0; bank < FLASH_NUM_BANK; bank++)
	{
		flash_pgm.p.command = IAP_CMD_PREPARE;
		flash_pgm.p.params.prepare.start_sector = 0;
		flash_pgm.p.params.prepare.end_sector = FLASH_NUM_SECTOR-1;
		flash_pgm.p.params.prepare.flash_bank = bank;
		flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
		lpc43xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
		if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
			return false;
		}

		flash_pgm.p.command = IAP_CMD_ERASE;
		flash_pgm.p.params.erase.start_sector = 0;
		flash_pgm.p.params.prepare.end_sector = FLASH_NUM_SECTOR-1;
		flash_pgm.p.params.erase.cpu_clk_khz = CPU_CLK_KHZ;
		flash_pgm.p.params.erase.flash_bank = bank;
		flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
		lpc43xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
		if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS)
		{
			return false;
		}
	}

	gdb_outf("Erase OK.\n");

	return true;
}

static int lpc43xx_flash_init(struct target_s *target)
{
	struct flash_program flash_pgm;

	/* Force internal clock */
	lpc43xx_set_internal_clock(target);

	/* Initialize flash IAP */
	flash_pgm.p.command = IAP_CMD_INIT;
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
	lpc43xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS)
	{
		return -1;
	}

	return 0;
}



/**
 * @brief find a sector number given linear offset
 */
static int32_t flash_bank(uint32_t addr)
{
	int32_t retVal;

	if (addr >= FLASH_BANK_A_BASE && addr < (FLASH_BANK_A_BASE+FLASH_BANK_A_SIZE))
	{
		retVal = 0;
	}
	else if (addr >= FLASH_BANK_B_BASE && addr < (FLASH_BANK_B_BASE+FLASH_BANK_B_SIZE))
	{
		retVal = 1;
	}
	else
	{
		retVal = -1;
	}

	return retVal;
}

/**
 * @brief find a sector number given linear offset
 */
static int32_t sector_number(uint32_t addr)
{
	int32_t retVal = 0;
	int32_t bank = flash_bank(addr);

	if (bank == 0)
	{
		addr = addr - FLASH_BANK_A_BASE;
	}
	else if (bank == 1)
	{
		addr = addr - FLASH_BANK_B_BASE;
	}
	else
	{
		retVal = -1;
	}

	if (retVal != -1)
	{
		/* from 47.5 "Sector numbers" (page 1218) UM10503.pdf (Rev 1.6) */
		if (addr < FLASH_LARGE_SECTOR_OFFSET)
		{
			retVal = addr >> 13;
		}
		else
		{
			retVal = 8 + ((addr - FLASH_LARGE_SECTOR_OFFSET) >> 16);
		}
	}

	return retVal;
}

static void lpc43xx_iap_call(struct target_s *target, struct flash_param *param, unsigned param_len)
{
	uint32_t regs[target->regs_size / sizeof(uint32_t)];
	uint32_t iap_entry;

	target_mem_read_words(target, &iap_entry, IAP_ENTRYPOINT_LOCATION, sizeof(iap_entry));

	/* fill out the remainder of the parameters and copy the structure to RAM */
	param->opcode = ARM_THUMB_BREAKPOINT; /* breakpoint */
	param->pad0 = 0x0000; /* pad */
	target_mem_write_words(target, IAP_RAM_BASE, (void *)param, param_len);

	/* set up for the call to the IAP ROM */
	target_regs_read(target, regs);
	regs[0] = IAP_RAM_BASE + offsetof(struct flash_param, command);
	regs[1] = IAP_RAM_BASE + offsetof(struct flash_param, result);

	regs[R_MSP] = IAP_RAM_BASE + IAP_RAM_SIZE;
	regs[R_LR] = IAP_RAM_BASE | 1;
	regs[R_PC] = iap_entry;
	target_regs_write(target, regs);

	/* start the target and wait for it to halt again */
	target_halt_resume(target, 0);
	while (!target_halt_wait(target));

	/* copy back just the parameters structure */
	target_mem_read_words(target, (void *)param, IAP_RAM_BASE, sizeof(struct flash_param));
}

static int lpc43xx_flash_prepare(struct target_s *target, uint32_t addr, int len)
{
	struct flash_program flash_pgm;

	/* prepare the sector(s) to be erased */
	flash_pgm.p.command = IAP_CMD_PREPARE;
	flash_pgm.p.params.prepare.start_sector = sector_number(addr);
	flash_pgm.p.params.prepare.end_sector = sector_number(addr+len);
	flash_pgm.p.params.prepare.flash_bank = flash_bank(addr);
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;

	lpc43xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	return 0;
}

	static int
lpc43xx_flash_erase(struct target_s *target, uint32_t addr, int len)
{
	struct flash_program flash_pgm;

	/* min block size */
	if (addr % 8192)
	{
		return -1;
	}

	/* init */
	if (lpc43xx_flash_init(target))
	{
		return -1;
	}

	/* prepare... */
	if (lpc43xx_flash_prepare(target, addr, len))
	{
		return -1;
	}

	/* and now erase them */
	flash_pgm.p.command = IAP_CMD_ERASE;
	flash_pgm.p.params.erase.start_sector = sector_number(addr);
	flash_pgm.p.params.erase.end_sector = sector_number(addr+len);
	flash_pgm.p.params.erase.cpu_clk_khz = CPU_CLK_KHZ;
	flash_pgm.p.params.erase.flash_bank = flash_bank(addr);
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
	lpc43xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	/* check erase ok */
	flash_pgm.p.command = IAP_CMD_BLANKCHECK;
	flash_pgm.p.params.blank_check.start_sector = sector_number(addr);
	flash_pgm.p.params.blank_check.end_sector = sector_number(addr+len);
	flash_pgm.p.params.blank_check.flash_bank = flash_bank(addr);
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
	lpc43xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	return 0;
}

static void lpc43xx_set_internal_clock(struct target_s *target)
{
	const uint32_t val2 = (1 << 11) | (1 << 24);
	target_mem_write_words(target, 0x40050000 + 0x06C, &val2, sizeof(val2));
}

static int lpc43xx_flash_write(struct target_s *target, uint32_t dest, const uint8_t *src, int len)
{
	unsigned first_chunk = dest / IAP_PGM_CHUNKSIZE;
	unsigned last_chunk = (dest + len - 1) / IAP_PGM_CHUNKSIZE;
	unsigned chunk_offset;
	unsigned chunk;
	struct flash_program flash_pgm;

	for (chunk = first_chunk; chunk <= last_chunk; chunk++)
	{
		if (chunk == first_chunk)
		{
			chunk_offset = dest % IAP_PGM_CHUNKSIZE;
		}
		else
		{
			chunk_offset = 0;
		}

		/* first and last chunk may require special handling */
		if ((chunk == first_chunk) || (chunk == last_chunk)) {

			/* fill with all ff to avoid sector rewrite corrupting other writes */
			memset(flash_pgm.data, 0xff, sizeof(flash_pgm.data));

			/* copy as much as fits */
			int copylen = IAP_PGM_CHUNKSIZE - chunk_offset;
			if (copylen > len)
				copylen = len;

			memcpy(flash_pgm.data + chunk_offset, src, copylen);

			/* update to suit */
			len -= copylen;
			src += copylen;
		} else {

			/* interior chunk, must be aligned and full-sized */
			memcpy(flash_pgm.data, src, IAP_PGM_CHUNKSIZE);
			len -= IAP_PGM_CHUNKSIZE;
			src += IAP_PGM_CHUNKSIZE;
		}

		/* prepare... */
		if (lpc43xx_flash_prepare(target, chunk * IAP_PGM_CHUNKSIZE, IAP_PGM_CHUNKSIZE))
		{
			return -1;
		}

		/* copy buffer into target memory */
		target_mem_write_words(target,
			IAP_RAM_BASE + offsetof(struct flash_program, data),
			(uint32_t*)flash_pgm.data, sizeof(flash_pgm.data));

		/* set the destination address and program */
		flash_pgm.p.command = IAP_CMD_PROGRAM;
		flash_pgm.p.params.program.dest = chunk * IAP_PGM_CHUNKSIZE;
		flash_pgm.p.params.program.source = IAP_RAM_BASE + offsetof(struct flash_program, data);
		flash_pgm.p.params.program.byte_count = IAP_PGM_CHUNKSIZE;
		flash_pgm.p.params.program.cpu_clk_khz = CPU_CLK_KHZ;
		flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
		lpc43xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm));
		if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
			return -1;
		}
	}

	return 0;
}

/* 
 * Call Boot ROM code to make a flash bank bootable by computing and writing the
 * correct signature into the exception table near the start of the bank.
 *
 * This is done indepently of writing to give the user a chance to verify flash
 * before changing it.
 */
static bool lpc43xx_cmd_mkboot(target *target, int argc, const char *argv[])
{
	/* Usage: mkboot 0 or mkboot 1 */
	if (argc == 2)
	{
		const long int bank = strtol(argv[1], NULL, 0);

		if (bank == 0 || bank == 1)
		{
			lpc43xx_flash_init(target);
			struct flash_program flash_pgm;

			/* special command to compute/write magic vector for signature */
			flash_pgm.p.command = IAP_CMD_SET_ACTIVE_BANK;
			flash_pgm.p.params.make_active.flash_bank = bank;
			flash_pgm.p.params.make_active.cpu_clk_khz = CPU_CLK_KHZ;
			flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
			lpc43xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm));
			if (flash_pgm.p.result[0] == IAP_STATUS_CMD_SUCCESS) {
				gdb_outf("Set bootable OK.\n");
				return true;
			}
			else
			{
				gdb_outf("Set bootable failed.\n");
			}
		}
		else
		{
			gdb_outf("Unexpected bank number, should be 0 or 1.\n");
		}
	}
	else
	{
		gdb_outf("Expected bank argument 0 or 1.\n");
	}


	return false;
}

