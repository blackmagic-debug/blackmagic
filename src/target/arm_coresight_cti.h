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

#ifndef TARGET_ARM_CORESIGHT_CTI_H
#define TARGET_ARM_CORESIGHT_CTI_H

#include "general.h"
#include "adiv5.h"
#include <stdint.h>

#define CTI_CHANNEL_INVALID -1
#define CTI_CHANNEL_ID(ch)  (1U << ch)

typedef struct arm_coresight_cti {
	/* AP from which this CPU hangs */
	adiv5_access_port_s *ap;
	/* Base address for the debug interface block */
	uint32_t base_addr;

	/* Indicate if the object was properly initialized */
	bool initialized;
} arm_coresight_cti_s;

void arm_coresight_cti_init(arm_coresight_cti_s *data, adiv5_access_port_s *ap, target_addr_t base_address);
void arm_coresight_cti_fini(arm_coresight_cti_s *data);
bool arm_coresight_cti_ensure_unlock(arm_coresight_cti_s *cti);
void arm_coresight_cti_enable(arm_coresight_cti_s *cti, bool enable);
uint32_t arm_coresight_cti_get_gate(arm_coresight_cti_s *cti);
void arm_coresight_cti_set_gate(arm_coresight_cti_s *cti, uint32_t gate_mask);
void arm_coresight_cti_set_input_channel(arm_coresight_cti_s *cti, uint8_t event_idx, int8_t channel);
void arm_coresight_cti_set_output_channel(arm_coresight_cti_s *cti, uint8_t event_idx, int8_t channel);
void arm_coresight_cti_acknowledge_interrupt(arm_coresight_cti_s *cti, uint8_t event_idx);
void arm_coresight_cti_pulse_channel(arm_coresight_cti_s *cti, uint8_t channel);
bool arm_coresight_cti_read_input_channel_status(arm_coresight_cti_s *cti, uint8_t channel);
bool arm_coresight_cti_read_output_channel_status(arm_coresight_cti_s *cti, uint8_t channel);

#endif /*TARGET_ARM_CORESIGHT_CTI_H*/