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

#ifndef __TARGET_PROBE_H
#define __TARGET_PROBE_H

#include "target.h"
#include "adiv5.h"

/* Probe for various targets.
 * Actual functions implemented in their respective drivers.
 */

bool cortexa_probe(ADIv5_AP_t *apb, uint32_t debug_base);

bool cortexm_probe(ADIv5_AP_t *ap);

bool kinetis_mdm_probe(ADIv5_AP_t *ap);
bool nrf51_mdm_probe(ADIv5_AP_t *ap);
bool efm32_aap_probe(ADIv5_AP_t *ap);
bool rp_rescue_probe(ADIv5_AP_t *ap);

bool ch32f1_probe(target *t); // will catch all the clones
bool gd32f1_probe(target *t);
bool stm32f1_probe(target *t);
bool stm32f4_probe(target *t);
bool stm32h7_probe(target *t);
bool stm32l0_probe(target *t);
bool stm32l1_probe(target *t);
bool stm32l4_probe(target *t);
bool stm32g0_probe(target *t);
bool lmi_probe(target *t);
bool lpc11xx_probe(target *t);
bool lpc15xx_probe(target *t);
bool lpc17xx_probe(target *t);
bool lpc43xx_probe(target *t);
bool lpc546xx_probe(target *t);
bool samx7x_probe(target *t);
bool sam3x_probe(target *t);
bool sam4l_probe(target *t);
bool nrf51_probe(target *t);
bool samd_probe(target *t);
bool samx5x_probe(target *t);
bool kinetis_probe(target *t);
bool efm32_probe(target *t);
bool msp432_probe(target *t);
bool ke04_probe(target *t);
bool rp_probe(target *t);

#endif /* __TARGET_PROBE_H */
