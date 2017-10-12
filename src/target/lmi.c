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
#include "target_internal.h"
#include "cortexm.h"

#define SRAM_BASE            0x20000000
#define STUB_BUFFER_BASE     ALIGN(SRAM_BASE + sizeof(lmi_flash_write_stub), 4)

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

static int lmi_flash_erase(struct target_flash *f, target_addr addr, size_t len);
static int lmi_flash_write(struct target_flash *f,
                           target_addr dest, const void *src, size_t len);

static const char lmi_driver_str[] = "TI Stellaris/Tiva";

static const uint16_t lmi_flash_write_stub[] = {
#include "flashstub/lmi.stub"
};

static void lmi_add_flash(target *t, size_t length)
{
	struct target_flash *f = calloc(1, sizeof(*f));
	f->start = 0;
	f->length = length;
	f->blocksize = 0x400;
	f->erase = lmi_flash_erase;
	f->write = lmi_flash_write;
	f->erased = 0xff;
	target_add_flash(t, f);
}

bool lmi_probe(target *t)
{
	uint32_t did1 = target_mem_read32(t, LMI_SCB_DID1);
	switch (did1 >> 16) {
	case 0x1049:	/* LM3S3748 */
		t->driver = lmi_driver_str;
		target_add_ram(t, 0x20000000, 0x8000);
		lmi_add_flash(t, 0x40000);
		return true;

	case 0x10A1:	/* TM4C123GH6PM */
		t->driver = lmi_driver_str;
		target_add_ram(t, 0x20000000, 0x10000);
		lmi_add_flash(t, 0x80000);
		/* On Tiva targets, asserting SRST results in the debug
		 * logic also being reset.  We can't assert SRST and must
		 * only use the AIRCR SYSRESETREQ. */
		t->target_options |= CORTEXM_TOPT_INHIBIT_SRST;
		return true;

	case 0x1022:    /* TM4C1230C3PM */
		t->driver = lmi_driver_str;
		target_add_ram(t, 0x20000000, 0x6000);
		lmi_add_flash(t, 0x10000);
		t->target_options |= CORTEXM_TOPT_INHIBIT_SRST;
		return true;
	}
	return false;
}

int lmi_flash_erase(struct target_flash *f, target_addr addr, size_t len)
{
	target  *t = f->t;

	target_check_error(t);

	while(len) {
		target_mem_write32(t, LMI_FLASH_FMA, addr);
		target_mem_write32(t, LMI_FLASH_FMC,
		                   LMI_FLASH_FMC_WRKEY | LMI_FLASH_FMC_ERASE);
		while (target_mem_read32(t, LMI_FLASH_FMC) &
		       LMI_FLASH_FMC_ERASE);

		if (target_check_error(t))
			return -1;

		len -= BLOCK_SIZE;
		addr += BLOCK_SIZE;
	}
	return 0;
}

int lmi_flash_write(struct target_flash *f,
                    target_addr dest, const void *src, size_t len)
{
	target  *t = f->t;

	target_check_error(t);

	target_mem_write(t, SRAM_BASE, lmi_flash_write_stub,
	                 sizeof(lmi_flash_write_stub));
	target_mem_write(t, STUB_BUFFER_BASE, src, len);

	if (target_check_error(t))
		return -1;

	return cortexm_run_stub(t, SRAM_BASE, dest, STUB_BUFFER_BASE, len, 0);
}
