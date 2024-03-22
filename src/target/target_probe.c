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

#ifndef _WIN32 /* PE-COFF does not allow aliases */
#ifdef __APPLE__
// __attribute__((alias)) is not supported in AppleClang, we need to define a
// __attribute__((weak)) placeholder body that'll get pivoted by the linker.
// if a "strong" definition is later available, the definition here will be
// removed at linking time.
// See:
// - https://bugs.llvm.org/show_bug.cgi?id=17775
// - "coalesced weak reference" in https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/MachOTopics/1-Articles/executing_files.html#//apple_ref/doc/uid/TP40001829-98432-TPXREF120
#define CORTEXAR_PROBE_WEAK_NOP(name)                                                                         \
	__attribute__((weak)) bool name(adiv5_access_port_s *const access_port, const target_addr_t base_address) \
	{                                                                                                         \
		return cortexar_probe_nop(access_port, base_address);                                                 \
	}
#define CORTEXM_PROBE_WEAK_NOP(name)                                        \
	__attribute__((weak)) bool name(adiv5_access_port_s *const access_port) \
	{                                                                       \
		return cortexm_probe_nop(access_port);                              \
	}
#define TARGET_PROBE_WEAK_NOP(name)                         \
	__attribute__((weak)) bool name(target_s *const target) \
	{                                                       \
		return target_probe_nop(target);                    \
	}
#define LPC55_DP_PREPARE_WEAK_NOP(name)                                   \
	__attribute__((weak)) void name(adiv5_debug_port_s *const debug_port) \
	{                                                                     \
		lpc55_dp_prepare_nop(debug_port);                                 \
	};
#define NRF91_DP_PREPARE_WEAK_NOP(name)                                   \
	__attribute__((weak)) bool name(adiv5_debug_port_s *const debug_port) \
	{                                                                     \
		return nrf91_dp_prepare_nop(debug_port);                                 \
	};
#else
#define CORTEXAR_PROBE_WEAK_NOP(name) \
	extern bool name(adiv5_access_port_s *, target_addr_t) __attribute__((weak, alias("cortexar_probe_nop")));
#define CORTEXM_PROBE_WEAK_NOP(name) \
	extern bool name(adiv5_access_port_s *) __attribute__((weak, alias("cortexm_probe_nop")));
#define TARGET_PROBE_WEAK_NOP(name) extern bool name(target_s *) __attribute__((weak, alias("target_probe_nop")));
#define LPC55_DP_PREPARE_WEAK_NOP(name) \
	extern void name(adiv5_debug_port_s *) __attribute__((weak, alias("lpc55_dp_prepare_nop")));
#define NRF91_DP_PREPARE_WEAK_NOP(name) \
	extern bool name(adiv5_debug_port_s *) __attribute__((weak, alias("nrf91_dp_prepare_nop")));
#endif

static inline bool cortexar_probe_nop(adiv5_access_port_s *const access_port, const target_addr_t base_address)
{
	(void)access_port;
	(void)base_address;
	return false;
}

static inline bool cortexm_probe_nop(adiv5_access_port_s *const access_port)
{
	(void)access_port;
	return false;
}

static inline bool target_probe_nop(target_s *const target)
{
	(void)target;
	return false;
}

static inline void lpc55_dp_prepare_nop(adiv5_debug_port_s *const debug_port)
{
	(void)debug_port;
}

static inline bool nrf91_dp_prepare_nop(adiv5_debug_port_s *const debug_port)
{
	(void)debug_port;
	return true;
}

/*
 * nop alias functions to allow support for target probe methods
 * to be disabled by not compiling/linking them in.
 */

CORTEXAR_PROBE_WEAK_NOP(cortexa_probe)
CORTEXAR_PROBE_WEAK_NOP(cortexr_probe)
CORTEXM_PROBE_WEAK_NOP(cortexm_probe)

