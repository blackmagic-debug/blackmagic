/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
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

#ifndef TARGET_AVR_PDI_H
#define TARGET_AVR_PDI_H

#include <stdint.h>
#include "target.h"

typedef struct avr_pdi {
	uint32_t idcode;

	uint8_t dev_index;
	target_halt_reason_e halt_reason;
} avr_pdi_s;

#define PDI_DATA_8  0x00U
#define PDI_DATA_16 0x01U
#define PDI_DATA_24 0x02U
#define PDI_DATA_32 0x03U

void avr_jtag_pdi_handler(uint8_t dev_index);
avr_pdi_s *avr_pdi_struct(target_s *target);

bool avr_pdi_write(const avr_pdi_s *pdi, uint8_t bytes, uint32_t reg, uint32_t value);
bool avr_pdi_read(const avr_pdi_s *pdi, uint8_t bytes, uint32_t reg, uint32_t *value);

#endif /*TARGET_AVR_PDI_H*/
