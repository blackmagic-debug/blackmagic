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

#ifndef TARGET_ADIV6_H
#define TARGET_ADIV6_H

#include "general.h"
#include "adiv5.h"
#include "adiv6_internal.h"

#define ADIV6_DP_DPIDR1   ADIV5_DP_REG(0x0U) /* Bank 1 */
#define ADIV6_DP_BASEPTR0 ADIV5_DP_REG(0x0U) /* Bank 2 */
#define ADIV6_DP_BASEPTR1 ADIV5_DP_REG(0x0U) /* Bank 3 */
#define ADIV6_DP_SELECT1  ADIV5_DP_REG(0x4U) /* Bank 5 */

/* DP DPIDR1 */
#define ADIV6_DP_DPIDR1_ASIZE_MASK     (0x7fU)
#define ADIV6_DP_DPIDR1_ERRMODE_OFFSET 7U
#define ADIV6_DP_DPIDR1_ERRMODE_MASK   (1U << ADIv5_DP_DPIDR1_ERRMODE_OFFSET)

/* DP BASEPTR0 */
#define ADIV6_DP_BASEPTR0_VALID    (1U << 0U)
#define ADIV6_DP_BASE_ADDRESS_MASK UINT64_C(0xfffffffffffff000)

#define ADIV6_AP_BANK_MASK 0x0ff0U

/* DP and AP discovery functions */
bool adiv6_dp_init(adiv5_debug_port_s *dp);

#if CONFIG_BMDA == 1
/* BMDA interposition functions for DP setup */
void bmda_adiv6_dp_init(adiv5_debug_port_s *dp);
#endif

/* ADIv6 logical operation functions for AP register I/O */
uint32_t adiv6_ap_reg_read(adiv5_access_port_s *ap, uint16_t addr);
void adiv6_ap_reg_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value);

#endif /* TARGET_ADIV6_H */
