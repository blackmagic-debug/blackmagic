/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019  Uwe Bonnes(bon@elektron.ikp.physik.tu-darmstadt.de)
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

/* This file implements a subset of JTAG-DP specific functions of the
 * ARM Debug Interface v5 Architecure Specification, ARM doc IHI0031A
 * used in BMP.
 */

#include "general.h"
#include "target.h"
#include "adiv5.h"
#include "stlinkv2.h"
#include "jtag_devs.h"

struct jtag_dev_s jtag_devs[JTAG_MAX_DEVS+1];
int jtag_dev_count;

int jtag_scan_stlinkv2(const uint8_t *irlens)
{
	uint32_t idcodes[JTAG_MAX_DEVS+1];
	(void) *irlens;
	target_list_free();

	jtag_dev_count = 0;
	memset(&jtag_devs, 0, sizeof(jtag_devs));
	if (stlink_enter_debug_jtag())
		return 0;
	jtag_dev_count = stlink_read_idcodes(idcodes);
	/* Check for known devices and handle accordingly */
	for(int i = 0; i < jtag_dev_count; i++)
		jtag_devs[i].idcode = idcodes[i];
	for(int i = 0; i < jtag_dev_count; i++)
		for(int j = 0; dev_descr[j].idcode; j++)
			if((jtag_devs[i].idcode & dev_descr[j].idmask) ==
			   dev_descr[j].idcode) {
				if(dev_descr[j].handler)
					dev_descr[j].handler(&jtag_devs[i]);
				break;
			}

	return jtag_dev_count;
}

void adiv5_jtag_dp_handler(jtag_dev_t *dev)
{
	ADIv5_DP_t *dp = (void*)calloc(1, sizeof(*dp));

	dp->dev = dev;
	dp->idcode = dev->idcode;

	dp->dp_read = stlink_dp_read;
	dp->error = stlink_dp_error;
	dp->low_access = stlink_dp_low_access;
	dp->abort = stlink_dp_abort;

	adiv5_dp_init(dp);
}
