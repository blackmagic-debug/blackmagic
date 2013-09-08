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

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "command.h"
#include "general.h"
#include "adiv5.h"
#include "target.h"

#define ARM_CPUID		0xE000ED00
#define MEMMAP			0x400FC040
#define ARM_THUMB_BREAKPOINT	0xBE00

#define R_MSP			17	// Main stack pointer register number
#define R_PC			15	// Program counter register number
#define R_LR			14	// Link register number

#define IAP_ENTRYPOINT		0x1FFF1FF1

#define LPC17XX_SRAM_BASE	0x10000000
#define LPC17XX_MIN_SRAM_SIZE	8192		// LPC1751

#define LPC1769_AHB_SRAM_BASE	0x2007C000
#define LPC1769_AHB_SRAM_SIZE	(16*1024)

#define IAP_RAM_BASE		0x10000000
#define IAP_RAM_SIZE		LPC17XX_MIN_SRAM_SIZE - 32

#define IAP_PGM_CHUNKSIZE	4096

#define IAP_CMD_PREPARE		50
#define IAP_CMD_PROGRAM		51
#define IAP_CMD_ERASE		52
#define IAP_CMD_BLANKCHECK	53
#define IAP_CMD_PARTID		54

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

/* CPU Frequency */
#define CPU_CLK_KHZ		12000

struct flash_param {
	uint16_t	opcode;		/* opcode to return after calling the ROM */
	uint16_t	pad0;
	uint32_t	command;	/* IAP command */
	union {
		struct {
			uint32_t start_sector;
			uint32_t end_sector;
		} prepare;
		struct {
			uint32_t start_sector;
			uint32_t end_sector;
			uint32_t cpu_clk_khz;
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
		} blank_check;
	} params;
	uint32_t	result[5];	/* result data */
};

struct flash_program {
	struct flash_param	p;
	uint8_t			data[IAP_PGM_CHUNKSIZE];
};

static bool lpc17xx_cmd_erase(struct target_s *target);
static void lpc17xx_iap_call(struct target_s *target, struct flash_param *param, unsigned param_len);
static int lpc17xx_flash_prepare(struct target_s *target, uint32_t addr, int len);
static int lpc17xx_flash_erase(struct target_s *target, uint32_t addr, int len);
static int lpc17xx_flash_write(struct target_s *target, uint32_t dest, const uint8_t *src,
			  int len);

const struct command_s lpc17xx_cmd_list[] = {
	{"erase_mass", (cmd_handler)lpc17xx_cmd_erase, "Erase entire flash memory"},
	{NULL, NULL, NULL}
};

/* blocksize is the erasure block size */
/* Currently for LPC1769: */
/* 512kB Flash @ 0x0000 0000 */
/*  32kB SRAM  @ 0x1000 0000 */
/*  16kB SRAM  @ 0x2007 C000 */
/*  16kB SRAM  @ 0x2008 0000 */
static const char lpc17xx_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	
	"<!DOCTYPE memory-map "
	" PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"\"http://sourceware.org/gdb/gdb-memory-map.dtd\">"
*/
  "<memory-map>"
  "  <memory type=\"flash\" start=\"0x0\" length=\"0x10000\">"
  "    <property name=\"blocksize\">0x1000</property>"
  "  </memory>"
  "  <memory type=\"flash\" start=\"0x10000\" length=\"0x70000\">"
  "    <property name=\"blocksize\">0x8000</property>"
  "  </memory>"
  "  <memory type=\"ram\" start=\"0x10000000\" length=\"0x8000\"/>"
  "  <memory type=\"ram\" start=\"0x2007C000\" length=\"0x4000\"/>"
  "  <memory type=\"ram\" start=\"0x20080000\" length=\"0x4000\"/>"
  "</memory-map>";


