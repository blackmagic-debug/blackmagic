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

/*
 * This file implements TI/LMI LM3S target specific functions providing
 * the XML memory map and Flash memory programming.
 *
 * According to: TivaTM TM4C123GH6PM Microcontroller Datasheet
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define SRAM_BASE        0x20000000U
#define STUB_BUFFER_BASE ALIGN(SRAM_BASE + sizeof(lmi_flash_write_stub), 4U)

#define BLOCK_SIZE 0x400U

#define LMI_SCB_BASE 0x400fe000U
#define LMI_SCB_DID0 (LMI_SCB_BASE + 0x000U)
#define LMI_SCB_DID1 (LMI_SCB_BASE + 0x004U)

#define DID0_CLASS_MASK           0x00ff0000U
#define DID0_CLASS_STELLARIS_FURY 0x00010000U
#define DID0_CLASS_STELLARIS_DUST 0x00030000U
#define DID0_CLASS_TIVA_C123      0x00050000U
#define DID0_CLASS_TIVA_C129      0x000a0000U

#define DID1_LM3S3748      0x1049U
#define DID1_LM3S5732      0x1096U
#define DID1_LM3S8962      0x10a6U
#define DID1_TM4C123GH6PM  0x10a1U
#define DID1_TM4C1230C3PM  0x1022U
#define DID1_TM4C1294NCPDT 0x101fU

#define LMI_FLASH_BASE 0x400fd000U
#define LMI_FLASH_FMA  (LMI_FLASH_BASE + 0x000U)
#define LMI_FLASH_FMC  (LMI_FLASH_BASE + 0x008U)

#define LMI_FLASH_FMC_WRITE  (1U << 0U)
#define LMI_FLASH_FMC_ERASE  (1U << 1U)
#define LMI_FLASH_FMC_MERASE (1U << 2U)
#define LMI_FLASH_FMC_COMT   (1U << 3U)
#define LMI_FLASH_FMC_WRKEY  0xa4420000U

static bool lmi_flash_erase(target_flash_s *f, target_addr_t addr, size_t len);
static bool lmi_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len);
static bool lmi_mass_erase(target_s *t);

static const char lmi_driver_str[] = "TI Stellaris/Tiva";

static const uint16_t lmi_flash_write_stub[] = {
#include "flashstub/lmi.stub"
};

static void lmi_add_flash(target_s *t, size_t length)
{
	target_flash_s *f = calloc(1, sizeof(*f));
	if (!f) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	f->start = 0;
	f->length = length;
	f->blocksize = 0x400;
	f->erase = lmi_flash_erase;
	f->write = lmi_flash_write;
	f->erased = 0xff;
	target_add_flash(t, f);
}

bool lm3s_probe(target_s *const t, const uint16_t did1)
{
	const char *driver = t->driver;
	t->driver = lmi_driver_str;
	switch (did1) {
	case DID1_LM3S3748:
	case DID1_LM3S5732:
		target_add_ram(t, 0x20000000U, 0x10000U);
		lmi_add_flash(t, 0x20000U);
		break;
	case DID1_LM3S8962:
		target_add_ram(t, 0x2000000U, 0x10000U);
		lmi_add_flash(t, 0x40000U);
		break;
	default:
		t->driver = driver;
		return false;
	}
	t->mass_erase = lmi_mass_erase;
	return true;
}

bool tm4c_probe(target_s *const t, const uint16_t did1)
{
	const char *driver = t->driver;
	t->driver = lmi_driver_str;
	switch (did1) {
	case DID1_TM4C123GH6PM:
		target_add_ram(t, 0x20000000, 0x10000);
		lmi_add_flash(t, 0x80000);
		/* On Tiva targets, asserting nRST results in the debug
		 * logic also being reset.  We can't assert nRST and must
		 * only use the AIRCR SYSRESETREQ. */
		t->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
		break;
	case DID1_TM4C1230C3PM:
		target_add_ram(t, 0x20000000, 0x6000);
		lmi_add_flash(t, 0x10000);
		t->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
		break;
	case DID1_TM4C1294NCPDT:
		target_add_ram(t, 0x20000000, 0x40000);
		lmi_add_flash(t, 0x100000);
		t->target_options |= CORTEXM_TOPT_INHIBIT_NRST;
		break;
	default:
		t->driver = driver;
		return false;
	}
	t->mass_erase = lmi_mass_erase;
	return true;
}

bool lmi_probe(target_s *const t)
{
	const uint32_t did0 = target_mem_read32(t, LMI_SCB_DID0);
	const uint16_t did1 = target_mem_read32(t, LMI_SCB_DID1) >> 16U;

	switch (did0 & DID0_CLASS_MASK) {
	case DID0_CLASS_STELLARIS_FURY:
	case DID0_CLASS_STELLARIS_DUST:
		return lm3s_probe(t, did1);
	case DID0_CLASS_TIVA_C123:
	case DID0_CLASS_TIVA_C129:
		return tm4c_probe(t, did1);
	default:
		return false;
	}
}

static bool lmi_flash_erase(target_flash_s *f, target_addr_t addr, const size_t len)
{
	target_s *t = f->t;
	target_check_error(t);

	const bool full_erase = addr == f->start && len == f->length;
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 500);

	for (size_t erased = 0; erased < len; erased += BLOCK_SIZE) {
		target_mem_write32(t, LMI_FLASH_FMA, addr);
		target_mem_write32(t, LMI_FLASH_FMC, LMI_FLASH_FMC_WRKEY | LMI_FLASH_FMC_ERASE);

		while (target_mem_read32(t, LMI_FLASH_FMC) & LMI_FLASH_FMC_ERASE) {
			if (full_erase)
				target_print_progress(&timeout);
		}

		if (target_check_error(t))
			return false;

		addr += BLOCK_SIZE;
	}
	return true;
}

static bool lmi_flash_write(target_flash_s *f, target_addr_t dest, const void *src, size_t len)
{
	target_s *t = f->t;
	target_check_error(t);
	target_mem_write(t, SRAM_BASE, lmi_flash_write_stub, sizeof(lmi_flash_write_stub));
	target_mem_write(t, STUB_BUFFER_BASE, src, len);
	if (target_check_error(t))
		return false;

	return cortexm_run_stub(t, SRAM_BASE, dest, STUB_BUFFER_BASE, len, 0) == 0;
}

static bool lmi_mass_erase(target_s *t)
{
	return lmi_flash_erase(t->flash, t->flash->start, t->flash->length);
}
