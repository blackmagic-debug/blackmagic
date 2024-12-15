/*
 * This file is part of the Black Magic Debug project.
 *
 * Based on work that is Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Copyright (C) 2024 Mary Guillemard <mary@mary.zone>
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
 * This file implements support ARMv8-A based processors.
 *
 * References:
 * DDI0487 - Arm Architecture Reference Manual for A-profile architecture
 *   https://documentation-service.arm.com/static/65fdad3c1bc22b03bca90781
 * 100442  - Arm Cortex-A55 Core Technical Reference Manual
 *   https://documentation-service.arm.com/static/649ac6d4df6cd61d528c2bf1
 */
#include "general.h"
#include "exception.h"
#include "adi.h"
#include "adiv5.h"
#include "target.h"
#include "target_internal.h"
#include "target_probe.h"
#include "jep106.h"
#include "cortex.h"
#include "cortex_internal.h"
#include "arm_coresight_cti.h"
#include "gdb_reg.h"
#include "gdb_packet.h"
#include "maths_utils.h"
#include "buffer_utils.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct cortexa_armv8_priv {
	/* Base core information */
	cortex_priv_s base;
	arm_coresight_cti_s cti;

	/* Cached value of EDSCR */
	uint32_t edscr;

	/* Indicate if the DC was init properly */
	bool dc_is_valid;
} cortexa_armv8_priv_s;

#define CORTEXA_ARMV8_TARGET_NAME ("ARM Cortex-A (ARMv8-A)")

#define CORTEXA_DBG_OSLAR_EL1 0x300U /* OS Lock Access Register */
#define CORTEXA_DBG_EDPRSR    0x314U /* Debug Processor Status Register */

#define CORTEXA_DBG_EDPRSR_POWERED_UP           (1U << 0U)
#define CORTEXA_DBG_EDPRSR_STICKY_PD            (1U << 1U)
#define CORTEXA_DBG_EDPRSR_RESET_STATUS         (1U << 2U)
#define CORTEXA_DBG_EDPRSR_STICKY_CORE_RESET    (1U << 3U)
#define CORTEXA_DBG_EDPRSR_HALTED               (1U << 4U)
#define CORTEXA_DBG_EDPRSR_OS_LOCK              (1U << 5U)
#define CORTEXA_DBG_EDPRSR_DOUBLE_LOCK          (1U << 6U)
#define CORTEXA_DBG_EDPRSR_STICKY_DEBUG_RESTART (1U << 11U)

static void cortexa_armv8_priv_free(void *const priv)
{
	arm_coresight_cti_fini(&((cortexa_armv8_priv_s *)priv)->cti);
	cortex_priv_free(priv);
}

static bool cortexa_armv8_oslock_unlock(target_s *const target)
{
	if ((cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR) & CORTEXA_DBG_EDPRSR_OS_LOCK)) {
		/* Lock set. Try to unlock */
		DEBUG_WARN("%s: OS lock set, unlocking\n", __func__);
		cortex_dbg_write32(target, CORTEXA_DBG_OSLAR_EL1, 0U);

		/* Read back to check if we succeeded */
		const bool locked = cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR) & CORTEXA_DBG_EDPRSR_OS_LOCK;
		if (locked)
			DEBUG_ERROR("%s: Lock sticky. Core not powered?\n", __func__);
		return !locked;
	}

	return true;
}

static bool cortexa_armv8_ensure_core_powered(target_s *const target)
{
	uint32_t edprsr = cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR);

	/* XXX: We don't have any way of powering it up, check if we are missing something from the docs */
	if (!(edprsr & CORTEXA_DBG_EDPRSR_POWERED_UP))
		return false;

	/* Check for the OS double lock */
	if (edprsr & CORTEXA_DBG_EDPRSR_DOUBLE_LOCK)
		return false;

	/*
	 * Finally, check for the normal OS Lock and clear it if it's set prior to halting the core.
	 * Trying to do this after target_halt_request() does not function over JTAG and triggers
	 * the lock sticky message.
	 */
	if (edprsr & CORTEXA_DBG_EDPRSR_OS_LOCK)
		return cortexa_armv8_oslock_unlock(target);

	return true;
}

bool cortexa_armv8_dc_probe(adiv5_access_port_s *const ap, const target_addr_t base_address)
{
	target_s *target = target_new();
	if (!target)
		return NULL;

	adiv5_ap_ref(ap);
	if (ap->dp->version >= 2 && ap->dp->target_designer_code != 0) {
		/* Use TARGETID register to identify target */
		target->designer_code = ap->dp->target_designer_code;
		target->part_id = ap->dp->target_partno;
	} else {
		/* Use AP DESIGNER and AP PARTNO to identify target */
		target->designer_code = ap->designer_code;
		target->part_id = ap->partno;
	}

	cortexa_armv8_priv_s *const priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
				 /* XXX: Free target? */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return NULL;
	}

	target->driver = CORTEXA_ARMV8_TARGET_NAME;
	target->priv = priv;
	target->priv_free = cortexa_armv8_priv_free;
	priv->base.ap = ap;
	priv->base.base_addr = base_address;

	/* Ensure the core is powered up and we can talk to it */
	if (!cortexa_armv8_ensure_core_powered(target))
		return false;

	priv->dc_is_valid = true;

	return true;
}

bool cortexa_armv8_cti_probe(adiv5_access_port_s *const ap, const target_addr_t base_address)
{
	target_s *target = target_list_get_last();

	if (!target)
		return false;

	/* Ensure that the previous target is actually from the same driver */
	if (strcmp(target->driver, CORTEXA_ARMV8_TARGET_NAME) != 0)
		return false;

	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Init CTI component */
	arm_coresight_cti_init(&priv->cti, ap, base_address);

	/* In case DC init failed, we should not try to do anything here */
	if (!priv->dc_is_valid)
		return false;

	/* XXX: Configure CTI component */
	/* XXX: Halt APIs */
	/* XXX: Try to halt the PE */
	/* XXX: Read CPUID */
	/* XXX: Detect debug features */
	/* XXX: Detect optional features */
	/* XXX: Attach / Detach APIs */
	/* XXX: Register IO APIs */
	/* XXX: Memory IO APIs */
	/* XXX: Breakpoint APIs */

	target_check_error(target);

	return true;
}