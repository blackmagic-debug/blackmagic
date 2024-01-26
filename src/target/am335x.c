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

/*
 * This file implements support for AM355x series devices, providing memory
 * maps and Flash programming routines.
 *
 * References:
 * SPRUH73Q - AM335x and AMIC110 Sitara™ Processors
 *   https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortex.h"

#define AM335x_CTRL_BASE      0x44e10000U
#define AM335x_CTRL_DEVICE_ID (AM335x_CTRL_BASE + 0x600U)

#define AM335x_CTRL_DEVICE_ID_MASK   0x0fffffffU
#define AM335x_CTRL_DEVICE_ID_AM335x 0x0b94402eU

bool am335x_cm3_probe(target_s *const target)
{
	/* Try to read out the device identification register and check this is actually an AM335x device */
	const uint32_t device_id = target_mem_read32(target, AM335x_CTRL_DEVICE_ID) & AM335x_CTRL_DEVICE_ID_MASK;
	if (device_id != AM335x_CTRL_DEVICE_ID_AM335x)
		return false;

	cortex_ap(target)->dp->quirks |= ADIV5_DP_QUIRK_DUPED_AP;
	return true;
}
