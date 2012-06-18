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
 * ST doc - PM0075
 *   Programming manual - STM32F10xxx Flash memory microcontrollers
 */

#include <stdlib.h>
#include <string.h>

#include "general.h"
#include "adiv5.h"
#include "target.h"
#include "stm32.h"

static int stm32md_flash_erase(struct target_s *target, uint32_t addr, int len);
static int stm32hd_flash_erase(struct target_s *target, uint32_t addr, int len);
static int stm32f1_flash_erase(struct target_s *target, uint32_t addr, int len, 
				uint32_t pagesize);
static int stm32f1_flash_write_words(struct target_s *target, uint32_t dest, 
			const uint32_t *src, int len);

static const char stm32f1_driver_str[] = "STM32, Medium density.";
static const char stm32hd_driver_str[] = "STM32, High density.";

static const char stm32f1_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x8000000\" length=\"0x20000\">"
	"    <property name=\"blocksize\">0x400</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x5000\"/>"
	"</memory-map>";

static const char stm32hd_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x8000000\" length=\"0x80000\">"
	"    <property name=\"blocksize\">0x800</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x10000\"/>"
	"</memory-map>";

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

#define KEY1 0x45670123
#define KEY2 0xCDEF89AB

#define SR_ERROR_MASK	0x14
#define SR_EOP		0x20

#define DBGMCU_IDCODE	0xE0042000

uint16_t stm32f1_flash_write_stub[] = {
// _start:
	0x4809,	// ldr r0, [pc, #36] // _flashbase
	0x490a,	// ldr r1, [pc, #40] // _addr
	0x467a, // mov r2, pc
	0x322c, // adds r2, #44
	0x4b09, // ldr r3, [pc, #36] // _size
 	0x2501, // movs r5, #1
// _next:
 	0xb153, // cbz r3, _done
	0x6105, // str r5, [r0, #16]
	0x8814, // ldrh r4, [r2]
	0x800c, // strh r4, [r1]
// _wait:
	0x68c4, // ldr r4, [r0, #12]
	0x2601, // movs r6, #1
	0x4234, // tst r4, r6
	0xd1fb, // bne _wait

	0x3b02, // subs r3, #2
	0x3102, // adds r1, #2
	0x3202, // adds r2, #2
	0xe7f3, // b _next
// _done:
	0xbe00, // bkpt
	0x0000,
// .org 0x28
// _flashbase:
 	0x2000, 0x4002, // .word 0x40022000 (FPEC_BASE)
// _addr:
// 	0x0000, 0x0000,
// _size:
// 	0x0000, 0x0000,
// _data:
// 	...
};

int stm32f1_probe(struct target_s *target)
{
	uint32_t idcode;

	idcode = adiv5_ap_mem_read(adiv5_target_ap(target), DBGMCU_IDCODE);
	switch(idcode & 0xFFF) {
	case 0x410:  /* Medium density */
	case 0x412:  /* Low denisty */
	case 0x420:  /* Value Line, Low-/Medium density */
		target->driver = stm32f1_driver_str;
		target->xml_mem_map = stm32f1_xml_memory_map;
		target->flash_erase = stm32md_flash_erase;
		target->flash_write_words = stm32f1_flash_write_words;
		return 0;
	case 0x414:	 /* High density */
	case 0x418:  /* Connectivity Line */
	case 0x428:	 /* Value Line, High Density */
		target->driver = stm32hd_driver_str;
		target->xml_mem_map = stm32hd_xml_memory_map;
		target->flash_erase = stm32hd_flash_erase;
		target->flash_write_words = stm32f1_flash_write_words;
		return 0;
	default:
		return -1;
	} 
}

static int stm32f1_flash_erase(struct target_s *target, uint32_t addr, int len, uint32_t pagesize)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint16_t sr;

	addr &= ~(pagesize - 1);
	len &= ~(pagesize - 1);

	/* Enable FPEC controller access */
	adiv5_ap_mem_write(ap, FLASH_KEYR, KEY1);
	adiv5_ap_mem_write(ap, FLASH_KEYR, KEY2);
	while(len) {
		/* Flash page erase instruction */
		adiv5_ap_mem_write(ap, FLASH_CR, 2);
		/* write address to FMA */
		adiv5_ap_mem_write(ap, FLASH_AR, addr); 
		/* Flash page erase start instruction */
		adiv5_ap_mem_write(ap, FLASH_CR, 0x42);

		/* Read FLASH_SR to poll for BSY bit */
		while(adiv5_ap_mem_read(ap, FLASH_SR) & 1)
			if(target_check_error(target))
				return -1;

		len -= pagesize;
		addr += pagesize;
	}

	/* Check for error */
	sr = adiv5_ap_mem_read(ap, FLASH_SR);
	if ((sr & SR_ERROR_MASK) || !(sr & SR_EOP))
		return -1;

	return 0;
}

static int stm32hd_flash_erase(struct target_s *target, uint32_t addr, int len)
{
	return stm32f1_flash_erase(target, addr, len, 0x800);
}

static int stm32md_flash_erase(struct target_s *target, uint32_t addr, int len)
{
	return stm32f1_flash_erase(target, addr, len, 0x400);
}

static int stm32f1_flash_write_words(struct target_s *target, uint32_t dest, 
			  const uint32_t *src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t data[(len>>2)+2];

	/* Construct data buffer used by stub */
	data[0] = dest & 0xFFFFFFFE;
	data[1] = len & 0xFFFFFFFE;
	memcpy(&data[2], src, len);

	/* Write stub and data to target ram and set PC */
	target_mem_write_words(target, 0x20000000, (void*)stm32f1_flash_write_stub, 0x2C);
	target_mem_write_words(target, 0x2000002C, data, len + 8);
	target_pc_write(target, 0x20000000);
	if(target_check_error(target))
		return -1;

	/* Execute the stub */
	target_halt_resume(target, 0);
	while(!target_halt_wait(target));

	/* Check for error */
	if (adiv5_ap_mem_read(ap, FLASH_SR) & SR_ERROR_MASK)
		return -1;

	return 0;
}

