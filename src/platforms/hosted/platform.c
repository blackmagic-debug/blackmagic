/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019
 * Written by Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

/* Handle different BMP pc-hosted platforms/
 */

#include "general.h"
#include "swdptap.h"
#include "jtagtap.h"

typedef enum bmp_t{
	BMP_TYPE_NONE = 0
}bmp_t;

bmp_t active_bmp = BMP_TYPE_NONE;
swd_proc_t swd_proc;
jtag_proc_t jtag_proc;

void platform_init(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	exit(-1);
}

int platform_adiv5_swdp_scan(void)
{
	return -1;
}

int platform_swdptap_init(void)
{
	return -1;
}

int platform_jtag_scan(const uint8_t *lrlens)
{
	(void) lrlens;
	return -1;
}

int platform_jtagtap_init(void)
{
	return 0;
}

int platform_adiv5_dp_defaults(void *arg)
{
	(void)arg;
	return -1;
}

int platform_jtag_dp_init()
{
        return 0;
}

char *platform_ident(void)
{
	switch (active_bmp) {
	  case BMP_TYPE_NONE:
		return "NONE";
	}
	return NULL;
}

const char *platform_target_voltage(void)
{
	return NULL;
}

void platform_srst_set_val(bool assert) {(void) assert;}

bool platform_srst_get_val(void) { return false;}
void platform_buffer_flush(void) {}
