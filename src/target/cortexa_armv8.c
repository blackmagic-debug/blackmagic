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

#define CORTEXA_DBG_EDECR     0x024U /* Debug Execution Control Register */
#define CORTEXA_DBG_EDSCR     0x088U /* Debug Status and Control Register */
#define CORTEXA_DBG_EDRCR     0x090U /* Debug Reserve Control Register */
#define CORTEXA_DBG_OSLAR_EL1 0x300U /* OS Lock Access Register */
#define CORTEXA_DBG_EDPRSR    0x314U /* Debug Processor Status Register */

#define CORTEXA_DBG_EDECR_SINGLE_STEP (1 << 2U)

#define CORTEXA_DBG_EDSCR_RX_FULL                    (1 << 30U)
#define CORTEXA_DBG_EDSCR_TX_FULL                    (1 << 29U)
#define CORTEXA_DBG_EDSCR_ITO                        (1 << 28U)
#define CORTEXA_DBG_EDSCR_RXO                        (1 << 27U)
#define CORTEXA_DBG_EDSCR_TXU                        (1 << 26U)
#define CORTEXA_DBG_EDSCR_PIPE_ADV                   (1 << 25U)
#define CORTEXA_DBG_EDSCR_ITE                        (1 << 24U)
#define CORTEXA_DBG_EDSCR_INTERRUPT_DISABLE          (1 << 22U)
#define CORTEXA_DBG_EDSCR_INTERRUPT_DISABLE_MASK     (0xff3fffffU)
#define CORTEXA_DBG_EDSCR_TDA                        (1 << 21U)
#define CORTEXA_DBG_EDSCR_MA                         (1 << 20U)
#define CORTEXA_DBG_EDSCR_HALTING_DBG_ENABLE         (1 << 14U)
#define CORTEXA_DBG_EDSCR_ERR                        (1 << 6U)
#define CORTEXA_DBG_EDSCR_STATUS_MASK                0x0000003fU
#define CORTEXA_DBG_EDSCR_STATUS_PE_EXIT_DBG         0x00000001U
#define CORTEXA_DBG_EDSCR_STATUS_PE_DGB              0x00000002U
#define CORTEXA_DBG_EDSCR_STATUS_BREAKPOINT          0x00000007U
#define CORTEXA_DBG_EDSCR_STATUS_EXT_DBG_REQ         0x00000013U
#define CORTEXA_DBG_EDSCR_STATUS_HALT_STEP_NORMAL    0x0000001bU
#define CORTEXA_DBG_EDSCR_STATUS_HALT_STEP_EXCLUSIVE 0x0000001fU
#define CORTEXA_DBG_EDSCR_STATUS_OS_UNLOCK_CATCH     0x00000023U
#define CORTEXA_DBG_EDSCR_STATUS_RESET_CATCH         0x00000027U
#define CORTEXA_DBG_EDSCR_STATUS_WATCHPOINT          0x0000002bU
#define CORTEXA_DBG_EDSCR_STATUS_HLT_INSTRUCTION     0x0000002fU
#define CORTEXA_DBG_EDSCR_STATUS_SW_ACCESS_DBG_REG   0x00000033U
#define CORTEXA_DBG_EDSCR_STATUS_EXCEPTION_CATCH     0x00000037U
#define CORTEXA_DBG_EDSCR_STATUS_HALT_STEP_NO_SYN    0x0000003bU

#define CORTEXA_DBG_EDRCR_CLR_STICKY_ERR (1U << 2U)

#define CORTEXA_DBG_EDPRSR_POWERED_UP           (1U << 0U)
#define CORTEXA_DBG_EDPRSR_STICKY_PD            (1U << 1U)
#define CORTEXA_DBG_EDPRSR_RESET_STATUS         (1U << 2U)
#define CORTEXA_DBG_EDPRSR_STICKY_CORE_RESET    (1U << 3U)
#define CORTEXA_DBG_EDPRSR_HALTED               (1U << 4U)
#define CORTEXA_DBG_EDPRSR_OS_LOCK              (1U << 5U)
#define CORTEXA_DBG_EDPRSR_DOUBLE_LOCK          (1U << 6U)
#define CORTEXA_DBG_EDPRSR_STICKY_DEBUG_RESTART (1U << 11U)

