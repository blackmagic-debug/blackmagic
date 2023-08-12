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

#ifndef TARGET_CORTEX_INTERNAL_H
#define TARGET_CORTEX_INTERNAL_H

#include "general.h"
#include "adiv5.h"
#include "target.h"

#define CORTEX_CTR_FORMAT_SHIFT            29U
#define CORTEX_CTR_FORMAT_ARMv6            0U
#define CORTEX_CTR_FORMAT_ARMv7            4U
#define CORTEX_CTR_ICACHE_LINE_MASK        0xfU
#define CORTEX_CTR_DCACHE_LINE_SHIFT       16U
#define CORTEX_CTR_DCACHE_LINE_MASK        0xfU
#define CORTEX_CTR_ICACHE_LINE(cache_type) (1U << ((cache_type)&CORTEX_CTR_ICACHE_LINE_MASK))
#define CORTEX_CTR_DCACHE_LINE(cache_type) \
	(1U << (((cache_type) >> CORTEX_CTR_DCACHE_LINE_SHIFT) & CORTEX_CTR_DCACHE_LINE_MASK))

#define CORTEX_MAX_BREAKPOINTS 8U
#define CORTEX_MAX_WATCHPOINTS 8U

typedef struct cortex_priv {
	/* AP from which this CPU hangs */
	adiv5_access_port_s *ap;
	/* Base address for the debug interface block */
	uint32_t base_addr;
	/* Cache parameters */
	uint16_t icache_line_length;
	uint16_t dcache_line_length;
	/* Breakpoint and watchpoint enablement storage */
	uint8_t breakpoints_mask;
	uint8_t watchpoints_mask;
	/* Watchpoint unit information */
	uint8_t watchpoints_available;
	/* Breakpoint unit information */
	uint8_t breakpoints_available;
} cortex_priv_s;

void cortex_priv_free(void *priv);

bool cortex_check_error(target_s *target);
uint32_t cortex_dbg_read32(target_s *target, uint16_t src);
void cortex_dbg_write32(target_s *target, uint16_t dest, uint32_t value);
void cortex_read_cpuid(target_s *target);

#endif /*TARGET_CORTEX_INTERNAL_H*/
