/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019  Black Sphere Technologies Ltd.
 * Written by Manuel Bleichenbacher <manuel.bleichenbacher@gmail.com>
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

/*
 * This file implements the platform agnostic functions for handling the SRST.
 */

#include "general.h"
#include "platform_support.h"

void platform_srst_reset()
{
    platform_srst_assert();
    platform_srst_release();
}

void platform_srst_assert(void)
{
    platform_srst_set_val(true);
    /* Hold reset for 1ms */
    platform_delay(1);
}

void platform_srst_release(void)
{
    platform_srst_set_val(false);

	/* Wait for SRST to go high
	   but no longer than 200ms */
	uint32_t start = platform_time_ms();
	platform_timeout timeout;
	platform_timeout_set(&timeout, 200);
	while (platform_srst_get_val() && !platform_timeout_is_expired(&timeout));

	if (platform_timeout_is_expired(&timeout)) {
        DEBUG("Timeout waiting for SRST to be released\n");
	} else {
        /* The high/low thresholds of the BMP and the target might differ and
            the target will need some time to become responsive after reset.
            So wait the same time again. Wait time is increased by 1ms as
            a delay of up to 999us can be reported as 0 due to the timer
            resolution. */
		platform_delay(platform_time_ms() - start + 1);
	}
}
