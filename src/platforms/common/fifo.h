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

#ifndef PLATFORMS_COMMON_FIFO_H
#define PLATFORMS_COMMON_FIFO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define FIFO_STATIC(name, fifo_size) \
	uint8_t name##_data[(fifo_size)];  \
	fifo_t name = {.buffer = name##_data, .head = 0, .tail = 0, .size = (fifo_size), .is_full = 0}

typedef struct fifo_s {
	uint8_t *buffer;
	size_t head;
	size_t tail;
	const size_t size;
	bool is_full;
} fifo_t;

bool fifo_push(fifo_t *f, uint8_t byte);
uint8_t fifo_pop(fifo_t *f);
size_t fifo_write(fifo_t *f, const uint8_t *data, size_t size);
size_t fifo_read(fifo_t *f, uint8_t *data, size_t size);
uint8_t *fifo_get_pointer(fifo_t *f, size_t size);
size_t fifo_discard(fifo_t *f, size_t del);
void fifo_reset(fifo_t *f);

static inline size_t fifo_get_used(fifo_t *f)
{
	if (f->is_full)
		return f->size;
	if (f->tail > f->head)
		return f->size + f->head - f->tail;
	return f->head - f->tail;
}

static inline size_t fifo_get_free(fifo_t *f)
{
	return f->size - fifo_get_used(f);
}

#endif /* PLATFORMS_COMMON_FIFO_H */
