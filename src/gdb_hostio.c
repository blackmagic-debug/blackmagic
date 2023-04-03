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

#include "general.h"
#include "target.h"
#include "gdb_main.h"
#include "gdb_hostio.h"
#include "gdb_packet.h"

int hostio_reply(target_controller_s *tc, char *pbuf, int len)
{
	(void)len;
	int retcode, items, errno_;
	char c, *p;
	if (pbuf[1] == '-')
		p = &pbuf[2];
	else
		p = &pbuf[1];
	items = sscanf(p, "%x,%x,%c", &retcode, &errno_, &c);
	if (pbuf[1] == '-')
		retcode = -retcode;

	/* if break is requested */
	tc->interrupted = items == 3 && c == 'C';
	tc->errno_ = errno_;

	return retcode;
}

static int hostio_get_response(target_controller_s *const tc)
{
	char *const packet_buffer = gdb_packet_buffer();
	const size_t size = gdb_getpacket(packet_buffer, GDB_PACKET_BUFFER_SIZE);
	return gdb_main_loop(tc, packet_buffer, GDB_PACKET_BUFFER_SIZE, size, true);
}

/* Interface to host system calls */
int hostio_open(target_controller_s *tc, target_addr_t path, size_t path_len, target_open_flags_e flags, mode_t mode)
{
	gdb_putpacket_f("Fopen,%08X/%X,%08X,%08X", path, path_len, flags, mode);
	return hostio_get_response(tc);
}

int hostio_close(target_controller_s *tc, int fd)
{
	gdb_putpacket_f("Fclose,%08X", fd);
	return hostio_get_response(tc);
}

int hostio_read(target_controller_s *tc, int fd, target_addr_t buf, unsigned int count)
{
	gdb_putpacket_f("Fread,%08X,%08X,%08X", fd, buf, count);
	return hostio_get_response(tc);
}

int hostio_write(target_controller_s *tc, int fd, target_addr_t buf, unsigned int count)
{
	gdb_putpacket_f("Fwrite,%08X,%08X,%08X", fd, buf, count);
	return hostio_get_response(tc);
}

long hostio_lseek(target_controller_s *tc, int fd, long offset, target_seek_flag_e flag)
{
	gdb_putpacket_f("Flseek,%08X,%08X,%08X", fd, offset, flag);
	return hostio_get_response(tc);
}

int hostio_rename(target_controller_s *tc, target_addr_t oldpath, size_t old_len, target_addr_t newpath, size_t new_len)
{
	gdb_putpacket_f("Frename,%08X/%X,%08X/%X", oldpath, old_len, newpath, new_len);
	return hostio_get_response(tc);
}

int hostio_unlink(target_controller_s *tc, target_addr_t path, size_t path_len)
{
	gdb_putpacket_f("Funlink,%08X/%X", path, path_len);
	return hostio_get_response(tc);
}

int hostio_stat(target_controller_s *tc, target_addr_t path, size_t path_len, target_addr_t buf)
{
	gdb_putpacket_f("Fstat,%08X/%X,%08X", path, path_len, buf);
	return hostio_get_response(tc);
}

int hostio_fstat(target_controller_s *tc, int fd, target_addr_t buf)
{
	gdb_putpacket_f("Ffstat,%X,%08X", fd, buf);
	return hostio_get_response(tc);
}

int hostio_gettimeofday(target_controller_s *tc, target_addr_t tv, target_addr_t tz)
{
	gdb_putpacket_f("Fgettimeofday,%08X,%08X", tv, tz);
	return hostio_get_response(tc);
}

int hostio_isatty(target_controller_s *tc, int fd)
{
	gdb_putpacket_f("Fisatty,%08X", fd);
	return hostio_get_response(tc);
}

int hostio_system(target_controller_s *tc, target_addr_t cmd, size_t cmd_len)
{
	gdb_putpacket_f("Fsystem,%08X/%X", cmd, cmd_len);
	return hostio_get_response(tc);
}
