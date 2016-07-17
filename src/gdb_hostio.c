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

int gdb_main_loop(struct target_controller *, bool in_syscall);

int hostio_reply(struct target_controller *tc, char *pbuf, int len)
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

/* Interface to host system calls */
int hostio_open(struct target_controller *tc,
	        target_addr path, size_t path_len,
                enum target_open_flags flags, mode_t mode)
{
	gdb_putpacket_f("Fopen,%08X/%X,%08X,%08X", path, path_len, flags, mode);;;;
	return gdb_main_loop(tc, true);
}

int hostio_close(struct target_controller *tc, int fd)
{
	gdb_putpacket_f("Fclose,%08X", fd);
	return gdb_main_loop(tc, true);
}

int hostio_read(struct target_controller *tc,
	         int fd, target_addr buf, unsigned int count)
{
	gdb_putpacket_f("Fread,%08X,%08X,%08X", fd, buf, count);
	return gdb_main_loop(tc, true);
}

int hostio_write(struct target_controller *tc,
	          int fd, target_addr buf, unsigned int count)
{
	gdb_putpacket_f("Fwrite,%08X,%08X,%08X", fd, buf, count);
	return gdb_main_loop(tc, true);
}

long hostio_lseek(struct target_controller *tc,
	           int fd, long offset, enum target_seek_flag flag)
{
	gdb_putpacket_f("Flseek,%08X,%08X,%08X", fd, offset, flag);
	return gdb_main_loop(tc, true);
}

int hostio_rename(struct target_controller *tc,
	           target_addr oldpath, size_t old_len,
	           target_addr newpath, size_t new_len)
{
	gdb_putpacket_f("Frename,%08X/%X,%08X/%X",
	                oldpath, old_len, newpath, new_len);
	return gdb_main_loop(tc, true);
}

int hostio_unlink(struct target_controller *tc,
	           target_addr path, size_t path_len)
{
	gdb_putpacket_f("Funlink,%08X/%X", path, path_len);
	return gdb_main_loop(tc, true);
}

int hostio_stat(struct target_controller *tc,
	         target_addr path, size_t path_len, target_addr buf)
{
	gdb_putpacket_f("Fstat,%08X/%X,%08X", path, path_len, buf);
	return gdb_main_loop(tc, true);
}

int hostio_fstat(struct target_controller *tc, int fd, target_addr buf)
{
	gdb_putpacket_f("Ffstat,%X,%08X", fd, buf);
	return gdb_main_loop(tc, true);
}

int hostio_gettimeofday(struct target_controller *tc,
		         target_addr tv, target_addr tz)
{
	gdb_putpacket_f("Fgettimeofday,%08X,%08X", tv, tz);
	return gdb_main_loop(tc, true);
}

int hostio_isatty(struct target_controller *tc, int fd)
{
	gdb_putpacket_f("Fisatty,%08X", fd);
	return gdb_main_loop(tc, true);
}

int hostio_system(struct target_controller *tc,
	          target_addr cmd, size_t cmd_len)
{
	gdb_putpacket_f("Fsystem,%08X/%X", cmd, cmd_len);
	return gdb_main_loop(tc, true);
}

