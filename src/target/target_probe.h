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

#define STRINGIFY(x) #x
/* Probe launch macro used by the CPU-generic layers to then call CPU-specific routines safely */
#define PROBE(x)                                    \
	do {                                            \
		DEBUG_TARGET("Calling " STRINGIFY(x) "\n"); \
		if ((x)(target))                            \
			return true;                            \
		target_check_error(target);                 \
	} while (0)

/*
 * Probe for various targets.
 * Actual functions implemented in their respective drivers.
 */

bool cortexa_probe(adiv5_access_port_s *ap, target_addr_t base_address);
bool cortexr_probe(adiv5_access_port_s *ap, target_addr_t base_address);
bool cortexm_probe(adiv5_access_port_s *ap);

bool riscv32_probe(target_s *target);
bool riscv64_probe(target_s *target);

bool efm32_aap_probe(adiv5_access_port_s *ap);
bool kinetis_mdm_probe(adiv5_access_port_s *ap);
bool lpc55_dmap_probe(adiv5_access_port_s *ap);
bool nrf51_ctrl_ap_probe(adiv5_access_port_s *ap);
bool nrf54l_ctrl_ap_probe(adiv5_access_port_s *ap);
bool rp2040_rescue_probe(adiv5_access_port_s *ap);

bool am335x_cm3_probe(target_s *target);
bool at32f40x_probe(target_s *target); // STM32 clones from Artery
bool apollo_3_probe(target_s *target);
bool at32f43x_probe(target_s *target);
bool ch32f1_probe(target_s *target); // will catch all the clones
bool ch579_probe(target_s *target);
bool efm32_probe(target_s *target);
bool gd32f1_probe(target_s *target);
bool gd32f4_probe(target_s *target);
bool gd32vf1_probe(target_s *target);
bool hc32l110_probe(target_s *target);
bool imxrt_probe(target_s *target);
bool ke04_probe(target_s *target);
bool kinetis_probe(target_s *target);
bool lmi_probe(target_s *target);
bool lpc11xx_probe(target_s *target);
bool lpc15xx_probe(target_s *target);
bool lpc17xx_probe(target_s *target);
bool lpc40xx_probe(target_s *target);
bool lpc43xx_probe(target_s *target);
bool lpc546xx_probe(target_s *target);
bool lpc55xx_probe(target_s *target);
bool mm32l0xx_probe(target_s *target);
bool mm32f3xx_probe(target_s *target);
bool msp432e4_probe(target_s *target);
bool msp432p4_probe(target_s *target);
bool mspm0_probe(target_s *target);
bool nrf51_probe(target_s *target);
bool nrf54l_probe(target_s *target);
bool nrf91_probe(target_s *target);
bool puya_probe(target_s *target);
bool renesas_ra_probe(target_s *target);
bool renesas_rz_probe(target_s *target);
bool rp2040_probe(target_s *target);
bool rp2350_probe(target_s *target);
bool s32k3xx_probe(target_s *target);
bool sam3x_probe(target_s *target);
bool sam4l_probe(target_s *target);
bool samd_probe(target_s *target);
bool samx5x_probe(target_s *target);
bool samx7x_probe(target_s *target);
bool stm32f1_probe(target_s *target);
bool stm32f4_probe(target_s *target);
bool stm32g0_probe(target_s *target);
bool stm32h5_probe(target_s *target);
bool stm32h7_probe(target_s *target);
bool stm32l0_probe(target_s *target);
bool stm32l1_probe(target_s *target);
bool stm32l4_probe(target_s *target);
bool stm32mp15_ca7_probe(target_s *target);
bool stm32mp15_cm4_probe(target_s *target);
bool stm32wb0_probe(target_s *target);
bool zynq7_probe(target_s *target);

void lpc55_dp_prepare(adiv5_debug_port_s *dp);

#endif /* TARGET_TARGET_PROBE_H */
