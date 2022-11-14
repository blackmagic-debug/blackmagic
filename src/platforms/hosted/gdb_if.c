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

#include "general.h"

#if defined(_WIN32) || defined(__CYGWIN__)
#define __USE_MINGW_ANSI_STDIO 1
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <fcntl.h>
#endif

#include <string.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>

#include "gdb_if.h"

static const uint16_t default_port = 2000U;
static const uint16_t max_port = default_port + 4U;

#if defined(_WIN32) || defined(__CYGWIN__)
const int op_would_block = WSAEWOULDBLOCK;
#else
const int op_would_block = EWOULDBLOCK;

static inline int closesocket(const int s)
{
	return close(s);
}
#endif

static int gdb_if_serv = -1;
static int gdb_if_conn = -1;

#define GDB_BUFFER_LEN 2048U
static size_t gdb_buffer_used = 0U;
static char gdb_buffer[GDB_BUFFER_LEN];

typedef struct sockaddr sockaddr_s;
typedef struct sockaddr_in sockaddr_in_s;
typedef struct sockaddr_in6 sockaddr_in6_s;
typedef struct sockaddr_storage sockaddr_storage_s;
typedef struct addrinfo addrinfo_s;

#if defined(_WIN32) || defined(__CYGWIN__)
typedef ADDRESS_FAMILY sa_family_t;
#endif

static inline size_t family_to_size(const sa_family_t family)
{
	if (family == AF_INET)
		return sizeof(sockaddr_in_s);
	if (family == AF_INET6)
		return sizeof(sockaddr_in6_s);
	return sizeof(sockaddr_storage_s);
}

static inline uint16_t u16_to_be(const uint16_t value)
{
	const uint8_t resultBytes[2] = {(uint8_t)(value >> 8U), (uint8_t)value};
	uint16_t result = 0;
	static_assert(sizeof(uint16_t) == sizeof(resultBytes), "uint16_t is not 2 bytes on this platform");
	memcpy(&result, resultBytes, sizeof(resultBytes));
	return result;
}

static sockaddr_storage_s sockaddr_prepare(const uint16_t port)
{
	addrinfo_s hints = {};
	/* Use AF_UNSPEC here to support either IPv4 or v6 */
	hints.ai_family = AF_UNSPEC;
	/* Ask for a normal TCP socket */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	/* This + the first parameter of getaddrinfo() being NULL guarantees an any address */
	hints.ai_flags = AI_PASSIVE;

	/* See what the host can give us for what we've requested */
	addrinfo_s *results = NULL;
	int res = getaddrinfo(NULL, "0", &hints, &results);
	if (res || !results) {
		DEBUG_WARN("getaddrinfo returned %d (errno = %d), results is %p\n", res, errno, results);
		return (sockaddr_storage_s){AF_UNSPEC};
	}

	/* Pick the first result, copy it and free the list structure getaddrinfo() returns */
	sockaddr_storage_s service;
	memcpy(&service, results->ai_addr, family_to_size(results->ai_addr->sa_family));
	freeaddrinfo(results);

	/* Copy in the port number as appropriate for the returned structure */
	const uint16_t port_be = u16_to_be(port);
	if (service.ss_family == AF_INET)
		((sockaddr_in_s *)&service)->sin_port = port_be;
	else if (service.ss_family == AF_INET6)
		((sockaddr_in6_s *)&service)->sin6_port = port_be;
	else
		return (sockaddr_storage_s){AF_UNSPEC};
	/* We now have a valid IPv4 or IPv6 sockaddr to work with so return it */
	return service;
}

static int socket_error()
{
#if defined(_WIN32) || defined(__CYGWIN__)
	return WSAGetLastError();
#else
	return errno;
#endif
}

static void display_socket_error(const int error, const int socket, const char *const operation)
{
#if defined(_WIN32) || defined(__CYGWIN__)
	char *message = NULL;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
		error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char *)&message, 0, NULL);
