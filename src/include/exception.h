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

/*
 * Exception handling to escape deep nesting.
 * Used for the case of communication failure and timeouts.
 *
 * Example usage:
 *
 * TRY (EXCEPTION_TIMEOUT) {
 *    ...
 *    raise_exception(EXCEPTION_TIMEOUT, "Timeout occurred");
 *    ...
 * }
 * CATCH () {
 *    case EXCEPTION_TIMEOUT:
 *        printf("timeout: %s\n", exception_frame.msg);
 * }
 *
 * Limitations:
 * Can't use break, or goto, from inside the TRY block.
 */

#ifndef INCLUDE_EXCEPTION_H
#define INCLUDE_EXCEPTION_H

#include <setjmp.h>
#include <stdint.h>

#define EXCEPTION_ERROR   0x01U
#define EXCEPTION_TIMEOUT 0x02U
#define EXCEPTION_ALL     UINT32_MAX

typedef struct exception exception_s;

struct exception {
	uint32_t type;
	const char *msg;
	/* private */
	uint32_t mask;
	jmp_buf jmpbuf;
	exception_s *outer;
};

extern exception_s *innermost_exception;

#define TRY(type_mask)                           \
	exception_s exception_frame;                 \
	exception_frame.type = 0U;                   \
	exception_frame.mask = (type_mask);          \
	exception_frame.outer = innermost_exception; \
	innermost_exception = &exception_frame;      \
	if (setjmp(exception_frame.jmpbuf) == 0)

#define CATCH()                                  \
	innermost_exception = exception_frame.outer; \
	if (exception_frame.type)                    \
		switch (exception_frame.type)

#define RETHROW              \
	if (innermost_exception) \
	raise_exception(exception_frame->type, exception_frame->msg)

void raise_exception(uint32_t type, const char *msg);

#endif /* INCLUDE_EXCEPTION_H */
