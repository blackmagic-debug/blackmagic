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

typedef struct avr_pdi avr_pdi_s;

struct avr_pdi {
	uint32_t idcode;

	uint8_t dev_index;
	target_halt_reason_e halt_reason;

	bool (*ensure_nvm_idle)(const avr_pdi_s *pdi);
};

#define PDI_DATA_8  0x00U
#define PDI_DATA_16 0x01U
#define PDI_DATA_24 0x02U
#define PDI_DATA_32 0x03U

#define PDI_REG_R3 3U
#define PDI_REG_R4 4U

#define PDI_MODE_IND_PTR    0x00U
#define PDI_MODE_IND_INCPTR 0x04U
#define PDI_MODE_DIR_PTR    0x08U
#define PDI_MODE_DIR_INCPTR 0x0cU /* "Reserved" */

#define PDI_FLASH_OFFSET 0x00800000U

void avr_jtag_pdi_handler(uint8_t dev_index);
avr_pdi_s *avr_pdi_struct(target_s *target);

bool avr_pdi_reg_write(const avr_pdi_s *pdi, uint8_t reg, uint8_t value);
uint8_t avr_pdi_reg_read(const avr_pdi_s *pdi, uint8_t reg);

bool avr_pdi_write(const avr_pdi_s *pdi, uint8_t bytes, uint32_t reg, uint32_t value);
bool avr_pdi_read8(const avr_pdi_s *pdi, uint32_t reg, uint8_t *value);
bool avr_pdi_read16(const avr_pdi_s *pdi, uint32_t reg, uint16_t *value);
bool avr_pdi_read24(const avr_pdi_s *pdi, uint32_t reg, uint32_t *value);
bool avr_pdi_read32(const avr_pdi_s *pdi, uint32_t reg, uint32_t *value);
bool avr_pdi_write_ind(const avr_pdi_s *pdi, uint32_t addr, uint8_t ptr_mode, const void *src, uint32_t count);
bool avr_pdi_read_ind(const avr_pdi_s *pdi, uint32_t addr, uint8_t ptr_mode, void *dst, uint32_t count);

#endif /*TARGET_AVR_PDI_H*/
