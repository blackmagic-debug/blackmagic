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

#ifndef INCLUDE_BUFFER_UTILS_H
#define INCLUDE_BUFFER_UTILS_H

#include <stdint.h>
#include <stddef.h>

static inline void write_le2(uint8_t *const buffer, const size_t offset, const uint16_t value)
{
	buffer[offset + 0U] = value & 0xffU;
	buffer[offset + 1U] = (value >> 8U) & 0xffU;
}

static inline void write_le4(uint8_t *const buffer, const size_t offset, const uint32_t value)
{
	buffer[offset + 0U] = value & 0xffU;
	buffer[offset + 1U] = (value >> 8U) & 0xffU;
	buffer[offset + 2U] = (value >> 16U) & 0xffU;
	buffer[offset + 3U] = (value >> 24U) & 0xffU;
}

static inline uint16_t read_le2(const uint8_t *const buffer, const size_t offset)
{
	return buffer[offset + 0U] | ((uint16_t)buffer[offset + 1U] << 8U);
}

static inline uint32_t read_le4(const uint8_t *const buffer, const size_t offset)
{
	return buffer[offset + 0U] | ((uint32_t)buffer[offset + 1U] << 8U) | ((uint32_t)buffer[offset + 2U] << 16U) |
		((uint32_t)buffer[offset + 3U] << 24U);
}

static inline uint32_t read_be4(const uint8_t *const buffer, const size_t offset)
{
	return ((uint32_t)buffer[offset + 0U] << 24U) | ((uint32_t)buffer[offset + 1U] << 16U) |
		((uint32_t)buffer[offset + 2U] << 8U) | buffer[offset + 3U];
}

#endif /*INCLUDE_BUFFER_UTILS_H*/
