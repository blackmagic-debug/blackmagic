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

#ifdef __APPLE__
// https://bugs.llvm.org/show_bug.cgi?id=17775
#define APPLE_STATIC
// https://gcc.gnu.org/onlinedocs/cpp/Pragmas.html
#define DO_PRAGMA_(x) _Pragma(#x)
#define DO_PRAGMA(x)  DO_PRAGMA_(x)
// __attribute__((alias)) is not supported in AppleClang.
#define weak_alias(name, aliasname)     DO_PRAGMA(weak name = aliasname)
#define CORTEXA_PROBE_WEAK_NOP(name)    weak_alias(name, cortexa_probe_nop)
#define CORTEXM_PROBE_WEAK_NOP(name)    weak_alias(name, cortexm_probe_nop)
#define TARGET_PROBE_WEAK_NOP(name)     weak_alias(name, target_probe_nop)
#define LPC55_DP_PREPARE_WEAK_NOP(name) weak_alias(name, lpc55_dp_prepare_nop)
#else
#define APPLE_STATIC static inline
#define CORTEXA_PROBE_WEAK_NOP(name) \
	extern bool name(adiv5_access_port_s *, uint32_t) __attribute__((weak, alias("cortexa_probe_nop")));
#define CORTEXM_PROBE_WEAK_NOP(name) \
	extern bool name(adiv5_access_port_s *) __attribute__((weak, alias("cortexm_probe_nop")));
#define TARGET_PROBE_WEAK_NOP(name) extern bool name(target_s *) __attribute__((weak, alias("target_probe_nop")));
#define LPC55_DP_PREPARE_WEAK_NOP(name) \
	extern void name(adiv5_debug_port_s *) __attribute__((weak, alias("lpc55_dp_prepare_nop")));
#endif

APPLE_STATIC bool cortexa_probe_nop(adiv5_access_port_s *apb, uint32_t debug_base)
{
	(void)apb;
	(void)debug_base;
	return false;
}

APPLE_STATIC bool cortexm_probe_nop(adiv5_access_port_s *access_port)
{
	(void)access_port;
	return false;
}

APPLE_STATIC bool target_probe_nop(target_s *target)
{
	(void)target;
	return false;
}

APPLE_STATIC void lpc55_dp_prepare_nop(adiv5_debug_port_s *debug_port)
{
	(void)debug_port;
}

/*
 * nop alias functions to allow support for target probe methods
 * to be disabled by not compiling/linking them in.
 */

CORTEXA_PROBE_WEAK_NOP(cortexa_probe)

CORTEXM_PROBE_WEAK_NOP(cortexm_probe)

CORTEXM_PROBE_WEAK_NOP(kinetis_mdm_probe)
CORTEXM_PROBE_WEAK_NOP(nrf51_mdm_probe)
CORTEXM_PROBE_WEAK_NOP(efm32_aap_probe)
CORTEXM_PROBE_WEAK_NOP(rp_rescue_probe)
CORTEXM_PROBE_WEAK_NOP(lpc55_dmap_probe)

TARGET_PROBE_WEAK_NOP(ch32f1_probe)
TARGET_PROBE_WEAK_NOP(gd32f1_probe)
TARGET_PROBE_WEAK_NOP(gd32f4_probe)
TARGET_PROBE_WEAK_NOP(stm32f1_probe)
TARGET_PROBE_WEAK_NOP(at32fxx_probe)
TARGET_PROBE_WEAK_NOP(stm32f4_probe)
TARGET_PROBE_WEAK_NOP(stm32h5_probe)
TARGET_PROBE_WEAK_NOP(stm32h7_probe)
TARGET_PROBE_WEAK_NOP(stm32mp15_cm4_probe)
TARGET_PROBE_WEAK_NOP(stm32l0_probe)
TARGET_PROBE_WEAK_NOP(stm32l1_probe)
TARGET_PROBE_WEAK_NOP(stm32l4_probe)
TARGET_PROBE_WEAK_NOP(stm32g0_probe)
TARGET_PROBE_WEAK_NOP(hc32l110_probe)
TARGET_PROBE_WEAK_NOP(lmi_probe)
TARGET_PROBE_WEAK_NOP(lpc11xx_probe)
TARGET_PROBE_WEAK_NOP(lpc15xx_probe)
TARGET_PROBE_WEAK_NOP(lpc17xx_probe)
TARGET_PROBE_WEAK_NOP(lpc40xx_probe)
TARGET_PROBE_WEAK_NOP(lpc43xx_probe)
TARGET_PROBE_WEAK_NOP(lpc546xx_probe)
TARGET_PROBE_WEAK_NOP(lpc55xx_probe)
TARGET_PROBE_WEAK_NOP(samx7x_probe)
TARGET_PROBE_WEAK_NOP(sam3x_probe)
TARGET_PROBE_WEAK_NOP(sam4l_probe)
TARGET_PROBE_WEAK_NOP(nrf51_probe)
TARGET_PROBE_WEAK_NOP(nrf91_probe)
TARGET_PROBE_WEAK_NOP(samd_probe)
TARGET_PROBE_WEAK_NOP(samx5x_probe)
TARGET_PROBE_WEAK_NOP(kinetis_probe)
TARGET_PROBE_WEAK_NOP(efm32_probe)
TARGET_PROBE_WEAK_NOP(msp432e4_probe)
TARGET_PROBE_WEAK_NOP(msp432p4_probe)
TARGET_PROBE_WEAK_NOP(ke04_probe)
TARGET_PROBE_WEAK_NOP(rp_probe)
TARGET_PROBE_WEAK_NOP(renesas_probe)
TARGET_PROBE_WEAK_NOP(mm32l0xx_probe)
TARGET_PROBE_WEAK_NOP(mm32f3xx_probe)
TARGET_PROBE_WEAK_NOP(imxrt_probe)

LPC55_DP_PREPARE_WEAK_NOP(lpc55_dp_prepare)
