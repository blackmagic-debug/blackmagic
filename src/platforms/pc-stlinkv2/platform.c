/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019 2019 Uwe Bonnes
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	 If not, see <http://www.gnu.org/licenses/>.
 */
#include "general.h"
#include "gdb_if.h"
#include "version.h"
#include "platform.h"

#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

#include "adiv5.h"
#include "stlinkv2.h"

int platform_hwversion(void)
{
	return stlink_hwversion();
}

const char *platform_target_voltage(void)
{
	return stlink_target_voltage();
}

void platform_init(int argc, char **argv)
{
	stlink_init(argc, argv);
}

static bool srst_status = false;
void platform_srst_set_val(bool assert)
{
	stlink_srst_set_val(assert);
	srst_status = assert;
}

bool platform_srst_get_val(void) { return srst_status; }

void platform_buffer_flush(void)
{
}

int platform_buffer_write(const uint8_t *data, int size)
{
	(void) data;
	(void) size;
	return size;
}

int platform_buffer_read(uint8_t *data, int size)
{
	(void) data;
	return size;
}

#if defined(_WIN32) && !defined(__MINGW32__)
#warning "This vasprintf() is dubious!"
int vasprintf(char **strp, const char *fmt, va_list ap)
{
	int size = 128, ret = 0;

	*strp = malloc(size);
	while(*strp && ((ret = vsnprintf(*strp, size, fmt, ap)) == size))
		*strp = realloc(*strp, size <<= 1);

	return ret;
}
#endif

void platform_delay(uint32_t ms)
{
	usleep(ms * 1000);
}

uint32_t platform_time_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}
