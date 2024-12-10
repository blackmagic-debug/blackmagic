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

#ifndef INCLUDE_GDB_MAIN_H
#define INCLUDE_GDB_MAIN_H

#include "target.h"
#include "gdb_packet.h"

extern bool gdb_target_running;
extern target_s *cur_target;

void gdb_poll_target(void);
void gdb_main(const gdb_packet_s *packet);
int32_t gdb_main_loop(target_controller_s *tc, const gdb_packet_s *packet, bool in_syscall);

#endif /* INCLUDE_GDB_MAIN_H */
