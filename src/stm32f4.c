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

#include <stdlib.h>
#include <string.h>

#include "general.h"
#include "adiv5.h"
#include "target.h"
#include "stm32_tgt.h"

static int stm32f4_flash_erase(struct target_s *target, uint32_t addr, int len);
static int stm32f4_flash_write_words(struct target_s *target, uint32_t dest, const uint32_t *src, 
			  int len);

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

#define FLASH_SR_BSY		(1 << 16)

#define KEY1 0x45670123
#define KEY2 0xCDEF89AB

#define SR_ERROR_MASK	0xF2
#define SR_EOP		0x01

/* This routine is uses word access.  Only usable on target voltage >2.7V */
uint16_t stm32f4_flash_write_stub[] = {
// _start:
	0x480a,	// ldr r0, [pc, #40] // _flashbase
	0x490b,	// ldr r1, [pc, #44] // _addr
	0x467a, // mov r2, pc
	0x3230, // adds r2, #48
	0x4b0a, // ldr r3, [pc, #36] // _size
 	0x4d07, // ldr r5, [pc, #28] // _cr
// _next:
 	0xb153, // cbz r3, _done
	0x6105, // str r5, [r0, #16]
	0x6814, // ldr r4, [r2]
	0x600c, // str r4, [r1]
// _wait:
	0x89c4, // ldrb r4, [r0, #14]
	0x2601, // movs r6, #1
	0x4234, // tst r4, r6
	0xd1fb, // bne _wait

	0x3b04, // subs r3, #4
	0x3104, // adds r1, #4
	0x3204, // adds r2, #4
	0xe7f3, // b _next
// _done:
	0xbe00, // bkpt
	0x0000,
// .org 0x28
//_cr:
	0x0201, 0x0000, //.word 0x00000201 (Value to write to FLASH_CR) */
// _flashbase:
 	0x3c00, 0x4002, // .word 0x40023c00 (FPEC_BASE)
// _addr:
// 	0x0000, 0x0000,
// _size:
// 	0x0000, 0x0000,
// _data:
// 	...
};

int stm32f4_probe(struct target_s *target)
{
	struct target_ap_s *t = (void *)target;
	uint32_t idcode;

	idcode = adiv5_ap_mem_read(t->ap, 0xE0042000);
	switch(idcode & 0xFFF) {
	case 0x411: /* Documented to be 0x413! This is what I read... */
	case 0x413:  
		target->driver = stm32f4_driver_str;
		target->xml_mem_map = stm32f4_xml_memory_map;
		target->flash_erase = stm32f4_flash_erase;
		target->flash_write_words = stm32f4_flash_write_words;
		return 0;
	} 
	return -1;
}

	
static int stm32f4_flash_erase(struct target_s *target, uint32_t addr, int len)
{
	struct target_ap_s *t = (void *)target;
	uint16_t sr;
	uint32_t cr;
	uint32_t pagesize;

	addr &= 0x07FFC000;

	/* Enable FPEC controller access */
	adiv5_ap_mem_write(t->ap, FLASH_KEYR, KEY1);
	adiv5_ap_mem_write(t->ap, FLASH_KEYR, KEY2);
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
		adiv5_ap_mem_write(t->ap, FLASH_CR, cr);
		/* write address to FMA */
		adiv5_ap_mem_write(t->ap, FLASH_CR, cr | FLASH_CR_STRT); 

		/* Read FLASH_SR to poll for BSY bit */
		while(adiv5_ap_mem_read(t->ap, FLASH_SR) & FLASH_SR_BSY)
			if(target_check_error(target)) 
				return -1;

		len -= pagesize;
		addr += pagesize;
	}

	/* Check for error */
	sr = adiv5_ap_mem_read(t->ap, FLASH_SR);
	if(sr & SR_ERROR_MASK)
		return -1;

	return 0;
}

static int stm32f4_flash_write_words(struct target_s *target, uint32_t dest, 
			  const uint32_t *src, int len)
{
	struct target_ap_s *t = (void *)target;
	uint32_t offset = dest % 4;
	uint32_t words = (offset + len + 3) / 4;
	uint32_t data[2 + words];
	uint16_t sr;

	/* Construct data buffer used by stub */
	data[0] = dest - offset;
	data[1] = words * 4;		/* length must always be a multiple of 4 */
	data[2] = 0xFFFFFFFF;		/* pad partial words with all 1s to avoid */
	data[words + 1] = 0xFFFFFFFF;	/* damaging overlapping areas */
	memcpy((uint8_t *)&data[2] + offset, src, len);

	/* Write stub and data to target ram and set PC */
	target_mem_write_words(target, 0x20000000, (void*)stm32f4_flash_write_stub, 0x30);
	target_mem_write_words(target, 0x20000030, data, sizeof(data));
	target_pc_write(target, 0x20000000);
	if(target_check_error(target)) 
		return -1;

	/* Execute the stub */
	target_halt_resume(target, 0);
	while(!target_halt_wait(target));

	/* Check for error */
	sr = adiv5_ap_mem_read(t->ap, FLASH_SR);
	if(sr & SR_ERROR_MASK)
		return -1;

	return 0;
}

