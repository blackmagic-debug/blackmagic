/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Written by L. E. Segovia <amy@amyspark.me>
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

#ifndef INCLUDE_TIMEOFDAY_H
#define INCLUDE_TIMEOFDAY_H

#if !defined(_MSC_VER)
#include <unistd.h>
#include <sys/time.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <winsock.h>

static inline int gettimeofday(PTIMEVAL current_time, void *ignoreme)
{
	(void)ignoreme;

	if (current_time == NULL)
		return -1;

	LARGE_INTEGER frequency;
	LARGE_INTEGER counter;

	memset(&frequency, 0, sizeof(LARGE_INTEGER));
	memset(&counter, 0, sizeof(LARGE_INTEGER));

	if (QueryPerformanceFrequency(&frequency) == FALSE)
		return -1;
	if (QueryPerformanceCounter(&counter) == FALSE)
		return -1;

	current_time->tv_sec = counter.QuadPart / frequency.QuadPart;
	current_time->tv_usec =
		(((counter.QuadPart % frequency.QuadPart) * 1000000000 + (frequency.QuadPart >> 1)) / frequency.QuadPart) /
		1000;
	if (current_time->tv_usec >= 1000000) {
		current_time->tv_sec++;
		current_time->tv_usec -= 1000000;
	}

	return 0;
}
#endif // _MSC_VER
#endif // INCLUDE_TIMEOFDAY_H