#define CORTEXA_CTI_CHANNEL_HALT_SINGLE      0U
#define CORTEXA_CTI_CHANNEL_RESTART          1U
#define CORTEXA_CTI_EVENT_HALT_PE_SINGLE_IDX 0U
#define CORTEXA_CTI_EVENT_RESTART_PE_IDX     1U

static void cortexa_armv8_halt_request(target_s *target);
static target_halt_reason_e cortexa_armv8_halt_poll(target_s *target, target_addr64_t *watch);
static void cortexa_armv8_halt_resume(target_s *target, bool step);
static bool cortexa_armv8_attach(target_s *target);
static void cortexa_armv8_detach(target_s *target);

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

bool cortexa_armv8_configure_cti(arm_coresight_cti_s *const cti)
{
	/* Ensure CTI is unlocked */
	if (!arm_coresight_cti_ensure_unlock(cti))
		return false;

	/* Ensure CTI is disabled */
	arm_coresight_cti_enable(cti, false);

	/* Do not allow any propagation of events to CTM by default */
	arm_coresight_cti_set_gate(cti, 0);

	/* Configure identity mapping for events (Following H5-1 and H5-2 example) */
	arm_coresight_cti_set_output_channel(cti, CORTEXA_CTI_EVENT_HALT_PE_SINGLE_IDX, CORTEXA_CTI_CHANNEL_HALT_SINGLE);
	arm_coresight_cti_set_output_channel(cti, CORTEXA_CTI_EVENT_RESTART_PE_IDX, CORTEXA_CTI_CHANNEL_RESTART);

	/* Now we enable CTI */
	arm_coresight_cti_enable(cti, true);

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

	/* Configure CTI component */
	if (!cortexa_armv8_configure_cti(&priv->cti))
		return false;

	target->halt_request = cortexa_armv8_halt_request;
	target->halt_poll = cortexa_armv8_halt_poll;
	target->halt_resume = cortexa_armv8_halt_resume;

	/* Try to halt the PE */
	target_halt_request(target);
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);
	target_halt_reason_e reason = TARGET_HALT_RUNNING;
	while (!platform_timeout_is_expired(&timeout) && reason == TARGET_HALT_RUNNING)
		reason = target_halt_poll(target, NULL);
	if (reason != TARGET_HALT_REQUEST) {
		DEBUG_ERROR("Failed to halt the core, reason: %d\n", reason);
		return false;
	}

	cortex_read_cpuid(target);

	/* XXX: Detect debug features */
	/* XXX: Detect optional features */

	target->attach = cortexa_armv8_attach;
	target->detach = cortexa_armv8_detach;

	/* XXX: Register IO APIs */
	/* XXX: Memory IO APIs */
	/* XXX: Breakpoint APIs */

	target_check_error(target);

	return true;
}

static void cortexa_armv8_halt_request(target_s *const target)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* First ensure that halting events are enabled */
	TRY (EXCEPTION_TIMEOUT) {
		priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
		priv->edscr |= CORTEXA_DBG_EDSCR_HALTING_DBG_ENABLE;
		cortex_dbg_write32(target, CORTEXA_DBG_EDSCR, priv->edscr);
	}
	CATCH () {
	default:
		tc_printf(target, "Timeout sending interrupt, is target in WFI?\n");
	}

	/* We assume that halting channel do not pass events to the CTM */
	/* XXX: SMP handling */

	/* Send CTI request */
	arm_coresight_cti_pulse_channel(&priv->cti, CORTEXA_CTI_CHANNEL_HALT_SINGLE);
}

