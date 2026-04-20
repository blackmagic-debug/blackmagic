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
#include <string.h>

#include <general.h>

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

static inline void write_be4(uint8_t *const buffer, const size_t offset, const uint32_t value)
{
	buffer[offset + 0U] = (value >> 24U) & 0xffU;
	buffer[offset + 1U] = (value >> 16U) & 0xffU;
	buffer[offset + 2U] = (value >> 8U) & 0xffU;
	buffer[offset + 3U] = value & 0xffU;
}

static inline uint16_t read_le2(const uint8_t *const buffer, const size_t offset)
{
	uint8_t data[2U];
	memcpy(data, buffer + offset, 2U);
	return data[0U] | ((uint16_t)data[1U] << 8U);
}

static inline uint32_t read_le4(const uint8_t *const buffer, const size_t offset)
{
	uint8_t data[4U];
	memcpy(data, buffer + offset, 4U);
	return data[0U] | ((uint32_t)data[1U] << 8U) | ((uint32_t)data[2U] << 16U) | ((uint32_t)data[3U] << 24U);
}

static inline uint64_t read_le8(const uint8_t *const buffer, const size_t offset)
{
	uint8_t data[8U];
	memcpy(data, buffer + offset, 8U);
	return data[0U] | ((uint64_t)data[1U] << 8U) | ((uint64_t)data[2U] << 16U) | ((uint64_t)data[3U] << 24U) |
		((uint64_t)data[4U] << 32U) | ((uint64_t)data[5U] << 40U) | ((uint64_t)data[6U] << 48U) |
		((uint64_t)data[7U] << 56U);
}

static inline uint32_t read_be4(const uint8_t *const buffer, const size_t offset)
{
	uint8_t data[4U];
	memcpy(data, buffer + offset, 4U);
	return ((uint32_t)data[0U] << 24U) | ((uint32_t)data[1U] << 16U) | ((uint32_t)data[2U] << 8U) | data[3U];
}

static inline uint64_t read_be8(const uint8_t *const buffer, const size_t offset)
{
	uint8_t data[8U];
	memcpy(data, buffer + offset, 8U);
	return ((uint64_t)data[0] << 56U) | ((uint64_t)data[1] << 48U) | ((uint64_t)data[2] << 40U) |
		((uint64_t)data[3] << 32U) | ((uint64_t)data[4] << 24U) | ((uint64_t)data[5] << 16U) |
		((uint64_t)data[6] << 8U) | data[7];
}

static inline size_t write_char(char *const buffer, const size_t buffer_size, const size_t offset, const char c)
{
	if (buffer && offset < buffer_size)
		buffer[offset] = c;
	return offset + 1U;
}

static inline uint8_t reverse_bits8(const uint8_t data)
{
	if (!BMD_CONSTANT_P(data)) {
#if defined(__arm__) || defined(__aarch64__)
		uint32_t result;
		__asm__("rbit %0, %1" : "=r"(result) : "r"(data));
		return (result & 0xff000000U) >> 24U;
#endif
	}
	// We don't have an optimized routine for this, so just use the fallback
	return ((data & 0x01U) << 7U) | ((data & 0x02U) << 5U) | ((data & 0x04U) << 3U) | ((data & 0x08U) << 1U) |
		((data & 0x10U) >> 1U) | ((data & 0x20U) >> 3U) | ((data & 0x40U) >> 5U) | ((data & 0x80U) >> 7U);
}

static inline uint16_t reverse_bits16(const uint16_t data)
{
	if (!BMD_CONSTANT_P(data)) {
#if defined(__arm__) || defined(__aarch64__)
		uint32_t result;
		__asm__("rbit %0, %1" : "=r"(result) : "r"(data));
		return (result & 0xffff0000U) >> 16U;
#endif
	}
	// We don't have an optimized routine for this, so just use the fallback
	return ((data & 0x0001U) << 15U) | ((data & 0x0002U) << 13U) | ((data & 0x0004U) << 11U) |
		((data & 0x0008U) << 9U) | ((data & 0x0010U) << 7U) | ((data & 0x0020U) << 5U) | ((data & 0x0040U) << 3U) |
		((data & 0x0080U) << 1U) | ((data & 0x0100U) >> 1U) | ((data & 0x0200U) >> 3U) | ((data & 0x0400U) >> 5U) |
		((data & 0x0800U) >> 7U) | ((data & 0x1000U) >> 9U) | ((data & 0x2000U) >> 11U) | ((data & 0x4000U) >> 13U) |
		((data & 0x8000U) >> 15U);
}

