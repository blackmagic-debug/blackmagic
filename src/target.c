/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2012  Black Sphere Technologies Ltd.
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

#include "general.h"
#include "target.h"

#include <stdlib.h>

target *target_list = NULL;
target *cur_target = NULL;
target *last_target = NULL;

target *target_new(unsigned size)
{
	target *t = (void*)calloc(1, size);
	t->next = target_list;
	target_list = t;

	return t;
}

void target_list_free(void)
{
	while(target_list) {
		target *t = target_list->next;
		free(target_list);
		target_list = t;
	}
	last_target = cur_target = NULL;
}

