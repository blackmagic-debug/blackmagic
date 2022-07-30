/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 Rafael Silva <perigoso@riseup.net>
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

#include "target_probe.h"

#define CORTEXA_PROBE_WEAK_NOP __attribute__((weak, alias("cortexa_probe_nop")))
#define CORTEXM_PROBE_WEAK_NOP __attribute__((weak, alias("cortexm_probe_nop")))
#define TARGET_PROBE_WEAK_NOP __attribute__((weak, alias("target_probe_nop")))

static inline bool cortexa_probe_nop(ADIv5_AP_t *apb, uint32_t debug_base)
{
	(void)apb;
	(void)debug_base;
	return false;
}

static inline bool cortexm_probe_nop(ADIv5_AP_t *ap)
{
	(void)ap;
	return false;
}

static inline bool target_probe_nop(target *t)
{
	(void)t;
	return false;
}

/*
 * nop alias functions to allow suport for target probe methods
 * to be disabled by not compiling/linking them in.
 */

bool cortexa_probe(ADIv5_AP_t *apb, uint32_t debug_base) CORTEXA_PROBE_WEAK_NOP;

bool cortexm_probe(ADIv5_AP_t *ap) CORTEXM_PROBE_WEAK_NOP;

bool kinetis_mdm_probe(ADIv5_AP_t *ap) CORTEXM_PROBE_WEAK_NOP;
bool nrf51_mdm_probe(ADIv5_AP_t *ap) CORTEXM_PROBE_WEAK_NOP;
bool efm32_aap_probe(ADIv5_AP_t *ap) CORTEXM_PROBE_WEAK_NOP;
bool rp_rescue_probe(ADIv5_AP_t *ap) CORTEXM_PROBE_WEAK_NOP;

bool ch32f1_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool gd32f1_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool stm32f1_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool at32fxx_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool stm32f4_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool stm32h7_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool stm32l0_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool stm32l1_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool stm32l4_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool stm32g0_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool lmi_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool lpc11xx_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool lpc15xx_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool lpc17xx_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool lpc43xx_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool lpc546xx_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool samx7x_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool sam3x_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool sam4l_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool nrf51_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool samd_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool samx5x_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool kinetis_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool efm32_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool msp432_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool ke04_probe(target *t) TARGET_PROBE_WEAK_NOP;
bool rp_probe(target *t) TARGET_PROBE_WEAK_NOP;