TARGET_PROBE_WEAK_NOP(riscv32_probe)
TARGET_PROBE_WEAK_NOP(riscv64_probe)

CORTEXM_PROBE_WEAK_NOP(efm32_aap_probe)
CORTEXM_PROBE_WEAK_NOP(kinetis_mdm_probe)
CORTEXM_PROBE_WEAK_NOP(lpc55_dmap_probe)
CORTEXM_PROBE_WEAK_NOP(nrf51_mdm_probe)
CORTEXM_PROBE_WEAK_NOP(rp_rescue_probe)

TARGET_PROBE_WEAK_NOP(at32f40x_probe)
TARGET_PROBE_WEAK_NOP(at32f43x_probe)
TARGET_PROBE_WEAK_NOP(ch32f1_probe)
TARGET_PROBE_WEAK_NOP(efm32_probe)
TARGET_PROBE_WEAK_NOP(gd32f1_probe)
TARGET_PROBE_WEAK_NOP(gd32f4_probe)
TARGET_PROBE_WEAK_NOP(gd32vf1_probe)
TARGET_PROBE_WEAK_NOP(hc32l110_probe)
TARGET_PROBE_WEAK_NOP(imxrt_probe)
TARGET_PROBE_WEAK_NOP(ke04_probe)
TARGET_PROBE_WEAK_NOP(kinetis_probe)
TARGET_PROBE_WEAK_NOP(lmi_probe)
TARGET_PROBE_WEAK_NOP(lpc11xx_probe)
TARGET_PROBE_WEAK_NOP(lpc15xx_probe)
TARGET_PROBE_WEAK_NOP(lpc17xx_probe)
TARGET_PROBE_WEAK_NOP(lpc40xx_probe)
TARGET_PROBE_WEAK_NOP(lpc43xx_probe)
TARGET_PROBE_WEAK_NOP(lpc546xx_probe)
TARGET_PROBE_WEAK_NOP(lpc55xx_probe)
TARGET_PROBE_WEAK_NOP(mm32l0xx_probe)
TARGET_PROBE_WEAK_NOP(mm32f3xx_probe)
TARGET_PROBE_WEAK_NOP(msp432e4_probe)
TARGET_PROBE_WEAK_NOP(msp432p4_probe)
TARGET_PROBE_WEAK_NOP(nrf51_probe)
TARGET_PROBE_WEAK_NOP(nrf91_probe)
TARGET_PROBE_WEAK_NOP(renesas_ra_probe)
TARGET_PROBE_WEAK_NOP(renesas_rz_probe)
TARGET_PROBE_WEAK_NOP(rp_probe)
TARGET_PROBE_WEAK_NOP(s32k3xx_probe)
TARGET_PROBE_WEAK_NOP(sam3x_probe)
TARGET_PROBE_WEAK_NOP(sam4l_probe)
TARGET_PROBE_WEAK_NOP(samd_probe)
TARGET_PROBE_WEAK_NOP(samx5x_probe)
TARGET_PROBE_WEAK_NOP(samx7x_probe)
TARGET_PROBE_WEAK_NOP(stm32f1_probe)
TARGET_PROBE_WEAK_NOP(stm32f4_probe)
TARGET_PROBE_WEAK_NOP(stm32g0_probe)
TARGET_PROBE_WEAK_NOP(stm32h5_probe)
TARGET_PROBE_WEAK_NOP(stm32h7_probe)
TARGET_PROBE_WEAK_NOP(stm32l0_probe)
TARGET_PROBE_WEAK_NOP(stm32l1_probe)
TARGET_PROBE_WEAK_NOP(stm32l4_probe)
TARGET_PROBE_WEAK_NOP(stm32mp15_ca7_probe)
TARGET_PROBE_WEAK_NOP(stm32mp15_cm4_probe)
TARGET_PROBE_WEAK_NOP(zynq7_probe)

LPC55_DP_PREPARE_WEAK_NOP(lpc55_dp_prepare)

#endif /* _WIN32 */
