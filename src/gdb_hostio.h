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

#ifndef GDB_HOSTIO_H
#define GDB_HOSTIO_H

#include "target.h"

int hostio_reply(struct target_controller *tc, char *packet, int len);

/* Interface to host system calls */
int hostio_open(
	struct target_controller *, target_addr_t path, size_t path_len, enum target_open_flags flags, mode_t mode);
int hostio_close(struct target_controller *, int fd);
int hostio_read(struct target_controller *, int fd, target_addr_t buf, unsigned int count);
int hostio_write(struct target_controller *, int fd, target_addr_t buf, unsigned int count);
long hostio_lseek(struct target_controller *, int fd, long offset, enum target_seek_flag flag);
int hostio_rename(
	struct target_controller *, target_addr_t oldpath, size_t old_len, target_addr_t newpath, size_t new_len);
int hostio_unlink(struct target_controller *, target_addr_t path, size_t path_len);
int hostio_stat(struct target_controller *, target_addr_t path, size_t path_len, target_addr_t buf);
int hostio_fstat(struct target_controller *, int fd, target_addr_t buf);
int hostio_gettimeofday(struct target_controller *, target_addr_t tv, target_addr_t tz);
int hostio_isatty(struct target_controller *, int fd);
int hostio_system(struct target_controller *, target_addr_t cmd, size_t cmd_len);

#endif /* GDB_HOSTIO_H */
