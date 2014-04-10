/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012 Vegard Storheil Eriksen <zyp@jvnv.net>
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

/* This file implements STM32L1 target specific functions for detecting
 * the device, providing the XML memory map and Flash memory programming.
 *
 * Refereces:
 * ST doc - RM0038
 *   Reference manual - STM32L151xx, STM32L152xx and STM32L162xx
 *   advanced ARM-based 32-bit MCUs
 * ST doc - PM0062
 *   Programming manual - STM32L151xx, STM32L152xx and STM32L162xx
 *   Flash and EEPROM programming
 */

#include <stdlib.h>
#include <string.h>

#include "general.h"
#include "adiv5.h"
#include "target.h"
#include "command.h"
#include "gdb_packet.h"

static int stm32l1_flash_erase(struct target_s *target, uint32_t addr, int len);
static int stm32l1_flash_write(struct target_s *target, uint32_t dest,
			const uint8_t *src, int len);

static const char stm32l1_driver_str[] = "STM32L1xx";

static const char stm32l1_xml_memory_map[] = "<?xml version=\"1.0\"?>"
/*	"<!DOCTYPE memory-map "
	"             PUBLIC \"+//IDN gnu.org//DTD GDB Memory Map V1.0//EN\""
	"                    \"http://sourceware.org/gdb/gdb-memory-map.dtd\">"*/
	"<memory-map>"
	"  <memory type=\"flash\" start=\"0x8000000\" length=\"0x80000\">"
	"    <property name=\"blocksize\">0x100</property>"
	"  </memory>"
	"  <memory type=\"ram\" start=\"0x20000000\" length=\"0x5000\"/>"
	"</memory-map>";

/* Flash Controller Register Map */
#define STM32L1_FLASH_BASE	0x40023C00
#define STM32L1_FLASH_ACR     (STM32L1_FLASH_BASE + 0x00)
#define STM32L1_FLASH_PECR    (STM32L1_FLASH_BASE + 0x04)
#define STM32L1_FLASH_PDKEYR  (STM32L1_FLASH_BASE + 0x08)
#define STM32L1_FLASH_PEKEYR  (STM32L1_FLASH_BASE + 0x0C)
#define STM32L1_FLASH_PRGKEYR (STM32L1_FLASH_BASE + 0x10)
#define STM32L1_FLASH_OPTKEYR (STM32L1_FLASH_BASE + 0x14)
#define STM32L1_FLASH_SR      (STM32L1_FLASH_BASE + 0x18)
#define STM32L1_FLASH_OBR     (STM32L1_FLASH_BASE + 0x1C)
#define STM32L1_FLASH_WRPR1   (STM32L1_FLASH_BASE + 0x20)
#define STM32L1_FLASH_WRPR2   (STM32L1_FLASH_BASE + 0x80)
#define STM32L1_FLASH_WRPR3   (STM32L1_FLASH_BASE + 0x84)

#define STM32L1_FLASH_PECR_FPRG  (1 << 10)
#define STM32L1_FLASH_PECR_ERASE (1 <<  9)
#define STM32L1_FLASH_PECR_PROG  (1 <<  3)

#define STM32L1_FLASH_SR_BSY (1 << 0)
#define STM32L1_FLASH_SR_EOP (1 << 1)

#define STM32L1_FLASH_SR_ERROR_MASK (0x1f << 8)

#define STM32L1_PEKEY1 0x89ABCDEF
#define STM32L1_PEKEY2 0x02030405
#define STM32L1_PRGKEY1 0x8C9DAEBF
#define STM32L1_PRGKEY2 0x13141516

#define STM32L1_DBGMCU_IDCODE	0xE0042000

bool stm32l1_probe(struct target_s *target)
{
	uint32_t idcode;

	idcode = adiv5_ap_mem_read(adiv5_target_ap(target), STM32L1_DBGMCU_IDCODE);
	switch(idcode & 0xFFF) {
	case 0x416:  /* Medium density */
	case 0x436:  /* High density */
		target->driver = stm32l1_driver_str;
		target->xml_mem_map = stm32l1_xml_memory_map;
		target->flash_erase = stm32l1_flash_erase;
		target->flash_write = stm32l1_flash_write;
		return true;
	}

	return false;
}

