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

#include "target_probes.h"

#define CORTEXA_PROBE_DUMMY __attribute__((weak, alias("_cortexa_probe_dummy")))
#define CORTEXM_PROBE_DUMMY __attribute__((weak, alias("_cortexm_probe_dummy")))
#define TARGET_PROBE_DUMMY  __attribute__((weak, alias("_target_probe_dummy")))

static inline bool _target_probe_dummy(target *t)
{
	(void)t;

	return false;
}

static inline bool _cortexa_probe_dummy(ADIv5_AP_t *apb, uint32_t debug_base)
{
	(void)apb;
	(void)debug_base;

	return false;
}

static inline bool _cortexm_probe_dummy(ADIv5_AP_t *ap)
{
	(void)ap;

	return false;
}

/*
 * "dummy" alias functions to allow suport for these targets
 * to be disabled by not compiling them in.
 */

bool cortexa_probe(ADIv5_AP_t *apb, uint32_t debug_base) CORTEXA_PROBE_DUMMY;

bool cortexm_probe(ADIv5_AP_t *ap) CORTEXM_PROBE_DUMMY;

bool kinetis_mdm_probe(ADIv5_AP_t *ap) CORTEXM_PROBE_DUMMY;
bool nrf51_mdm_probe(ADIv5_AP_t *ap) CORTEXM_PROBE_DUMMY;
bool efm32_aap_probe(ADIv5_AP_t *ap) CORTEXM_PROBE_DUMMY;
bool rp_rescue_probe(ADIv5_AP_t *ap) CORTEXM_PROBE_DUMMY;

bool ch32f1_probe(target *t) TARGET_PROBE_DUMMY; // will catch all the clones
bool gd32f1_probe(target *t) TARGET_PROBE_DUMMY;
bool stm32f1_probe(target *t) TARGET_PROBE_DUMMY;
bool stm32f4_probe(target *t) TARGET_PROBE_DUMMY;
bool stm32h7_probe(target *t) TARGET_PROBE_DUMMY;
bool stm32l0_probe(target *t) TARGET_PROBE_DUMMY;
bool stm32l1_probe(target *t) TARGET_PROBE_DUMMY;
bool stm32l4_probe(target *t) TARGET_PROBE_DUMMY;
bool stm32g0_probe(target *t) TARGET_PROBE_DUMMY;
bool lmi_probe(target *t) TARGET_PROBE_DUMMY;
bool lpc11xx_probe(target *t) TARGET_PROBE_DUMMY;
bool lpc15xx_probe(target *t) TARGET_PROBE_DUMMY;
bool lpc17xx_probe(target *t) TARGET_PROBE_DUMMY;
bool lpc43xx_probe(target *t) TARGET_PROBE_DUMMY;
bool lpc546xx_probe(target *t) TARGET_PROBE_DUMMY;
bool samx7x_probe(target *t) TARGET_PROBE_DUMMY;
bool sam3x_probe(target *t) TARGET_PROBE_DUMMY;
bool sam4l_probe(target *t) TARGET_PROBE_DUMMY;
bool nrf51_probe(target *t) TARGET_PROBE_DUMMY;
bool samd_probe(target *t) TARGET_PROBE_DUMMY;
bool samx5x_probe(target *t) TARGET_PROBE_DUMMY;
bool kinetis_probe(target *t) TARGET_PROBE_DUMMY;
bool efm32_probe(target *t) TARGET_PROBE_DUMMY;
bool msp432_probe(target *t) TARGET_PROBE_DUMMY;
bool ke04_probe(target *t) TARGET_PROBE_DUMMY;
bool rp_probe(target *t) TARGET_PROBE_DUMMY;
