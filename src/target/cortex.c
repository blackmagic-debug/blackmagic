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

/*
 * This file implements generic support for ARM Cortex family of processors.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "jep106.h"
#include "cortex.h"
#include "cortex_internal.h"

#define CORTEX_CPUID 0xd00U

adiv5_access_port_s *cortex_ap(target_s *t)
{
	return ((cortex_priv_s *)t->priv)->ap;
}

void cortex_priv_free(void *priv)
{
	adiv5_ap_unref(((cortex_priv_s *)priv)->ap);
	free(priv);
}

bool cortex_check_error(target_s *t)
{
	adiv5_access_port_s *ap = cortex_ap(t);
	return adiv5_dp_error(ap->dp) != 0;
}

uint32_t cortex_dbg_read32(target_s *const target, const uint16_t src)
{
	/* Translate the offset given in the src parameter into an address in the debug address space and read */
	const cortex_priv_s *const priv = (cortex_priv_s *)target->priv;
	adiv5_access_port_s *const ap = cortex_ap(target);
	uint32_t result = 0;
	adiv5_mem_read(ap, &result, priv->base_addr + src, sizeof(result));
	return result;
}

void cortex_dbg_write32(target_s *const target, const uint16_t dest, const uint32_t value)
{
	/* Translate the offset given int he dest parameter into an address int he debug address space and write */
	const cortex_priv_s *const priv = (cortex_priv_s *)target->priv;
	adiv5_access_port_s *const ap = cortex_ap(target);
	adiv5_mem_write(ap, priv->base_addr + dest, &value, sizeof(value));
}

void cortex_read_cpuid(target_s *target)
{
	/*
	 * The CPUID register is defined in the ARMv6, ARMv7 and ARMv8 architectures
	 * The PARTNO field is implementation defined, that is, the actual values are
	 * found in the Technical Reference Manual for each Cortex core.
	 */
	target->cpuid = cortex_dbg_read32(target, CORTEX_CPUID);
	const uint16_t cpuid_partno = target->cpuid & CORTEX_CPUID_PARTNO_MASK;
	switch (cpuid_partno) {
	case CORTEX_A5:
		target->core = "A5";
		break;
	case CORTEX_A7:
		target->core = "A7";
		break;
	case CORTEX_A8:
		target->core = "A8";
		break;
	case CORTEX_A9:
		target->core = "A9";
		break;
	case STAR_MC1:
		target->core = "STAR-MC1";
		break;
	case CORTEX_M33:
		target->core = "M33";
		break;
	case CORTEX_M23:
		target->core = "M23";
		break;
	case CORTEX_M3:
		target->core = "M3";
		break;
	case CORTEX_M4:
		target->core = "M4";
		break;
	case CORTEX_M7:
		target->core = "M7";
		if ((target->cpuid & CORTEX_CPUID_REVISION_MASK) == 0 && (target->cpuid & CORTEX_CPUID_PATCH_MASK) < 2U)
			DEBUG_WARN("Silicon bug: Single stepping will enter pending "
					   "exception handler with this M7 core revision!\n");
		break;
	case CORTEX_M0P:
		target->core = "M0+";
		break;
	case CORTEX_M0:
		target->core = "M0";
		break;
	default: {
		const adiv5_access_port_s *const ap = cortex_ap(target);
		if (ap->designer_code == JEP106_MANUFACTURER_ATMEL) /* Protected Atmel device? */
			break;
		DEBUG_WARN("Unexpected Cortex CPU partno %04x\n", cpuid_partno);
	}
	}
	DEBUG_INFO("CPUID 0x%08" PRIx32 " (%s var %" PRIx32 " rev %" PRIx32 ")\n", target->cpuid, target->core,
		(target->cpuid & CORTEX_CPUID_REVISION_MASK) >> 20U, target->cpuid & CORTEX_CPUID_PATCH_MASK);
}
