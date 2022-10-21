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

#ifndef TARGET_TARGET_PROBE_H
#define TARGET_TARGET_PROBE_H

#include "target.h"
#include "adiv5.h"

/*
 * Probe for various targets.
 * Actual functions implemented in their respective drivers.
 */

bool cortexa_probe(adiv5_access_port_s *apb, uint32_t debug_base);

bool cortexm_probe(adiv5_access_port_s *ap);

bool kinetis_mdm_probe(adiv5_access_port_s *ap);
bool nrf51_mdm_probe(adiv5_access_port_s *ap);
bool efm32_aap_probe(adiv5_access_port_s *ap);
bool rp_rescue_probe(adiv5_access_port_s *ap);

bool ch32f1_probe(target_s *t);  // will catch all the clones
bool at32fxx_probe(target_s *t); // STM32 clones from Artery
bool mm32l0xx_probe(target_s *t);
bool mm32f3xx_probe(target_s *t);
bool gd32f1_probe(target_s *t);
bool gd32f4_probe(target_s *t);
bool stm32f1_probe(target_s *t);
bool stm32f4_probe(target_s *t);
bool stm32h7_probe(target_s *t);
bool stm32l0_probe(target_s *t);
bool stm32l1_probe(target_s *t);
bool stm32l4_probe(target_s *t);
bool stm32g0_probe(target_s *t);
bool lmi_probe(target_s *t);
bool lpc11xx_probe(target_s *t);
bool lpc15xx_probe(target_s *t);
bool lpc17xx_probe(target_s *t);
bool lpc43xx_probe(target_s *t);
bool lpc546xx_probe(target_s *t);
bool samx7x_probe(target_s *t);
bool sam3x_probe(target_s *t);
bool sam4l_probe(target_s *t);
bool nrf51_probe(target_s *t);
bool samd_probe(target_s *t);
bool samx5x_probe(target_s *t);
bool kinetis_probe(target_s *t);
bool efm32_probe(target_s *t);
bool msp432_probe(target_s *t);
bool ke04_probe(target_s *t);
bool rp_probe(target_s *t);
bool renesas_probe(target_s *t);

#endif /* TARGET_TARGET_PROBE_H */
