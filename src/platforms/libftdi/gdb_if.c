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

/* This file implements a transparent channel over which the GDB Remote
 * Serial Debugging protocol is implemented.  This implementation for Linux
 * uses a TCP server on port 2000.
 */
#include <stdio.h>

#ifndef WIN32
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <sys/select.h>
#else
#   include <winsock2.h>
#   include <windows.h>
#   include <ws2tcpip.h>
#endif

#include <assert.h>

#include "general.h"
#include "gdb_if.h"

static int gdb_if_serv, gdb_if_conn;

int gdb_if_init(void)
{
#ifdef WIN32
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
	struct sockaddr_in addr;
	int opt;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(2000);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	assert((gdb_if_serv = socket(PF_INET, SOCK_STREAM, 0)) != -1);
	opt = 1;
	assert(setsockopt(gdb_if_serv, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt)) != -1);
	assert(setsockopt(gdb_if_serv, IPPROTO_TCP, TCP_NODELAY, (void*)&opt, sizeof(opt)) != -1);

	assert(bind(gdb_if_serv, (void*)&addr, sizeof(addr)) != -1);
	assert(listen(gdb_if_serv, 1) != -1);

	DEBUG("Listening on TCP:2000\n");

	return 0;
}


unsigned char gdb_if_getchar(void)
{
	unsigned char ret;
	int i = 0;

	while(i <= 0) {
		if(gdb_if_conn <= 0) {
			gdb_if_conn = accept(gdb_if_serv, NULL, NULL);
			DEBUG("Got connection\n");
		}
		i = recv(gdb_if_conn, (void*)&ret, 1, 0);
		if(i <= 0) {
			gdb_if_conn = -1;
			DEBUG("Dropped broken connection\n");
			/* Return '+' in case we were waiting for an ACK */
			return '+';
		}
	}
	return ret;
}

unsigned char gdb_if_getchar_to(int timeout)
{
	fd_set fds;
	struct timeval tv;

	if(gdb_if_conn == -1) return -1;

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;

	FD_ZERO(&fds);
	FD_SET(gdb_if_conn, &fds);

	if(select(gdb_if_conn+1, &fds, NULL, NULL, &tv) > 0)
		return gdb_if_getchar();

	return -1;
}

void gdb_if_putchar(unsigned char c, int flush)
{
	static uint8_t buf[2048];
	static int bufsize = 0;
	if (gdb_if_conn > 0) {
		buf[bufsize++] = c;
		if (flush || (bufsize == sizeof(buf))) {
			send(gdb_if_conn, buf, bufsize, 0);
			bufsize = 0;
		}
	}
}