bool
lpc17xx_probe(struct target_s *target)
{
	struct flash_program flash_pgm;
	uint32_t idcode;

	/* Read the Part ID */
	memset(&flash_pgm.p, 0, sizeof(flash_pgm.p));
	flash_pgm.p.command = IAP_CMD_PREPARE;
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
//	lpc17xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
//	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
//		return false;
//	}

//	switch (flash_pgm.p.result[1]) {
//		case 0x26113F37: /* LPC1769 */
//		case 0x26013F37: /* LPC1768 */
//		case 0x26012837: /* LPC1767 */
//		case 0x26013F33: /* LPC1766 */
//		case 0x26013733: /* LPC1765 */
//		case 0x26011922: /* LPC1764 */
//		case 0x25113737: /* LPC1759 */
//		case 0x25013F37: /* LPC1758 */
//		case 0x25011723: /* LPC1756 */
//		case 0x25011722: /* LPC1754 */
//		case 0x25001121: /* LPC1752 */
//		case 0x25001118: /* LPC1751 */
//		case 0x25001110: /* LPC1751 (No CRP) */

			target->driver = "LPC17xx";
			target->xml_mem_map = lpc17xx_xml_memory_map;
			target->flash_erase = lpc17xx_flash_erase;
			target->flash_write = lpc17xx_flash_write;
			target_add_commands(target, lpc17xx_cmd_list, "LPC17xx");

			return true;
//	}

	return false;
}

/**
 * @breif Unmaps the boot ROM from 0x00000000 leaving the user flash visible
 */
static void
lpc17xx_unmap_boot_rom(struct target_s *target)
{
	/* From §33.6 Debug memory re-mapping (Page 643) UM10360.pdf (Rev 2) */
	adiv5_ap_mem_write(adiv5_target_ap(target), MEMMAP, 1);
}

static bool
lpc17xx_cmd_erase(struct target_s *target)
{
	struct flash_program flash_pgm;

	gdb_outf("lpc17xx_cmd_erase remap boot..\n");
	lpc17xx_unmap_boot_rom(target);

	gdb_outf("lpc17xx_cmd_erase part ID..\n");

	memset(&flash_pgm.p, 0, sizeof(flash_pgm.p));
	flash_pgm.p.command = IAP_CMD_PARTID;
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
	lpc17xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
	  gdb_outf("lpc17xx_cmd_erase part ID fail %d..\n", flash_pgm.p.result[0]);
		return false;
	}
	gdb_outf("lpc17xx_cmd_erase part ID.. 0x%08lx\n", flash_pgm.p.result[1]);

	gdb_outf("lpc17xx_cmd_erase prepare..\n");

	memset(&flash_pgm.p, 0, sizeof(flash_pgm.p));
	flash_pgm.p.command = IAP_CMD_PREPARE;
	flash_pgm.p.params.prepare.start_sector = 0;
	flash_pgm.p.params.prepare.end_sector = 30-1;
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
	lpc17xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return false;
	}

	gdb_outf("lpc17xx_cmd_erase..\n");

	memset(&flash_pgm.p, 0, sizeof(flash_pgm.p));
	flash_pgm.p.command = IAP_CMD_ERASE;
	flash_pgm.p.params.erase.start_sector = 0;
	flash_pgm.p.params.erase.end_sector = 30-1;
	flash_pgm.p.params.erase.cpu_clk_khz = CPU_CLK_KHZ;
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
	lpc17xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return false;
	}

	gdb_outf("lpc17xx_cmd_erase blankcheck..\n");

	memset(&flash_pgm.p, 0, sizeof(flash_pgm.p));
	flash_pgm.p.command = IAP_CMD_BLANKCHECK;
	flash_pgm.p.params.blank_check.start_sector = 0;
	flash_pgm.p.params.blank_check.end_sector = 30-1;
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
	lpc17xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
	  gdb_outf("lpc17xx_cmd_erase blankcheck failed %d..\n", flash_pgm.p.result[0]);
	  gdb_outf("fail location is 0x%08lx\n", flash_pgm.p.result[1]);
	  gdb_outf("data is 0x%08lx\n", flash_pgm.p.result[2]);
		return false;
	}

	
	gdb_outf("Erase OK.\n");

	return true;
}

/**
 * @brief find a sector number given linear offset
 */
static int32_t sector_number(uint32_t addr)
{
	/* From §32.5 Sector Numbers (Page 620) UM10360.pdf (Rev 2) */
	if (addr < 0x00010000) {
		return addr >> 12; // 4K block
	} else {
		return 16 + ((addr - 0x00010000) >> 15); // 32K block
	}
}

