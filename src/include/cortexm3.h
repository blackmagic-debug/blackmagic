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

#ifndef __CORTEXM3_H
#define __CORTEXM3_H

#include "target.h"

/* target options recognised by the Cortex-M target */
#define	TOPT_FLAVOUR_V6M	(1<<0)		/* if not set, target is assumed to be v7m */
#define	TOPT_FLAVOUR_V7MF	(1<<1)		/* if set, floating-point enabled. */

int cm3_probe(struct target_s *target);

#endif

