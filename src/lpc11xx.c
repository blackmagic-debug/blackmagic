/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Mike Smith <drziplok@me.com>
 * Copyright (C) 2015 Gareth McMullin <gareth@blacksphere.co.nz>
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

#include "general.h"
#include "target.h"
#include "cortexm.h"
#include "lpc_common.h"

#define IAP_PGM_CHUNKSIZE	256	/* should fit in RAM on any device */


#define MIN_RAM_SIZE_FOR_LPC8xx		1024
#define MIN_RAM_SIZE_FOR_LPC1xxx	2048
#define RAM_USAGE_FOR_IAP_ROUTINES	32	/* IAP routines use 32 bytes at top of ram */

#define IAP_ENTRYPOINT	0x1fff1ff1
#define IAP_RAM_BASE	0x10000000

static const char lpc8xx_driver[] = "lpc8xx";
static const char lpc11xx_driver[] = "lpc11xx";
static void lpc11x_iap_call(target *t, struct flash_param *param, unsigned param_len);
static int lpc11xx_flash_prepare(target *t, uint32_t addr, int len);
static int lpc11xx_flash_erase(target *t, uint32_t addr, size_t len);
static int lpc11xx_flash_write(target *t, uint32_t dest, const uint8_t *src,
			  size_t len);

struct flash_program {
	struct flash_param p;
	uint8_t	data[IAP_PGM_CHUNKSIZE];
};

/*
 * Note that this memory map is actually for the largest of the lpc11xx devices;
 * There seems to be no good way to decode the part number to determine the RAM
 * and flash sizes.
 */
static const char lpc11xx_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x00000000\" length=\"0x20000\">"
	"    <property name=\"blocksize\">0x1000</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x10000000\" length=\"0x2000\"/>"
	"</memory-map>";

/*
 * Memory map for the lpc8xx devices, which otherwise look much like the lpc11xx.
 *
 * We could decode the RAM/flash sizes, but we just encode the largest possible here.
 *
 * Note that the LPC810 and LPC811 map their flash oddly; see the NXP LPC800 user
 * manual (UM10601) for more details.
 */
static const char lpc8xx_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x00000000\" length=\"0x4000\">"
	"    <property name=\"blocksize\">0x400</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x10000000\" length=\"0x1000\"/>"
	"</memory-map>";

bool
lpc11xx_probe(target *t)
{
	uint32_t idcode;

	/* read the device ID register */
	idcode = target_mem_read32(t, 0x400483F4);

	switch (idcode) {

	case 0x041E502B:
	case 0x2516D02B:
	case 0x0416502B:
	case 0x2516902B:	/* lpc1111 */
	case 0x2524D02B:
	case 0x0425502B:
	case 0x2524902B:
	case 0x1421102B:	/* lpc1112 */
	case 0x0434502B:
	case 0x2532902B:
	case 0x0434102B:
	case 0x2532102B:	/* lpc1113 */
	case 0x0444502B:
	case 0x2540902B:
	case 0x0444102B:
	case 0x2540102B:
	case 0x1440102B:	/* lpc1114 */
	case 0x0A40902B:
	case 0x1A40902B:
	case 0x2058002B:	/* lpc1115 */
	case 0x1431102B:	/* lpc11c22 */
	case 0x1430102B:	/* lpc11c24 */
	case 0x095C802B:	/* lpc11u12x/201 */
	case 0x295C802B:
	case 0x097A802B:	/* lpc11u13/201 */
	case 0x297A802B:
	case 0x0998802B:	/* lpc11u14x/201 */
	case 0x2998802B:
	case 0x2972402B:	/* lpc11u23/301 */
	case 0x2988402B:	/* lpc11u24x/301 */
	case 0x2980002B:	/* lpc11u24x/401 */
		t->driver = lpc11xx_driver;
		t->xml_mem_map = lpc11xx_xml_memory_map;
		t->flash_erase = lpc11xx_flash_erase;
		t->flash_write = lpc11xx_flash_write;

		return true;

	case 0x1812202b:	/* LPC812M101FDH20 */
		t->driver = lpc8xx_driver;
		t->xml_mem_map = lpc8xx_xml_memory_map;
		t->flash_erase = lpc11xx_flash_erase;
		t->flash_write = lpc11xx_flash_write;

		return true;
	}

	return false;
}

static void
lpc11x_iap_call(target *t, struct flash_param *param, unsigned param_len)
{
	uint32_t regs[t->regs_size / sizeof(uint32_t)];

	/* fill out the remainder of the parameters and copy the structure to RAM */
	param->opcode = ARM_THUMB_BREAKPOINT;
	param->pad0 = 0x0000;
	target_mem_write(t, IAP_RAM_BASE, param, param_len);

	/* set up for the call to the IAP ROM */
	target_regs_read(t, regs);
	regs[0] = IAP_RAM_BASE + offsetof(struct flash_param, command);
	regs[1] = IAP_RAM_BASE + offsetof(struct flash_param, result);

	// stack pointer - top of the smallest ram less 32 for IAP usage
	if (t->driver == lpc8xx_driver)
		regs[REG_MSP] = IAP_RAM_BASE + MIN_RAM_SIZE_FOR_LPC8xx - RAM_USAGE_FOR_IAP_ROUTINES;
	else
		regs[REG_MSP] = IAP_RAM_BASE + MIN_RAM_SIZE_FOR_LPC1xxx - RAM_USAGE_FOR_IAP_ROUTINES;
	regs[REG_LR] = IAP_RAM_BASE | 1;
	regs[REG_PC] = IAP_ENTRYPOINT;
	target_regs_write(t, regs);

	/* start the target and wait for it to halt again */
	target_halt_resume(t, 0);
	while (!target_halt_wait(t));

	/* copy back just the parameters structure */
	target_mem_read(t, param, IAP_RAM_BASE, sizeof(struct flash_param));
}