static void
lpc17xx_iap_call(struct target_s *target, struct flash_param *param, unsigned param_len)
{
	uint32_t regs[target->regs_size / 4]; // target->regs_size is in bytes

	memset(regs, 0, target->regs_size);

	/* fill out the remainder of the parameters and copy the structure to RAM */
	param->opcode = ARM_THUMB_BREAKPOINT; /* breakpoint */
	param->pad0 = 0x0000; /* pad */
	target_mem_write_words(target, IAP_RAM_BASE, (void *)param, param_len);

	/* set up for the call to the IAP ROM */
	target_regs_read(target, regs);
	regs[0] = IAP_RAM_BASE + offsetof(struct flash_param, command);
	regs[1] = IAP_RAM_BASE + offsetof(struct flash_param, result);

	// stack pointer - top of the smallest ram less 32 for IAP usage
	regs[R_MSP] = IAP_RAM_BASE + IAP_RAM_SIZE;
	regs[R_LR] = IAP_RAM_BASE | 1;
	regs[R_PC] = IAP_ENTRYPOINT;
	target_regs_write(target, regs);

	/* start the target and wait for it to halt again */
	target_halt_resume(target, 0);
	while (!target_halt_wait(target));

	/* copy back just the parameters structure */
	target_mem_read_words(target, (void *)param, IAP_RAM_BASE, sizeof(struct flash_param));
}

static int
lpc17xx_flash_prepare(struct target_s *target, uint32_t addr, int len)
{
	struct flash_program flash_pgm;

	/* prepare the sector(s) to be erased */
	memset(&flash_pgm.p, 0, sizeof(flash_pgm.p));
	flash_pgm.p.command = IAP_CMD_PREPARE;
	flash_pgm.p.params.prepare.start_sector = sector_number(addr);
	flash_pgm.p.params.prepare.end_sector = sector_number(addr+len);
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
	lpc17xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	return 0;
}

static int
lpc17xx_flash_erase(struct target_s *target, uint32_t addr, int len)
{
	struct flash_program flash_pgm;

	/* min block size */
	if (addr % 4096) {
		return -1;
	}

	lpc17xx_unmap_boot_rom(target);

	/* prepare... */
	if (lpc17xx_flash_prepare(target, addr, len)) {
		return -1;
	}

	/* and now erase them */
	memset(&flash_pgm.p, 0, sizeof(flash_pgm.p));
	flash_pgm.p.command = IAP_CMD_ERASE;
	flash_pgm.p.params.erase.start_sector = sector_number(addr);
	flash_pgm.p.params.erase.end_sector = sector_number(addr+len);
	flash_pgm.p.params.erase.cpu_clk_khz = CPU_CLK_KHZ;
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
	lpc17xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	memset(&flash_pgm.p, 0, sizeof(flash_pgm.p));
	flash_pgm.p.command = IAP_CMD_BLANKCHECK;
	flash_pgm.p.params.blank_check.start_sector = sector_number(addr);
	flash_pgm.p.params.blank_check.end_sector = sector_number(addr+len);
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
	lpc17xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	return 0;
}

static int
lpc17xx_flash_write(struct target_s *target, uint32_t dest, const uint8_t *src, int len)
{
	unsigned first_chunk = dest / IAP_PGM_CHUNKSIZE;
	unsigned last_chunk = (dest + len - 1) / IAP_PGM_CHUNKSIZE;
	unsigned chunk_offset = dest % IAP_PGM_CHUNKSIZE;
	unsigned chunk;
	struct flash_program flash_pgm;

	for (chunk = first_chunk; chunk <= last_chunk; chunk++) {

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
			chunk_offset = 0;

			/* if we are programming the vectors, calculate the magic number */
			/* From §32.3.1.1 Criterion for.. (Page 616) UM10360.pdf (Rev 2) */
			if (dest == 0) {
				uint32_t *w = (uint32_t *)(&flash_pgm.data[0]);
				uint32_t sum = 0;
				
				if (copylen >= 7) {
					for (unsigned i = 0; i < 7; i++)
						sum += w[i];
					w[7] = 0 - sum;
				} else {
					/* Can't possibly calculate magic number */
					return -1;
				}
			}

		} else {
			/* interior chunk, must be aligned and full-sized */
			memcpy(flash_pgm.data, src, IAP_PGM_CHUNKSIZE);
			len -= IAP_PGM_CHUNKSIZE;
			src += IAP_PGM_CHUNKSIZE;
		}

		lpc17xx_unmap_boot_rom(target);

		/* prepare... */
		if (lpc17xx_flash_prepare(target, chunk * IAP_PGM_CHUNKSIZE, IAP_PGM_CHUNKSIZE))
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
		lpc17xx_iap_call(target, &flash_pgm.p, sizeof(flash_pgm));
		if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
			return -1;
		}

	}

	return 0;
}
