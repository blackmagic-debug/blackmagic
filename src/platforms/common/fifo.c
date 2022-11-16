/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022 1BitSquared <info@1bitsquared.com>
 * Written by anyn99 <blnkdnl@googlemail.com>
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

#include <stdlib.h>
#include <string.h>
#include "fifo.h"

bool fifo_push(fifo_t *const f, const uint8_t byte)
{
	if (f->is_full)
		return false;

	f->buffer[f->head++] = byte;
	f->head %= f->size;

	if (f->head == f->tail)
		f->is_full = true;

	return true;
}

uint8_t fifo_pop(fifo_t *const f)
{
	if (f->head == f->tail && !f->is_full)
		return 0;

	const uint8_t result = f->buffer[f->tail++];
	f->tail %= f->size;

	if (f->is_full)
		f->is_full = false;

	return result;
}

size_t fifo_write(fifo_t *const f, const uint8_t *data, size_t size)
{
	if (size > fifo_get_free(f))
		size = fifo_get_free(f);

	for (size_t idx = 0; idx < size; ++idx) {
		if (!fifo_push(f, data[idx]))
			return idx;
	}

	return size;
}

size_t fifo_read(fifo_t *const f, uint8_t *const data, size_t size)
{
	if (size > fifo_get_used(f))
		size = fifo_get_used(f);

	for (size_t idx = 0; idx < size; ++idx)
		data[idx] = fifo_pop(f);

	return size;
}

uint8_t *fifo_get_pointer(fifo_t *const f, const size_t size)
{
	size_t tailsize = f->size - f->tail;

	if ((size <= tailsize || f->tail <= f->head) && !f->is_full)
		return f->buffer + f->tail;

	uint8_t *tailstart = f->buffer + f->tail;

	uint8_t *overlap = NULL;
	size_t overlapsize = 0;

	if (tailsize + f->head > f->tail) // used size overlapping tail? save it!
	{
		overlapsize = tailsize + f->head - f->tail;
		overlap = malloc(overlapsize);
		memcpy(overlap, &f->buffer[f->tail], overlapsize);

		// % prevents tailstart pointing outside array
		tailstart = f->buffer + ((f->tail + overlapsize) % f->size);
		tailsize -= overlapsize;
	}

	memmove(f->buffer + overlapsize + tailsize, f->buffer, f->head);
	memcpy(f->buffer + overlapsize, tailstart, tailsize);

	if (overlap) {
		memcpy(f->buffer, overlap, overlapsize);
		free(overlap);
	}

	f->tail = 0;
	f->head = (overlapsize + tailsize + f->head) % f->size;

	return f->buffer;
}

size_t fifo_discard(fifo_t *const f, size_t amount)
{
	if (amount > fifo_get_used(f))
		amount = fifo_get_used(f);

	if (amount && f->is_full)
		f->is_full = false;
	f->tail = (f->tail + amount) % f->size;
	return amount;
}

void fifo_reset(fifo_t *const f)
{
	f->head = 0;
	f->tail = 0;
	f->is_full = false;
}
