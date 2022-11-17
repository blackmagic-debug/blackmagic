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

int hostio_reply(target_controller_s *tc, char *packet, int len);

/* Interface to host system calls */
int hostio_open(target_controller_s *, target_addr_t path, size_t path_len, target_open_flags_e flags, mode_t mode);
int hostio_close(target_controller_s *, int fd);
int hostio_read(target_controller_s *, int fd, target_addr_t buf, unsigned int count);
int hostio_write(target_controller_s *, int fd, target_addr_t buf, unsigned int count);
long hostio_lseek(target_controller_s *, int fd, long offset, target_seek_flag_e flag);
int hostio_rename(target_controller_s *, target_addr_t oldpath, size_t old_len, target_addr_t newpath, size_t new_len);
int hostio_unlink(target_controller_s *, target_addr_t path, size_t path_len);
int hostio_stat(target_controller_s *, target_addr_t path, size_t path_len, target_addr_t buf);
int hostio_fstat(target_controller_s *, int fd, target_addr_t buf);
int hostio_gettimeofday(target_controller_s *, target_addr_t tv, target_addr_t tz);
int hostio_isatty(target_controller_s *, int fd);
int hostio_system(target_controller_s *, target_addr_t cmd, size_t cmd_len);

#endif /* GDB_HOSTIO_H */
