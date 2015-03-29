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

/* This file implements TI/LMI LM3S target specific functions providing
 * the XML memory map and Flash memory programming.
 *
 * According to: TivaTM TM4C123GH6PM Microcontroller Datasheet
 */

#include "general.h"
#include "target.h"
#include "cortexm.h"

#define SRAM_BASE            0x20000000
#define BLOCK_SIZE           0x400
#define LMI_SCB_BASE         0x400FE000
#define LMI_SCB_DID1         (LMI_SCB_BASE + 0x004)

#define LMI_FLASH_BASE       0x400FD000
#define LMI_FLASH_FMA        (LMI_FLASH_BASE + 0x000)
#define LMI_FLASH_FMC        (LMI_FLASH_BASE + 0x008)

#define LMI_FLASH_FMC_WRITE  (1 << 0)
#define LMI_FLASH_FMC_ERASE  (1 << 1)
#define LMI_FLASH_FMC_MERASE (1 << 2)
#define LMI_FLASH_FMC_COMT   (1 << 3)
#define LMI_FLASH_FMC_WRKEY  0xA4420000

static int lmi_flash_erase(target *t, uint32_t addr, size_t len);
static int lmi_flash_write(target *t, uint32_t dest,
			  const uint8_t *src, size_t len);

static const char lmi_driver_str[] = "TI Stellaris/Tiva";

static const char lmi_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0\" length=\"0x20000\">"
	"    <property name=\"blocksize\">0x400</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x10000\"/>"
	"</memory-map>";

static const char tm4c123gh6pm_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0\" length=\"0x40000\">"
	"    <property name=\"blocksize\">0x400</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x8000\"/>"
	"</memory-map>";


static const uint16_t lmi_flash_write_stub[] = {
// _start:
	0x4809,	// ldr r0, [pc, #36] // _flashbase
	0x490b,	// ldr r1, [pc, #44] // _addr
	0x467a, // mov r2, pc
	0x3230, // adds r2, #48
	0x4b0a, // ldr r3, [pc, #40] // _size
	0x4d08, // ldr r5, [pc, #32] // _flash_write_cmd
// _next:
 	0xb15b, // cbz r3, _done
	0x6001, // str r1, [r0, #0]
	0x6814, // ldr r4, [r2]
	0x6044, // str r4, [r0, #4]
	0x6085, // str r5, [r0, #8]
// _wait:
	0x6884, // ldr r4, [r0, #8]
	0x2601, // movs r6, #1
	0x4234, // tst r4, r6
	0xd1fb, // bne _wait

	0x3b01, // subs r3, #1
	0x3104, // adds r1, #4
	0x3204, // adds r2, #4
	0xe7f2, // b _next
// _done:
	0xbe00, // bkpt
// _flashbase:
 	0xd000, 0x400f, // .word 0x400fd000
// _flash_write_cmd:
 	0x0001, 0xa442, // .word 0xa4420001
// _addr:
// 	0x0000, 0x0000,
// _size:
// 	0x0000, 0x0000,
// _data:
// 	...
};

bool lmi_probe(target *t)
{
	uint32_t did1 = target_mem_read32(t, LMI_SCB_DID1);
	switch (did1 >> 16) {
	case 0x1049:	/* LM3S3748 */
		t->driver = lmi_driver_str;
		t->xml_mem_map = lmi_xml_memory_map;
		t->flash_erase = lmi_flash_erase;
		t->flash_write = lmi_flash_write;
		return true;

	case 0x10A1:	/* TM4C123GH6PM */
		t->driver = lmi_driver_str;
		t->xml_mem_map = tm4c123gh6pm_xml_memory_map;
		t->flash_erase = lmi_flash_erase;
		t->flash_write = lmi_flash_write;
		return true;
	}
	return false;
}

int lmi_flash_erase(target *t, uint32_t addr, size_t len)
{
	addr &= ~(BLOCK_SIZE - 1);
	len &= ~(BLOCK_SIZE - 1);

	while(len) {
		target_mem_write32(t, LMI_FLASH_FMA, addr);
		target_mem_write32(t, LMI_FLASH_FMC,
		                   LMI_FLASH_FMC_WRKEY | LMI_FLASH_FMC_ERASE);
		while (target_mem_read32(t, LMI_FLASH_FMC) &
		       LMI_FLASH_FMC_ERASE);

		len -= BLOCK_SIZE;
		addr += BLOCK_SIZE;
	}
	return 0;
}

int lmi_flash_write(target *t, uint32_t dest, const uint8_t *src, size_t len)
{
	uint32_t data[(len>>2)+2];
	data[0] = dest;
	data[1] = len >> 2;
	memcpy(&data[2], src, len);
	DEBUG("Sending stub\n");
	target_mem_write(t, SRAM_BASE, lmi_flash_write_stub, 0x30);
	DEBUG("Sending data\n");
	target_mem_write(t, 0x20000030, data, len + 8);
	DEBUG("Running stub\n");
	cortexm_pc_write(t, 0x20000000);
	target_halt_resume(t, 0);
	DEBUG("Waiting for halt\n");
	while(!target_halt_wait(t));

	return 0;
}

