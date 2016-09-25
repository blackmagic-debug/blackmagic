/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2015  Black Sphere Technologies Ltd.
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
#include "exception.h"

struct exception *innermost_exception;

void raise_exception(uint32_t type, const char *msg)
{
	struct exception *e;
	DEBUG("Exception: %s\n", msg);
	for (e = innermost_exception; e; e = e->outer) {
		if (e->mask & type) {
			e->type = type;
			e->msg = msg;
			innermost_exception = e->outer;
			longjmp(e->jmpbuf, type);
		}
	}
	abort();
}

