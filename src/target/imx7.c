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
 * This file implements NXP i.MX 7Solo/Dual detection and reset.
 *
 * Reference:
 * - MX7DRM, Rev. 0.1, 08/2016
 * - SoC detection from U-Boot v2016.11
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortexm.h"

#define IMX7_AHP_AP_IDR		0x64770001

#define CCM_ANALOG_DIGPROG	0x30360800

#define SRC_A7RCR1		0x30390008
#define SRC_A7RCR1_A7_CORE1_ENABLE	(0x1 << 1)

#define SRC_M4RCR		0x3039000C
#define SRC_M4RCR_SW_M4C_NON_SCLR_RST	(0x1 << 0)
#define SRC_M4RCR_SW_M4C_RST		(0x1 << 1)

static bool imx7_rom_table_filter(uint32_t addr);
extern uint32_t adiv5_mem_read32(ADIv5_AP_t *ap, uint32_t addr);

static void imx7m4_extended_reset(target *t);
static bool imx7m4_attach(target *t);

void imx7_ahb_probe(ADIv5_AP_t *ap)
{
	uint32_t val;
	switch(ap->idr) {
	case IMX7_AHP_AP_IDR:
		break;
	default:
		return;
	}

	val = adiv5_mem_read32(ap, SRC_A7RCR1);
	if (!(val & SRC_A7RCR1_A7_CORE1_ENABLE)) {
		DEBUG("i.MX 7 Secondary A7 core not enabled, installing ROM filter.\n");
		ap->dp->rom_table_filter = imx7_rom_table_filter;
	}
}

static bool imx7_rom_table_filter(uint32_t addr)
{
	switch (addr) {
	case 0x80072000UL:
	case 0x80073000UL:
	case 0x8007d000UL:
	case 0x80082000UL:
		return true;
	default:
		return false;
	}
}

bool imx7m4_probe(target *t)
{
	t->idcode = (target_mem_read32(t, CCM_ANALOG_DIGPROG) >> 16) & 0xff;
	switch(t->idcode) {
	case 0x72:
		t->driver = "i.MX 7Solo/Dual ARM Cortex-M4";
		t->extended_reset = imx7m4_extended_reset;
		t->attach = imx7m4_attach;
		target_add_ram(t, 0x00000000, 0x8000); /* OCRAM_S ALIAS CODE */
		target_add_ram(t, 0x00180000, 0x8000); /* OCRAM_S */
		target_add_ram(t, 0x00900000, 0x20000); /* OCRAM ALIAS CODE */
		target_add_ram(t, 0x1fff8000, 0x8000); /* TCML */
		target_add_ram(t, 0x20000000, 0x8000); /* TCMU */
		target_add_ram(t, 0x20200000, 0x20000); /* OCRAM */
		target_add_ram(t, 0x80000000, 0x60000000); /* DDR */

		return true;
	}

	return false;
}

static void
imx7m4_extended_reset(target *t)
{
	uint32_t val;

	/* Reset Cortex-M4 core */
	val = target_mem_read32(t, SRC_M4RCR);
	val |= SRC_M4RCR_SW_M4C_RST;
	target_mem_write32(t, SRC_M4RCR, val);
}

static bool imx7m4_attach(target *t)
{
	uint32_t val;

	/* Take Cortex-M4 core out of non-clearing reset */
	val = target_mem_read32(t, SRC_M4RCR);
	val &= ~SRC_M4RCR_SW_M4C_NON_SCLR_RST;
	target_mem_write32(t, SRC_M4RCR, val);

	return cortexm_attach(t);
}