#else
	const char *message = strerror(error);
#endif
	DEBUG_WARN("Error %s %d, got error %d: %s\n", operation, socket, error, message);
#if defined(_WIN32) || defined(__CYGWIN__)
	LocalFree(message);
#endif
}

static void handle_error(const int socket, const char *const operation)
{
	display_socket_error(socket_error(), socket, operation);
	closesocket(socket);
}

int gdb_if_init(void)
{
#if defined(_WIN32) || defined(__CYGWIN__)
	WSADATA wsa_data = {};
	const int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
	if (result != NO_ERROR) {
		DEBUG_WARN("WSAStartup failed with error: %d\n", result);
		return -1;
	}
#endif
	for (uint16_t port = default_port; port < max_port; ++port) {
		const sockaddr_storage_s addr = sockaddr_prepare(port);
		if (addr.ss_family == AF_UNSPEC) {
			DEBUG_WARN("Failed to get a suitable socket address\n");
			return -1;
		}

		gdb_if_serv = socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP);
		if (gdb_if_serv == -1) {
			display_socket_error(socket_error(), gdb_if_serv, "socket returned");
			continue;
		}

		if (!socket_set_int_opt(gdb_if_serv, SOL_SOCKET, SO_REUSEADDR, 1) ||
			!socket_set_int_opt(gdb_if_serv, IPPROTO_TCP, TCP_NODELAY, 1))
			continue;

		if (bind(gdb_if_serv, (sockaddr_s *)&addr, family_to_size(addr.ss_family)) == -1) {
			handle_error(gdb_if_serv, "binding socket");
			continue;
		}

		if (listen(gdb_if_serv, 1) == -1) {
			handle_error(gdb_if_serv, "listening on socket");
			continue;
		}

		DEBUG_WARN("Listening on TCP port: %d\n", port);
		return 0;
	}

	DEBUG_WARN("Failed to aquire a port to listen on\n");
	return -1;
}

char gdb_if_getchar(void)
{
	char ret;
	int i = 0;
#if defined(_WIN32) || defined(__CYGWIN__)
	int iResult;
	unsigned long opt;
#else
	int flags;
#endif
	while (i <= 0) {
		if (gdb_if_conn <= 0) {
#if defined(_WIN32) || defined(__CYGWIN__)
			opt = 1;
			iResult = ioctlsocket(gdb_if_serv, FIONBIO, &opt);
			if (iResult != NO_ERROR) {
				DEBUG_WARN("ioctlsocket failed with error: %ld\n", iResult);
			}
#else
			flags = fcntl(gdb_if_serv, F_GETFL);
			fcntl(gdb_if_serv, F_SETFL, flags | O_NONBLOCK);
#endif
			while (1) {
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
						DEBUG_WARN("error when accepting connection: %d", WSAGetLastError());
#else
						DEBUG_WARN("error when accepting connection: %s", strerror(errno));
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
		i = recv(gdb_if_conn, (void *)&ret, 1, 0);
		if (i <= 0) {
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

char gdb_if_getchar_to(uint32_t timeout)
{
	fd_set fds;
#if defined(__CYGWIN__)
	TIMEVAL tv;
#else
	struct timeval tv;
#endif

	if (gdb_if_conn == -1)
		return -1;

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;

	FD_ZERO(&fds);
	FD_SET(gdb_if_conn, &fds);

	if (select(gdb_if_conn + 1, &fds, NULL, NULL, &tv) > 0)
		return gdb_if_getchar();

	return -1;
}

void gdb_if_putchar(char c, int flush)
{
	if (gdb_if_conn == -1)
		return;
	gdb_buffer[gdb_buffer_used++] = c;
	if (flush || gdb_buffer_used == GDB_BUFFER_LEN) {
		send(gdb_if_conn, gdb_buffer, gdb_buffer_used, 0);
		gdb_buffer_used = 0;
	}
}
