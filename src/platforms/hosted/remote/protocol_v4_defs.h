/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PLATFORMS_HOSTED_REMOTE_PROTOCOL_V4_DEFS_H
#define PLATFORMS_HOSTED_REMOTE_PROTOCOL_V4_DEFS_H

/* Bring in the v3 protocol definitions and undefine the memory portion of the ADIv5 acceleration protocol */
#include "protocol_v3_defs.h"

#undef REMOTE_ADIV5_MEM_READ_STR
#undef REMOTE_ADIV5_MEM_READ_LENGTH
#undef REMOTE_ADIV5_MEM_WRITE_STR
#undef REMOTE_ADIV5_MEM_WRITE_LENGTH

/*
 * This version of the protocol introduces a command for determining what protocol acclerations are available.
 * It also introduces commands for determining the compiled-in supported target architectures and families
 */
#define REMOTE_HL_ACCEL    'A'
#define REMOTE_HL_ARCHS    'a'
#define REMOTE_HL_FAMILIES 'F'

/* High-level protocol message for asking about the available accelerations, target architectures and families */
#define REMOTE_HL_ACCEL_STR                                          \
	(char[])                                                         \
	{                                                                \
		REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_HL_ACCEL, REMOTE_EOM, 0 \
	}
#define REMOTE_HL_ARCHS_STR                                          \
	(char[])                                                         \
	{                                                                \
		REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_HL_ARCHS, REMOTE_EOM, 0 \
	}
#define REMOTE_HL_FAMILIES_STR                                          \
	(char[])                                                            \
	{                                                                   \
		REMOTE_SOM, REMOTE_HL_PACKET, REMOTE_HL_FAMILIES, REMOTE_EOM, 0 \
	}

/* Remote protocol enabled acceleration bit values */
#define REMOTE_ACCEL_ADIV5     (1U << 0U)
#define REMOTE_ACCEL_CORTEX_AR (1U << 1U)
#define REMOTE_ACCEL_RISCV     (1U << 2U)
#define REMOTE_ACCEL_ADIV6     (1U << 3U)

/* Remote protocol enabled architecture support bit values */
#define REMOTE_ARCH_CORTEXM  (1U << 0U)
#define REMOTE_ARCH_CORTEXAR (1U << 1U)
#define REMOTE_ARCH_RISCV32  (1U << 2U)
#define REMOTE_ARCH_RISCV64  (1U << 3U)

/* Remote protocol enabled families support bit values */
#define REMOTE_FAMILY_AT32        (1U << 0U)
#define REMOTE_FAMILY_APOLLO3     (1U << 1U)
#define REMOTE_FAMILY_CH32        (1U << 2U)
#define REMOTE_FAMILY_CH579       (1U << 3U)
#define REMOTE_FAMILY_EFM         (1U << 4U)
#define REMOTE_FAMILY_GD32        (1U << 5U)
#define REMOTE_FAMILY_HC32        (1U << 6U)
#define REMOTE_FAMILY_LPC         (1U << 7U)
#define REMOTE_FAMILY_MM32        (1U << 8U)
#define REMOTE_FAMILY_NRF         (1U << 9U)
#define REMOTE_FAMILY_NXP_KINETIS (1U << 10U)
#define REMOTE_FAMILY_PUYA        (1U << 11U)
#define REMOTE_FAMILY_RENESAS_RA  (1U << 12U)
#define REMOTE_FAMILY_RENESAS_RZ  (1U << 13U)
#define REMOTE_FAMILY_RP          (1U << 14U)
#define REMOTE_FAMILY_SAM         (1U << 15U)
#define REMOTE_FAMILY_STM32       (1U << 16U)
#define REMOTE_FAMILY_TI          (1U << 17U)
#define REMOTE_FAMILY_XILINX      (1U << 18U)
#define REMOTE_FAMILY_NXP_IMXRT   (1U << 19U)

/*
 * The project reserves all unused bit values in both the architectures and families bitmasks
 * for future use for officially supported target architectures and target families. If you are
 * building target support that will live out-of-tree from the official BMD repo, DO NOT add
 * your target's family to these definitions. If you are planning to PR the support then you may
 * reserve a new bit if your PR introduces a new family (exisiting families such as STM32 do not
 * need new bits as they are already covered). We will co-ordinate with you in making sure the bit
 * is unique when going through the PR'ing process.
 */

