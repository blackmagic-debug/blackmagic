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
#include "command.h"
#include "gdb_packet.h"

static bool stm32f1_cmd_erase_mass(target *t);
static bool stm32f1_cmd_option(target *t, int argc, char *argv[]);

const struct command_s stm32f1_cmd_list[] = {
	{"erase_mass", (cmd_handler)stm32f1_cmd_erase_mass, "Erase entire flash memory"},
	{"option", (cmd_handler)stm32f1_cmd_option, "Manipulate option bytes"},
	{NULL, NULL, NULL}
};


static int stm32md_flash_erase(struct target_s *target, uint32_t addr, int len);
static int stm32hd_flash_erase(struct target_s *target, uint32_t addr, int len);
static int stm32f1_flash_erase(struct target_s *target, uint32_t addr, int len,
				uint32_t pagesize);
static int stm32f1_flash_write(struct target_s *target, uint32_t dest,
			const uint8_t *src, int len);

static const char stm32f1_driver_str[] = "STM32, Medium density.";
static const char stm32hd_driver_str[] = "STM32, High density.";
static const char stm32f3_driver_str[] = "STM32F3xx";
static const char stm32f03_driver_str[] = "STM32F03x";
static const char stm32f05_driver_str[] = "STM32F05x";
static const char stm32f07_driver_str[] = "STM32F07x";

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

uint16_t stm32f1_flash_write_stub[] = {
// _start:
	0x4809,	// ldr r0, [pc, #36] // _flashbase
	0x490a,	// ldr r1, [pc, #40] // _addr
	0x467a, // mov r2, pc
	0x322c, // adds r2, #44
	0x4b09, // ldr r3, [pc, #36] // _size
 	0x2501, // movs r5, #1
// _next:
	0x2b00, // cmp r3, #0
	0xd00a, // beq _done
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
	0xe7f2, // b _next
// _done:
	0xbe00, // bkpt
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

bool stm32f1_probe(struct target_s *target)
{

	target->idcode = adiv5_ap_mem_read(adiv5_target_ap(target), DBGMCU_IDCODE) & 0xfff;
	switch(target->idcode) {
	case 0x410:  /* Medium density */
	case 0x412:  /* Low denisty */
	case 0x420:  /* Value Line, Low-/Medium density */
		target->driver = stm32f1_driver_str;
		target->xml_mem_map = stm32f1_xml_memory_map;
		target->flash_erase = stm32md_flash_erase;
		target->flash_write = stm32f1_flash_write;
		target_add_commands(target, stm32f1_cmd_list, "STM32 LD/MD");
		return true;
	case 0x414:	 /* High density */
	case 0x418:  /* Connectivity Line */
	case 0x428:	 /* Value Line, High Density */
		target->driver = stm32hd_driver_str;
		target->xml_mem_map = stm32hd_xml_memory_map;
		target->flash_erase = stm32hd_flash_erase;
		target->flash_write = stm32f1_flash_write;
		target_add_commands(target, stm32f1_cmd_list, "STM32 HD/CL");
		return true;
	case 0x422:  /* STM32F30x */
	case 0x432:  /* STM32F37x */
		target->driver = stm32f3_driver_str;
		target->xml_mem_map = stm32hd_xml_memory_map;
		target->flash_erase = stm32hd_flash_erase;
		target->flash_write = stm32f1_flash_write;
		target_add_commands(target, stm32f1_cmd_list, "STM32F3");
		return true;
	}

	target->idcode = adiv5_ap_mem_read(adiv5_target_ap(target), DBGMCU_IDCODE_F0) & 0xfff;
	switch(target->idcode) {
	case 0x444:  /* STM32F03 */
	case 0x440:  /* STM32F05 */
	case 0x448:  /* STM32F07 */
		switch(target->idcode) {
		case 0x444:  /* STM32F03 */
			target->driver = stm32f03_driver_str;
			break;
		case 0x440:  /* STM32F05 */
			target->driver = stm32f05_driver_str;
			break;
		case 0x448:  /* STM32F07 */
			target->driver = stm32f07_driver_str;
			break;
		}
		target->xml_mem_map = stm32f1_xml_memory_map;
		target->flash_erase = stm32md_flash_erase;
		target->flash_write = stm32f1_flash_write;
		target_add_commands(target, stm32f1_cmd_list, "STM32F0");
		return true;
	}

	return false;
}

static void stm32f1_flash_unlock(ADIv5_AP_t *ap)
{
	adiv5_ap_mem_write(ap, FLASH_KEYR, KEY1);
	adiv5_ap_mem_write(ap, FLASH_KEYR, KEY2);
}

static int stm32f1_flash_erase(struct target_s *target, uint32_t addr, int len, uint32_t pagesize)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint16_t sr;

	addr &= ~(pagesize - 1);
	len &= ~(pagesize - 1);

	stm32f1_flash_unlock(ap);

	while(len) {
		/* Flash page erase instruction */
		adiv5_ap_mem_write(ap, FLASH_CR, FLASH_CR_PER);
		/* write address to FMA */
		adiv5_ap_mem_write(ap, FLASH_AR, addr);
		/* Flash page erase start instruction */
		adiv5_ap_mem_write(ap, FLASH_CR, FLASH_CR_STRT | FLASH_CR_PER);

		/* Read FLASH_SR to poll for BSY bit */
		while(adiv5_ap_mem_read(ap, FLASH_SR) & FLASH_SR_BSY)
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

static int stm32f1_flash_write(struct target_s *target, uint32_t dest,
			  const uint8_t *src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint32_t offset = dest % 4;
	uint32_t words = (offset + len + 3) / 4;
	uint32_t data[2 + words];

	/* Construct data buffer used by stub */
	data[0] = dest - offset;
	data[1] = words * 4;		/* length must always be a multiple of 4 */
	data[2] = 0xFFFFFFFF;		/* pad partial words with all 1s to avoid */
	data[words + 1] = 0xFFFFFFFF;	/* damaging overlapping areas */
	memcpy((uint8_t *)&data[2] + offset, src, len);

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

static bool stm32f1_cmd_erase_mass(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	stm32f1_flash_unlock(ap);

	/* Flash mass erase start instruction */
	adiv5_ap_mem_write(ap, FLASH_CR, FLASH_CR_MER);
	adiv5_ap_mem_write(ap, FLASH_CR, FLASH_CR_STRT | FLASH_CR_MER);

	/* Read FLASH_SR to poll for BSY bit */
	while(adiv5_ap_mem_read(ap, FLASH_SR) & FLASH_SR_BSY)
		if(target_check_error(t))
			return false;

	/* Check for error */
	uint16_t sr = adiv5_ap_mem_read(ap, FLASH_SR);
	if ((sr & SR_ERROR_MASK) || !(sr & SR_EOP))
		return false;

	return true;
}

static bool stm32f1_option_erase(target *t)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	/* Erase option bytes instruction */
	adiv5_ap_mem_write(ap, FLASH_CR, FLASH_CR_OPTER | FLASH_CR_OPTWRE);
	adiv5_ap_mem_write(ap, FLASH_CR,
			FLASH_CR_STRT | FLASH_CR_OPTER | FLASH_CR_OPTWRE);
	/* Read FLASH_SR to poll for BSY bit */
	while(adiv5_ap_mem_read(ap, FLASH_SR) & FLASH_SR_BSY)
		if(target_check_error(t))
			return false;
	return true;
}

static bool stm32f1_option_write_erased(target *t, uint32_t addr, uint16_t value)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);

	if (value == 0xffff)
		return true;
	/* Erase option bytes instruction */
	adiv5_ap_mem_write(ap, FLASH_CR, FLASH_CR_OPTPG | FLASH_CR_OPTWRE);
	adiv5_ap_mem_write_halfword(ap, addr, value);
	/* Read FLASH_SR to poll for BSY bit */
	while(adiv5_ap_mem_read(ap, FLASH_SR) & FLASH_SR_BSY)
		if(target_check_error(t))
			return false;
	return true;
}

