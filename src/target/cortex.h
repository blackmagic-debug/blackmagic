/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
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

#ifndef TARGET_CORTEX_H
#define TARGET_CORTEX_H

#include "general.h"
#include "adiv5.h"
#include "target.h"

#define CORTEX_THUMB_BREAKPOINT 0xbe00U

/* Cortex-M CPU IDs */
#define CORTEX_M0  0xc200U
#define CORTEX_M0P 0xc600U
#define CORTEX_M3  0xc230U
#define CORTEX_M4  0xc240U
#define CORTEX_M7  0xc270U
#define CORTEX_M23 0xd200U
#define CORTEX_M33 0xd210U
#define STAR_MC1   0x1320U

/* Cortex-A CPU IDs */
#define CORTEX_A5 0xc050U
#define CORTEX_A7 0xc070U
#define CORTEX_A8 0xc080U
#define CORTEX_A9 0xc090U

/* Cortex general purpose register offsets */
#define CORTEX_REG_SP      13U
#define CORTEX_REG_LR      14U
#define CORTEX_REG_PC      15U
#define CORTEX_REG_XPSR    16U
#define CORTEX_REG_CPSR    16U
#define CORTEX_REG_MSP     17U
#define CORTEX_REG_PSP     18U
#define CORTEX_REG_SPECIAL 19U

#define CORTEX_CPUID_PARTNO_MASK   0xfff0U
#define CORTEX_CPUID_REVISION_MASK 0x00f00000U
#define CORTEX_CPUID_PATCH_MASK    0xfU

#define CORTEX_FLOAT_REG_COUNT     33U
#define CORTEX_DOUBLE_REG_COUNT    17U
#define CORTEXM_GENERAL_REG_COUNT  20U
#define CORTEXAR_GENERAL_REG_COUNT 17U

adiv5_access_port_s *cortex_ap(target_s *target);

#endif /* TARGET_CORTEX_H */
