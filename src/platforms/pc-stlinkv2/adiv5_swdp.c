/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2019 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

/* This file implements the SW-DP specific functions of the
 * ARM Debug Interface v5 Architecure Specification, ARM doc IHI0031A.
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "adiv5.h"
#include "stlinkv2.h"

int adiv5_swdp_scan(void)
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
