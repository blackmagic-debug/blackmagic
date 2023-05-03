/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019  Dave Marples <dave@marples.net>
 * Modifications (C) 2020 Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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

#include <sys/stat.h>
#include <sys/select.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

#include "general.h"
#include "remote.h"
#include "bmp_hosted.h"
#include "utils.h"
#include "cortexm.h"

static int fd; /* File descriptor for connection to GDB remote */

/* A nice routine grabbed from
 * https://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c
 */
static bool set_interface_attribs(void)
{
	struct termios tty;
	memset(&tty, 0, sizeof tty);
	if (tcgetattr(fd, &tty) != 0) {
		DEBUG_ERROR("error %d from tcgetattr", errno);
		return false;
	}

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8-bit chars
	// disable IGNBRK for mismatched speed tests; otherwise receive break
	// as \000 chars
	tty.c_iflag &= ~IGNBRK; // disable break processing
	tty.c_lflag = 0;        // no signaling chars, no echo,
	// no canonical processing
	tty.c_oflag = 0;     // no remapping, no delays
	tty.c_cc[VMIN] = 0;  // read doesn't block
	tty.c_cc[VTIME] = 5; // 0.5 seconds read timeout

	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

	tty.c_cflag |= (CLOCAL | CREAD); // ignore modem controls,
	// enable reading
	tty.c_cflag &= ~CSTOPB;
#if defined(CRTSCTS)
	tty.c_cflag &= ~CRTSCTS;
#endif
	if (tcsetattr(fd, TCSANOW, &tty) != 0) {
		DEBUG_ERROR("error %d from tcsetattr", errno);
		return false;
	}
	return true;
}

#ifdef __APPLE__
bool serial_open(const bmda_cli_options_s *cl_opts, const char *serial)
{
	char name[4096];
	if (!cl_opts->opt_device) {
		/* Try to find some BMP if0*/
		if (!serial) {
			DEBUG_WARN("No serial device found\n");
			return false;
		} else {
			sprintf(name, "/dev/cu.usbmodem%s1", serial);
		}
	} else {
		strncpy(name, cl_opts->opt_device, sizeof(name) - 1U);
	}
	fd = open(name, O_RDWR | O_SYNC | O_NOCTTY);
	if (fd < 0) {
		DEBUG_ERROR("Couldn't open serial port %s\n", name);
		return false;
	}
	/* BMP only offers an USB-Serial connection with no real serial
     * line in between. No need for baudrate or parity.!
     */
	return set_interface_attribs();
}
#else
#define BMP_IDSTRING_BLACKSPHERE "usb-Black_Sphere_Technologies_Black_Magic_Probe"
#define BMP_IDSTRING_BLACKMAGIC  "usb-Black_Magic_Debug_Black_Magic_Probe"
#define BMP_IDSTRING_1BITSQUARED "usb-1BitSquared_Black_Magic_Probe"
#define DEVICE_BY_ID             "/dev/serial/by-id/"

typedef struct dirent dirent_s;

bool device_is_bmp_gdb_port(const char *const device)
{
	const size_t length = strlen(device);
	if (begins_with(device, length, BMP_IDSTRING_BLACKSPHERE) || begins_with(device, length, BMP_IDSTRING_BLACKMAGIC) ||
		begins_with(device, length, BMP_IDSTRING_1BITSQUARED)) {
		return ends_with(device, length, "-if00");
	}
	return false;
}

static bool match_serial(const char *const device, const char *const serial)
{
	const char *const last_underscore = strrchr(device, '_');
	/* Fail the match if we can't find the _ just before the serial string. */
	if (!last_underscore)
		return false;
	/* This represents the first byte of the serial number string */
	const char *const begin = last_underscore + 1;
	/* This represents one past the last byte of the serial number string */
	const char *const end = device + strlen(device) - 5U;
	/* Try to match the (partial) serial string in the correct part of the device string */
	return contains_substring(begin, end - begin, serial);
}

