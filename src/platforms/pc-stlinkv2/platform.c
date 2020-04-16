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
#include "target.h"
#include "target_internal.h"
#include "swdptap.h"

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

int platform_swdptap_init(void)
{
	return 0;
}

swd_proc_t swd_proc;

static int adiv5_swdp_scan_stlinkv2(void)
{
	target_list_free();
	ADIv5_DP_t *dp = (void*)calloc(1, sizeof(*dp));
	if (stlink_enter_debug_swd())
		return 0;
	dp->idcode = stlink_read_coreid();
	dp->dp_read = stlink_dp_read;
	dp->error = stlink_dp_error;
	dp->low_access = stlink_dp_low_access;
	dp->abort = stlink_dp_abort;

	stlink_dp_error(dp);
	adiv5_dp_init(dp);

	return target_list?1:0;
	return 0;
}
int platform_adiv5_swdp_scan(void)
{
	return adiv5_swdp_scan_stlinkv2();
}

int platform_jtag_scan(const uint8_t *lrlens)
{
	return jtag_scan_stlinkv2(lrlens);
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
