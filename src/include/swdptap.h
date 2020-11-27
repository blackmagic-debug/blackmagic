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

#ifndef __SWDPTAP_H
#define __SWDPTAP_H
typedef struct swd_proc_s {
	uint32_t (*swdptap_seq_in)(int ticks);
	bool (*swdptap_seq_in_parity)(uint32_t *data, int ticks);
	void (*swdptap_seq_out)(uint32_t MS, int ticks);
	void (*swdptap_seq_out_parity)(uint32_t MS, int ticks);
} swd_proc_t;
extern swd_proc_t swd_proc;

# if PC_HOSTED == 1
int platform_swdptap_init(void);
# else
int swdptap_init(void);
# endif
#endif
