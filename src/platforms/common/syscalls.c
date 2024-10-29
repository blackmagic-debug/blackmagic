/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2024 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
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
#include "platform.h"
#include "usb_serial.h"
#include <sys/stat.h>
#include <string.h>

typedef struct stat stat_s;

#include <errno.h>

extern uint8_t heap_start;
extern uint8_t heap_end;
static uint8_t *heap_current = &heap_start;

/*
 * newlib defines _write as a weak link'd function for user code to override.
 *
 * This function forms the root of the implementation of a variety of functions
 * that can write to stdout/stderr, including printf().
 *
 * The result of this function is the number of bytes written.
 */
/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _write(const int file, const void *const ptr, const size_t len)
{
	(void)file;
#ifdef PLATFORM_HAS_DEBUG
	if (debug_bmp)
		return debug_serial_debug_write(ptr, len);
#else
	(void)ptr;
#endif
	return len;
}

/* This defines stubs for the newlib fake file IO layer for compatibility with GCC 12 `-specs=nosys.specs` */

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _read(const int file, void *const buffer, const size_t length)
{
	(void)file;
	(void)buffer;
	return length;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) off_t _lseek(const int file, const off_t offset, const int direction)
{
	(void)file;
	(void)offset;
	(void)direction;
	return 0;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _fstat(const int file, stat_s *stats)
{
	(void)file;
	memset(stats, 0, sizeof(*stats));
	return 0;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _isatty(const int file)
{
	(void)file;
	return true;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _close(const int file)
{
	(void)file;
	return 0;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) pid_t _getpid(void)
{
	return 1;
}

/* NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) */
__attribute__((used)) int _kill(const int pid, const int signal)
{
	(void)pid;
	(void)signal;
	return 0;
}

__attribute__((used)) void *_sbrk(const ptrdiff_t alloc_size)
{
	/* Check if this allocation would exhaust the heap */
	if (heap_current + alloc_size > &heap_end) {
		errno = ENOMEM;
		return (void *)-1;
	}

	/*
	 * Everything is ok, so make a copy of the heap pointer to return then add the
	 * allocation to the heap pointer
	 */
	void *const result = heap_current;
	heap_current += alloc_size;
	return result;
}

/* ARM EABI Personality functions for newlib-4.3.0 */
__attribute__((weak)) void __aeabi_unwind_cpp_pr0()
{
}

__attribute__((weak)) void __aeabi_unwind_cpp_pr1()
{
}

__attribute__((weak)) void __aeabi_unwind_cpp_pr2()
{
}
