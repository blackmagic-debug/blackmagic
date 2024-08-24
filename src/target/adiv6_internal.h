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

#ifndef TARGET_ADIV6_INTERNAL_H
#define TARGET_ADIV6_INTERNAL_H

#include "adiv5_internal.h"

/* CoreSight ROM registers */
#define CORESIGHT_ROM_PRIDR0      ADIV5_AP_REG(0xc00U)
#define CORESIGHT_ROM_DBGRSTRR    ADIV5_AP_REG(0xc10U)
#define CORESIGHT_ROM_DBGRSTAR    ADIV5_AP_REG(0xc14U)
#define CORESIGHT_ROM_DBGPCR_BASE ADIV5_AP_REG(0xa00U)
#define CORESIGHT_ROM_DBGPSR_BASE ADIV5_AP_REG(0xa80U)
#define CORESIGHT_ROM_DEVID       ADIV5_AP_REG(0xfc8U)

#define CORESIGHT_ROM_PRIDR0_VERSION_MASK      (0xfU << 0U)
#define CORESIGHT_ROM_PRIDR0_VERSION_NOT_IMPL  0x0U
#define CORESIGHT_ROM_PRIDR0_HAS_DBG_RESET_REQ (1U << 4U)
#define CORESIGHT_ROM_PRIDR0_HAS_SYS_RESET_REQ (1U << 5U)
#define CORESIGHT_ROM_DBGPCR_PRESENT           (1U << 0U)
#define CORESIGHT_ROM_DBGPCR_PWRREQ            (1U << 1U)
#define CORESIGHT_ROM_DBGPSR_STATUS_ON         (1U << 0)
#define CORESIGHT_ROM_DBGRST_REQ               (1U << 0U)
#define CORESIGHT_ROM_DEVID_FORMAT             (0xfU << 0U)
#define CORESIGHT_ROM_DEVID_FORMAT_32BIT       0U
#define CORESIGHT_ROM_DEVID_FORMAT_64BIT       1U
#define CORESIGHT_ROM_DEVID_SYSMEM             (1U << 4U)
#define CORESIGHT_ROM_DEVID_HAS_POWERREQ       (1U << 5U)

#define CORESIGHT_ROM_ROMENTRY_ENTRY_MASK        (0x3U << 0U)
#define CORESIGHT_ROM_ROMENTRY_ENTRY_FINAL       0U
#define CORESIGHT_ROM_ROMENTRY_ENTRY_INVALID     1U
#define CORESIGHT_ROM_ROMENTRY_ENTRY_NOT_PRESENT 2U
#define CORESIGHT_ROM_ROMENTRY_ENTRY_PRESENT     3U
#define CORESIGHT_ROM_ROMENTRY_POWERID_VALID     (1U << 2U)
#define CORESIGHT_ROM_ROMENTRY_POWERID_SHIFT     4U
#define CORESIGHT_ROM_ROMENTRY_POWERID_MASK      (0x1fU << CORESIGHT_ROM_ROMENTRY_POWERID_SHIFT)
#define CORESIGHT_ROM_ROMENTRY_OFFSET_MASK       UINT64_C(0xfffffffffffff000)

typedef struct adiv6_access_port {
	adiv5_access_port_s base;
	target_addr64_t ap_address;
} adiv6_access_port_s;

#endif /* TARGET_ADIV6_INTERNAL_H */