/*
 * This version of the protocol introduces ADIv5 commands for setting the version of the DP being talked to,
 * and the TARGETSEL value for the DP for SWD multi-drop
 */
#define REMOTE_DP_VERSION   'V'
#define REMOTE_DP_TARGETSEL 'T'

/* This version of the protocol introduces 64-bit support for the ADIv5 acceleration protocol */
#define REMOTE_UINT64           '%', '0', '1', '6', 'l', 'l', 'x'
#define REMOTE_ADIV5_ADDR64     REMOTE_UINT64
#define REMOTE_ADIV5_DP_VERSION REMOTE_UINT8

/* ADIv5 remote protocol memory I/O messages */
#define REMOTE_ADIV5_MEM_READ_STR                                                                      \
	(char[])                                                                                           \
	{                                                                                                  \
		REMOTE_SOM, REMOTE_ADIV5_PACKET, REMOTE_MEM_READ, REMOTE_ADIV5_DEV_INDEX, REMOTE_ADIV5_AP_SEL, \
			REMOTE_ADIV5_CSW, REMOTE_ADIV5_ADDR64, REMOTE_ADIV5_COUNT, REMOTE_EOM, 0                   \
	}
/* 2 leader bytes and one trailer byte gives 3 bytes response overhead */
#define REMOTE_ADIV5_MEM_READ_LENGTH 3U
#define REMOTE_ADIV5_MEM_WRITE_STR                                                                      \
	(char[])                                                                                            \
	{                                                                                                   \
		REMOTE_SOM, REMOTE_ADIV5_PACKET, REMOTE_MEM_WRITE, REMOTE_ADIV5_DEV_INDEX, REMOTE_ADIV5_AP_SEL, \
			REMOTE_ADIV5_CSW, REMOTE_ADIV5_ALIGNMENT, REMOTE_ADIV5_ADDR64, REMOTE_ADIV5_COUNT, 0        \
	}
/*
 * 3 leader bytes + 2 bytes for dev index + 2 bytes for AP select + 8 for CSW + 2 for the alignment +
 * 16 for the address and 8 for the count and one trailer gives 42 bytes request overhead
 */
#define REMOTE_ADIV5_MEM_WRITE_LENGTH 42U
#define REMOTE_DP_VERSION_STR                                                                      \
	(char[])                                                                                       \
	{                                                                                              \
		REMOTE_SOM, REMOTE_ADIV5_PACKET, REMOTE_DP_VERSION, REMOTE_ADIV5_DP_VERSION, REMOTE_EOM, 0 \
	}
#define REMOTE_DP_TARGETSEL_STR                                                                \
	(char[])                                                                                   \
	{                                                                                          \
		REMOTE_SOM, REMOTE_ADIV5_PACKET, REMOTE_DP_TARGETSEL, REMOTE_ADIV5_DATA, REMOTE_EOM, 0 \
	}

/* ADIv6 acceleration protocol elements */
#define REMOTE_ADIV6_PACKET '6'

/* ADIv6 remote protocol messages */
#define REMOTE_ADIV6_AP_READ_STR                                                                      \
	(char[])                                                                                          \
	{                                                                                                 \
		REMOTE_SOM, REMOTE_ADIV5_PACKET, REMOTE_ADIV6_PACKET, REMOTE_AP_READ, REMOTE_ADIV5_DEV_INDEX, \
			REMOTE_ADIV5_ADDR64, REMOTE_ADIV5_ADDR16, REMOTE_EOM, 0                                   \
	}
#define REMOTE_ADIV6_AP_WRITE_STR                                                                      \
	(char[])                                                                                           \
	{                                                                                                  \
		REMOTE_SOM, REMOTE_ADIV5_PACKET, REMOTE_ADIV6_PACKET, REMOTE_AP_WRITE, REMOTE_ADIV5_DEV_INDEX, \
			REMOTE_ADIV5_ADDR64, REMOTE_ADIV5_ADDR16, REMOTE_ADIV5_DATA, REMOTE_EOM, 0                 \
	}
