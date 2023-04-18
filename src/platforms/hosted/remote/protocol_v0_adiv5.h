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

#ifndef PLATFORMS_HOSTED_REMOTE_PROTOCOL_V0_ADIV5_H
#define PLATFORMS_HOSTED_REMOTE_PROTOCOL_V0_ADIV5_H

#include <stdint.h>
#include <stddef.h>
#include "adiv5.h"

uint32_t remote_v0_adiv5_raw_access(adiv5_debug_port_s *dp, uint8_t rnw, uint16_t addr, uint32_t request_value);
uint32_t remote_v0_adiv5_dp_read(adiv5_debug_port_s *dp, uint16_t addr);
uint32_t remote_v0_adiv5_ap_read(adiv5_access_port_s *ap, uint16_t addr);
void remote_v0_adiv5_ap_write(adiv5_access_port_s *ap, uint16_t addr, uint32_t value);
void remote_v0_adiv5_mem_read_bytes(adiv5_access_port_s *ap, void *dest, uint32_t src, size_t read_length);
void remote_v0_adiv5_mem_write_bytes(
	adiv5_access_port_s *ap, uint32_t dest, const void *src, size_t write_length, align_e align);

#endif /*PLATFORMS_HOSTED_REMOTE_PROTOCOL_V0_ADIV5_H*/
