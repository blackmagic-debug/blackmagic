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
 * Mathematical helper functions
 */

#include "maths_utils.h"

#ifdef __GNUC__
#define unlikely(x) __builtin_expect(x, 0)
#else
#define unlikely(x) x
#endif

uint8_t ulog2(uint32_t value)
{
	if (unlikely(!value))
		return UINT8_MAX;
#if defined(__GNUC__)
	return (uint8_t)((sizeof(uint32_t) * 8U) - (uint8_t)__builtin_clz(value));
#elif defined(_MSC_VER)
	return (uint8_t)((sizeof(uint32_t) * 8U) - (uint8_t)__lzcnt(value));
#else
	uint8_t result = 0U;
	if (value <= UINT32_C(0x0000ffff)) {
		result += 16;
		value <<= 16U;
	}
	if (value <= UINT32_C(0x00ffffff)) {
		result += 8;
		value <<= 8U;
	}
	if (value <= UINT32_C(0x0fffffff)) {
		result += 4;
		value <<= 4U;
	}
	if (value <= UINT32_C(0x3fffffff)) {
		result += 2;
		value <<= 2U;
	}
	if (value <= UINT32_C(0x7fffffff))
		++result;
	return (sizeof(uint8_t) * 8U) - result;
#endif
}