#define REMOTE_ADIV6_MEM_READ_STR                                                                         \
	(char[])                                                                                              \
	{                                                                                                     \
		REMOTE_SOM, REMOTE_ADIV5_PACKET, REMOTE_ADIV6_PACKET, REMOTE_MEM_READ, REMOTE_ADIV5_DEV_INDEX,    \
			REMOTE_ADIV5_ADDR64, REMOTE_ADIV5_CSW, REMOTE_ADIV5_ADDR64, REMOTE_ADIV5_COUNT, REMOTE_EOM, 0 \
	}
/* 2 leader bytes and one trailer byte gives 3 bytes response overhead */
#define REMOTE_ADIV6_MEM_READ_LENGTH 3U
#define REMOTE_ADIV6_MEM_WRITE_STR                                                                                    \
	(char[])                                                                                                          \
	{                                                                                                                 \
		REMOTE_SOM, REMOTE_ADIV5_PACKET, REMOTE_ADIV6_PACKET, REMOTE_MEM_WRITE, REMOTE_ADIV5_DEV_INDEX,               \
			REMOTE_ADIV5_ADDR64, REMOTE_ADIV5_CSW, REMOTE_ADIV5_ALIGNMENT, REMOTE_ADIV5_ADDR64, REMOTE_ADIV5_COUNT, 0 \
	}
/*
 * 3 leader bytes + 2 bytes for dev index + 16 bytes for the DP resource bus AP base address + 8 for CSW +
 * 2 for the alignment + 16 for the address and 8 for the count and one trailer gives 57 bytes request overhead
 */
#define REMOTE_ADIV6_MEM_WRITE_LENGTH 57U

/* This version of the protocol introduces an optional RISC-V acceleration protocol */
#define REMOTE_RISCV_PACKET    'R'
#define REMOTE_RISCV_PROTOCOLS 'P'
#define REMOTE_RISCV_DMI_READ  'd'
#define REMOTE_RISCV_DMI_WRITE 'D'

/* Supported RISC-V DTM protocols */
#define REMOTE_RISCV_JTAG 'J'

#define REMOTE_RISCV_PROTOCOL    '%', 'c'
#define REMOTE_RISCV_DEV_INDEX   REMOTE_UINT8
#define REMOTE_RISCV_IDLE_CYCLES REMOTE_UINT8
#define REMOTE_RISCV_ADDR_WIDTH  REMOTE_UINT8
#define REMOTE_RISCV_ADDR32      REMOTE_UINT32
#define REMOTE_RISCV_DATA        REMOTE_UINT32

/* RISC-V remote protocol messages */
#define REMOTE_RISCV_PROTOCOLS_STR                                             \
	(char[])                                                                   \
	{                                                                          \
		REMOTE_SOM, REMOTE_RISCV_PACKET, REMOTE_RISCV_PROTOCOLS, REMOTE_EOM, 0 \
	}
#define REMOTE_RISCV_INIT_STR                                                              \
	(char[])                                                                               \
	{                                                                                      \
		REMOTE_SOM, REMOTE_RISCV_PACKET, REMOTE_INIT, REMOTE_RISCV_PROTOCOL, REMOTE_EOM, 0 \
	}
#define REMOTE_RISCV_DMI_READ_STR                                                                                 \
	(char[])                                                                                                      \
	{                                                                                                             \
		REMOTE_SOM, REMOTE_RISCV_PACKET, REMOTE_RISCV_DMI_READ, REMOTE_RISCV_DEV_INDEX, REMOTE_RISCV_IDLE_CYCLES, \
			REMOTE_RISCV_ADDR_WIDTH, REMOTE_RISCV_ADDR32, REMOTE_EOM, 0                                           \
	}
#define REMOTE_RISCV_DMI_WRITE_STR                                                                                 \
	(char[])                                                                                                       \
	{                                                                                                              \
		REMOTE_SOM, REMOTE_RISCV_PACKET, REMOTE_RISCV_DMI_WRITE, REMOTE_RISCV_DEV_INDEX, REMOTE_RISCV_IDLE_CYCLES, \
			REMOTE_RISCV_ADDR_WIDTH, REMOTE_RISCV_ADDR32, REMOTE_RISCV_DATA, REMOTE_EOM, 0                         \
	}

/* Remote protocol enabled RISC-V protocols bit values */
#define REMOTE_RISCV_PROTOCOL_JTAG (1U << 0U)

#endif /*PLATFORMS_HOSTED_REMOTE_PROTOCOL_V4_DEFS_H*/
