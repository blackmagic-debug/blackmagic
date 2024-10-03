/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011 Black Sphere Technologies Ltd.
 * Copyright (C) 2020 Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
 * Copyright (C) 2022-2024 1BitSquared <info@1bitsquared.com>
 *
 * Originally written by Gareth McMullin <gareth@blacksphere.co.nz> and others.
 * Modified by Uwe Bonnes <bon@elektron.ikp.physik.tu-darmstadt.de>
 * Modified by Rachel Mant <git@dragonmux.network>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* This file defines various utility routines for BMDA */

#include "general.h"

#include <string.h>
#include <stdio.h>

#include "timeofday.h"
#include "timing.h"
#include "bmp_hosted.h"

void platform_delay(uint32_t ms)
{
#if defined(_WIN32) && !defined(__MINGW32__)
	Sleep(ms);
#else
	usleep(ms * 1000U);
#endif
}

uint32_t platform_time_ms(void)
{
	timeval_s tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000U) + (tv.tv_usec / 1000U);
}

char *format_string(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	const int len = vsnprintf(NULL, 0, format, args);
	va_end(args);
	if (len <= 0)
		return NULL;
	char *const ret = (char *)malloc(len + 1);
	if (!ret)
		return NULL;
	va_start(args, format);
	vsprintf(ret, format, args);
	va_end(args);
	return ret;
}

bool begins_with(const char *const str, const size_t str_length, const char *const value)
{
	const size_t value_length = strlen(value);
	if (str_length < value_length)
		return false;
	return memcmp(str, value, value_length) == 0;
}

bool ends_with(const char *const str, const size_t str_length, const char *const value)
{
	const size_t value_length = strlen(value);
	if (str_length < value_length)
		return false;
	const size_t offset = str_length - value_length;
	return memcmp(str + offset, value, value_length) == 0;
}

bool contains_substring(const char *const str, const size_t str_len, const char *const search)
{
	const size_t search_len = strlen(search);
	if (str_len < search_len)
		return false;
	/* For each possible valid offset */
	for (size_t offset = 0; offset <= str_len - search_len; ++offset) {
		/* If we have a match, we're done */
		if (memcmp(str + offset, search, search_len) == 0)
			return true;
	}
	/* We failed to find a match */
	return false;
}