static target_halt_reason_e cortexa_armv8_halt_poll(target_s *const target, target_addr64_t *const watch)
{
	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	volatile uint32_t edprsr = 0;
	TRY (EXCEPTION_ALL) {
		/* If this times out because the target is in WFI then the target is still running. */
		edprsr = cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR);
	}
	CATCH () {
	case EXCEPTION_ERROR:
		/* Things went seriously wrong and there is no recovery from this... */
		target_list_free();
		return TARGET_HALT_ERROR;
	case EXCEPTION_TIMEOUT:
		/* XXX: Is that also valid for our target? */
		/* Timeout isn't actually a problem and probably means target is in WFI */
		return TARGET_HALT_RUNNING;
	}

	/* Check that the core is powered up */
	/* XXX: Should we add a new status in that case? */
	if (!(edprsr & CORTEXA_DBG_EDPRSR_POWERED_UP))
		return TARGET_HALT_ERROR;

	/* Check that the core actually halted */
	if (!(edprsr & CORTEXA_DBG_EDPRSR_HALTED))
		return TARGET_HALT_RUNNING;

	priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);

	/* Ensure the OS lock is cleared as a precaution */
	cortexa_armv8_oslock_unlock(target);

	/* Make sure halting debug is enabled (so breakpoints work) */
	priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
	priv->edscr |= CORTEXA_DBG_EDSCR_HALTING_DBG_ENABLE;
	cortex_dbg_write32(target, CORTEXA_DBG_EDSCR, priv->edscr);

	/* XXX: Save the target core's registers as debugging operations clobber them */

	target_halt_reason_e reason = TARGET_HALT_FAULT;
	/* Determine why we halted exactly from the Method Of Entry bits */
	switch (priv->edscr & CORTEXA_DBG_EDSCR_STATUS_MASK) {
	case CORTEXA_DBG_EDSCR_STATUS_PE_EXIT_DBG:
		reason = TARGET_HALT_RUNNING;
		break;
	case CORTEXA_DBG_EDSCR_STATUS_PE_DGB:
	case CORTEXA_DBG_EDSCR_STATUS_EXT_DBG_REQ:
		reason = TARGET_HALT_REQUEST;
		break;
	case CORTEXA_DBG_EDSCR_STATUS_BREAKPOINT:
	case CORTEXA_DBG_EDSCR_STATUS_HLT_INSTRUCTION:
	case CORTEXA_DBG_EDSCR_STATUS_EXCEPTION_CATCH:
		reason = TARGET_HALT_BREAKPOINT;
		break;
	case CORTEXA_DBG_EDSCR_STATUS_WATCHPOINT: {
		if (priv->base.watchpoints_mask == 1U) {
			for (const breakwatch_s *breakwatch = target->bw_list; breakwatch; breakwatch = breakwatch->next) {
				if (breakwatch->type != TARGET_WATCH_READ && breakwatch->type != TARGET_WATCH_WRITE &&
					breakwatch->type != TARGET_WATCH_ACCESS)
					continue;
				*watch = breakwatch->addr;
				break;
			}
			reason = TARGET_HALT_WATCHPOINT;
		} else
			reason = TARGET_HALT_BREAKPOINT;
		break;
	}
	case CORTEXA_DBG_EDSCR_STATUS_HALT_STEP_NORMAL:
	case CORTEXA_DBG_EDSCR_STATUS_HALT_STEP_EXCLUSIVE:
	case CORTEXA_DBG_EDSCR_STATUS_HALT_STEP_NO_SYN:
		reason = TARGET_HALT_BREAKPOINT;
		break;

	case CORTEXA_DBG_EDSCR_STATUS_OS_UNLOCK_CATCH:
	case CORTEXA_DBG_EDSCR_STATUS_RESET_CATCH:
	case CORTEXA_DBG_EDSCR_STATUS_SW_ACCESS_DBG_REG:
		/* XXX What do we do for those cases? */
		break;
	}
	/* Check if we halted because we were actually single-stepping */
	return reason;
}