static bool stm32f1_option_write(target *t, uint32_t addr, uint16_t value)
{
	ADIv5_AP_t *ap = adiv5_target_ap(t);
	uint16_t opt_val[8];
	int i, index;

	index = (addr - FLASH_OBP_RDP) / 2;
	if ((index < 0) || (index > 7))
		 return false;
	/* Retrieve old values */
	for (i = 0; i < 16; i = i +4) {
		 uint32_t val = adiv5_ap_mem_read(ap, FLASH_OBP_RDP + i);
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
	ADIv5_AP_t *ap = adiv5_target_ap(t);
	uint32_t rdprt;

	switch(t->idcode) {
	case 0x422:  /* STM32F30x */
	case 0x432:  /* STM32F37x */
	case 0x440:  /* STM32F0 */
		flash_obp_rdp_key = FLASH_OBP_RDP_KEY_F3;
		break;
	default: flash_obp_rdp_key = FLASH_OBP_RDP_KEY;
	}
	rdprt = (adiv5_ap_mem_read(ap, FLASH_OBR) & FLASH_OBR_RDPRT);
	stm32f1_flash_unlock(ap);
	adiv5_ap_mem_write(ap, FLASH_OPTKEYR, KEY1);
	adiv5_ap_mem_write(ap, FLASH_OPTKEYR, KEY2);

	if ((argc == 2) && !strcmp(argv[1], "erase")) {
		stm32f1_option_erase(t);
		stm32f1_option_write_erased(t, FLASH_OBP_RDP, flash_obp_rdp_key);
	} else if (rdprt) {
		gdb_out("Device is Read Protected\n");
		gdb_out("Use \"monitor option erase\" to unprotect, erasing device\n");
		return true;
	} else if (argc == 3) {
		addr = strtol(argv[1], NULL, 0);
		val = strtol(argv[2], NULL, 0);
		stm32f1_option_write(t, addr, val);
	} else {
		gdb_out("usage: monitor option erase\n");
		gdb_out("usage: monitor option <addr> <value>\n");
	}

	if (0 && flash_obp_rdp_key == FLASH_OBP_RDP_KEY_F3) {
		/* Reload option bytes on F0 and F3*/
		val = adiv5_ap_mem_read(ap, FLASH_CR);
		val |= FLASH_CR_OBL_LAUNCH;
		stm32f1_option_write(t, FLASH_CR, val);
		val &= ~FLASH_CR_OBL_LAUNCH;
		stm32f1_option_write(t, FLASH_CR, val);
	}

	for (int i = 0; i < 0xf; i += 4) {
		addr = 0x1ffff800 + i;
		val = adiv5_ap_mem_read(ap, addr);
		gdb_outf("0x%08X: 0x%04X\n", addr, val & 0xFFFF);
		gdb_outf("0x%08X: 0x%04X\n", addr + 2, val >> 16);
	}
	return true;
}