bool serial_open(const bmda_cli_options_s *const cl_opts, const char *const serial)
{
	char name[4096];
	if (!cl_opts->opt_device) {
		/* Try to find some BMP if0*/
		DIR *dir = opendir(DEVICE_BY_ID);
		if (!dir) {
			DEBUG_WARN("No serial devices found\n");
			return false;
		}
		size_t matches = 0;
		size_t total = 0;
		while (true) {
			const dirent_s *const entry = readdir(dir);
			if (entry == NULL)
				break;
			if (device_is_bmp_gdb_port(entry->d_name)) {
				++total;
				if (serial && !match_serial(entry->d_name, serial))
					continue;
				++matches;
				const size_t path_len = sizeof(DEVICE_BY_ID) - 1U;
				memcpy(name, DEVICE_BY_ID, path_len);
				const size_t name_len = strlen(entry->d_name);
				const size_t truncated_len = MIN(name_len, sizeof(name) - path_len - 2U);
				memcpy(name + path_len, entry->d_name, truncated_len);
				name[path_len + truncated_len] = '\0';
			}
		}
		closedir(dir);
		if (total == 0) {
			DEBUG_ERROR("No Black Magic Probes found\n");
			return false;
		}
		if (matches != 1) {
			DEBUG_INFO("Available Probes:\n");
			dir = opendir(DEVICE_BY_ID);
			if (dir) {
				while (true) {
					const dirent_s *const entry = readdir(dir);
					if (entry == NULL)
						break;
					if (device_is_bmp_gdb_port(entry->d_name))
						DEBUG_WARN("%s\n", entry->d_name);
				}
				closedir(dir);
				if (serial)
					DEBUG_ERROR("No match for (partial) serial number \"%s\"\n", serial);
				else
					DEBUG_WARN("Select probe with `-s <(Partial) Serial Number>`\n");
			} else
				DEBUG_ERROR("Could not scan %s: %s\n", name, strerror(errno));
			return false;
		}
	} else {
		const size_t path_len = strlen(cl_opts->opt_device);
		const size_t truncated_len = MIN(path_len, sizeof(name) - 1U);
		memcpy(name, cl_opts->opt_device, truncated_len);
		name[truncated_len] = '\0';
	}
	fd = open(name, O_RDWR | O_SYNC | O_NOCTTY);
	if (fd < 0) {
		DEBUG_ERROR("Couldn't open serial port %s\n", name);
		return false;
	}
	/* BMP only offers an USB-Serial connection with no real serial
	 * line in between. No need for baudrate or parity.!
	 */
	return set_interface_attribs();
}
#endif

void serial_close(void)
{
	close(fd);
}

bool platform_buffer_write(const void *const data, const size_t length)
{
	DEBUG_WIRE("%s\n", (const char *)data);
	const ssize_t written = write(fd, data, length);
	if (written < 0) {
		const int error = errno;
		DEBUG_ERROR("Failed to write (%d): %s\n", errno, strerror(error));
		exit(-2);
	}
	return (size_t)written == length;
}

/* XXX: We should either return size_t or bool */
/* XXX: This needs documenting that it can abort the program with exit(), or the error handling fixed */
int platform_buffer_read(void *const data, size_t length)
{
	char response = 0;
	timeval_s timeout = {
		.tv_sec = cortexm_wait_timeout / 1000U,
		.tv_usec = 1000U * (cortexm_wait_timeout % 1000U),
	};

	/* Drain the buffer for the remote till we see a start-of-response byte */
	while (response != REMOTE_RESP) {
		fd_set select_set;
		FD_ZERO(&select_set);
		FD_SET(fd, &select_set);

		const int result = select(FD_SETSIZE, &select_set, NULL, NULL, &timeout);
		if (result < 0) {
			DEBUG_ERROR("Failed on select\n");
			return -3;
		}
		if (result == 0) {
			DEBUG_ERROR("Timeout while waiting for BMP response\n");
			return -4;
		}
		if (read(fd, &response, 1) != 1) {
			const int error = errno;
			DEBUG_ERROR("Failed to read response (%d): %s\n", error, strerror(error));
			return -6;
		}
	}
	/* Now collect the response */
	for (size_t offset = 0; offset < length;) {
		fd_set select_set;
		FD_ZERO(&select_set);
		FD_SET(fd, &select_set);
		const int result = select(FD_SETSIZE, &select_set, NULL, NULL, &timeout);
		if (result < 0) {
			DEBUG_ERROR("Failed on select\n");
			exit(-4);
		}
		if (result == 0) {
			DEBUG_ERROR("Timeout on read\n");
			return -5;
		}
		if (read(fd, data + offset, 1) != 1) {
			const int error = errno;
			DEBUG_ERROR("Failed to read response (%d): %s\n", error, strerror(error));
			return -6;
		}
		char *const buffer = (char *)data;
		if (buffer[offset] == REMOTE_EOM) {
			buffer[offset] = 0;
			DEBUG_WIRE("       %s\n", buffer);
			return offset;
		}
		++offset;
	}

	DEBUG_ERROR("Failed to read\n");
	return -6;
}
