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

/* Exception handling to escape deep nesting.
 * Used for the case of communicaiton failure and timeouts.
 */

/* Example usage:
 *
 * volatile struct exception e;
 * TRY_CATCH (e, EXCEPTION_TIMEOUT) {
 *    ...
 *    raise_exception(EXCEPTION_TIMEOUT, "Timeout occurred");
 *    ...
 * }
 * if (e.type == EXCEPTION_TIMEOUT) {
 *    printf("timeout: %s\n", e.msg);
 * }
 */

/* Limitations:
 * Can't use break, return, goto, etc from inside the TRY_CATCH block.
 */

#ifndef __EXCEPTION_H
#define __EXCEPTION_H

#include <setjmp.h>
#include <stdint.h>

#define EXCEPTION_ERROR   0x01
#define EXCEPTION_TIMEOUT 0x02
#define EXCEPTION_ALL     -1

struct exception {
	uint32_t type;
	const char *msg;
	/* private */
	uint32_t mask;
	jmp_buf jmpbuf;
	struct exception *outer;
};

extern struct exception *innermost_exception;

#define TRY_CATCH(e, type_mask) \
	(e).type = 0; \
	(e).mask = (type_mask); \
	(e).outer = innermost_exception; \
	innermost_exception = (void*)&(e); \
	if (setjmp(innermost_exception->jmpbuf) == 0) \
		for (;innermost_exception == &(e); innermost_exception = (e).outer)

void raise_exception(uint32_t type, const char *msg);

#endif

