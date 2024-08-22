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
 * This file implements transport generic ADIv6 functions.
 *
 * See the following ARM Reference Documents:
 * ARM Debug Interface v6 Architecture Specification, IHI0074 ver. e
 * - https://developer.arm.com/documentation/ihi0074/latest/
 */

#include "general.h"
#include "adiv6.h"

target_addr64_t adiv6_dp_read_base_address(adiv5_debug_port_s *const dp)
{
	/* BASEPTR0 is on bank 2 */
	adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK2);
	const uint32_t baseptr0 = adiv5_dp_read(dp, ADIV6_DP_BASEPTR0);
	/* BASEPTR1 is on bank 3 */
	adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK3);
	const uint32_t baseptr1 = adiv5_dp_read(dp, ADIV6_DP_BASEPTR1);
	adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK0);
	/* Now re-combine the values and return */
	return baseptr0 | ((uint64_t)baseptr1 << 32U);
}

bool adiv6_dp_init(adiv5_debug_port_s *const dp)
{
	/* DPIDR1 is on bank 1 */
	adiv5_dp_write(dp, ADIV5_DP_SELECT, ADIV5_DP_BANK1);
	/* Read the other DPIDR and figure out the DP bus address width */
	const uint32_t dpidr1 = adiv5_dp_read(dp, ADIV6_DP_DPIDR1);
	dp->address_width = dpidr1 & ADIV6_DP_DPIDR1_ASIZE_MASK;

	DEBUG_INFO("DP DPIDR1 0x%08" PRIx32 " %u-bit addressing\n", dpidr1, dp->address_width);

	/* Now we know how wide the DP bus addresses are, read out the base pointers and validate them */
	target_addr64_t base_address = adiv6_dp_read_base_address(dp);
	if (!(base_address & ADIV6_DP_BASEPTR0_VALID)) {
		DEBUG_INFO("No valid base address on DP\n");
		return false;
	}
	if ((base_address & ((UINT64_C(1) << dp->address_width) - 1U)) != base_address) {
		DEBUG_INFO("Bad base address %" PRIx32 "%08" PRIx32 "on DP\n", (uint32_t)(base_address >> 32U),
			(uint32_t)base_address);
		return false;
	}
	base_address &= ~ADIV6_DP_BASEPTR0_VALID;
	DEBUG_INFO("Inspecting resource address 0x%" PRIx32 "%08" PRIx32 "\n", (uint32_t)(base_address >> 32U),
		(uint32_t)base_address);

	return false;
}
