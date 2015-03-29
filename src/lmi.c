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
#define STUB_BUFFER_BASE     (SRAM_BASE + 0x30)

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
#include "../flashstub/lmi.stub"
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

	target_mem_write(t, SRAM_BASE, lmi_flash_write_stub,
	                 sizeof(lmi_flash_write_stub));
	target_mem_write(t, STUB_BUFFER_BASE, data, len + 8);
	return cortexm_run_stub(t, SRAM_BASE, 0, 0, 0, 0);
}

