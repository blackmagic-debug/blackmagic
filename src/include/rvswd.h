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

#ifndef INCLUDE_RVSWD_H
#define INCLUDE_RVSWD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Functions interface talking RVSWD */
typedef struct rvswd_proc {
	/* Perform a clock_cycles read */
	uint32_t (*seq_in)(size_t clock_cycles);
	/* Perform a clock_cycles read + parity */
	bool (*seq_in_parity)(uint32_t *ret, size_t clock_cycles);
	/* Perform a clock_cycles write with the provided data */
	void (*seq_out)(uint32_t tms_states, size_t clock_cycles);
	/* Perform a clock_cycles write + parity with the provided data */
	void (*seq_out_parity)(uint32_t tms_states, size_t clock_cycles);
} rvswd_proc_s;

extern rvswd_proc_s rvswd_proc;

void rvswd_init(void);

#endif /* INCLUDE_RVSWD_H */
