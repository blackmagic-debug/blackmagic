/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2016  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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

#include "general.h"

void platform_timeout_set(platform_timeout_s *const t, uint32_t ms)
{
	if (ms < SYSTICKMS)
		ms = SYSTICKMS;
	t->time = platform_time_ms() + ms;
}

bool platform_timeout_is_expired(const platform_timeout_s *const t)
{
	/* Cache the current time for the whole calculation */
	const uint32_t counter = platform_time_ms();
	/*
	 * Check for the tricky overflow condition and handle that properly -
	 * when time_ms approaches UINT32_MAX and we try to set a timeout that
	 * overflows to a low t->time value, if we simply compare with `<`, we will
	 * erroneously consider the timeout expired for a few ms right at the start of
	 * the valid interval. Instead, force that region of time to be considered
	 * not expired by checking the MSb's of the two values and handling that specially.
	 */
	if ((counter & UINT32_C(0x80000000)) && !(t->time & UINT32_C(0x80000000)))
		return false;
	return counter > t->time;
}
