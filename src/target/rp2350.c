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

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortex.h"

#define RP2350_BOOTROM_BASE  0x00000000U
#define RP2350_BOOTROM_MAGIC (RP2350_BOOTROM_BASE + 0x0010U)

#define RP2350_BOOTROM_MAGIC_VALUE   ((uint32_t)'M' | ((uint32_t)'u' << 8U) | (2U << 16U))
#define RP2350_BOOTROM_MAGIC_MASK    0x00ffffffU
#define RP2350_BOOTROM_VERSION_SHIFT 24U

#define ID_RP2350 0x0040U

bool rp2350_probe(target_s *const target)
{
	/* Check that the target has the right part number */
	if (target->part_id != ID_RP2350)
		return false;

	/* Check the boot ROM magic for a more positive identification of the part */
	const uint32_t boot_magic = target_mem32_read32(target, RP2350_BOOTROM_MAGIC);
	if ((boot_magic & RP2350_BOOTROM_MAGIC_MASK) != RP2350_BOOTROM_MAGIC_VALUE) {
		DEBUG_ERROR("Wrong Bootmagic %08" PRIx32 " found!\n", boot_magic);
		return false;
	}
	DEBUG_TARGET("Boot ROM version: %x\n", (uint8_t)(boot_magic >> RP2350_BOOTROM_VERSION_SHIFT));

	target->driver = "RP2350";
	return true;
}