static inline uint32_t reverse_bits24(const uint32_t data)
{
	if (!BMD_CONSTANT_P(data)) {
#if defined(__arm__) || defined(__aarch64__)
		uint32_t result;
		__asm__("rbit %0, %1" : "=r"(result) : "r"(data));
		return (result & 0xffffff00U) >> 8U;
#endif
	}
	// We don't have an optimized routine for this, so just use the fallback
	return ((data & 0x00000001U) << 23U) | ((data & 0x00000002U) << 21U) | ((data & 0x00000004U) << 19U) |
		((data & 0x00000008U) << 17U) | ((data & 0x00000010U) << 15U) | ((data & 0x00000020U) << 13U) |
		((data & 0x00000040U) << 11U) | ((data & 0x00000080U) << 9U) | ((data & 0x00000100U) << 7U) |
		((data & 0x00000200U) << 5U) | ((data & 0x00000400U) << 3U) | ((data & 0x00000800U) << 1U) |
		((data & 0x00001000U) >> 1U) | ((data & 0x00002000U) >> 3U) | ((data & 0x00004000U) >> 5U) |
		((data & 0x00008000U) >> 7U) | ((data & 0x00010000U) >> 9U) | ((data & 0x00020000U) >> 11U) |
		((data & 0x00040000U) >> 13U) | ((data & 0x00080000U) >> 15U) | ((data & 0x00100000U) >> 17U) |
		((data & 0x00200000U) >> 19U) | ((data & 0x00400000U) >> 21U) | ((data & 0x00800000U) >> 23U);
}

static inline uint32_t reverse_bits32(const uint32_t data)
{
	if (!BMD_CONSTANT_P(data)) {
#if defined(__arm__) || defined(__aarch64__)
		uint32_t result;
		__asm__("rbit %0, %1" : "=r"(result) : "r"(data));
		return result;
#endif
	}
	// We don't have an optimized routine for this, so just use the fallback
	return ((data & 0x00000001U) << 31U) | ((data & 0x00000002U) << 29U) | ((data & 0x00000004U) << 27U) |
		((data & 0x00000008U) << 25U) | ((data & 0x00000010U) << 23U) | ((data & 0x00000020U) << 21U) |
		((data & 0x00000040U) << 19U) | ((data & 0x00000080U) << 17U) | ((data & 0x00000100U) << 15U) |
		((data & 0x00000200U) << 13U) | ((data & 0x00000400U) << 11U) | ((data & 0x00000800U) << 9U) |
		((data & 0x00001000U) << 7U) | ((data & 0x00002000U) << 5U) | ((data & 0x00004000U) << 3U) |
		((data & 0x00008000U) << 1U) | ((data & 0x00010000U) >> 1U) | ((data & 0x00020000U) >> 3U) |
		((data & 0x00040000U) >> 5U) | ((data & 0x00080000U) >> 7U) | ((data & 0x00100000U) >> 9U) |
		((data & 0x00200000U) >> 11U) | ((data & 0x00400000U) >> 13U) | ((data & 0x00800000U) >> 15U) |
		((data & 0x01000000U) >> 17U) | ((data & 0x02000000U) >> 19U) | ((data & 0x04000000U) >> 21U) |
		((data & 0x08000000U) >> 23U) | ((data & 0x10000000U) >> 25U) | ((data & 0x20000000U) >> 27U) |
		((data & 0x40000000U) >> 29U) | ((data & 0x80000000U) >> 31U);
}

#endif /*INCLUDE_BUFFER_UTILS_H*/
