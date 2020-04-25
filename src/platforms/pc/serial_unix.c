/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019  Dave Marples <dave@marples.net>
 * with additions from Uwe Bonnes (bon@elektron.ikp.physik.tu-darmstadt.de)
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
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>

#include "general.h"
#include "remote.h"
#include "cl_utils.h"

static int fd;  /* File descriptor for connection to GDB remote */

/* A nice routine grabbed from
 * https://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c
 */
static int set_interface_attribs(void)
{
	struct termios tty;
	memset (&tty, 0, sizeof tty);
	if (tcgetattr (fd, &tty) != 0) {
      fprintf(stderr,"error %d from tcgetattr", errno);
      return -1;
    }

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
	// disable IGNBRK for mismatched speed tests; otherwise receive break
	// as \000 chars
	tty.c_iflag &= ~IGNBRK;         // disable break processing
	tty.c_lflag = 0;                // no signaling chars, no echo,
	// no canonical processing
	tty.c_oflag = 0;                // no remapping, no delays
	tty.c_cc[VMIN]  = 0;            // read doesn't block
	tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

	tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
	// enable reading
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;

	if (tcsetattr (fd, TCSANOW, &tty) != 0) {
		fprintf(stderr,"error %d from tcsetattr", errno);
		return -1;
    }
	return 0;
}
#define BMP_IDSTRING "usb-Black_Sphere_Technologies_Black_Magic_Probe"
#define DEVICE_BY_ID "/dev/serial/by-id/"
int serial_open(BMP_CL_OPTIONS_t *opt)
{
	char name[4096];
	if (!opt->opt_device) {
		/* Try to find some BMP if0*/
		struct dirent *dp;
		DIR *dir = opendir(DEVICE_BY_ID);
		if (!dir) {
			fprintf(stderr, "No serial device found\n");
			return -1;
		}
		int num_devices = 0;
		int num_total = 0;
		while ((dp = readdir(dir)) != NULL) {
			if ((strstr(dp->d_name, BMP_IDSTRING)) &&
				(strstr(dp->d_name, "-if00"))) {
				num_total++;
				if (((opt->opt_serial) &&
				 (!strstr(dp->d_name, opt->opt_serial))))
					continue;
				num_devices++;
				strcpy(name, DEVICE_BY_ID);
				strncat(name, dp->d_name, sizeof(name) - strlen(name) - 1);
			}
		}
		closedir(dir);
		if ((num_devices == 0) && (num_total == 0)){
			fprintf(stderr, "No BMP probe found\n");
			return -1;
		} else if (num_devices != 1) {
			fprintf(stderr, "Available Probes:\n");
			dir = opendir(DEVICE_BY_ID);
			if (dir) {
				while ((dp = readdir(dir)) != NULL) {
					if ((strstr(dp->d_name, BMP_IDSTRING)) &&
						(strstr(dp->d_name, "-if00")))
						fprintf(stderr, "%s\n", dp->d_name);
				}
				closedir(dir);
				if (opt->opt_serial)
					fprintf(stderr, "Do no match given serial \"%s\"\n", opt->opt_serial);
				else
					fprintf(stderr, "Select Probe with -s <(Partial) Serial Number\n");
			} else {
				fprintf(stderr, "Could not opendir %s: %s\n", name, strerror(errno));
			}
			return -1;
		}
	} else {
		strncpy(name, opt->opt_device, sizeof(name) - 1);
	}
	fd = open(name, O_RDWR | O_SYNC | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr,"Couldn't open serial port %s\n", name);
		return -1;
    }
	/* BMP only offers an USB-Serial connection with no real serial
	 * line in between. No need for baudrate or parity.!
	 */
	return set_interface_attribs();
}

void serial_close(void)
{
	close(fd);
}

int platform_buffer_write(const uint8_t *data, int size)
{
	int s;

	if (cl_debuglevel)
		printf("%s\n",data);
	s = write(fd, data, size);
	if (s < 0) {
		fprintf(stderr, "Failed to write\n");
		exit(-2);
    }

	return size;
}

int platform_buffer_read(uint8_t *data, int maxsize)
{
	uint8_t *c;
	int s;
	int ret;
	uint32_t endTime;
	fd_set  rset;
	struct timeval tv;

	c = data;
	tv.tv_sec = 0;

	endTime = platform_time_ms() + RESP_TIMEOUT;
	tv.tv_usec = 1000 * (endTime - platform_time_ms());

	/* Look for start of response */
	do {
		FD_ZERO(&rset);
		FD_SET(fd, &rset);

		ret = select(fd + 1, &rset, NULL, NULL, &tv);
		if (ret < 0) {
			fprintf(stderr,"Failed on select\n");
			exit(-4);
		}
		if(ret == 0) {
			fprintf(stderr,"Timeout on read RESP\n");
			exit(-3);
		}

		s = read(fd, c, 1);
    }
	while ((s > 0) && (*c != REMOTE_RESP));
	/* Now collect the response */
	do {
		FD_ZERO(&rset);
		FD_SET(fd, &rset);
		ret = select(fd + 1, &rset, NULL, NULL, &tv);
		if (ret < 0) {
			fprintf(stderr,"Failed on select\n");
			exit(-4);
		}
		if(ret == 0) {
			fprintf(stderr,"Timeout on read\n");
			exit(-3);
		}
		s = read(fd, c, 1);
		if (*c==REMOTE_EOM) {
			*c = 0;
			if (cl_debuglevel)
				printf("       %s\n",data);
			return (c - data);
		} else {
			c++;
		}
	}while ((s >= 0) && ((c - data) < maxsize));

	fprintf(stderr,"Failed to read\n");
	exit(-3);
	return 0;
}
