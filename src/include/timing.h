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

#ifndef INCLUDE_TIMING_H
#define INCLUDE_TIMING_H

#include <stdint.h>

#if !defined(SYSTICKHZ)
#define SYSTICKHZ 1000U
#endif

#define SYSTICKMS (1000U / SYSTICKHZ)
#define MORSECNT  ((SYSTICKHZ / 10U) - 1U)

struct platform_timeout {
	uint32_t time;
};

extern uint32_t target_clk_divider;
uint32_t platform_time_ms(void);

#endif /* INCLUDE_TIMING_H */