static int flash_page_size(target *t)
{
	if (t->driver == lpc8xx_driver)
		return 1024;
	else
		return 4096;
}

static int
lpc11xx_flash_prepare(target *t, uint32_t addr, int len)
{
	struct flash_program flash_pgm;
	/* prepare the sector(s) to be erased */
	memset(&flash_pgm.p, 0, sizeof(flash_pgm.p));
	flash_pgm.p.command = IAP_CMD_PREPARE;
	flash_pgm.p.prepare.start_sector = addr / flash_page_size(t);
	flash_pgm.p.prepare.end_sector = (addr + len - 1) / flash_page_size(t);

	lpc11x_iap_call(t, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	return 0;
}

static int
lpc11xx_flash_erase(target *t, uint32_t addr, size_t len)
{
	struct flash_program flash_pgm;

	if (addr % flash_page_size(t))
		return -1;

	/* prepare... */
	if (lpc11xx_flash_prepare(t, addr, len))
		return -1;

	/* and now erase them */
	flash_pgm.p.command = IAP_CMD_ERASE;
	flash_pgm.p.erase.start_sector = addr / flash_page_size(t);
	flash_pgm.p.erase.end_sector = (addr + len - 1) / flash_page_size(t);
	flash_pgm.p.erase.cpu_clk_khz = CPU_CLK_KHZ;
	flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
	lpc11x_iap_call(t, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	/* check erase ok */
	flash_pgm.p.command = IAP_CMD_BLANKCHECK;
	lpc11x_iap_call(t, &flash_pgm.p, sizeof(flash_pgm.p));
	if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
		return -1;
	}

	return 0;
}

static int
lpc11xx_flash_write(target *t, uint32_t dest, const uint8_t *src, size_t len)
{
	unsigned first_chunk = dest / IAP_PGM_CHUNKSIZE;
	unsigned last_chunk = (dest + len - 1) / IAP_PGM_CHUNKSIZE;
	unsigned chunk_offset = dest % IAP_PGM_CHUNKSIZE;
	unsigned chunk;
	struct flash_program flash_pgm;

	for (chunk = first_chunk; chunk <= last_chunk; chunk++) {

		DEBUG("chunk %u len %zu\n", chunk, len);
		/* first and last chunk may require special handling */
		if ((chunk == first_chunk) || (chunk == last_chunk)) {

			/* fill with all ff to avoid sector rewrite corrupting other writes */
			memset(flash_pgm.data, 0xff, sizeof(flash_pgm.data));

			/* copy as much as fits */
			size_t copylen = IAP_PGM_CHUNKSIZE - chunk_offset;
			if (copylen > len)
				copylen = len;

			memcpy(flash_pgm.data + chunk_offset, src, copylen);
			/* if we are programming the vectors, calculate the magic number */
			if ((chunk == 0) && (chunk_offset == 0)) {
				if (copylen < 32) {
					/* we have to be programming at least the first 8 vectors... */
					return -1;
				}

				uint32_t *w = (uint32_t *)(&flash_pgm.data[0]);
				uint32_t sum = 0;

				for (unsigned i = 0; i < 7; i++)
					sum += w[i];
				w[7] = ~sum + 1;
			}

			/* update to suit */
			len -= copylen;
			src += copylen;
			chunk_offset = 0;
		} else {
			/* interior chunk, must be aligned and full-sized */
			memcpy(flash_pgm.data, src, IAP_PGM_CHUNKSIZE);
			len -= IAP_PGM_CHUNKSIZE;
			src += IAP_PGM_CHUNKSIZE;
		}

		/* prepare... */
		if (lpc11xx_flash_prepare(t, chunk * IAP_PGM_CHUNKSIZE, IAP_PGM_CHUNKSIZE))
			return -1;

		/* set the destination address and program */
		flash_pgm.p.command = IAP_CMD_PROGRAM;
		flash_pgm.p.program.dest = chunk * IAP_PGM_CHUNKSIZE;
		flash_pgm.p.program.source = IAP_RAM_BASE + offsetof(struct flash_program, data);
		flash_pgm.p.program.byte_count = IAP_PGM_CHUNKSIZE;
		flash_pgm.p.program.cpu_clk_khz = CPU_CLK_KHZ;
		flash_pgm.p.result[0] = IAP_STATUS_CMD_SUCCESS;
		lpc11x_iap_call(t, &flash_pgm.p, sizeof(flash_pgm));
		if (flash_pgm.p.result[0] != IAP_STATUS_CMD_SUCCESS) {
			return -1;
		}

	}

	return 0;
}