static void cortexa_armv8_halt_resume(target_s *const target, const bool step)
{
	uint32_t edprsr = cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR);

	/* Check that the core is powered up */
	if (!(edprsr & CORTEXA_DBG_EDPRSR_POWERED_UP))
		return;

	if (!(edprsr & CORTEXA_DBG_EDPRSR_HALTED))
		return;

	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* Ensure consistent single step state */
	cortex_dbg_write32(
		target, CORTEXA_DBG_EDECR, cortex_dbg_read32(target, CORTEXA_DBG_EDECR) & ~CORTEXA_DBG_EDECR_SINGLE_STEP);

	/* XXX: Restore the core's registers so the running program doesn't know we've been in there */

	/* First ensure that halting events are enabled */
	priv->edscr = cortex_dbg_read32(target, CORTEXA_DBG_EDSCR);
	priv->edscr |= CORTEXA_DBG_EDSCR_HALTING_DBG_ENABLE;

	/* Handle single step */
	if (step) {
		cortex_dbg_write32(
			target, CORTEXA_DBG_EDECR, cortex_dbg_read32(target, CORTEXA_DBG_EDECR) | CORTEXA_DBG_EDECR_SINGLE_STEP);
		priv->edscr |= CORTEXA_DBG_EDSCR_INTERRUPT_DISABLE;
	} else {
		priv->edscr &= ~CORTEXA_DBG_EDSCR_INTERRUPT_DISABLE;
	}
	cortex_dbg_write32(target, CORTEXA_DBG_EDSCR, priv->edscr);

	/* Clear any possible error that might have happened */
	cortex_dbg_write32(target, CORTEXA_DBG_EDRCR, CORTEXA_DBG_EDRCR_CLR_STICKY_ERR);

	/* XXX: Mark the fault status and address cache invalid */

	/* We assume that halting channel do not pass events to the CTM */

	/* Acknowledge pending halt PE event */
	arm_coresight_cti_acknowledge_interrupt(&priv->cti, CORTEXA_CTI_EVENT_HALT_PE_SINGLE_IDX);

	/* Wait for it to be deasserted */
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);

	bool halt_pe_event_high = true;
	while (!platform_timeout_is_expired(&timeout) && halt_pe_event_high)
		halt_pe_event_high = arm_coresight_cti_read_output_channel_status(&priv->cti, CORTEXA_CTI_CHANNEL_HALT_SINGLE);

	if (halt_pe_event_high) {
		DEBUG_ERROR("Failed to acknowledge pending halt PE event!\n");
		return;
	}

	/* Send CTI request */
	arm_coresight_cti_pulse_channel(&priv->cti, CORTEXA_CTI_CHANNEL_RESTART);

	/* Then poll for when the core actually resumes */
	platform_timeout_set(&timeout, 250);
	edprsr = 0;
	while ((edprsr & CORTEXA_DBG_EDPRSR_STICKY_DEBUG_RESTART) && !platform_timeout_is_expired(&timeout))
		edprsr = cortex_dbg_read32(target, CORTEXA_DBG_EDPRSR);

	if (edprsr & CORTEXA_DBG_EDPRSR_STICKY_DEBUG_RESTART)
		DEBUG_ERROR("Failed to resume PE!\n");
}

static bool cortexa_armv8_attach(target_s *target)
{
	adiv5_access_port_s *ap = cortex_ap(target);
	/* Mark the DP as being in fault so error recovery will switch to this core when in multi-drop mode */
	ap->dp->fault = 1;

	/* Clear any pending fault condition (and switch to this core) */
	target_check_error(target);

	/* Ensure the OS lock is unset just in case it was re-set between probe and attach */
	cortexa_armv8_oslock_unlock(target);
	/* Try to halt the core */
	target_halt_request(target);
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, 250);
	target_halt_reason_e reason = TARGET_HALT_RUNNING;
	while (!platform_timeout_is_expired(&timeout) && reason == TARGET_HALT_RUNNING)
		reason = target_halt_poll(target, NULL);
	if (reason != TARGET_HALT_REQUEST) {
		DEBUG_ERROR("Failed to halt the core\n");
		return false;
	}

	cortexa_armv8_priv_s *const priv = (cortexa_armv8_priv_s *)target->priv;

	/* XXX: Clear any stale breakpoints */
	priv->base.breakpoints_mask = 0U;

	/* XXX: Clear any stale watchpoints */
	priv->base.watchpoints_mask = 0U;

	return true;
}

static void cortexa_armv8_detach(target_s *target)
{
	/* XXX: Clear any set breakpoints */
	/* XXX: Clear any set watchpoints */

	target_halt_resume(target, false);
}
