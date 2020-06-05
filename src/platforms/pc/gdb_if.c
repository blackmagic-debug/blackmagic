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

#if defined(_WIN32) || defined(__CYGWIN__)
#   define __USE_MINGW_ANSI_STDIO 1
#   include <winsock2.h>
#   include <windows.h>
#   include <ws2tcpip.h>
#else
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <netinet/tcp.h>
#   include <sys/select.h>
#   include <fcntl.h>
#endif

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "general.h"
#include "gdb_if.h"

static int gdb_if_serv, gdb_if_conn;
#define DEFAULT_PORT 2000
#define NUM_GDB_SERVER 4
int gdb_if_init(void)
{
#if defined(_WIN32) || defined(__CYGWIN__)
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
	struct sockaddr_in addr;
	int opt;
	int port = DEFAULT_PORT - 1;

	do {
		port ++;
		if (port > DEFAULT_PORT + NUM_GDB_SERVER)
			return - 1;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = htonl(INADDR_ANY);

		gdb_if_serv = socket(PF_INET, SOCK_STREAM, 0);
		if (gdb_if_serv == -1)
			continue;

		opt = 1;
		if (setsockopt(gdb_if_serv, SOL_SOCKET, SO_REUSEADDR, (void*)&opt, sizeof(opt)) == -1) {
			close(gdb_if_serv);
			continue;
		}
		if (setsockopt(gdb_if_serv, IPPROTO_TCP, TCP_NODELAY, (void*)&opt, sizeof(opt)) == -1) {
			close(gdb_if_serv);
			continue;
		}
		if (bind(gdb_if_serv, (void*)&addr, sizeof(addr)) == -1) {
			close(gdb_if_serv);
			continue;
		}
		if (listen(gdb_if_serv, 1) == -1) {
			close(gdb_if_serv);
			continue;
		}
		break;
	} while(1);
	DEBUG_WARN("Listening on TCP: %4d\n", port);

	return 0;
}


unsigned char gdb_if_getchar(void)
{
	unsigned char ret;
	int i = 0;
#if defined(_WIN32) || defined(__CYGWIN__)
	unsigned long opt;
#else
	int flags;
#endif
	while(i <= 0) {
		if(gdb_if_conn <= 0) {
#if defined(_WIN32) || defined(__CYGWIN__)
			opt = 1;
			ioctlsocket(gdb_if_serv, FIONBIO, &opt);
#else
			flags = fcntl(gdb_if_serv, F_GETFL);
			fcntl(gdb_if_serv, F_SETFL, flags | O_NONBLOCK);
#endif
			while(1) {
				gdb_if_conn = accept(gdb_if_serv, NULL, NULL);
				if (gdb_if_conn == -1) {
#if defined(_WIN32) || defined(__CYGWIN__)
					if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
					if (errno == EWOULDBLOCK) {
#endif
						SET_IDLE_STATE(1);
						platform_delay(100);
					} else {
#if defined(_WIN32) || defined(__CYGWIN__)
						DEBUG_WARN("error when accepting connection: %d",
								   WSAGetLastError());
#else
						DEBUG_WARN("error when accepting connection: %s",
								   strerror(errno));
#endif
						exit(1);
					}
				} else {
#if defined(_WIN32) || defined(__CYGWIN__)
					opt = 0;
					ioctlsocket(gdb_if_serv, FIONBIO, &opt);
#else
					fcntl(gdb_if_serv, F_SETFL, flags);
#endif
					break;
				}
			}
			DEBUG_INFO("Got connection\n");
#if defined(_WIN32) || defined(__CYGWIN__)
			opt = 0;
			ioctlsocket(gdb_if_conn, FIONBIO, &opt);
#else
			flags = fcntl(gdb_if_conn, F_GETFL);
			fcntl(gdb_if_conn, F_SETFL, flags & ~O_NONBLOCK);
#endif
		}
		i = recv(gdb_if_conn, (void*)&ret, 1, 0);
		if(i <= 0) {
			gdb_if_conn = -1;
#if defined(_WIN32) || defined(__CYGWIN__)
			DEBUG_INFO("Dropped broken connection: %d\n", WSAGetLastError());
#else
			DEBUG_INFO("Dropped broken connection: %s\n", strerror(errno));
#endif
			/* Return '+' in case we were waiting for an ACK */
			return '+';
		}
	}
	return ret;
}

unsigned char gdb_if_getchar_to(int timeout)
{
	fd_set fds;
# if defined(__CYGWIN__)
        TIMEVAL tv;
#else
	struct timeval tv;
#endif

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
#if defined(__WIN32__) || defined(__CYGWIN__)
	static char buf[2048];
#else
	static uint8_t buf[2048];
#endif
	static int bufsize = 0;
	if (gdb_if_conn > 0) {
		buf[bufsize++] = c;
		if (flush || (bufsize == sizeof(buf))) {
			send(gdb_if_conn, buf, bufsize, 0);
			bufsize = 0;
		}
	}
}
