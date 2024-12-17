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
 * This file implements support for ARM CoreSight Cross Trigger (CTI)
 *
 * References:
 * DDI0480E - ARM CoreSight SoC-400 Technical Reference Manual
 *   https://documentation-service.arm.com/static/5f873b64f86e16515cdb7582
 * 100806   - Arm CoreSight System-on-Chip SoC-600 Technical Reference Manual
 *   https://documentation-service.arm.com/static/65f96505d98ff22ceb0c0b79
 * IHI0029  - Arm CoreSight Architecture Specification
 *   https://documentation-service.arm.com/static/63a03a981d698c4dc521ca77
 */

#include "arm_coresight_cti.h"
#include "general.h"
#include <stdint.h>

#define CTI_CONTROL       0x000u
#define CTI_INTACK        0x010u
#define CTI_APPSET        0x014u
#define CTI_APPCLEAR      0x018u
#define CTI_APPPULSE      0x01cu
#define CTI_INEN(n)       (0x020u + ((n)*4U))
#define CTI_OUTEN(n)      (0x0a0u + ((n)*4U))
#define CTI_TRIGINSTATUS  0x130u
#define CTI_TRIGOUTSTATUS 0x134u
#define CTI_CHINSTATUS    0x138u
#define CTI_CHOUTSTATUS   0x13cu
#define CTI_GATE          0x140u
#define CTI_ASIC_CTL      0x144u
#define IT_CHINACK        0xedcU
#define IT_TRIGINACK      0xee0U
#define IT_CHOUT          0xee4U
#define IT_TRIGOUT        0xee8U
#define IT_CHOUTACK       0xeecU
#define IT_TRIGOUTACK     0xef0U
#define IT_CHIN           0xef4U
#define IT_TRIGIN         0xef8U
#define IT_CTRL           0xf00U
#define CLAIM_SET         0xfa0U
#define CLAIM_CLR         0xfa4U
#define CTI_LAR           0xfb0U
#define CTI_LSR           0xfb4U
#define AUTH_STATUS       0xfb8U

#define CTI_CONTROL_GLBEN  (1 << 0U)
#define CTI_LAR_UNLOCK_KEY 0xc5acce55u
#define CTI_LSR_SLI        (1 << 0U)
#define CTI_LSR_SLK        (1 << 1U)

void arm_coresight_cti_init(arm_coresight_cti_s *data, adiv5_access_port_s *ap, target_addr_t base_address)
{
	adiv5_ap_ref(ap);

	data->ap = ap;
	data->base_addr = base_address;
	data->initialized = true;
}

void arm_coresight_cti_fini(arm_coresight_cti_s *data)
{
	if (!data->initialized)
		return;

	adiv5_ap_unref(data->ap);
	data->initialized = false;
}

static uint32_t arm_coresight_cti_read32(arm_coresight_cti_s *const cti, const uint16_t src)
{
	adiv5_access_port_s *const ap = cti->ap;

	uint32_t result = 0;
	adiv5_mem_read(ap, &result, cti->base_addr + src, sizeof(result));
	return result;
}

static void arm_coresight_cti_write32(arm_coresight_cti_s *const cti, const uint16_t dest, const uint32_t value)
{
	adiv5_mem_write(cti->ap, cti->base_addr + dest, &value, sizeof(value));
}

bool arm_coresight_cti_ensure_unlock(arm_coresight_cti_s *cti)
{
	const uint32_t lock_status = arm_coresight_cti_read32(cti, CTI_LSR);

	/* If lock register is implemented and active, unlock */
	if (lock_status & (CTI_LSR_SLI | CTI_LSR_SLK)) {
		arm_coresight_cti_write32(cti, CTI_LAR, CTI_LAR_UNLOCK_KEY);

		const bool locked = arm_coresight_cti_read32(cti, CTI_LSR) & CTI_LSR_SLK;

		if (locked)
			DEBUG_ERROR("%s: Lock sticky. Core not powered?\n", __func__);

		return !locked;
	}

	return true;
}

void arm_coresight_cti_enable(arm_coresight_cti_s *cti, bool enable)
{
	arm_coresight_cti_write32(cti, CTI_CONTROL, enable ? CTI_CONTROL_GLBEN : 0);
}

uint32_t arm_coresight_cti_get_gate(arm_coresight_cti_s *cti)
{
	return arm_coresight_cti_read32(cti, CTI_GATE);
}

void arm_coresight_cti_set_gate(arm_coresight_cti_s *cti, uint32_t gate_mask)
{
	arm_coresight_cti_write32(cti, CTI_GATE, gate_mask);
}

void arm_coresight_cti_set_input_channel(arm_coresight_cti_s *cti, uint8_t event_idx, int8_t channel)
{
	if (event_idx > 31) {
		DEBUG_ERROR("%s: Invalid event_idx %d\n", __func__, event_idx);
		return;
	}

	if (channel > 31) {
		DEBUG_ERROR("%s: Invalid channel %d\n", __func__, channel);
		return;
	}

	if (channel != CTI_CHANNEL_INVALID) {
		arm_coresight_cti_write32(cti, CTI_INEN(event_idx), CTI_CHANNEL_ID(channel));
	} else {
		arm_coresight_cti_write32(cti, CTI_INEN(event_idx), 0);
	}
}

void arm_coresight_cti_set_output_channel(arm_coresight_cti_s *cti, uint8_t event_idx, int8_t channel)
{
	if (event_idx > 31) {
		DEBUG_ERROR("%s: Invalid event_idx %d\n", __func__, event_idx);
		return;
	}

	if (channel > 31) {
		DEBUG_ERROR("%s: Invalid channel %d\n", __func__, channel);
		return;
	}

	if (channel != CTI_CHANNEL_INVALID) {
		arm_coresight_cti_write32(cti, CTI_OUTEN(event_idx), CTI_CHANNEL_ID(channel));
	} else {
		arm_coresight_cti_write32(cti, CTI_OUTEN(event_idx), 0);
	}
}

void arm_coresight_cti_acknowledge_interrupt(arm_coresight_cti_s *cti, uint8_t event_idx)
{
	if (event_idx > 31) {
		DEBUG_ERROR("%s: Invalid event_idx %d\n", __func__, event_idx);
		return;
	}

	arm_coresight_cti_write32(cti, CTI_INTACK, 1 << event_idx);
}

void arm_coresight_cti_pulse_channel(arm_coresight_cti_s *cti, uint8_t channel)
{
	if (channel > 31) {
		DEBUG_ERROR("%s: Invalid channel %d\n", __func__, channel);
		return;
	}

	arm_coresight_cti_write32(cti, CTI_APPPULSE, CTI_CHANNEL_ID(channel));
}

bool arm_coresight_cti_read_input_channel_status(arm_coresight_cti_s *cti, uint8_t channel)
{
	if (channel > 31) {
		DEBUG_ERROR("%s: Invalid channel %d\n", __func__, channel);
		return false;
	}

	return ((arm_coresight_cti_read32(cti, CTI_CHINSTATUS) >> channel) & 1) != 0;
}

bool arm_coresight_cti_read_output_channel_status(arm_coresight_cti_s *cti, uint8_t channel)
{
	if (channel > 31) {
		DEBUG_ERROR("%s: Invalid channel %d\n", __func__, channel);
		return false;
	}

	return ((arm_coresight_cti_read32(cti, CTI_CHOUTSTATUS) >> channel) & 1) != 0;
}