static void stm32l1_flash_unlock(ADIv5_AP_t *ap)
{
	adiv5_ap_mem_write(ap, STM32L1_FLASH_PEKEYR, STM32L1_PEKEY1);
	adiv5_ap_mem_write(ap, STM32L1_FLASH_PEKEYR, STM32L1_PEKEY2);
	adiv5_ap_mem_write(ap, STM32L1_FLASH_PRGKEYR, STM32L1_PRGKEY1);
	adiv5_ap_mem_write(ap, STM32L1_FLASH_PRGKEYR, STM32L1_PRGKEY2);
}

static int stm32l1_flash_erase(struct target_s *target, uint32_t addr, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint16_t sr;

	addr &= ~255;
	len &= ~255;

	stm32l1_flash_unlock(ap);

	/* Flash page erase instruction */
	adiv5_ap_mem_write(ap, STM32L1_FLASH_PECR, STM32L1_FLASH_PECR_ERASE | STM32L1_FLASH_PECR_PROG);

	/* Read FLASH_SR to poll for BSY bit */
	while(adiv5_ap_mem_read(ap, STM32L1_FLASH_SR) & STM32L1_FLASH_SR_BSY)
		if(target_check_error(target))
			return -1;

	while(len) {
		/* Write first word of page to 0 */
		adiv5_ap_mem_write(ap, addr, 0);

		len -= 256;
		addr += 256;
	}

	/* Disable programming mode */
	adiv5_ap_mem_write(ap, STM32L1_FLASH_PECR, 0);

	/* Check for error */
	sr = adiv5_ap_mem_read(ap, STM32L1_FLASH_SR);
	if ((sr & STM32L1_FLASH_SR_ERROR_MASK) || !(sr & STM32L1_FLASH_SR_EOP))
		return -1;

	return 0;
}

static int stm32l1_flash_write(struct target_s *target, uint32_t dest,
			  const uint8_t *src, int len)
{
	ADIv5_AP_t *ap = adiv5_target_ap(target);
	uint16_t sr;

	/* Handle non word-aligned start */
	if(dest & 3) {
		uint32_t data = 0;
		uint32_t wlen = 4 - (dest & 3);
		if(wlen > len)
			wlen = len;

		memcpy((uint8_t *)&data + (dest & 3), src, wlen);
		adiv5_ap_mem_write(ap, dest & ~3, data);
		src += wlen;
		dest += wlen;
		len -= wlen;
	}

	/* Handle non half-page-aligned start */
	if(dest & 127 && len >= 4) {
		uint32_t xlen = 128 - (dest & 127);
		if(xlen > len)
			xlen = len & ~3;

		target_mem_write_words(target, dest, (uint32_t*)src, xlen);
		src += xlen;
		dest += xlen;
		len -= xlen;
	}

	/* Write half-pages */
	if(len > 128) {
		/* Enable half page mode */
		adiv5_ap_mem_write(ap, STM32L1_FLASH_PECR, STM32L1_FLASH_PECR_FPRG | STM32L1_FLASH_PECR_PROG);

		/* Read FLASH_SR to poll for BSY bit */
		while(adiv5_ap_mem_read(ap, STM32L1_FLASH_SR) & STM32L1_FLASH_SR_BSY)
			if(target_check_error(target))
				return -1;

		target_mem_write_words(target, dest, (uint32_t*)src, len & ~127);
		src += len & ~127;
		dest += len & ~127;
		len -= len & ~127;

		/* Disable half page mode */
		adiv5_ap_mem_write(ap, STM32L1_FLASH_PECR, 0);

		/* Read FLASH_SR to poll for BSY bit */
		while(adiv5_ap_mem_read(ap, STM32L1_FLASH_SR) & STM32L1_FLASH_SR_BSY)
			if(target_check_error(target))
				return -1;
	}

	/* Handle non-full page at the end */
	if(len >= 4) {
		target_mem_write_words(target, dest, (uint32_t*)src, len & ~3);
		src += len & ~3;
		dest += len & ~3;
		len -= len & ~3;
	}

	/* Handle non-full word at the end */
	if(len) {
		uint32_t data = 0;

		memcpy((uint8_t *)&data, src, len);
		adiv5_ap_mem_write(ap, dest, data);
	}

	/* Check for error */
	sr = adiv5_ap_mem_read(ap, STM32L1_FLASH_SR);
	if ((sr & STM32L1_FLASH_SR_ERROR_MASK) || !(sr & STM32L1_FLASH_SR_EOP))
		return -1;

	return 0;
}
