/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

/* Find all known serial connected debuggers */

#include "general.h"
#include <stdint.h>
#include "platform.h"
#include "bmp_hosted.h"
#include "version.h"

void bmp_ident(bmp_info_t *info)
{
	(void) info;
	DEBUG_INFO("BMP hosted %s\n", FIRMWARE_VERSION);
}

void libusb_exit_function(bmp_info_t *info) {(void)info;};
int find_debuggers(BMP_CL_OPTIONS_t *cl_opts, bmp_info_t *info)
{
	(void)cl_opts;
	(void)info;
	return -1;
};
