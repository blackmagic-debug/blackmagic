/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
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

/* Provides main entry point.  Initialise subsystems and enter GDB
 * protocol loop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "gdb_if.h"
#include "gdb_main.h"
#include "jtagtap.h"
#include "jtag_scan.h"

#include "target.h"

int
main(int argc, char **argv)
{
#if defined(LIBFTDI)
	assert(platform_init(argc, argv) == 0);
#else
	(void) argc;
	(void) argv;
	assert(platform_init() == 0);
#endif
	PLATFORM_SET_FATAL_ERROR_RECOVERY();

	gdb_main();

	/* Should never get here */
	return 0;
}

